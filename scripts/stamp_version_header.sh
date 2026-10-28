#!/usr/bin/env bash
# Copy the SDK headers from <src-dir> to <out-dir> with <version> written into m2_version.h as
# M2_VERSION_MAJOR/MINOR/PATCH. The release zips the result as m2-sdk-v<version>-headers.zip: a mod
# built from that zip has no sdk.mk and no manifest.yaml, so the header itself carries the version.
#
#   scripts/stamp_version_header.sh 0.4.0 m2 dist/headers
#
# The defines replace m2_version.h's one marker line. A version m2_version_num() cannot carry (not
# MAJOR.MINOR.PATCH, a leading zero, MINOR or PATCH over 99), or a marker that is missing or repeated,
# stops with an error and writes nothing.
set -euo pipefail

version="${1:?usage: stamp_version_header.sh <version> <src-dir> <out-dir>}"
src="${2:?usage: stamp_version_header.sh <version> <src-dir> <out-dir>}"
out="${3:?usage: stamp_version_header.sh <version> <src-dir> <out-dir>}"

if [[ ! "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]?)\.(0|[1-9][0-9]?)$ ]]; then
  echo "error: '$version' is not MAJOR.MINOR.PATCH with no leading zeros and MINOR, PATCH under 100" >&2
  exit 1
fi
major="${BASH_REMATCH[1]}"
minor="${BASH_REMATCH[2]}"
patch="${BASH_REMATCH[3]}"

header="$src/m2_version.h"
marker='/* M2_VERSION_STAMP */'
test -f "$header" || { echo "error: $header not found" >&2; exit 1; }
count="$(grep -cxF "$marker" "$header" || true)"
if [ "$count" != 1 ]; then
  echo "error: $header has $count lines '$marker', not exactly one" >&2
  exit 1
fi

mkdir -p "$out"
cp "$src"/*.h "$out"/
awk -v marker="$marker" -v major="$major" -v minor="$minor" -v patch="$patch" '
  $0 == marker {
    print "#define M2_VERSION_MAJOR " major
    print "#define M2_VERSION_MINOR " minor
    print "#define M2_VERSION_PATCH " patch
    next
  }
  { print }
' "$header" > "$out/m2_version.h"
echo "stamped m2 $version into $out/m2_version.h"
