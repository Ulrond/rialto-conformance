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

  1. Deploy the standalone package (packaging/package.sh output) to the target.
  2. Launch the on-target binary in automated mode with the target's KVP profile:
         rialto_conformance -a -p <kvpProfile>
  3. Pull the produced xUnit/JUnit XML back to the host and adjudicate it.

raft is GIVEN the target by config (--rack/--slotName select the slot in
rack_config.yml; the slot's `platform` links to device_config.yml). It has no
concept of the platform — the only platform-specific input is which KVP profile
device_config names, and that is shipped, not coded. The same binary and the
same cases run on every target.

Run:
    python raft/suites/test_rialto_conformance.py \
        --config raft/rack_config.yml --rack rack1 --slotName reference-target
"""

import os
import subprocess
import xml.etree.ElementTree as ET

from raft import RAFTUnitTestCase, RAFTUnitTestMain  # provided by python_raft / ut-raft

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


class RialtoConformance(RAFTUnitTestCase):
    """Deploy + run the conformance binary on a config-named target, collect xUnit."""

    def setUp(self):
        self.dut.session.open()
        # Per-target conformance params come from device_config.yml — never code.
        # self.dut.config is the cpe entry (deviceConfig/cpe1); the conformance
        # block + the rialto capability gate both live under it.
        self.conf = self.dut.config.get("conformance", {})
        self.install_dir = self.conf.get("installDir", "/opt/rialto-conformance")
        self.binary = self.conf.get("binary", "rialto_conformance")
        self.results_dir = self.conf.get("resultsDir", f"{self.install_dir}/results")
        # The deviceConfig file IS the on-target KVP profile (it carries
        # deviceConfig/cpe1/rialto/*). Ship it and load it with -p.
        self.device_config = os.path.join(REPO_ROOT, "raft", "device_config.yml")
        self.remote_profile = f"{self.install_dir}/deviceConfig.yml"
        self.local_results = os.path.join(REPO_ROOT, "logs", "results")
        os.makedirs(self.local_results, exist_ok=True)

    # --- deploy -------------------------------------------------------------
    def test_00_deploy_package(self):
        """Build + install the standalone package onto the target."""
        package = self._build_package()
        remote_pkg = f"/tmp/{os.path.basename(package)}"
        self._copy_to_target(package, remote_pkg)
        self.dut.session.write(f"mkdir -p {self.install_dir} && tar -xzf {remote_pkg} -C {self.install_dir}")
        self.dut.session.read_until(self.dut.session.prompt, timeout=60)
        # Confirm the binary landed and is executable.
        self.dut.session.write(f"test -x {self.install_dir}/{self.binary} && echo DEPLOY_OK")
        out = self.dut.session.read_until(self.dut.session.prompt, timeout=30)
        self.assertIn("DEPLOY_OK", out, "conformance binary not deployed/executable on target")

    # --- run + adjudicate ---------------------------------------------------
    def test_10_run_conformance(self):
        """Run `-a -p <profile>` against the installed Rialto and parse xUnit."""
        remote_xml = f"{self.results_dir}/rialto_conformance.xml"
        self.dut.session.write(f"mkdir -p {self.results_dir}")
        self.dut.session.read_until(self.dut.session.prompt, timeout=15)

        # Ship the deviceConfig (the capability profile) to the target.
        self._copy_to_target(self.device_config, self.remote_profile)

        # Automated mode emits xUnit/JUnit XML; -p loads the deviceConfig so the
        # single binary self-selects the cases this target's platform exposes.
        self.dut.session.write(
            f"cd {self.install_dir} && ./{self.binary} -a -p {self.remote_profile} "
            f"-l {self.results_dir}/ ; echo RUN_DONE_$?"
        )
        self.dut.session.read_until("RUN_DONE_", timeout=900)

        local_xml = os.path.join(self.local_results, "rialto_conformance.xml")
        self._copy_from_target(remote_xml, local_xml)
        self._adjudicate(local_xml)

    def tearDown(self):
        self.dut.session.close()

    # --- helpers ------------------------------------------------------------
    def _build_package(self):
        """Produce the installable tarball on the host (packaging/package.sh)."""
        out = subprocess.run(
            [os.path.join(REPO_ROOT, "packaging", "package.sh")],
            cwd=REPO_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        # package.sh prints the artifact path on its last line.
        path = out.stdout.strip().splitlines()[-1]
        self.assertTrue(os.path.isfile(path), f"package.sh did not produce an artifact: {path}")
        return path

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
            f"conformance: {total} cases, {failures} failed, {errors} errored, "
            f"{skipped} skipped (capability-gated)"
        )
        self.assertEqual(failures, 0, f"{failures} conformance case(s) failed")
        self.assertEqual(errors, 0, f"{errors} conformance case(s) errored")
        self.assertGreater(total - skipped, 0, "no applicable cases ran on this target")


if __name__ == "__main__":
    RAFTUnitTestMain()
