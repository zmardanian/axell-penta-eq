#!/usr/bin/env bash
# Builds the GitHub-ready public repository (~/axell-penta-eq-public): sources, tests, docs and licences - no dev
# logs, build output or downloaded tools. A new repository gets one clean commit; an existing one (an earlier
# release) is updated in place: the tree is synced and committed on top of its history as "Axell Penta EQ v<ver>"
# with tag v<ver>. Sets the remote, never pushes.
set -euo pipefail
cd "$(dirname "$0")/.."
SRC="$PWD"
PUB="${PENTA_PUBLIC_DIR:-$HOME/axell-penta-eq-public}"
CORE="${AXELL_CORE:-$(realpath ../axell-core)}"
VER=$(grep -oP 'project\(AxellPentaEQ VERSION \K[0-9.]+' CMakeLists.txt)
FINAL="$PUB"
UPDATE=0
if [ -d "$PUB/.git" ]; then
  UPDATE=1
  PUB="$(mktemp -d)/stage"
elif [ -e "$PUB" ]; then
  echo "$PUB exists but is not a git repository - remove it first."; exit 1
fi
mkdir -p "$PUB"
for f in CMakeLists.txt LICENSE README.md CHANGELOG.md THIRD_PARTY_NOTICES.md; do cp "$f" "$PUB/"; done
cp -r Resources Source scripts packaging .github "$PUB/"   # .github: the CI workflow (GitHub Actions)
mkdir -p "$PUB/tests" "$PUB/docs" "$PUB/tools/proto"
cp tests/*.cpp tests/*.h tests/lsan.supp tests/report.md "$PUB/tests/"
mkdir -p "$PUB/tests/independent"
cp tests/independent/*.cpp tests/independent/*.py tests/independent/README.md tests/independent/audit-report.txt "$PUB/tests/independent/"
cp docs/ENGINEERING_SPEC.md docs/DECISIONS.md docs/DEVELOPMENT.md docs/pluginval-log.txt docs/vst3-validator-log.txt "$PUB/docs/"
cp -r docs/research docs/screenshots docs/preset-curves "$PUB/docs/"
cp tools/proto/*.py "$PUB/tools/proto/"
# 3.0: the Axell core (the brand look, the "Ess" detector source), vendored so the repository builds on its own
mkdir -p "$PUB/external/axell-core"
for d in CMakeLists.txt LICENSE README.md include src resources tests tools; do cp -r "$CORE/$d" "$PUB/external/axell-core/"; done
rm -rf "$PUB/external/axell-core/tools/__pycache__"
echo "axell-core $(git -C "$CORE" rev-parse --short HEAD)" > "$PUB/external/axell-core/VERSION"
cat > "$PUB/.gitignore" <<'IGN'
build/
build-*/
tools/*
!tools/proto/
tools/proto/__pycache__/
__pycache__/
tests/report-asan.md
*.user
IGN
if [ "$UPDATE" = 1 ]; then
  git -C "$FINAL" diff --quiet && git -C "$FINAL" diff --cached --quiet || { echo "$FINAL has uncommitted changes."; exit 1; }
  git -C "$FINAL" rev-parse -q --verify "refs/tags/v$VER" >/dev/null && { echo "tag v$VER already exists in $FINAL."; exit 1; }
  rsync -a --delete --exclude .git "$PUB/" "$FINAL/"
  rm -rf "$(dirname "$PUB")"
  cd "$FINAL"
  git add -A
  git commit -qm "Axell Penta EQ v$VER"
  git tag "v$VER"
  echo "Public repo updated: $FINAL (commit $(git rev-parse --short HEAD) on top of $(git rev-parse --short HEAD~1), tag v$VER, NOT pushed)"
  exit 0
fi
cd "$PUB"
git init -q -b main
git config user.name "zmardanian"
git config user.email "333182917+zmardanian@users.noreply.github.com"
git add -A
git commit -qm "Axell Penta EQ v$VER"
git tag "v$VER"
git remote add origin "https://github.com/zmardanian/axell-penta-eq.git"
echo "Public repo ready: $PUB (commit $(git rev-parse --short HEAD), tag v$VER, remote origin set, NOT pushed)"
