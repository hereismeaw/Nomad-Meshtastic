"""Package successful builds and record evidence without accessing the device."""

import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
FIRMWARE = ROOT / "firmware"
BUILDS = {
    "radiomaster-nomad": "nomad-rgb-build.log",
    "radiomaster-nomad-rf-lab": "nomad-rgb-lab-build.log",
    "tlora-v2-1-1_8": "regression-esp32-build.log",
}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def git(directory, *arguments):
    return subprocess.check_output(["git", "-C", str(directory), *arguments], text=True).strip()


def main():
    builds = {}
    for environment, log_name in BUILDS.items():
        log = (ROOT / log_name).read_text(encoding="utf-8", errors="replace")
        if not re.search(rf"^{re.escape(environment)}\s+SUCCESS\s+", log, re.MULTILINE):
            raise RuntimeError(f"No successful build evidence: {environment}")
        usage = {}
        for key, label in (("static_ram", "RAM"), ("application_flash", "Flash")):
            matches = re.findall(rf"{label}:.*used (\d+) bytes from (\d+) bytes", log)
            if matches:
                used, maximum = matches[-1]
                usage[key] = {"used_bytes": int(used), "maximum_bytes": int(maximum)}
        builds[environment] = {"status": "passed", "log": log_name, "usage": usage}

    tests = {}
    for suite, name in (("test_radio", "regression-native-final.log"), ("test_nomad_policy", "policy-native-final.log")):
        log = (ROOT / name).read_text(encoding="utf-8", errors="replace")
        cases = re.findall(r"(\d+) test cases: (\d+) succeeded", log)
        if not cases or cases[-1][0] != cases[-1][1] or "RESULT: FILTERED" not in log:
            raise RuntimeError(f"No successful filtered test evidence: {suite}")
        tests[suite] = {"environment": "coverage", "cases_passed": int(cases[-1][1]), "log": name, "scope": "filtered"}
    calibration = (ROOT / "calibration-tests.log").read_text(encoding="utf-8", errors="replace")
    count = re.search(r"Ran (\d+) tests", calibration)
    if not count or not re.search(r"^OK$", calibration, re.MULTILINE):
        raise RuntimeError("Calibration generator tests have not passed")
    tests["calibration_generator"] = {"cases_passed": int(count[1]), "log": "calibration-tests.log"}
    status_log = (ROOT / "nomad-rgb-policy-tests.log").read_text(encoding="utf-8", errors="replace")
    if "PASS" not in status_log:
        raise RuntimeError("Status-light policy tests have not passed")
    tests["nomad_status"] = {"environment": "Linux standalone, ASan/UBSan", "log": "nomad-rgb-policy-tests.log",
                             "scope": "boot timing, timer wrap, Bluetooth and radio indication priority"}

    for environment in BUILDS:
        source = FIRMWARE / ".pio" / "build" / environment
        destination = ROOT / "artifacts" / environment
        destination.mkdir(parents=True, exist_ok=True)
        if not source.exists() and environment == "tlora-v2-1-1_8":
            previous = json.loads((ROOT / "nomad-build-report.json").read_text(encoding="utf-8"))
            packaged = previous["builds"][environment]["artifacts"]
            for item in packaged:
                path = ROOT / item["path"]
                if not path.is_file() or path.stat().st_size != item["bytes"] or digest(path) != item["sha256"]:
                    raise RuntimeError(f"Previous regression artifact missing or changed: {path}")
            builds[environment].update(artifacts=packaged, reused_previous_artifacts=True,
                                       scope="Previous regression build; not rebuilt for Nomad-only status-light changes")
            continue
        packaged = []
        for path in sorted(source.iterdir()):
            if path.is_file() and (path.suffix in (".bin", ".elf") or path.name.endswith(".mt.json")):
                target = destination / path.name
                shutil.copy2(path, target)
                packaged.append({"path": str(target.relative_to(ROOT)).replace("\\", "/"), "bytes": target.stat().st_size,
                                 "sha256": digest(target)})
        if not any(item["path"].endswith(".factory.bin") for item in packaged):
            raise RuntimeError(f"Missing factory image: {environment}")
        (destination / "SHA256.txt").write_text(
            "".join(f"{item['sha256']}  {Path(item['path']).name}\n" for item in packaged), encoding="utf-8")
        builds[environment]["artifacts"] = packaged

    patch = ROOT / "nomad-changes.patch"
    subprocess.run(["git", "-C", str(FIRMWARE), "diff", "--binary", f"--output={patch}"], check=True)
    report = {
        "source_revisions": {"meshtastic": git(FIRMWARE, "rev-parse", "HEAD"),
                             "expresslrs": git(ROOT / "ExpressLRS", "rev-parse", "HEAD"),
                             "targets": git(ROOT / "targets", "rev-parse", "HEAD")},
        "modified_firmware_files": git(FIRMWARE, "diff", "--name-only").splitlines(),
        "source_patch": {"path": patch.name, "sha256": digest(patch)},
        "builds": builds,
        "tests": tests,
        "hardware": {"observed_port": "COM3", "usb_bridge": "Silicon Labs CP210x", "flashed": False,
                     "original_flash_backed_up": False, "conducted_power_measured": False,
                     "backpack_firmware_written": False, "dual_rx_verified_on_hardware": False},
        "limitations": ["No per-unit measured calibration is shipped; RF TX remains inhibited without calibration and local enable.",
                        "No phone/serial endpoints for board-specific per-radio C++ configuration.",
                        "Experimental parallel TX, diversity and optional cross-preset bridging are not implemented.",
                        "Airtime enforcement remains shared and conservative; per-radio airtime ledgers are not implemented.",
                        "Real ACK exchanges, simultaneous reception, thermal behavior and RF isolation require bench validation."]
    }
    hardware = ROOT / "nomad-hardware-report.json"
    if hardware.exists():
        report["hardware"].update(json.loads(hardware.read_text(encoding="utf-8")))
    (ROOT / "nomad-build-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Packaged all successful builds; wrote nomad-build-report.json")


if __name__ == "__main__":
    main()
