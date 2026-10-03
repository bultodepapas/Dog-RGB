"""Build a fail-closed P0-R1 readiness record without deciding acceptance."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import re
import subprocess
import sys
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
# This is a frozen, literal commit, never a branch name, HEAD, or CLI override.
# It preserves lineage to the prior reviewed baseline. Candidate contents are
# rebaselined by the static source manifest below, not by moving this origin.
# The origin must remain an ancestor of reviewed HEAD. This gate prepares a
# candidate for independent review; it does not record or decide acceptance.
CANDIDATE_ORIGIN_COMMIT = "d58be9a0f4d9e9a31f5a302040b5575e27b1cfd0"
ACCEPTANCE_MAY_BE_DECIDED_BY_THIS_TOOL = False
EXPECTED_HOST_TESTS = 67
EXPECTED_EVIDENCE_BYTES = 9_197
EXPECTED_EVIDENCE_SHA256 = "98978d48429f446c9ac82ad91cbba46936d5aed788d9a836a1338c4490831e9c"

CANDIDATE_PATHS = (
    "tools/cloud_phase0/fixtures/reference_manifest.json",
    "tools/cloud_phase0/generate_evidence.py",
    "tools/cloud_phase0/legacy_v2.py",
    "tools/cloud_phase0/reference_fixtures.py",
    "tools/cloud_phase0/storage_model.py",
    "tools/cloud_phase0/test_phase0.py",
    "tools/cloud_phase0/test_integrity.py",
    "tools/cloud_phase0/review_integrity_test.py",
    "tools/cloud_phase0/track_v3.py",
)

# Frozen bytes and SHA-256 values bind the corrected candidate. Keep this
# explicit manifest in sync with CANDIDATE_PATHS; do not resolve values
# dynamically from HEAD or the worktree.
CANDIDATE_SOURCE_MANIFEST: dict[str, dict[str, object]] = {
    "tools/cloud_phase0/fixtures/reference_manifest.json": {
        "bytes": 2_358,
        "sha256": "d28a3354389c251053ed2d840c4b01d8bd379592c692ff7411df18038769826e",
    },
    "tools/cloud_phase0/generate_evidence.py": {
        "bytes": 3_197,
        "sha256": "6b6b9b4319cd859ec2884b6b8c68b1eb8f413da1982acce461418187a2851ff8",
    },
    "tools/cloud_phase0/legacy_v2.py": {
        "bytes": 4_131,
        "sha256": "b6ca6dc6a2f4adcdc0904640736beb497c7e86086a9ef8423728a21c3158b5f9",
    },
    "tools/cloud_phase0/reference_fixtures.py": {
        "bytes": 6_131,
        "sha256": "47f3a25eeb74494ed78754963ef3c84f4ebb2a6a87799fd02002fd596168c9aa",
    },
    "tools/cloud_phase0/storage_model.py": {
        "bytes": 101_136,
        "sha256": "0106a89c140d26439839a2c7ad80950d72b707049d52fe4c35476656050c85b7",
    },
    "tools/cloud_phase0/test_phase0.py": {
        "bytes": 53_425,
        "sha256": "77fb9bef715ce34fa2beb77bb4971de0f637613aa905c1d752fb52d3bd5ed472",
    },
    "tools/cloud_phase0/test_integrity.py": {
        "bytes": 21_850,
        "sha256": "f162452367048b4ffd6d08c64bc838962c86f11f9a840fe1149e8d5419fcc14e",
    },
    "tools/cloud_phase0/review_integrity_test.py": {
        "bytes": 272,
        "sha256": "baeea9dd09976d5ec16a365d03e205c85b91111d4da61406345b03cd8c5fa99c",
    },
    "tools/cloud_phase0/track_v3.py": {
        "bytes": 11_273,
        "sha256": "b205d374909552d15ad5add25d05aa00a185c4741907b883dd0486656329fbef",
    },
}

REQUIRED_REGRESSIONS = (
    (
        "reclaim-intent-exact-slot-binding",
        "test_stale_reclaim_intent_cannot_erase_a_refilled_sector",
    ),
    (
        "loss-tombstone-prevents-sequence-reuse",
        "test_empty_loss_tombstone_prevents_sequence_reuse_after_journal_fallback",
    ),
    (
        "maximum-loss-interval-is-bounded",
        "test_recovery_of_maximum_loss_interval_is_bounded",
    ),
    (
        "new-loss-survives-prior-ack-transition",
        "test_new_loss_is_durable_during_prior_loss_ack_transition",
    ),
    (
        "acked-corrupt-payload-is-not-reported-as-new-loss",
        "test_acked_corrupt_payload_is_not_misclassified_as_unsynchronized_loss",
    ),
    (
        "consumed-intent-cannot-erase-corrupt-refill",
        "test_consumed_stale_intent_cannot_erase_a_corrupt_refilled_slot",
    ),
    (
        "sparse-loss-finalizes-after-live-chunk-ack",
        "test_acknowledged_sparse_loss_cannot_bridge_a_live_unacked_chunk",
    ),
)

REQUIRED_INTEGRITY_REGRESSIONS = (
    (
        "acked-reclaimed-identity-cannot-be-reused",
        "test_acknowledged_reclaimed_chunk_identity_cannot_be_reused",
    ),
    (
        "stale-receipt-cannot-ack-resealed-identity",
        "test_stale_receipt_cannot_ack_resealed_logical_identity",
    ),
    (
        "corrupt-first-loss-fails-closed",
        "test_corrupt_committed_first_loss_fails_closed_before_journal_commit",
    ),
    (
        "commit-marker-damage-preserves-slot-identity",
        "test_commit_marker_damage_preserves_slot_identity",
    ),
    (
        "commit-marker-damage-preserves-loss-and-journal",
        "test_commit_marker_damage_preserves_loss_and_journal",
    ),
    (
        "damaged-commit-marker-invalid-body-is-read-only",
        "test_damaged_commit_marker_with_invalid_body_is_read_only",
    ),
)


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def run(command: list[str]) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        command,
        cwd=REPOSITORY_ROOT,
        check=False,
        capture_output=True,
    )


def git_bytes(*arguments: str) -> subprocess.CompletedProcess[bytes]:
    return run(["git", *arguments])


def parse_unittest_count(stderr: bytes) -> int | None:
    match = re.search(rb"Ran (\d+) tests?", stderr)
    return int(match.group(1)) if match else None


def source_digest(content: bytes) -> dict[str, object]:
    return {"bytes": len(content), "sha256": sha256_bytes(content)}


def artifact_matches_manifest(
    content: bytes | None,
    expected: dict[str, object] | None,
) -> bool:
    if content is None or expected is None:
        return False
    expected_bytes = expected.get("bytes")
    expected_sha256 = expected.get("sha256")
    return (
        type(expected_bytes) is int
        and expected_bytes >= 0
        and isinstance(expected_sha256, str)
        and re.fullmatch(r"[0-9a-f]{64}", expected_sha256) is not None
        and len(content) == expected_bytes
        and sha256_bytes(content) == expected_sha256
    )


def candidate_source_digests(
    reviewed_commit: str,
) -> tuple[dict[str, dict[str, object]], bool]:
    result: dict[str, dict[str, object]] = {}
    manifest_complete = set(CANDIDATE_SOURCE_MANIFEST) == set(CANDIDATE_PATHS)
    candidate_matches_manifest = manifest_complete
    for relative_path in CANDIDATE_PATHS:
        expected = CANDIDATE_SOURCE_MANIFEST.get(relative_path)
        try:
            working_content = (REPOSITORY_ROOT / relative_path).read_bytes()
        except OSError:
            working_content = None
        committed_process = git_bytes("show", f"{reviewed_commit}:{relative_path}")
        committed_content = (
            committed_process.stdout if committed_process.returncode == 0 else None
        )
        working_matches = artifact_matches_manifest(working_content, expected)
        committed_matches = artifact_matches_manifest(committed_content, expected)
        path_matches = working_matches and committed_matches
        candidate_matches_manifest = candidate_matches_manifest and path_matches
        item: dict[str, object] = {
            "working_tree": source_digest(working_content) if working_content is not None else None,
            "committed": source_digest(committed_content) if committed_content is not None else None,
            "matches_manifest": path_matches,
        }
        if expected is not None:
            item["expected"] = expected
        else:
            item["expected"] = "pending"
        result[relative_path] = item
    return result, candidate_matches_manifest


def missing_required_regressions(
    test_source: str,
    integrity_test_source: str,
) -> list[str]:
    missing = [
        test_name
        for _, test_name in REQUIRED_REGRESSIONS
        if f"def {test_name}(" not in test_source
    ]
    missing.extend(
        test_name
        for _, test_name in REQUIRED_INTEGRITY_REGRESSIONS
        if f"def {test_name}(" not in integrity_test_source
    )
    return missing


def canonical_evidence_matches(
    output: bytes,
    returncode: int,
    expected_bytes: int,
    expected_sha256: str,
) -> bool:
    """Require the exact serialized evidence bytes, including LF terminators."""
    try:
        evidence_document = json.loads(output)
        evidence_schema = evidence_document.get("evidence_schema")
    except (json.JSONDecodeError, UnicodeDecodeError, AttributeError):
        return False
    return (
        returncode == 0
        and evidence_schema == "dog-rgb-cloud-phase0b/1"
        and len(output) == expected_bytes
        and sha256_bytes(output) == expected_sha256
    )


def is_review_eligible(
    automated_checks_passed: bool,
    worktree_clean: bool,
    allow_dirty: bool,
) -> bool:
    """Keep author-mode runs and dirty trees out of independent review."""
    return automated_checks_passed and worktree_clean and not allow_dirty


def decoded_stdout(process: subprocess.CompletedProcess[bytes]) -> str:
    return process.stdout.decode("utf-8", errors="replace").strip()


def build_readiness_record(allow_dirty: bool) -> tuple[dict[str, object], bool]:
    failures: list[str] = []
    head_process = git_bytes("rev-parse", "HEAD")
    reviewed_commit = decoded_stdout(head_process)
    if head_process.returncode != 0 or not re.fullmatch(r"[0-9a-f]{40}", reviewed_commit):
        failures.append("unable to resolve a full reviewed commit")

    status_process = git_bytes("status", "--porcelain=v1", "--untracked-files=all")
    worktree_entries = decoded_stdout(status_process).splitlines()
    worktree_clean = status_process.returncode == 0 and not worktree_entries
    if not worktree_clean and not allow_dirty:
        failures.append("worktree is not clean")

    ancestor_process = git_bytes(
        "merge-base",
        "--is-ancestor",
        CANDIDATE_ORIGIN_COMMIT,
        reviewed_commit,
    )
    origin_is_ancestor = ancestor_process.returncode == 0
    if not origin_is_ancestor:
        failures.append("candidate origin commit is not an ancestor of the reviewed commit")

    source_digests, candidate_matches_manifest = candidate_source_digests(reviewed_commit)
    if not candidate_matches_manifest:
        failures.append("candidate source files do not match the frozen source manifest")

    storage_digest = source_digests["tools/cloud_phase0/storage_model.py"]
    storage_expected = CANDIDATE_SOURCE_MANIFEST.get("tools/cloud_phase0/storage_model.py")
    storage_artifact_matches = bool(
        storage_expected is not None
        and storage_digest["matches_manifest"]
    )

    test_source = (REPOSITORY_ROOT / "tools/cloud_phase0/test_phase0.py").read_text(encoding="utf-8")
    integrity_test_source = (
        REPOSITORY_ROOT / "tools/cloud_phase0/test_integrity.py"
    ).read_text(encoding="utf-8")
    missing_regressions = missing_required_regressions(test_source, integrity_test_source)
    if missing_regressions:
        failures.append("one or more required historical regressions are missing")

    test_command = [
        "python",
        "-m",
        "unittest",
        "discover",
        "-s",
        "tools/cloud_phase0",
        "-p",
        "test_*.py",
        "-v",
    ]
    test_process = run([
        sys.executable,
        "-m",
        "unittest",
        "discover",
        "-s",
        "tools/cloud_phase0",
        "-p",
        "test_*.py",
        "-v",
    ])
    test_count = parse_unittest_count(test_process.stderr)
    tests_passed = test_process.returncode == 0 and test_count == EXPECTED_HOST_TESTS
    if not tests_passed:
        failures.append(
            f"the frozen host matrix did not pass exactly {EXPECTED_HOST_TESTS} tests"
        )

    evidence_command = ["python", "tools/cloud_phase0/generate_evidence.py"]
    evidence_process = run([sys.executable, "tools/cloud_phase0/generate_evidence.py"])
    evidence_sha256 = sha256_bytes(evidence_process.stdout)
    evidence_bytes = len(evidence_process.stdout)
    try:
        evidence_document = json.loads(evidence_process.stdout)
        evidence_schema = evidence_document.get("evidence_schema")
    except (json.JSONDecodeError, UnicodeDecodeError, AttributeError):
        evidence_schema = None
    evidence_matches = canonical_evidence_matches(
        evidence_process.stdout,
        evidence_process.returncode,
        EXPECTED_EVIDENCE_BYTES,
        EXPECTED_EVIDENCE_SHA256,
    )
    if not evidence_matches:
        failures.append("canonical evidence bytes do not match the frozen digest")

    automated_checks_passed = not failures or (
        allow_dirty and failures == ["worktree is not clean"]
    )
    review_eligible = is_review_eligible(
        automated_checks_passed,
        worktree_clean,
        allow_dirty,
    )
    record: dict[str, object] = {
        "schema": "dog-rgb-cloud-phase0b-review-readiness/2",
        "decision": "awaiting_independent_review",
        "acceptance_may_be_decided_by_this_tool": ACCEPTANCE_MAY_BE_DECIDED_BY_THIS_TOOL,
        "candidate_origin_commit": CANDIDATE_ORIGIN_COMMIT,
        "reviewed_commit": reviewed_commit,
        "environment": {
            "python": platform.python_version(),
            "platform": platform.platform(),
        },
        "repository": {
            "worktree_clean": worktree_clean,
            "dirty_entry_count": len(worktree_entries),
            "origin_is_ancestor": origin_is_ancestor,
            "candidate_matches_manifest": candidate_matches_manifest,
        },
        "candidate_sources": source_digests,
        "storage_artifact": {
            "expected_bytes": storage_expected.get("bytes") if storage_expected else None,
            "expected_sha256": storage_expected.get("sha256") if storage_expected else None,
            "matches": storage_artifact_matches,
        },
        "host_matrix": {
            "command": test_command,
            "returncode": test_process.returncode,
            "tests_run": test_count,
            "expected_tests": EXPECTED_HOST_TESTS,
            "passed": tests_passed,
            "required_regressions": [
                {"id": regression_id, "test": test_name}
                for regression_id, test_name in (
                    *REQUIRED_REGRESSIONS,
                    *REQUIRED_INTEGRITY_REGRESSIONS,
                )
            ],
            "missing_required_regressions": missing_regressions,
        },
        "canonical_evidence": {
            "command": evidence_command,
            "returncode": evidence_process.returncode,
            "schema": evidence_schema,
            "bytes": evidence_bytes,
            "sha256": evidence_sha256,
            "expected_bytes": EXPECTED_EVIDENCE_BYTES,
            "expected_sha256": EXPECTED_EVIDENCE_SHA256,
            "matches": evidence_matches,
        },
        "automated_checks_passed": automated_checks_passed,
        "review_eligible": review_eligible,
        "failures": failures,
        "required_human_action": (
            "An independent reviewer must inspect every invariant and commit "
            "docs/cloud/phase0-outbox-remediation-review-2026-10-02.md "
            "with accepted or rejected."
        ),
    }
    return record, automated_checks_passed


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--allow-dirty",
        action="store_true",
        help="author-only self-test; output is never review-eligible",
    )
    args = parser.parse_args()
    record, passed = build_readiness_record(args.allow_dirty)
    print(json.dumps(record, indent=2, sort_keys=True))
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
