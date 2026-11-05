"""Synthetic generator tests; these values are not measurements of any hardware."""

import tempfile
import unittest
from pathlib import Path

from nomad_calibration import generate


class CalibrationGeneratorTests(unittest.TestCase):
    columns = "unit_serial,meter_serial,measured_at,radio,frequency_khz,target_dbm,measured_dbm,drive_dbm,dac\n"
    synthetic = "TEST-ONLY,TEST-METER,TEST-DATE,0,923125,30,30.0,5,95\n"

    def run_generator(self, rows):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "synthetic.csv"
            destination = Path(temporary) / "synthetic.h"
            source.write_text(self.columns + rows)
            generate(source, destination)
            return destination.read_text()

    def test_valid_synthetic_step(self):
        header = self.run_generator(self.synthetic)
        self.assertIn("{0, 923125, 30, 3000, 5, 95}", header)
        self.assertIn("#define NOMAD_CALIBRATION_STEPS", header)

    def test_missing_provenance_rejected(self):
        with self.assertRaises(ValueError):
            self.run_generator(self.synthetic.replace("TEST-METER", ""))

    def test_unvalidated_drive_rejected(self):
        with self.assertRaises(ValueError):
            self.run_generator(self.synthetic.replace(",5,95", ",22,95"))

    def test_wrong_measurement_rejected(self):
        with self.assertRaises(ValueError):
            self.run_generator(self.synthetic.replace(",30.0,", ",27.0,"))

    def test_duplicate_rejected(self):
        with self.assertRaises(ValueError):
            self.run_generator(self.synthetic * 2)

    def test_empty_table_rejected(self):
        with self.assertRaises(ValueError):
            self.run_generator("")


if __name__ == "__main__":
    unittest.main()
