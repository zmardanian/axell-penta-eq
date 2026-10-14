#!/usr/bin/env bash
# Builds the GitHub-ready public repository (~/axell-penta-eq-public): one clean commit with sources,
# tests, docs and licences - no dev logs, build output or downloaded tools. Sets the remote, never pushes.
set -euo pipefail
cd "$(dirname "$0")/.."
SRC="$PWD"
PUB="${PENTA_PUBLIC_DIR:-$HOME/axell-penta-eq-public}"
VER=$(grep -oP 'project\(AxellPentaEQ VERSION \K[0-9.]+' CMakeLists.txt)
[ -e "$PUB" ] && { echo "$PUB exists - remove it first if you want to regenerate it."; exit 1; }
mkdir -p "$PUB"
for f in CMakeLists.txt LICENSE README.md CHANGELOG.md THIRD_PARTY_NOTICES.md; do cp "$f" "$PUB/"; done
cp -r Resources Source scripts packaging "$PUB/"
mkdir -p "$PUB/tests" "$PUB/docs" "$PUB/tools/proto"
cp tests/*.cpp tests/*.h tests/lsan.supp tests/report.md "$PUB/tests/"
mkdir -p "$PUB/tests/independent"
cp tests/independent/*.cpp tests/independent/*.py tests/independent/README.md tests/independent/audit-report.txt "$PUB/tests/independent/"
cp docs/ENGINEERING_SPEC.md docs/DECISIONS.md docs/DEVELOPMENT.md docs/pluginval-log.txt docs/vst3-validator-log.txt "$PUB/docs/"
cp -r docs/research docs/screenshots docs/preset-curves "$PUB/docs/"
cp tools/proto/*.py "$PUB/tools/proto/"
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
cd "$PUB"
git init -q -b main
git config user.name "zmardanian"
git config user.email "333182917+zmardanian@users.noreply.github.com"
git add -A
git commit -qm "Axell Penta EQ v$VER"
git tag "v$VER"
git remote add origin "https://github.com/zmardanian/axell-penta-eq.git"
echo "Public repo ready: $PUB (commit $(git rev-parse --short HEAD), tag v$VER, remote origin set, NOT pushed)"
