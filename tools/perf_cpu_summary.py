#!/usr/bin/env python3
"""Summarize a heliasdream `perf record` (no call graph or DWARF) per thread.

    perf script -i cpu-flat.data -F comm,tid,ip,sym,dso > flat.script
    python3 perf_cpu_summary.py flat.script maps.txt [--top 25] [--thread bb-cp0]

Each sample is put in a bucket: guest (the game's own code, anonymous
executable memory at the eboot's address range), heliasdream (our code),
radv/vulkan, libc/libstdc++, other libs, kernel. Then the top functions
per thread are listed. Guest addresses are printed raw (game address =
the runtime address, the eboot loads at 0x400000); no game code is read.
"""
import argparse, bisect, collections, re, sys

ap = argparse.ArgumentParser()
ap.add_argument('script')
ap.add_argument('maps')
ap.add_argument('--top', type=int, default=25)
ap.add_argument('--thread', help='only threads whose name starts with this')
a = ap.parse_args()

maps = []
for line in open(a.maps):
    m = re.match(r'([0-9a-f]+)-([0-9a-f]+) (\S+) \S+ \S+ \S+\s*(.*)', line)
    if m and 'x' in m.group(3):
        maps.append((int(m.group(1), 16), int(m.group(2), 16), m.group(4).strip()))
maps.sort()
starts = [m[0] for m in maps]

def region(ip):
    i = bisect.bisect_right(starts, ip) - 1
    if i >= 0 and maps[i][0] <= ip < maps[i][1]:
        return maps[i]
    return None

def bucket(ip, sym, dso):
    d = dso.lower()
    if ip >= 0xffff800000000000 or '[kernel' in d or d.endswith('.ko') or '[unknown]' == d and ip >= 0xffff800000000000:
        return 'kernel'
    if 'heliasdream' in d:
        return 'heliasdream'
    if 'radv' in d or 'vulkan' in d or 'libdrm' in d or 'mesa' in d:
        return 'radv/vulkan/drm'
    if 'libc.so' in d or 'ld-linux' in d or 'libm.so' in d or 'libpthread' in d:
        return 'libc'
    if 'mangohud' in d:
        return 'mangohud'
    if d in ('[unknown]', '', '//anon', '[anon]') or 'anon' in d:
        r = region(ip)
        if r and 0x400000 <= ip < 0x100000000:
            return 'guest (eboot)'
        return 'anon jit/thunks' if r else 'unknown'
    return 'other lib: ' + d.split('/')[-1]

per_thread = collections.defaultdict(collections.Counter)
per_sym = collections.defaultdict(collections.Counter)
total = collections.Counter()
rx = re.compile(r'^\s*(.+?)\s+(\d+)\s+([0-9a-f]+)\s+(.*?)\s+\((.*)\)\s*$')
for line in open(a.script, errors='replace'):
    m = rx.match(line)
    if not m:
        continue
    comm, tid, ip, sym, dso = m.group(1), m.group(2), int(m.group(3), 16), m.group(4), m.group(5)
    if a.thread and not comm.startswith(a.thread):
        continue
    b = bucket(ip, sym, dso)
    key = comm
    per_thread[key][b] += 1
    total[key] += 1
    if b == 'guest (eboot)':
        name = 'guest 0x%x (4 KiB page)' % (ip & ~0xfff)
    elif sym in ('[unknown]', ''):
        name = '%s 0x%x' % (b, ip & ~0xff)
    else:
        name = '%s  [%s]' % (sym, b)
    per_sym[key][name] += 1

all_n = sum(total.values()) or 1
print('samples: %d' % all_n)
for comm, n in total.most_common():
    if n < all_n * 0.005:
        continue
    print('\n== %s: %d samples (%.1f%%)' % (comm, n, 100.0 * n / all_n))
    print('   by bucket: ' + ', '.join('%s %.1f%%' % (k, 100.0 * v / n) for k, v in per_thread[comm].most_common()))
    for name, v in per_sym[comm].most_common(a.top):
        print('   %6.2f%%  %s' % (100.0 * v / n, name))
