#!/usr/bin/env python3
"""Host tests for install/provision shell-script behavior."""

from __future__ import annotations

import os
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path


TEST_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = TEST_DIR.parent.parent
INSTALL_SH = PROJECT_ROOT / "install.sh"
PROVISION_SH = PROJECT_ROOT / "scripts" / "provision.sh"


def _write_executable(path: Path, content: str) -> None:
    path.write_text(content, encoding="utf-8")
    mode = path.stat().st_mode
    path.chmod(mode | stat.S_IXUSR)


class InstallProvisionScriptTests(unittest.TestCase):
    def _run_install_with_prefs(self, prefs_text: str, extra_args: list[str]) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            home = tmp / "home"
            config_dir = home / ".config" / "zclaw"
            config_dir.mkdir(parents=True, exist_ok=True)
            (config_dir / "install.env").write_text(prefs_text, encoding="utf-8")

            env = os.environ.copy()
            env["HOME"] = str(home)
            env["XDG_CONFIG_HOME"] = str(home / ".config")
            # Keep PATH narrow so QEMU is treated as missing in CI/macOS hosts.
            env["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"
            env["TERM"] = "dumb"

            cmd = [
                str(INSTALL_SH),
                "--no-build",
                "--no-install-idf",
                "--no-repair-idf",
                "--no-flash",
                "--no-provision",
                "--no-monitor",
                *extra_args,
            ]
            return subprocess.run(
                cmd,
                cwd=PROJECT_ROOT,
                env=env,
                text=True,
                capture_output=True,
                check=False,
            )

    def test_install_auto_applies_saved_qemu_choice(self) -> None:
        prefs = """# zclaw install.sh preferences
INSTALL_IDF=n
REPAIR_IDF=
INSTALL_QEMU=n
INSTALL_CJSON=
BUILD_NOW=n
REPAIR_BUILD_IDF=
FLASH_NOW=
FLASH_MODE=1
PROVISION_NOW=
MONITOR_AFTER_FLASH=
LAST_PORT=
"""
        proc = self._run_install_with_prefs(prefs, [])
        output = f"{proc.stdout}\n{proc.stderr}"
        self.assertEqual(proc.returncode, 0, msg=output)
        self.assertIn("Install QEMU for ESP32 emulation?: no (saved)", output)

    def test_install_cli_override_beats_saved_qemu_choice(self) -> None:
        prefs = """# zclaw install.sh preferences
INSTALL_IDF=n
REPAIR_IDF=
INSTALL_QEMU=y
INSTALL_CJSON=
BUILD_NOW=n
REPAIR_BUILD_IDF=
FLASH_NOW=
FLASH_MODE=1
PROVISION_NOW=
MONITOR_AFTER_FLASH=
LAST_PORT=
"""
        proc = self._run_install_with_prefs(prefs, ["--no-qemu"])
        output = f"{proc.stdout}\n{proc.stderr}"
        self.assertEqual(proc.returncode, 0, msg=output)
        self.assertIn("Install QEMU for ESP32 emulation?: no", output)
        self.assertNotIn("Install QEMU for ESP32 emulation?: no (saved)", output)

    def _run_provision_detect(self, env_ssid: str, nmcli_output: str) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            bin_dir = tmp / "bin"
            bin_dir.mkdir(parents=True, exist_ok=True)

            _write_executable(
                bin_dir / "uname",
                "#!/bin/sh\n"
                "echo Linux\n",
            )
            _write_executable(
                bin_dir / "nmcli",
                "#!/bin/sh\n"
                f"printf '%s\\n' '{nmcli_output}'\n",
            )

            env = os.environ.copy()
            env["PATH"] = f"{bin_dir}:/usr/bin:/bin"
            env["ZCLAW_WIFI_SSID"] = env_ssid
            env["TERM"] = "dumb"

            return subprocess.run(
                [str(PROVISION_SH), "--print-detected-ssid"],
                cwd=PROJECT_ROOT,
                env=env,
                text=True,
                capture_output=True,
                check=False,
            )

    def test_provision_detect_ignores_placeholder_env_ssid(self) -> None:
        proc = self._run_provision_detect("<redacted>", "yes:RealNetwork")
        output = f"{proc.stdout}\n{proc.stderr}"
        self.assertEqual(proc.returncode, 0, msg=output)
        self.assertEqual(proc.stdout.strip(), "RealNetwork")

    def test_provision_detect_uses_non_placeholder_env_ssid(self) -> None:
        proc = self._run_provision_detect(":smiley:", "yes:RealNetwork")
        output = f"{proc.stdout}\n{proc.stderr}"
        self.assertEqual(proc.returncode, 0, msg=output)
        self.assertEqual(proc.stdout.strip(), ":smiley:")


if __name__ == "__main__":
    unittest.main()
