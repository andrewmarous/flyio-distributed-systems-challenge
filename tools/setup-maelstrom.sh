#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="0.2.4"
SHA256="301ec71d6b12af0d765edb413f5cf5aa1046b5609bd4e31376a0b549548e5799"
ARCHIVE="$ROOT/third_party/maelstrom.tar.bz2"
DEST="$ROOT/third_party/maelstrom"

checksum() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  else
    shasum -a 256 "$1" | awk '{print $1}'
  fi
}

mkdir -p "$ROOT/third_party"
TEMP="$(mktemp -d "$ROOT/third_party/.maelstrom-XXXXXX")"
trap 'rm -rf "$TEMP"' EXIT

if [[ ! -f "$ARCHIVE" ]]; then
  curl -fL --retry 3 \
    "https://github.com/jepsen-io/maelstrom/releases/download/v$VERSION/maelstrom.tar.bz2" \
    -o "$TEMP/maelstrom.tar.bz2"
  [[ "$(checksum "$TEMP/maelstrom.tar.bz2")" == "$SHA256" ]] || {
    echo "Maelstrom archive checksum mismatch" >&2; exit 1;
  }
  mv "$TEMP/maelstrom.tar.bz2" "$ARCHIVE"
fi

[[ "$(checksum "$ARCHIVE")" == "$SHA256" ]] || {
  echo "Maelstrom archive checksum mismatch: $ARCHIVE" >&2; exit 1;
}

# Re-extract the verified archive rather than trusting an existing installation.
tar -xjf "$ARCHIVE" -C "$TEMP"
[[ -f "$TEMP/maelstrom/lib/maelstrom.jar" ]] || {
  echo "Archive does not contain the Maelstrom runtime" >&2; exit 1;
}
rm -rf "$DEST"
mv "$TEMP/maelstrom" "$DEST"
printf 'Installed Maelstrom %s in %s\n' "$VERSION" "$DEST"
