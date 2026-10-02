"""Focused tests for the P0-R1 review-readiness verifier."""

from __future__ import annotations

import io
import json
import unittest
from pathlib import Path

import generate_evidence as evidence_generator
import verify_review_candidate as review


class ReviewReadinessVerifierTests(unittest.TestCase):
    def test_sha256_bytes_uses_raw_bytes(self) -> None:
        self.assertEqual(
            review.sha256_bytes(b"dog-rgb\n"),
            "5a1f80900c1e0362930b40b65000565bd2dfb0aef11ba6fc9d7bed45e768c5c0",
        )

    def test_json_serialization_is_deterministic_utf8_with_lf(self) -> None:
        evidence = {"label": "café", "evidence_schema": "dog-rgb-cloud-phase0b/1"}
        expected = (
            b'{\n'
            b'  "evidence_schema": "dog-rgb-cloud-phase0b/1",\n'
            b'  "label": "caf\xc3\xa9"\n'
            b'}\n'
        )
        serialized = evidence_generator.canonical_json_bytes(evidence)

        self.assertEqual(serialized, expected)
        self.assertEqual(evidence_generator.canonical_json_bytes(evidence), serialized)
        self.assertEqual(json.loads(serialized), evidence)
        self.assertNotIn(b"\r", serialized)

    def test_markdown_serialization_uses_utf8_and_lf(self) -> None:
        evidence = {
            "storage": {
                "raw_ring": {
                    "retention": [
                        {
                            "profile": "adaptive_4h_moving_20h_stationary",
                            "retention_days": 1.25,
                        }
                    ],
                    "workload": {
                        "capacity_chunks": 12,
                        "program_bytes_per_successful_seal": 10.0,
                        "erase_bytes_per_successful_seal": 20.0,
                        "power_cuts": 1,
                        "unacknowledged_after_run": 2,
                    },
                },
                "littlefs_segment_log": {
                    "retention": [
                        {
                            "profile": "adaptive_4h_moving_20h_stationary",
                            "retention_days": 2.5,
                        }
                    ],
                    "workload": {
                        "capacity_chunks": 24,
                        "program_bytes_per_successful_seal": 30.0,
                        "erase_bytes_per_successful_seal": 40.0,
                        "power_cuts": 3,
                        "unacknowledged_after_run": 4,
                    },
                },
            }
        }

        serialized = evidence_generator.canonical_markdown_bytes(evidence)

        self.assertTrue(serialized.endswith(b"\n"))
        self.assertNotIn(b"\r", serialized)
        self.assertIn(b"| Raw ring | 12 | 1.250 |", serialized)

    def test_stdout_write_bypasses_windows_text_newline_translation(self) -> None:
        class WindowsLikeStdout:
            def __init__(self) -> None:
                self.buffer = io.BytesIO()
                self.text_output = ""

            def write(self, value: str) -> int:
                translated = value.replace("\n", "\r\n")
                self.text_output += translated
                return len(value)

        stream = WindowsLikeStdout()
        payload = evidence_generator.canonical_json_bytes(
            {"evidence_schema": "dog-rgb-cloud-phase0b/1", "label": "café"}
        )

        evidence_generator.write_stdout(payload, stream)

        self.assertEqual(stream.buffer.getvalue(), payload)
        self.assertNotIn(b"\r", stream.buffer.getvalue())
        self.assertEqual(stream.text_output, "")

    def test_verifier_rejects_newline_or_content_drift(self) -> None:
        expected = (
            b'{\n'
            b'  "evidence_schema": "dog-rgb-cloud-phase0b/1",\n'
            b'  "label": "caf\xc3\xa9"\n'
            b'}\n'
        )
        expected_digest = review.sha256_bytes(expected)
        arguments = (0, len(expected), expected_digest)

        self.assertTrue(review.canonical_evidence_matches(expected, *arguments))
        self.assertFalse(
            review.canonical_evidence_matches(
                expected.replace(b"\n", b"\r\n"),
                *arguments,
            )
        )
        drifted = expected.replace(b'"label": "caf\xc3\xa9"', b'"label": "doggo"')
        self.assertEqual(len(drifted), len(expected))
        self.assertFalse(review.canonical_evidence_matches(drifted, *arguments))
        self.assertFalse(
            review.canonical_evidence_matches(
                expected,
                1,
                len(expected),
                expected_digest,
            )
        )

    def test_dirty_or_author_mode_is_never_review_eligible(self) -> None:
        self.assertFalse(review.ACCEPTANCE_MAY_BE_DECIDED_BY_THIS_TOOL)
        self.assertTrue(review.is_review_eligible(True, True, False))
        self.assertFalse(review.is_review_eligible(True, False, False))
        self.assertFalse(review.is_review_eligible(True, True, True))

    def test_unittest_count_is_parsed_only_from_runner_summary(self) -> None:
        self.assertEqual(review.parse_unittest_count(b"Ran 51 tests in 1.0s\n\nOK\n"), 51)
        self.assertIsNone(review.parse_unittest_count(b"51 green checks"))

    def test_all_required_regressions_exist(self) -> None:
        source = Path(review.REPOSITORY_ROOT / "tools/cloud_phase0/test_phase0.py").read_text(
            encoding="utf-8"
        )
        integrity_source = Path(
            review.REPOSITORY_ROOT / "tools/cloud_phase0/test_integrity.py"
        ).read_text(encoding="utf-8")
        self.assertEqual(review.missing_required_regressions(source, integrity_source), [])

    def test_all_candidate_source_artifacts_match_static_manifest(self) -> None:
        self.assertEqual(len(review.CANDIDATE_PATHS), 9)
        self.assertEqual(set(review.CANDIDATE_SOURCE_MANIFEST), set(review.CANDIDATE_PATHS))
        for path in review.CANDIDATE_PATHS:
            with self.subTest(path=path):
                expected = review.CANDIDATE_SOURCE_MANIFEST[path]
                content = (review.REPOSITORY_ROOT / path).read_bytes()
                self.assertTrue(
                    review.artifact_matches_manifest(content, expected),
                    path,
                )

    def test_source_manifest_rejects_drift_untracked_candidate_and_wrong_byte_count(self) -> None:
        content = b"candidate artifact\n"
        expected = {
            "bytes": len(content),
            "sha256": review.sha256_bytes(content),
        }

        self.assertTrue(review.artifact_matches_manifest(content, expected))
        self.assertFalse(review.artifact_matches_manifest(b"candidate artifacx\n", expected))
        # No committed blob is how candidate_source_digests represents an untracked path.
        self.assertFalse(review.artifact_matches_manifest(None, expected))
        wrong_count = {**expected, "bytes": len(content) + 1}
        self.assertFalse(review.artifact_matches_manifest(content, wrong_count))


if __name__ == "__main__":
    unittest.main()
