"""Focused tests for the P0-R1 review-readiness verifier."""

from __future__ import annotations

import subprocess
import unittest
from pathlib import Path

import verify_review_candidate as review


class ReviewReadinessVerifierTests(unittest.TestCase):
    def test_sha256_bytes_uses_raw_bytes(self) -> None:
        self.assertEqual(
            review.sha256_bytes(b"dog-rgb\n"),
            "5a1f80900c1e0362930b40b65000565bd2dfb0aef11ba6fc9d7bed45e768c5c0",
        )

    def test_unittest_count_is_parsed_only_from_runner_summary(self) -> None:
        self.assertEqual(review.parse_unittest_count(b"Ran 51 tests in 1.0s\n\nOK\n"), 51)
        self.assertIsNone(review.parse_unittest_count(b"51 green checks"))

    def test_all_seven_required_regressions_exist(self) -> None:
        source = Path(review.REPOSITORY_ROOT / "tools/cloud_phase0/test_phase0.py").read_text(
            encoding="utf-8"
        )
        self.assertEqual(review.missing_required_regressions(source), [])

    def test_candidate_source_digest_is_bound_to_storage_artifact(self) -> None:
        digest = review.candidate_source_digests()["tools/cloud_phase0/storage_model.py"]
        self.assertEqual(digest["bytes"], review.EXPECTED_STORAGE_MODEL_BYTES)
        self.assertEqual(digest["sha256"], review.EXPECTED_STORAGE_MODEL_SHA256)


if __name__ == "__main__":
    unittest.main()
