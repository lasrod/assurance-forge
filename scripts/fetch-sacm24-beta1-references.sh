#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Fetch the OMG SACM 2.4 Beta 1 reference artifacts into a local project folder.

Usage:
  bash scripts/fetch-sacm24-beta1-references.sh [options]

Options:
  --dest PATH       Destination directory. Default: third_party/sacm-2.4-beta1
  --help            Show this help.

SACM 2.4 Beta 1 is not a formal specification. OMG publishes it "for
informational purposes"; SACM 2.3 (formal/23-05-08) stays the version this
project claims conformance to. These files are study material for
docs/sacm/sacm-2.4-beta1-impact.md and are never read by the build.

A beta document can be replaced in place, so each download is compared with the
hash recorded when the impact analysis was written. A mismatch means the
analysis no longer describes what OMG is publishing.

Review OMG licensing and your project policy before committing downloaded files.
USAGE
}

DEST="third_party/sacm-2.4-beta1"
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
  # omg.org answers a default curl/wget user agent with an error page.
  if command -v curl >/dev/null 2>&1; then
    curl -L --fail --show-error -A "Mozilla/5.0" --output "$out" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -U "Mozilla/5.0" -O "$out" "$url"
  else
    echo "error: need curl or wget" >&2
    exit 1
  fi
}

fetch_one "https://www.omg.org/spec/SACM/2.4/Beta1/PDF" "$DEST/SACM-2.4-beta1-ptc-26-06-34.pdf"
fetch_one "https://www.omg.org/spec/SACM/2.4/Beta1/PDF/changebar" "$DEST/SACM-2.4-beta1-changebar-ptc-26-06-35.pdf"
fetch_one "https://www.omg.org/spec/SACM/20260504/SACM2.4_Metamodel.xml" "$DEST/SACM-2.4-beta1-metamodel-ptc-26-05-28.xml"
fetch_one "https://www.omg.org/cgi-bin/doc?ptc/26-05-21.xml" "$DEST/SACM-2.4-beta1-uml-profile-ptc-26-05-21.xml"

cat > "$DEST/README.md" <<README
# SACM 2.4 Beta 1 reference artifacts

Downloaded from official OMG URLs by scripts/fetch-sacm24-beta1-references.sh.
Beta, informational only: SACM 2.3 remains the formal specification.

- Specification PDF (normative): https://www.omg.org/spec/SACM/2.4/Beta1/PDF, OMG file ID ptc/26-06-34
- Specification with change bars (informative): https://www.omg.org/spec/SACM/2.4/Beta1/PDF/changebar, OMG file ID ptc/26-06-35
- Metamodel XML (normative, machine readable): https://www.omg.org/spec/SACM/20260504/SACM2.4_Metamodel.xml, OMG file ID ptc/26-05-28
- UML Profile XMI (informative, machine readable): https://www.omg.org/cgi-bin/doc?ptc/26-05-21.xml, OMG file ID ptc/26-05-21

Review OMG licensing and project policy before committing these files.
README

# Hashes as downloaded on 2026-10-02.
cat > "$DEST/SHA256SUMS.expected" <<'SUMS'
25f32d1cd4bf32dfa9c92c7c1840e1a113b1e91c0e97252f6551878c39997ff5  SACM-2.4-beta1-ptc-26-06-34.pdf
6840a0f67104d07997168d07079c0e168f5ac65ef5fa1779eccb9ebef1fe5f96  SACM-2.4-beta1-changebar-ptc-26-06-35.pdf
0581dfa0b9dfaf17ff0f6c63b74f8e7a8396bea03f315e1334e94f23db110830  SACM-2.4-beta1-metamodel-ptc-26-05-28.xml
9149a0414628e1b70c1f7c2235f5180c64e7a144601eb2ae78d539681918838b  SACM-2.4-beta1-uml-profile-ptc-26-05-21.xml
SUMS

if command -v sha256sum >/dev/null 2>&1; then
  check_hashes() { (cd "$DEST" && sha256sum --check --quiet SHA256SUMS.expected); }
elif command -v shasum >/dev/null 2>&1; then
  check_hashes() { (cd "$DEST" && shasum -a 256 --check --quiet SHA256SUMS.expected); }
else
  echo "warning: no sha256sum or shasum; downloads were not verified" >&2
  check_hashes() { return 0; }
fi

if ! check_hashes; then
  echo "error: a downloaded file differs from the copy docs/sacm/sacm-2.4-beta1-impact.md analysed." >&2
  echo "       OMG has probably republished the beta; re-run the analysis before relying on it." >&2
  exit 1
fi

echo "downloaded SACM 2.4 Beta 1 references to $DEST"
