#!/usr/bin/env bash
# Regenerate the Playwright screenshot baselines.
#
# Baselines are pixel comparisons, so they are only valid against the renderer
# that produced them. CI runs the visual job inside the image pinned below, so
# baselines must be generated there too -- ones made on a developer's Windows
# or macOS machine carry a different platform suffix and different font
# rendering, and CI would never match them.
#
# The repo is mounted read-only and the needed files are copied into the
# container. Running `npm ci` against a read-write mount would replace the
# host's node_modules with Linux binaries and break local test runs.
#
# Usage:  bash tools/ap_portal_preview/gen_baselines.sh
# Needs:  docker, ~2 GB of image download on first use. Without docker, take
#         the `visual-baselines` artifact from the CI job instead.
set -euo pipefail

IMAGE="mcr.microsoft.com/playwright:v1.62.1-noble"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SNAPS="tests/ap-portal-visual/ap-portal.visual.spec.ts-snapshots"
# Inside the repo on purpose: under Git Bash `mktemp -d` returns an MSYS path
# that Docker Desktop cannot bind-mount, and the copy back silently yields
# nothing. A repo-relative directory is mountable everywhere.
OUT="$REPO/.baseline-tmp"
rm -rf "$OUT"
mkdir -p "$OUT"
trap 'rm -rf "$OUT"' EXIT

echo "Generating baselines in $IMAGE ..."

# MSYS_NO_PATHCONV stops Git Bash on Windows from rewriting the container paths.
MSYS_NO_PATHCONV=1 docker run --rm \
  -v "$REPO:/src:ro" \
  -v "$OUT:/out" \
  "$IMAGE" bash -c '
set -e
command -v python3 >/dev/null || { apt-get update -qq && apt-get install -y -qq python3; }
mkdir -p /w/tests /w/tools /w/Platformio/Dog-RGB/src/web
cp /src/package.json /src/package-lock.json /src/playwright.config.ts /w/
cp -r /src/tests/. /w/tests/
cp -r /src/tools/. /w/tools/
cp /src/Platformio/Dog-RGB/src/web/pages.cpp /w/Platformio/Dog-RGB/src/web/
cd /w
rm -rf "tests/ap-portal-visual/ap-portal.visual.spec.ts-snapshots"
npm ci --silent
AP_PORTAL_VISUAL=1 npx playwright test tests/ap-portal-visual/ \
  --project=iphone-13-pro-max-chromium --update-snapshots --reporter=line
cp -r "tests/ap-portal-visual/ap-portal.visual.spec.ts-snapshots/." /out/
'

count="$(find "$OUT" -name '*.png' | wc -l)"
if [ "$count" -eq 0 ]; then
  echo "ERROR: the container produced no baselines; nothing was replaced." >&2
  exit 1
fi

rm -rf "${REPO:?}/$SNAPS"
mkdir -p "$REPO/$SNAPS"
cp -r "$OUT/." "$REPO/$SNAPS/"

echo "Wrote $count baselines to $SNAPS"
echo "Review the diff, then commit them."
