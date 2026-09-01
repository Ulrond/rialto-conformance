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
import sys
import time
import tempfile
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# python_raft is installed as a source tree by install.sh (framework.lock), not
# as a distribution — only its requirements are pip-installed. Its own examples
# put the checkout on sys.path and import through it, so this does the same.
sys.path.insert(0, os.path.join(REPO_ROOT, "framework", "python_raft"))
sys.path.insert(0, os.path.join(REPO_ROOT, "framework", "ut-raft"))

from framework.core.raftUnittest import RAFTUnitTestCase, RAFTUnitTestMain  # noqa: E402

# Scope is passed to the binary as RIALTO_CONFORMANCE_SCOPE, not as ut-core's
# -e/-d. ut-core 5.1.0 parses those and computes a filter, but never applies it in
# automated mode (runTests() is a bare RUN_ALL_TESTS()), so -e is inert under -a.
# src/main.cpp sets the GoogleTest filter from this variable instead.
SCOPES = ["full", "L1", "L2", "L3", "L4"]


class RialtoConformance(RAFTUnitTestCase):
    """Deploy, launch, run and adjudicate the conformance binary on a config-named target."""

    def setUp(self):
        self.dut.session.open()
        # Each phase gets its own interactive channel: tearDown closes the client,
        # and the console reuses whatever channel it holds rather than noticing
        # that one died with it — the next write would go to a closed socket.
        self.dut.session.open_interactive_shell()
        # Per-target conformance params come from device_config.yml — never code.
        # self.cpe is the deviceConfig entry raft matched to this slot's platform
        # (deviceConfig/cpe1 for linux-emulator); the host-only `conformance`
        # block on it carries the orchestration inputs + the HFP URL.
        self.assertIsNotNone(
            self.cpe,
            "no deviceConfig cpe entry matches this slot's platform — check that "
            "the slot's `platform` in rack_config.yml names one in device_config.yml",
        )
        self.conf = self.cpe.get("conformance", {})
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
            self._on_target(
                f"mkdir -p {self.install_dir} && tar -xzf {remote_pkg} -C {self.install_dir}",
                "UNPACK_DONE_", timeout=60,
            )

        # Either way, the binary must be there and executable before we go on.
        out = self._on_target(f"test -x {self.install_dir}/{self.binary}", "DEPLOY_DONE_", timeout=30)
        self.assertIn(
            "DEPLOY_DONE_0", out,
            f"conformance binary not present/executable at {self.install_dir}/{self.binary}",
        )

    # --- launch -------------------------------------------------------------
    def test_05_launch_target(self):
        """Bring the target environment up with the slot's `launch` command."""
        if not self.launch_cmd:
            self.log.step("launch: none configured — assuming the target is already up")
            return
        self.log.step(f"launch: {self.launch_cmd}")
        out = self._on_target(
            f"cd {self.install_dir} && {self.launch_cmd}", "LAUNCH_DONE_", timeout=300,
        )
        self.log.debug(f"launch output:\n{out}")
        self.assertIn(
            "LAUNCH_DONE_0", out,
            f"target launch command failed: {self.launch_cmd}\n--- target output ---\n{out}",
        )

    # --- run + adjudicate ---------------------------------------------------
    def test_10_run_conformance(self):
        """Run the selected scope against the launched target and parse xUnit."""
        remote_xml = f"{self.results_dir}/rialto_conformance.xml"
        self._on_target(f"mkdir -p {self.results_dir}", "RESULTS_DIR_DONE_", timeout=15)

        # Host fetches the platform HFP from its URL, then ships the resolved file
        # to the target. deviceConfig is never shipped; the target sees only the HFP.
        local_hfp = self._fetch_hfp(self.hfp_url)
        self._copy_to_target(local_hfp, self.remote_hfp)

        try:
            # Automated mode emits xUnit/JUnit XML; -p loads the HFP so the single
            # binary self-selects the cases this target's platform exposes. -e
            # narrows to one level when a scope was asked for.
            self.log.step(f"run: scope={self.scope} tier={self.tier}")
            run_out = self._on_target(
                f"cd {self.install_dir} && {self._env_prefix()}RIALTO_CONFORMANCE_TIER={self.tier} "
                f"RIALTO_CONFORMANCE_SCOPE={self.scope} "
                f"./{self.binary} -a -p {self.remote_hfp} "
                f"-l {self.results_dir}/",
                "RUN_DONE_", timeout=900,
            )
            # The tail of the run says how the binary ended, which is the first
            # thing wanted when no report turns up.
            self.log.step(f"run ended: ...{run_out[-400:]}")

            # ut-core names its report after the run's timestamp
            # (ut-log_<date>_<time>-report.xml), so the newest one in the results
            # directory is this run's. Collect it to a stable name on the target
            # rather than guessing the name from the host.
            collected = self._on_target(
                f"cp \"$(ls -1t {self.results_dir}/*-report.xml | head -1)\" {remote_xml}",
                "COLLECT_DONE_", timeout=30,
            )
            self.assertIn(
                "COLLECT_DONE_0", collected,
                f"the run produced no xUnit report in {self.results_dir} — "
                f"the binary did not complete\n--- target output ---\n{collected}",
            )

            local_xml = os.path.join(self.local_results, "rialto_conformance.xml")
            self._copy_from_target(remote_xml, local_xml)
        finally:
            self._teardown_target()

        self._adjudicate(local_xml)

    def tearDown(self):
        self.dut.session.close()

    # --- helpers ------------------------------------------------------------
    def _on_target(self, command, marker, timeout):
        """Run one command on the target and wait for it to finish.

        The console is an interactive shell, so it echoes back what it is sent
        before running it. A completion marker written plainly is therefore
        present the instant the command is typed — waiting for it returns while
        the command is still running, and every later read is out of step with
        the shell. Sending the marker as two shell literals keeps it out of the
        echoed line, so the first occurrence read back is the real one.

        Returns the console output, ending `<marker><exit status>`.
        """
        emit = f'echo "{marker[:3]}""{marker[3:]}"$?'
        self.dut.session.write(f"{command} ; {emit}")

        # Read until the marker arrives or the deadline passes, rather than in one
        # call: a single read_until has been observed returning mid-command on a
        # long run, and taking that at face value means acting on a command that
        # has not finished — collecting a report the binary has not written yet.
        # Re-reading costs nothing when the marker is already there.
        deadline = time.monotonic() + timeout
        out = ""
        while marker not in out:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            out += self.dut.session.read_until(marker, timeout=min(30.0, remaining))
        return out

    def _env_prefix(self):
        """Shell prefix that sources the launch-written env file, if configured."""
        return f". ./{self.env_file} && " if self.env_file else ""

    def _teardown_target(self):
        """Run the slot's `teardown` command, if any. Never fails the run."""
        if not self.teardown_cmd:
            return
        try:
            self._on_target(f"cd {self.install_dir} && {self.teardown_cmd}", "TEARDOWN_DONE_", timeout=120)
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

    def _console(self):
        """The slot's ssh console entry from rack_config.yml.

        rawConfig is the slot's device dict as raft decoded it; the console it
        actually opened is the first enabled one, which is what `default` names.
        """
        dut = self.dut.rawConfig.get("dut", {})
        for entry in dut.get("consoles", []):
            for console in entry.values():
                return console
        return {}

    def _ssh_target(self):
        console = self._console()
        user = console.get("username", "root")
        ip = console.get("ip") or self.dut.rawConfig.get("dut", {}).get("ip")
        return f"{user}@{ip}"

    def _scp(self, src, dst):
        """Copy a file to or from the target over scp.

        The slot's console is the authority on how the target is reached, so the
        port comes from it — a slot on any other port than 22 would otherwise be
        copied to whatever answers on 22. An optional `key` is used when the slot
        names one; batch mode keeps a missing credential a failure rather than an
        interactive prompt that never returns.
        """
        console = self._console()
        cmd = ["scp", "-q", "-B",
               "-o", "StrictHostKeyChecking=no",
               "-o", "UserKnownHostsFile=/dev/null",
               "-o", "LogLevel=ERROR",
               "-P", str(console.get("port", 22))]
        key = console.get("key")
        if key:
            if not os.path.isabs(key):
                key = os.path.join(REPO_ROOT, key)
            cmd += ["-i", key]
        subprocess.run(cmd + [src, dst], check=True)

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
