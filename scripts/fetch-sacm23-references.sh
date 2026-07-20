#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Fetch official SACM 2.3 reference artifacts into a local project folder.

Usage:
  bash scripts/fetch-sacm23-references.sh [options]

Options:
  --dest PATH       Destination directory. Default: third_party/sacm-2.3
  --help            Show this help.

This script downloads official OMG-hosted artifacts for local reference pinning.
Review OMG licensing and your project policy before committing downloaded files.
USAGE
}

DEST="third_party/sacm-2.3"
while [ "$#" -gt 0 ]; do
  case "$1" in
    --dest)
      if [ "$#" -lt 2 ]; then echo "error: --dest needs a path" >&2; exit 2; fi
      DEST="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

mkdir -p "$DEST"

fetch_one() {
  local url="$1"
  local out="$2"
  if [ -s "$out" ]; then
    echo "exists, skipping: $out"
    return
  fi
  if command -v curl >/dev/null 2>&1; then
    curl -L --fail --show-error --output "$out" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$out" "$url"
  else
    echo "error: need curl or wget" >&2
    exit 1
  fi
}

fetch_one "https://www.omg.org/spec/SACM/2.3/PDF" "$DEST/SACM-2.3-formal-23-05-08.pdf"
fetch_one "https://www.omg.org/spec/SACM/20220301/SACM.xml" "$DEST/SACM-2.3-ptc-22-03-13.xml"

cat > "$DEST/README.md" <<README
# SACM 2.3 reference artifacts

Downloaded from official OMG URLs by scripts/fetch-sacm23-references.sh.

- Specification PDF: https://www.omg.org/spec/SACM/2.3/PDF, OMG file ID formal/23-05-08
- Normative machine-readable SACM XML: https://www.omg.org/spec/SACM/20220301/SACM.xml, OMG file ID ptc/22-03-13

Review OMG licensing and project policy before committing these files.
README

if command -v sha256sum >/dev/null 2>&1; then
  (cd "$DEST" && sha256sum SACM-2.3-formal-23-05-08.pdf SACM-2.3-ptc-22-03-13.xml > SHA256SUMS)
elif command -v shasum >/dev/null 2>&1; then
  (cd "$DEST" && shasum -a 256 SACM-2.3-formal-23-05-08.pdf SACM-2.3-ptc-22-03-13.xml > SHA256SUMS)
fi

echo "downloaded SACM 2.3 references to $DEST"
