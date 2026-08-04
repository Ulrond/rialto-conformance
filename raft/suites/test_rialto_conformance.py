#!/usr/bin/env python3
#
# Copyright 2026 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
"""ut-raft adjudicator for the rialto-conformance suite (§4).

Host-side orchestration over a RAFT console session:

  1. Deploy a **prebuilt** package to the target (`deploy: fetch`), or use what
     the engineer already placed there (`deploy: none`).
  2. Launch the target environment with the per-slot `conformance.launch`
     command, if the slot names one.
  3. Fetch the platform's HFP (Hardware Feature Profile) from the URL named by
     device_config's `conformance.hfp`, ship the resolved file to the target, and
     run the on-target binary in automated mode with it:
         rialto_conformance -a -p <hfp>
  4. Pull the produced xUnit/JUnit XML back to the host and adjudicate it.

**This suite never builds.** Building is `build.sh` + `packaging/package.sh`;
testing is this. The tarball arrives prebuilt from `conformance.package`, or is
already on the target. Keeping the seam clean is the point — see issue #104.

raft is GIVEN the target by config (--rack/--slotName select the slot in
rack_config.yml; the slot's `platform` links to device_config.yml). It has no
concept of the platform: the platform-specific inputs are the HFP that
device_config names by URL and the `launch` command it names by string.
deviceConfig is host-only; the HOST fetches the HFP and ships the resolved file,
and the target consumes only that. The same binary and the same cases run on
every target — emulator, VM or real box. Only the slot changes.

Scope and tier come from the environment (set by `test.sh`):
    RIALTO_CONFORMANCE_SCOPE   full | L1 | L2 | L3 | L4      (default: full)
    RIALTO_CONFORMANCE_TIER    core | extended | all         (default: core)

Run (via the isolated host venv that install.sh creates):
    python_venv/bin/python raft/suites/test_rialto_conformance.py \
        --config raft/rack_config.yml --rack rack1 --slotName reference-target
"""

import glob
import os
import shutil
import subprocess
import tempfile
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

from raft import RAFTUnitTestCase, RAFTUnitTestMain  # provided by python_raft / ut-raft

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# Scope is passed to the binary as RIALTO_CONFORMANCE_SCOPE, not as ut-core's
# -e/-d. ut-core 5.1.0 parses those and computes a filter, but never applies it in
# automated mode (runTests() is a bare RUN_ALL_TESTS()), so -e is inert under -a.
# src/main.cpp sets the GoogleTest filter from this variable instead.
SCOPES = ["full", "L1", "L2", "L3", "L4"]


class RialtoConformance(RAFTUnitTestCase):
    """Deploy, launch, run and adjudicate the conformance binary on a config-named target."""

    def setUp(self):
        self.dut.session.open()
        # Per-target conformance params come from device_config.yml — never code.
        # self.dut.config is the cpe entry (deviceConfig/cpe1); the host-only
        # `conformance` block carries the orchestration inputs + the HFP URL.
        self.conf = self.dut.config.get("conformance", {})
        self.install_dir = self.conf.get("installDir", "/opt/rialto-conformance")
        self.binary = self.conf.get("binary", "rialto_conformance")
        self.results_dir = self.conf.get("resultsDir", f"{self.install_dir}/results")

        # Deploy mode: `fetch` ships the prebuilt package named by `package`;
        # `none` trusts what the engineer already put on the target.
        self.deploy_mode = str(self.conf.get("deploy", "fetch")).lower()
        self.package_loc = self.conf.get("package")
        # Optional per-slot bring-up/tear-down, run ON the target. raft only runs
        # the string; whatever it does is the platform's business.
        self.launch_cmd = self.conf.get("launch")
        self.teardown_cmd = self.conf.get("teardown")
        # Each console command is its own shell, so a launch script cannot export
        # into the run. A launch that needs to hand environment to the binary
        # (socket path, plugin paths, ...) writes this file; the run sources it.
        self.env_file = self.conf.get("envFile")

        # The capability gate is the platform's HFP, named by URL. The HOST fetches
        # it (deviceConfig is never shipped) and ships the resolved file to the
        # target, where the binary loads it with -p.
        self.hfp_url = self.conf.get("hfp")
        self.assertTrue(self.hfp_url, "device_config conformance.hfp (HFP URL) is not set")
        self.remote_hfp = f"{self.install_dir}/hfp.yml"

        self.scope = os.environ.get("RIALTO_CONFORMANCE_SCOPE", "full")
        self.tier = os.environ.get("RIALTO_CONFORMANCE_TIER", "core")
        self.assertIn(
            self.scope, SCOPES,
            f"unknown RIALTO_CONFORMANCE_SCOPE {self.scope!r} (want full or L1-L4)",
        )

        self.local_results = os.path.join(REPO_ROOT, "logs", "results")
        os.makedirs(self.local_results, exist_ok=True)

    # --- deploy -------------------------------------------------------------
    def test_00_deploy_package(self):
        """Install a PREBUILT package onto the target (or accept a pre-placed one)."""
        if self.deploy_mode == "none":
            self.log.step("deploy: none — using the package already on the target")
        else:
            self.assertEqual(
                self.deploy_mode, "fetch",
                f"unknown conformance.deploy {self.deploy_mode!r} (want 'fetch' or 'none')",
            )
            package = self._resolve_package()
            self.log.step(f"deploy: fetch — shipping {os.path.basename(package)}")
            remote_pkg = f"/tmp/{os.path.basename(package)}"
            self._copy_to_target(package, remote_pkg)
            self.dut.session.write(f"mkdir -p {self.install_dir} && tar -xzf {remote_pkg} -C {self.install_dir}")
            self.dut.session.read_until(self.dut.session.prompt, timeout=60)

        # Either way, the binary must be there and executable before we go on.
        self.dut.session.write(f"test -x {self.install_dir}/{self.binary} && echo DEPLOY_OK")
        out = self.dut.session.read_until(self.dut.session.prompt, timeout=30)
        self.assertIn(
            "DEPLOY_OK", out,
            f"conformance binary not present/executable at {self.install_dir}/{self.binary}",
        )

    # --- launch -------------------------------------------------------------
    def test_05_launch_target(self):
        """Bring the target environment up with the slot's `launch` command."""
        if not self.launch_cmd:
            self.log.step("launch: none configured — assuming the target is already up")
            return
        self.log.step(f"launch: {self.launch_cmd}")
        self.dut.session.write(f"cd {self.install_dir} && {self.launch_cmd} ; echo LAUNCH_DONE_$?")
        out = self.dut.session.read_until("LAUNCH_DONE_", timeout=300)
        self.assertIn(
            "LAUNCH_DONE_0", out,
            f"target launch command failed: {self.launch_cmd}",
        )

    # --- run + adjudicate ---------------------------------------------------
    def test_10_run_conformance(self):
        """Run the selected scope against the launched target and parse xUnit."""
        remote_xml = f"{self.results_dir}/rialto_conformance.xml"
        self.dut.session.write(f"mkdir -p {self.results_dir}")
        self.dut.session.read_until(self.dut.session.prompt, timeout=15)

        # Host fetches the platform HFP from its URL, then ships the resolved file
        # to the target. deviceConfig is never shipped; the target sees only the HFP.
        local_hfp = self._fetch_hfp(self.hfp_url)
        self._copy_to_target(local_hfp, self.remote_hfp)

        try:
            # Automated mode emits xUnit/JUnit XML; -p loads the HFP so the single
            # binary self-selects the cases this target's platform exposes. -e
            # narrows to one level when a scope was asked for.
            self.log.step(f"run: scope={self.scope} tier={self.tier}")
            self.dut.session.write(
                f"cd {self.install_dir} && {self._env_prefix()}RIALTO_CONFORMANCE_TIER={self.tier} "
                f"RIALTO_CONFORMANCE_SCOPE={self.scope} "
                f"./{self.binary} -a -p {self.remote_hfp} "
                f"-l {self.results_dir}/ ; echo RUN_DONE_$?"
            )
            self.dut.session.read_until("RUN_DONE_", timeout=900)

            local_xml = os.path.join(self.local_results, "rialto_conformance.xml")
            self._copy_from_target(remote_xml, local_xml)
        finally:
            self._teardown_target()

        self._adjudicate(local_xml)

    def tearDown(self):
        self.dut.session.close()

    # --- helpers ------------------------------------------------------------
    def _env_prefix(self):
        """Shell prefix that sources the launch-written env file, if configured."""
        return f". ./{self.env_file} && " if self.env_file else ""

    def _teardown_target(self):
        """Run the slot's `teardown` command, if any. Never fails the run."""
        if not self.teardown_cmd:
            return
        try:
            self.dut.session.write(f"cd {self.install_dir} && {self.teardown_cmd} ; echo TEARDOWN_DONE_$?")
            self.dut.session.read_until("TEARDOWN_DONE_", timeout=120)
        except Exception as exc:                                  # noqa: BLE001
            self.log.step(f"teardown command raised (ignored): {exc}")

    def _resolve_package(self):
        """Resolve `conformance.package` to a local prebuilt tarball on the host.

        Accepts a path (absolute or repo-relative, glob allowed — newest match
        wins), file:// or http(s)://. This suite NEVER builds: if nothing matches,
        that is a hard failure telling the engineer to run build.sh first.
        """
        loc = self.package_loc
        self.assertTrue(
            loc,
            "device_config conformance.package is not set (needed for deploy: fetch)",
        )
        parts = urllib.parse.urlparse(loc)
        if parts.scheme in ("http", "https"):
            local = os.path.join(tempfile.mkdtemp(prefix="pkg-"), os.path.basename(parts.path) or "package.tar.gz")
            with urllib.request.urlopen(loc, timeout=120) as resp, open(local, "wb") as out:
                shutil.copyfileobj(resp, out)
            return local

        path = parts.netloc + parts.path if parts.scheme == "file" else loc
        if not os.path.isabs(path):
            path = os.path.join(REPO_ROOT, path)
        # glob() on a plain existing path returns just that path, so this covers
        # both the glob and the literal case. Newest match wins.
        matches = sorted(glob.glob(path), key=os.path.getmtime)
        self.assertTrue(
            matches,
            f"no prebuilt package at {loc!r} — run ./build.sh (then packaging/package.sh) first; "
            "this suite does not build",
        )
        return matches[-1]

    def _fetch_hfp(self, url):
        """Resolve the platform HFP URL to a local file on the host.

        Supports http(s):// (fetched) and file:// (copied). A file:// path may be
        absolute (file:///abs) or repo-relative (file://profiles/hfp.x.yaml). The
        target never fetches — the host resolves the URL and ships the result.
        """
        parts = urllib.parse.urlparse(url)
        local = os.path.join(tempfile.mkdtemp(prefix="hfp-"), "hfp.yml")
        if parts.scheme in ("http", "https"):
            with urllib.request.urlopen(url, timeout=30) as resp, open(local, "wb") as out:
                shutil.copyfileobj(resp, out)
        elif parts.scheme == "file":
            src = parts.netloc + parts.path       # netloc is set for file://relative/...
            if not os.path.isabs(src):
                src = os.path.join(REPO_ROOT, src)
            shutil.copyfile(src, local)
        else:
            self.fail(f"unsupported HFP URL scheme in {url!r} (want http(s):// or file://)")
        self.assertTrue(os.path.getsize(local) > 0, f"fetched HFP is empty: {url}")
        return local

    def _copy_to_target(self, local, remote):
        """Prefer the RAFT console's file transfer; fall back to scp via config."""
        copy_to = getattr(self.dut, "copyToDevice", None) or getattr(self.dut.session, "copy_to", None)
        if callable(copy_to):
            copy_to(local, remote)
            return
        self._scp(local, f"{self._ssh_target()}:{remote}")

    def _copy_from_target(self, remote, local):
        copy_from = getattr(self.dut, "copyFromDevice", None) or getattr(self.dut.session, "copy_from", None)
        if callable(copy_from):
            copy_from(remote, local)
            return
        self._scp(f"{self._ssh_target()}:{remote}", local)

    def _ssh_target(self):
        console = self.dut.config.get("consoles", [{}])[0].get("default", {})
        user = console.get("username", "root")
        ip = console.get("ip") or self.dut.config.get("ip")
        return f"{user}@{ip}"

    @staticmethod
    def _scp(src, dst):
        subprocess.run(["scp", "-q", src, dst], check=True)

    def _adjudicate(self, xml_path):
        """xUnit verdict: the conformance gate fails on any failure or error."""
        self.assertTrue(os.path.isfile(xml_path), f"no xUnit result collected: {xml_path}")
        tree = ET.parse(xml_path)
        root = tree.getroot()
        suites = [root] if root.tag == "testsuite" else root.findall(".//testsuite")
        total = failures = errors = skipped = 0
        for suite in suites:
            total += int(suite.get("tests", 0))
            failures += int(suite.get("failures", 0))
            errors += int(suite.get("errors", 0))
            skipped += int(suite.get("skipped", 0))
        self.log.step(
            f"conformance [{self.scope}/{self.tier}]: {total} cases, {failures} failed, "
            f"{errors} errored, {skipped} skipped (capability-gated)"
        )
        self.assertEqual(failures, 0, f"{failures} conformance case(s) failed")
        self.assertEqual(errors, 0, f"{errors} conformance case(s) errored")
        self.assertGreater(total - skipped, 0, "no applicable cases ran on this target")


if __name__ == "__main__":
    RAFTUnitTestMain()
