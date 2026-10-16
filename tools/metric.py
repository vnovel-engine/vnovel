#!/usr/bin/env python3
"""The goal's metric: per-row fraction of pixels differing in any channel by
more than a stated threshold, over the message-window region.

    python3 tools/metric.py <ref.png> <ours.png> [threshold]

PASS iff no row in y=80..560 exceeds 1%. This is the colour test; OCR is only
for text/geometry, never for this.
"""
import sys

THRESH = int(sys.argv[3]) if len(sys.argv) > 3 else 24
W = 800


def load(p):
    import os
    import subprocess
    import tempfile
    raw = tempfile.mktemp(suffix='.rgb')
    subprocess.run(['magick', p, '-depth', '8', 'rgb:' + raw], check=True)
    d = open(raw, 'rb').read()
    os.unlink(raw)
    return d


def main():
    a = load(sys.argv[1])
    b = load(sys.argv[2])
    print('thresh=%d  rows y=80..560  (fraction of pixels differing > thresh)' % THRESH)
    worst = 0.0
    worst_y = -1
    over = 0
    for y in range(80, 561):
        diff = 0
        tot = 0
        for x in range(20, 780):
            o = (y * W + x) * 3
            tot += 1
            if (abs(a[o] - b[o]) > THRESH or abs(a[o + 1] - b[o + 1]) > THRESH
                    or abs(a[o + 2] - b[o + 2]) > THRESH):
                diff += 1
        f = diff / tot
        if f > worst:
            worst, worst_y = f, y
        if f > 0.01:
            over += 1
        if y % 20 == 0 or f > 0.10:
            print('  y=%3d  %6.1f%%  %s' % (y, f * 100, '#' * int(f * 50)))
    print()
    print('worst row: y=%d at %.1f%%' % (worst_y, worst * 100))
    print('rows over the 1%% criterion: %d of 481' % over)
    print('VERDICT: %s' % ('PASS' if over == 0 else 'FAIL'))
    return 0 if over == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
