"""Regression checks for simulator configuration, reproducibility and report semantics.

Run after building sim.exe: python test_simulator.py [path/to/sim.exe]
Uses only Python's standard library; each case plays a small number of games.
"""
from pathlib import Path
import re
import subprocess
import sys
import unittest

SIM = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).with_name("sim.exe")


def run(*args):
    return subprocess.run([str(SIM), *args], text=True, capture_output=True, timeout=30)


class SimulatorTests(unittest.TestCase):
    def success(self, *args):
        result = run("--games", "8", "--maxtempo", "12", *args)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_compensation_and_cash_override(self):
        output = self.success()
        self.assertIn("rules v1.5", output)
        self.assertIn("Initial cash by order: 1000000/1000000/1000000/1000000", output)
        self.assertIn("Initial cash by order: 1200000/1200000/1200000/1200000",
                      self.success("--cash", "1200000"))
        self.assertIn("Initial cash by order: 925000/975000/1025000/1075000",
                      self.success("--orderstep", "50000"))

    def test_rent_scaling_preserves_grade_factors(self):
        output = self.success("--rentpct", "80")
        self.assertIn("Rent factors D/C/B/A/A+: 3/6/9/15/30", output)
        self.assertIn("rent at base_rent=10000: 24000/48000/72000/120000/240000", output)
        output = self.success("--rentpct", "33", "--rentx", "2")
        self.assertIn("rent at base_rent=10000: 19800/39600/59400/99000/198000", output)

    def test_reports_and_invariants(self):
        # Explicitly exercise the old >2000-game branch that skipped invariants.
        output = self.success("--games", "2001", "--maxtempo", "1")
        self.assertIn("Games completed:        2001", output)
        self.assertIn("Incomplete games:       0", output)
        self.assertIn("Invariant failures:     0", output)
        count = int(re.search(r"Invariant checks run:\s+(\d+)", output).group(1))
        self.assertGreater(count, 2001)
        self.assertIn("Net worth growth:", output)
        self.assertIn("Cash supply growth:", output)
        self.assertNotIn("Money supply growth:", output)
        self.assertIn("After tempo 1: games=2001", output)
        self.assertIn("Liquidity reference:    127500", output)
        self.assertIn("First grade A:", output)

    def test_reproducible_runs(self):
        self.assertEqual(self.success("--seed", "17"), self.success("--seed", "17"))

    def test_bot_options_and_short_checkpoints(self):
        output = self.success("--reserve", "250000", "--upgrade-first", "--highest-grade", "--maxtempo", "1")
        self.assertIn("Bot: reserve=250000, upgrade_first=1", output)
        self.assertIn("highest_grade=1", output)
        self.assertEqual(output.count("After tempo 1:"), 1)

    def test_zero_initial_wealth_is_not_a_growth_ratio(self):
        output = self.success("--cash", "0", "--orderstep", "0", "--maxtempo", "1", "--salary", "0", "--nocards")
        self.assertIn("Net worth growth:       n/a", output)
        self.assertIn("Cash supply growth:     n/a", output)
        self.assertIn("First grade A:          n/a", output)

    def test_invalid_arguments_are_refused(self):
        cases = [
            ["--games", "0"], ["--games", "-1"], ["--games", "abc"],
            ["--seed", "-1"], ["--seed", "4294967296"],
            ["--salary"], ["--salary", "-1"], ["--reserve", "-1"],
            ["--cash", "1", "--orderstep", "50000"], ["--orderstep", "-1"],
            ["--rentpct", "-1"], ["--exambonus", "nan"],
            ["--exambonus", "inf"], ["--stealprob", "1.1"], ["--unknown"],
        ]
        for args in cases:
            with self.subTest(args=args):
                result = run(*args)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn("Configuration error:", result.stderr)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
