#!/usr/bin/env bash
# Download mercs2_mod_runtime <version> from crates.io and unpack it into <dir>/mercs2_mod_runtime-<version>.
#
#   scripts/fetch_runtime.sh 0.4.0 build
#
# The expected sha256 is the `cksum` the crates.io index records for that version, read before the
# download, so the check is against what the registry published rather than the bytes that arrived. A
# yanked version, a missing version, or a checksum mismatch stops the build.
set -euo pipefail

name=mercs2_mod_runtime
version="${1:?usage: fetch_runtime.sh <version> <dir>}"
dir="${2:?usage: fetch_runtime.sh <version> <dir>}"

case "$version" in
  [0-9]*.[0-9]*.[0-9]*) ;;
  *) echo "error: '$version' is not a MAJOR.MINOR.PATCH version" >&2; exit 1 ;;
esac

# Sparse index path for a name of four or more characters: <first two>/<next two>/<name>.
index_url="https://index.crates.io/${name:0:2}/${name:2:2}/${name}"
crate_url="https://static.crates.io/crates/${name}/${name}-${version}.crate"
ua="mercs2-sdk build (https://github.com/Mercenaries-Fan-Build/mercs2-sdk)"

index="$(curl -fsSL -A "$ua" "$index_url")" \
  || { echo "error: could not read the crates.io index entry for $name ($index_url)" >&2; exit 1; }

# One JSON object per published version, one per line.
entry="$(printf '%s\n' "$index" | grep -F "\"vers\":\"$version\"" || true)"
[ -n "$entry" ] || { echo "error: $name $version is not on crates.io" >&2; exit 1; }
case "$entry" in
  *'"yanked":false'*) ;;
  *) echo "error: $name $version is yanked on crates.io" >&2; exit 1 ;;
esac
want="$(printf '%s\n' "$entry" | sed -n 's/.*"cksum":"\([0-9a-f]\{64\}\)".*/\1/p')"
[ -n "$want" ] || { echo "error: the index entry for $name $version has no sha256 cksum" >&2; exit 1; }

mkdir -p "$dir"
crate="$dir/${name}-${version}.crate"
curl -fsSL -A "$ua" -o "$crate" "$crate_url" \
  || { echo "error: could not download $crate_url" >&2; exit 1; }
got="$(shasum -a 256 "$crate" | cut -d' ' -f1)"
if [ "$want" != "$got" ]; then
  echo "error: $crate failed verification: crates.io records $want, the download is $got" >&2
  exit 1
fi

rm -rf "$dir/${name}-${version}"
tar -xzf "$crate" -C "$dir"
test -f "$dir/${name}-${version}/Cargo.toml" \
  || { echo "error: $crate did not unpack to $dir/${name}-${version}" >&2; exit 1; }
echo "$name $version verified ($got) and unpacked to $dir/${name}-${version}"
