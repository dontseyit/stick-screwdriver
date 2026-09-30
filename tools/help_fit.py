#!/usr/bin/env python3
# -----------------------------------------------------------------------------
#  help_fit - does every HELP page actually fit on the panel?
#
#  Font0 advances a fixed six pixels per glyph, so the height of a help page is
#  arithmetic: word-wrap each of the three strings at the width it is drawn to,
#  count the lines, and add up the gaps. There is no reason to guess at it.
#
#  Written without measuring, nineteen of the first twenty pages ran over - the
#  worst by more than four lines - and every one of them would have been clipped
#  mid-sentence on the device, which reads as a sentence that simply stops.
#
#  Run it after editing any app's .what/.how/.care:
#
#      python3 tools/help_fit.py
#
#  It reads the AppInfo blocks straight out of src/, so a new tool is included
#  the moment it is written, wherever in the tree it lives.
# -----------------------------------------------------------------------------
import re, glob
# Font0 in LovyanGFX is the 5x7 GLCD with one pixel of spacing: 6 px per glyph.
CH = 6
def wrap(text, px):
    cols = px // CH
    lines, cur = 0, ""
    for w in text.split():
        cand = (cur + " " + w) if cur else w
        if cur and len(cand) > cols:
            lines += 1; cur = w
        else:
            cur = cand
    return lines + (1 if cur else 0)

TOP, FOOT, LINEH = 14, 12, 9
Y0   = TOP + 23          # body start, no subtitle line
MAXY = 135 - FOOT        # 123
worst = []
# Every .cpp, not only src/apps: CONSOLE lives in src/web and had a HELP page
# nothing was checking.
for f in sorted(glob.glob('src/**/*.cpp', recursive=True)):
    src = open(f).read()
    m = re.search(r'constexpr AppInfo kInfo\{(.*?)\n\};', src, re.S)
    if not m: continue
    body = m.group(1)
    def field(n):
        g = re.search(r'\.%s\s*=\s*"((?:[^"\\]|\\.)*)"' % n, body)
        return g.group(1).replace('\\"','"') if g else None
    title = field('title'); what = field('what')
    if not what: continue
    n1 = wrap(what, 228)
    n2 = wrap(field('how') or '', 194)
    n3 = wrap(field('care') or '', 194)
    y = Y0 + LINEH*n1 + 3 + LINEH*n2 + 2 + LINEH*n3
    over = y - MAXY
    flag = "OVER by %2d px" % over if over > 0 else "ok"
    print("  %-12s what %d  use %d  care %d  -> %3d px  %s" % (title, n1, n2, n3, y, flag))
    if over > 0: worst.append((over, title))
print()
print("pages over the panel:", len(worst))
for o,t in sorted(worst, reverse=True): print("   %-12s %d px" % (t, o))
