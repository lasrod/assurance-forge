#!/usr/bin/env python3
"""Generate `docs/features/feature-matrix.json` from the capability matrix.

The JSON exists so that downstream consumers -- primarily the documentation site
in `assurance-forge-site` -- render support claims from this repository instead
of maintaining a second, divergent copy. Capabilities are implemented and tested
here, so this is where their status can be checked; anywhere else it is a
transcription that goes stale without anyone noticing.

`check_feature_matrix.py` fails if the committed JSON differs from what this
script would produce, the same way `i18n_catalog_check` guards the compiled
translation catalog.

Usage:
  python tools/features/export_feature_matrix.py           # write the JSON
  python tools/features/export_feature_matrix.py --stdout  # print it instead
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from feature_matrix import JSON_PATH, MATRIX_PATH, REPO, parse, render_json  # noqa: E402


def main(argv):
    if not MATRIX_PATH.exists():
        print(f"error: matrix not found at {MATRIX_PATH}", file=sys.stderr)
        return 1

    parsed = parse()
    if not parsed["features"]:
        print("error: no capability rows parsed from the matrix", file=sys.stderr)
        return 1

    rendered = render_json(parsed)
    if "--stdout" in argv:
        sys.stdout.write(rendered)
        return 0

    JSON_PATH.write_text(rendered, encoding="utf-8", newline="\n")
    print(f"wrote {JSON_PATH.relative_to(REPO)} "
          f"({len(parsed['features'])} capabilities, {len(parsed['areas'])} areas)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
