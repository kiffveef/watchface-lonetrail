#!/usr/bin/env bash
# Route / Orbit を順にビルドして dist/ に .pbw を集める。
# 使い方: scripts/build-all.sh [route|orbit ...]  (省略時は両方)
set -euo pipefail

cd "$(dirname "$0")/.."

readonly PACKAGE_JSON=package.json
readonly DESIGN_CONFIG=src/c/design_config.h
readonly DIST=dist

designs=("$@")
if [ ${#designs[@]} -eq 0 ]; then
  designs=(route orbit)
fi

# package.json はデザインごとに上書きするので、終了時に必ず元に戻す
cp "$PACKAGE_JSON" "$PACKAGE_JSON.orig"
cleanup() {
  mv "$PACKAGE_JSON.orig" "$PACKAGE_JSON"
  rm -f "$DESIGN_CONFIG"
}
trap cleanup EXIT

version=$(python3 -c "import json; print(json.load(open('$PACKAGE_JSON'))['version'])")
mkdir -p "$DIST"

for design in "${designs[@]}"; do
  overlay="designs/$design.json"
  if [ ! -f "$overlay" ]; then
    echo "unknown design: $design (expected designs/<name>.json)" >&2
    exit 1
  fi

  # designs/<name>.json の値で package.json を浅くマージする(pebble 配下は1段深く)
  python3 - "$PACKAGE_JSON.orig" "$overlay" "$PACKAGE_JSON" <<'EOF'
import json, sys
base = json.load(open(sys.argv[1]))
overlay = json.load(open(sys.argv[2]))
base["pebble"].update(overlay.pop("pebble", {}))
base.update(overlay)
json.dump(base, open(sys.argv[3], "w"), indent=2, ensure_ascii=False)
open(sys.argv[3], "a").write("\n")
EOF

  use_orbit=0
  if [ "$design" = orbit ]; then
    use_orbit=1
  fi
  printf '#pragma once\n#define LONETRAIL_USE_ORBIT %d\n' "$use_orbit" > "$DESIGN_CONFIG"

  pebble clean >/dev/null
  pebble build
  cp build/*.pbw "$DIST/lonetrail-$design-$version.pbw"
  echo "==> $DIST/lonetrail-$design-$version.pbw"
done
