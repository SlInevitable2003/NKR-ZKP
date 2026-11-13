"""Pure-logic tests; these do not build or run any protocol binary."""

from __future__ import annotations

import unittest

from scripts.bench.cases import (
    expand_functional,
    expand_p1,
    expand_p2,
    expand_p3,
    filter_tier,
    matmul_size_for_bits,
    next_power_of_two,
    param_for_bits,
    stem_for,
)
from scripts.bench.config import Config
from scripts.bench.protocols import parse_profile, parse_stark


class ScaleMappingTest(unittest.TestCase):
    def test_matmul_fits_exact_power(self) -> None:
        for k in range(20, 27):
            size = matmul_size_for_bits(k)
            self.assertEqual(next_power_of_two(size ** 3 + size ** 2), 1 << k)

    def test_circuit_params(self) -> None:
        self.assertEqual(param_for_bits("fibonacci", 20), 1 << 20)
        self.assertEqual(param_for_bits("poseidon", 20), 1 << 11)
        self.assertEqual(param_for_bits("sql", 20), 1 << 10)

    def test_stem_density_suffix(self) -> None:
        self.assertEqual(stem_for("matmul", 101, "full"), "matmul_101")
        self.assertEqual(stem_for("matmul", 101, "sparse"), "matmul_101_sparse")


class CaseExpansionTest(unittest.TestCase):
    def setUp(self) -> None:
        self.config = Config.load()

    def test_functional_count(self) -> None:
        self.assertEqual(len(expand_functional(self.config)), 36)

    def test_performance_counts(self) -> None:
        self.assertEqual(len(expand_p1(self.config)), 210)
        self.assertEqual(len(expand_p2(self.config)), 140)
        self.assertEqual(len(expand_p3(self.config)), 20)

    def test_smoke_keeps_p3(self) -> None:
        cases = filter_tier(expand_p3(self.config), "smoke", self.config)
        self.assertTrue(cases)
        self.assertTrue(all(case.waves == 1 for case in cases))

    def test_standard_keeps_p3(self) -> None:
        cases = filter_tier(expand_p3(self.config), "standard", self.config)
        self.assertEqual(len(cases), 20)


class ParserTest(unittest.TestCase):
    def test_profile(self) -> None:
        text = "[profile] groth16.prove.total = 12.500 ms\n"
        self.assertEqual(parse_profile(text)["groth16.prove.total"], 12.5)

    def test_stark(self) -> None:
        text = (
            "Proof generated in 123 ms\n"
            "Proof size: 4.5 KB\n"
            "Proof verified in 1.25 ms\n"
        )
        parsed = parse_stark(text)
        self.assertEqual(parsed["prove_total_ms"], 123.0)
        self.assertEqual(parsed["proof_size_kb"], 4.5)
        self.assertEqual(parsed["verify_total_ms"], 1.25)


if __name__ == "__main__":
    unittest.main()
