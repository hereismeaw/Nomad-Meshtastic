"""Turn this unit's conducted RF measurements into an explicit firmware header."""

import argparse
import csv
from pathlib import Path


def generate(source: Path, destination: Path) -> None:
    steps = []
    seen = set()
    with source.open(newline="", encoding="utf-8-sig") as stream:
        for row in csv.DictReader(stream):
            for field in ("unit_serial", "meter_serial", "measured_at"):
                if not row.get(field, "").strip():
                    raise ValueError(f"Missing measurement provenance: {field}")
            radio = int(row["radio"])
            frequency = int(row["frequency_khz"])
            target = int(row["target_dbm"])
            measured = round(float(row["measured_dbm"]) * 100)
            drive = int(row["drive_dbm"])
            dac = int(row["dac"])
            if not (radio in (0, 1) and 150000 <= frequency <= 2500000):
                raise ValueError("Invalid radio or frequency")
            if not (-17 <= drive <= 5 and 95 <= dac <= 120):
                raise ValueError("Setting is outside the verified Nomad reference envelope")
            if not (-20 <= target <= 30 and abs(measured - target * 100) <= 50):
                raise ValueError("Measurement must match the target within 0.5 dB")
            key = (radio, frequency, target)
            if key in seen:
                raise ValueError(f"Duplicate measurement step: {key}")
            seen.add(key)
            steps.append(f"    {{{radio}, {frequency}, {target}, {measured}, {drive}, {dac}}},")
    if not steps:
        raise ValueError("No measurements; refusing to create an enabled calibration table")
    destination.write_text(
        "#pragma once\n#define NOMAD_CALIBRATION_STEPS \\\n"
        + " \\\n".join(steps)
        + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("header", type=Path)
    args = parser.parse_args()
    generate(args.csv, args.header)
