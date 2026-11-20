# Fonts for tests

Faces for the tests of `af-layout`, which measures and wraps text the way the
diagram draws it. The application itself does not read these files: the shell
hands `af-layout` DATUM's measured faces as bytes.

| File | What it is |
|---|---|
| `IBMPlexSans-Regular.ttf`, `IBMPlexSans-SemiBold.ttf` | DATUM's measured faces (`@datum-ui/fonts`, `measured/`), unchanged: IBM's `@ibm/plex-sans` 1.1.0 |
| `IBMPlexMono-Regular.ttf` | DATUM's measured face for identifiers, unchanged: IBM's `@ibm/plex-mono` 1.1.0 |
| `AFTestSansJP-Regular.ttf`, `AFTestSansJP-SemiBold.ttf` | Subsets of DATUM's measured IBM Plex Sans JP (`@ibm/plex-sans-jp` 3.0.0): only the characters of the sample cases' Japanese text, kana, CJK punctuation and full-width forms |

The Japanese faces are about 5.7 MB each, so the tests use subsets of them.
A subset keeps each kept glyph's advance and the kerning between kept glyphs.
Because a subset is a modified version under the SIL Open Font License, and
"Plex" is a Reserved Font Name, the subsets are renamed "AF Test Sans JP".
`make_test_subset.py` makes them:

```bash
python -I testdata/fonts/make_test_subset.py <datum-ui>/packages/fonts/dist/measured testdata/fonts .
```

Measuring against what a browser draws uses the whole faces, in the browser
build (goal G7 of the diagram plan).

SHA-256 of the sources, from DATUM at commit `2dd399b` (Plex Mono at `419eebf`):

| Source | SHA-256 |
|---|---|
| `IBMPlexSans-Regular.ttf` | `3761bea7ea7c938f59be5f3f85b2aed2b862d70dfcd5c753e4f14a41fa3c51cd` |
| `IBMPlexSans-SemiBold.ttf` | `55458c051eab3a2a4bc7cad2a6663388bb7ef9fc337a114e796ed5bdb4351dd9` |
| `IBMPlexSansJP-Regular.ttf` | `e5e9ee949e05ca25bf75be44d6412c7071fcdeb8ca6c4361a8d6aeb81f96289a` |
| `IBMPlexSansJP-SemiBold.ttf` | `86b084fe5d11172153f81c5dd5f1bef8515d2b4ab6df63463801c218d97c0291` |
| `IBMPlexMono-Regular.ttf` | `06942df4a547296a3f3f7167f6f313dd224aa3b8a39430d0203f33d19cd630e2` |

The licences are `LICENSE-ibm-plex-sans.txt`, `LICENSE-ibm-plex-sans-jp.txt`
and `LICENSE-ibm-plex-mono.txt` (SIL Open Font License 1.1).
