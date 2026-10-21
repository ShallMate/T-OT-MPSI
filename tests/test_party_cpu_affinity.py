import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "party_cpu_affinity",
    Path(__file__).resolve().parents[1] / "scripts" / "party_cpu_affinity.py",
)
affinity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(affinity)


class PartyCpuAffinityTest(unittest.TestCase):
    def test_prefers_different_physical_cores(self):
        topology = {0: (0, 0), 1: (0, 1), 8: (0, 0), 9: (0, 1)}
        with patch.object(affinity.os, "sched_getaffinity", return_value=set(topology)), \
             patch.object(affinity, "physical_core", side_effect=topology.__getitem__):
            self.assertEqual(affinity.select_party_cpus(2), ([0, 1], 2))

    def test_respects_allowed_cpu_subset(self):
        topology = {3: (0, 3), 8: (0, 0), 11: (0, 3)}
        with patch.object(affinity.os, "sched_getaffinity", return_value=set(topology)), \
             patch.object(affinity, "physical_core", side_effect=topology.__getitem__):
            self.assertEqual(affinity.select_party_cpus(2), ([3, 8], 2))

    def test_excess_parties_share_one_cpu_each(self):
        with patch.object(affinity.os, "sched_getaffinity", return_value={4, 6}), \
             patch.object(affinity, "physical_core", side_effect=lambda cpu: (0, cpu)):
            self.assertEqual(affinity.select_party_cpus(5), ([4, 6, 4, 6, 4], 2))

    def test_rejects_empty_cpu_budget(self):
        with patch.object(affinity.os, "sched_getaffinity", return_value=set()):
            with self.assertRaises(ValueError):
                affinity.select_party_cpus(1)


if __name__ == "__main__":
    unittest.main()
