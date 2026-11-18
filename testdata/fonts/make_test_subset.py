import sys, re, glob
from fontTools import subset
from fontTools.ttLib import TTFont
src_dir, out_dir, repo = sys.argv[1], sys.argv[2], sys.argv[3]
text = set()
for f in glob.glob(repo + '/testdata/sacm/*.xmi'):
    for ch in open(f, encoding='utf-8').read():
        if ord(ch) >= 0x2000: text.add(ch)
ranges = [(0x3000,0x303F),(0x3040,0x309F),(0x30A0,0x30FF),(0x31F0,0x31FF),(0xFF00,0xFFEF)]
for a,b in ranges:
    for c in range(a,b+1): text.add(chr(c))
for weight in ('Regular','SemiBold'):
    font = TTFont(f'{src_dir}/IBMPlexSansJP-{weight}.ttf')
    opts = subset.Options(); opts.layout_features=['*']; opts.name_IDs=['*']; opts.notdef_outline=True; opts.hinting=False; opts.glyph_names=False
    s = subset.Subsetter(opts); s.populate(unicodes=[ord(c) for c in text]); s.subset(font)
    for rec in font['name'].names:
        v = rec.toUnicode()
        v2 = v.replace('IBM Plex Sans JP','AF Test Sans JP').replace('IBMPlexSansJP','AFTestSansJP').replace('Plex','AF Test')
        if v2 != v: rec.string = v2
    out = f'{out_dir}/AFTestSansJP-{weight}.ttf'
    font.save(out)
    print(out, len(text), 'chars')
