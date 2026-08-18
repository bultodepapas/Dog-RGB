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
CANDIDATE_ORIGIN_COMMIT = "255136d6ca9b5bcc128af338c444476ebea64a26"
EXPECTED_STORAGE_MODEL_BYTES = 93_767
EXPECTED_STORAGE_MODEL_SHA256 = "9d7f0c059399708b4a3162d231a18d00c8378e85c4837f30b5ccd1369574b3d8"
EXPECTED_EVIDENCE_BYTES = 9_505
EXPECTED_EVIDENCE_SHA256 = "be28dcad59cb034a1a9aa28a8ee82d03b7bd343f8225f5a63a50efd8aed13475"

CANDIDATE_PATHS = (
    "tools/cloud_phase0/fixtures/reference_manifest.json",
    "tools/cloud_phase0/generate_evidence.py",
    "tools/cloud_phase0/legacy_v2.py",
    "tools/cloud_phase0/reference_fixtures.py",
    "tools/cloud_phase0/storage_model.py",
    "tools/cloud_phase0/test_phase0.py",
    "tools/cloud_phase0/track_v3.py",
)

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


def candidate_source_digests() -> dict[str, dict[str, object]]:
    result: dict[str, dict[str, object]] = {}
    for relative_path in CANDIDATE_PATHS:
        content = (REPOSITORY_ROOT / relative_path).read_bytes()
        result[relative_path] = {
            "bytes": len(content),
            "sha256": sha256_bytes(content),
        }
    return result


def missing_required_regressions(test_source: str) -> list[str]:
    return [
        test_name
        for _, test_name in REQUIRED_REGRESSIONS
        if f"def {test_name}(" not in test_source
    ]


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

    diff_process = git_bytes(
        "diff",
        "--quiet",
        f"{CANDIDATE_ORIGIN_COMMIT}..{reviewed_commit}",
        "--",
        *CANDIDATE_PATHS,
    )
    candidate_unchanged_from_origin = diff_process.returncode == 0
    if not candidate_unchanged_from_origin:
        failures.append("candidate files differ from the hardened origin commit")

    source_digests = candidate_source_digests()
    storage_digest = source_digests["tools/cloud_phase0/storage_model.py"]
    storage_artifact_matches = (
        storage_digest["bytes"] == EXPECTED_STORAGE_MODEL_BYTES
        and storage_digest["sha256"] == EXPECTED_STORAGE_MODEL_SHA256
    )
    if not storage_artifact_matches:
        failures.append("storage_model.py does not match the frozen reviewed artifact")

    test_source = (REPOSITORY_ROOT / "tools/cloud_phase0/test_phase0.py").read_text(encoding="utf-8")
    missing_regressions = missing_required_regressions(test_source)
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
    tests_passed = test_process.returncode == 0 and test_count == 51
    if not tests_passed:
        failures.append("the frozen host matrix did not pass exactly 51 tests")

    evidence_command = ["python", "tools/cloud_phase0/generate_evidence.py"]
    evidence_process = run([sys.executable, "tools/cloud_phase0/generate_evidence.py"])
    evidence_sha256 = sha256_bytes(evidence_process.stdout)
    evidence_bytes = len(evidence_process.stdout)
    try:
        evidence_document = json.loads(evidence_process.stdout)
        evidence_schema = evidence_document.get("evidence_schema")
    except (json.JSONDecodeError, UnicodeDecodeError):
        evidence_schema = None
    evidence_matches = (
        evidence_process.returncode == 0
        and evidence_schema == "dog-rgb-cloud-phase0b/1"
        and evidence_bytes == EXPECTED_EVIDENCE_BYTES
        and evidence_sha256 == EXPECTED_EVIDENCE_SHA256
    )
    if not evidence_matches:
        failures.append("canonical evidence bytes do not match the frozen digest")

    automated_checks_passed = not failures or (
        allow_dirty and failures == ["worktree is not clean"]
    )
    review_eligible = automated_checks_passed and worktree_clean and not allow_dirty
    record: dict[str, object] = {
        "schema": "dog-rgb-cloud-phase0b-review-readiness/1",
        "decision": "awaiting_independent_review",
        "acceptance_may_be_decided_by_this_tool": False,
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
            "candidate_unchanged_from_origin": candidate_unchanged_from_origin,
        },
        "candidate_sources": source_digests,
        "storage_artifact": {
            "expected_bytes": EXPECTED_STORAGE_MODEL_BYTES,
            "expected_sha256": EXPECTED_STORAGE_MODEL_SHA256,
            "matches": storage_artifact_matches,
        },
        "host_matrix": {
            "command": test_command,
            "returncode": test_process.returncode,
            "tests_run": test_count,
            "expected_tests": 51,
            "passed": tests_passed,
            "required_regressions": [
                {"id": regression_id, "test": test_name}
                for regression_id, test_name in REQUIRED_REGRESSIONS
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
            "docs/cloud/phase0-outbox-independent-review.md with accepted or rejected."
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
