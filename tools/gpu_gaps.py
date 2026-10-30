#!/usr/bin/env python3
"""Name the GPU time between profiled draws (HELIAS_GPU_PROFILE=2), or the
command processor's in-place recording sites (HELIAS_CMD_CENSUS=1), or the
GPU's idle time between command buffers (--idle, any run with the busy meter).

    tools/gpu_gaps.py LOG [--exe build/heliasdream] [--top 25] [--windows] [--census | --idle]

Each 300-flip report's "gpu gaps:" line charges the GPU time between two
profiled draws or dispatches (and before the first and after the last of a
command buffer) to the recording sites - g_cmd()'s and rec()'s callers, as
offsets in heliasdream's image - that recorded in that gap. This sums every line
of LOG, names the sites with addr2line, and prints the gaps by total time.
With --census it reads the "cmd census:" lines instead (the recorder's
report: who still records in place, after waiting for the command stream,
and how often per flip) and prints the sites by their mean per flip. With
--idle it reads the "gpu idle:" lines (test7): the idle time by kind (late:
submitted after the GPU ran dry; queued: between command buffers already
queued; before present) and size, the submissions' latency, and the "late"
gaps by what ended the command buffer before them (submit_locked's caller,
or flush_locked's - a CPU wait for the GPU). A
Windows log takes --exe build/win/heliasdream-win-<rev>.debug.exe --windows (the
offsets are added to the PE's link base).
"""
import argparse
import collections
import re
import subprocess

ap = argparse.ArgumentParser()
ap.add_argument('log')
ap.add_argument('--exe', default='build/heliasdream')
ap.add_argument('--top', type=int, default=25)
ap.add_argument('--windows', action='store_true', help='offsets are from a Windows run: add the PE link base')
ap.add_argument('--census', action='store_true', help='the "cmd census:" lines (HELIAS_CMD_CENSUS=1) instead of the gaps')
ap.add_argument('--idle', action='store_true', help='the "gpu idle:" lines (test7) instead of the gaps')
a = ap.parse_args()


def name_sites(sites, exe, windows):
    base = 0
    if windows:
        out = subprocess.run(['objdump', '-p', exe], capture_output=True, text=True).stdout
        m = re.search(r'ImageBase\s+([0-9a-fA-F]+)', out)
        base = int(m.group(1), 16) if m else 0x140000000
    names = {}
    if sites:
        out = subprocess.run(['addr2line', '-f', '-C', '-s', '-e', exe] + ['%x' % (int(s, 16) + base) for s in sites],
                             capture_output=True, text=True).stdout.split('\n')
        for i, s in enumerate(sites):
            fn = out[2 * i] if 2 * i < len(out) else '?'
            loc = out[2 * i + 1] if 2 * i + 1 < len(out) else '?'
            names[s] = '%s (%s)' % (re.sub(r'\(.*', '', fn), loc)
    return names


if a.idle:
    kinds = collections.defaultdict(list)
    lines = 0
    late = collections.Counter()
    late_n = collections.Counter()
    late_lines = 0
    queued = collections.Counter()  # rc4
    queued_n = collections.Counter()
    by_len = collections.Counter()
    by_len_n = collections.Counter()
    queued_lines = 0
    for line in open(a.log, errors='replace'):
        if 'gpu idle queued after:' in line:  # rc4
            queued_lines += 1
            head, _, tail = line.partition('; by the command buffer before:')
            for m in re.finditer(r' ([0-9a-f]+|present|unknown)=([0-9.]+)ms/s\(([0-9.]+)/s\)', head):
                queued[m.group(1)] += float(m.group(2))
                queued_n[m.group(1)] += float(m.group(3))
            for m in re.finditer(r' (lt50us|50_200us|200us_1ms|gt1ms) ([0-9.]+)ms/s\(([0-9.]+)/s\)', tail):
                by_len[m.group(1)] += float(m.group(2))
                by_len_n[m.group(1)] += float(m.group(3))
            continue
        if 'gpu idle late after:' in line:
            late_lines += 1
            for m in re.finditer(r' ([0-9a-f]+|present)=([0-9.]+)ms/s\(([0-9.]+)/s\)', line):
                late[m.group(1)] += float(m.group(2))
                late_n[m.group(1)] += float(m.group(3))
            continue
        if 'gpu idle:' not in line:
            continue
        lines += 1
        m = re.search(r'gpu idle: ([0-9.]+) ms/s in ([0-9.]+) gaps/s', line)
        if m:
            kinds['all'].append(float(m.group(1)))
        for k in ('late', 'queued', 'before present'):
            m = re.search(r' %s ([0-9.]+) ms/s \(([0-9.]+)/s: <25us ([0-9.]+), 25-250us ([0-9.]+), >250us ([0-9.]+)' % k, line)
            if m:
                kinds[k].append(tuple(float(x) for x in m.groups()))
        m = re.search(r'submits ([0-9.]+)/s, ([0-9.]+)% found the GPU idle; hand-over to recorded ([0-9.]+) us .*?to vkQueueSubmit ([0-9.]+) us', line)
        if m:
            kinds['submits'].append(tuple(float(x) for x in m.groups()))
        m = re.search(r'queued after an occlusion resolve ([0-9.]+) ms/s \(([0-9.]+)/s\)', line)
        if m:  # rc3
            kinds['queued after occlusion'].append(tuple(float(x) for x in m.groups()))
        m = re.search(r'flush_locked\) ([0-9.]+)/s, ([0-9.]+) ms/s; splits ([0-9.]+)/s', line)
        if m:
            kinds['waits'].append(tuple(float(x) for x in m.groups()))
        m = re.search(r'batched ([0-9.]+) jobs/s into ([0-9.]+) submits/s, held ([0-9.]+)/s for ([0-9.]+) ms/s', line)
        if m:  # test7c, HELIAS_SUBMIT_BATCH
            kinds['batch'].append(tuple(float(x) for x in m.groups()))
    if not lines:
        raise SystemExit('no "gpu idle:" lines (a build from test7 on, with the GPU busy meter on)')
    mean = lambda v: sum(v) / len(v) if v else 0.0
    print('%d reports; GPU idle between command buffers %.2f ms/s on average' % (lines, mean(kinds['all'])))
    for k in ('late', 'queued', 'before present'):
        v = kinds[k]
        if v:
            print('  %-15s %6.2f ms/s %6.0f gaps/s  (<25us %.2f, 25-250us %.2f, >250us %.2f ms/s)' %
                  ((k,) + tuple(mean([x[i] for x in v]) for i in range(5))))
    if kinds['queued after occlusion']:  # rc3
        v = kinds['queued after occlusion']
        print('    of the queued, after an occlusion resolve %.2f ms/s %.0f gaps/s' % tuple(mean([x[i] for x in v]) for i in range(2)))
    if kinds['submits']:
        v = kinds['submits']
        print('  submits %.0f/s, %.0f%% found the GPU idle; hand-over to recorded %.0f us, to vkQueueSubmit %.0f us' %
              tuple(mean([x[i] for x in v]) for i in range(4)))
    if kinds['waits']:
        v = kinds['waits']
        print('  cpu waits for the GPU %.0f/s, %.2f ms/s; splits %.0f/s' % tuple(mean([x[i] for x in v]) for i in range(3)))
    if kinds['batch']:
        v = kinds['batch']
        print('  batched %.0f jobs/s into %.0f submits/s; held %.0f/s for %.2f ms/s' % tuple(mean([x[i] for x in v]) for i in range(4)))
    if late_lines:
        names = name_sites(sorted(s for s in late if s != 'present'), a.exe, a.windows)
        print('late gaps by what ended the command buffer before them (mean over %d reports):' % late_lines)
        for site, ms in late.most_common(a.top):
            print('%8.2f ms/s %6.0f/s  %s' % (ms / late_lines, late_n[site] / late_lines, names.get(site, site)))
    if queued_lines:  # rc4
        names = name_sites(sorted(s for s in queued if s not in ('present', 'unknown')), a.exe, a.windows)
        print('queued gaps by what ended the command buffer before them (mean over %d reports):' % queued_lines)
        for site, ms in queued.most_common(a.top):
            print('%8.2f ms/s %6.0f/s  %s' % (ms / queued_lines, queued_n[site] / queued_lines, names.get(site, site)))
        print('queued gaps by the length of the command buffer before them:')
        for k in ('lt50us', '50_200us', '200us_1ms', 'gt1ms'):
            print('%8.2f ms/s %6.0f/s  %s' % (by_len[k] / queued_lines, by_len_n[k] / queued_lines, k))
    raise SystemExit(0)

total = collections.Counter()
count = collections.Counter()
reports = 0
for line in open(a.log, errors='replace'):
    if a.census:
        if 'cmd census:' not in line:
            continue
        reports += 1
        for m in re.finditer(r' ([0-9a-f]+)=([0-9.]+)/flip', line):
            total[m.group(1)] += float(m.group(2))
        continue
    if 'gpu gaps:' not in line:
        continue
    reports += 1
    for m in re.finditer(r' ([0-9a-f+]+|none)=([0-9.]+)ms\((\d+)\)', line):
        total[m.group(1)] += float(m.group(2))
        count[m.group(1)] += int(m.group(3))
if not reports:
    raise SystemExit('no "cmd census:" lines (run with HELIAS_CMD_CENSUS=1)' if a.census else
                     'no "gpu gaps:" lines (run with HELIAS_GPU_PROFILE=2)')

base = 0
if a.windows:
    out = subprocess.run(['objdump', '-p', a.exe], capture_output=True, text=True).stdout
    m = re.search(r'ImageBase\s+([0-9a-fA-F]+)', out)
    base = int(m.group(1), 16) if m else 0x140000000
sites = sorted({s for k in total for s in k.split('+') if s != 'none'})
names = {}
if sites:
    out = subprocess.run(['addr2line', '-f', '-C', '-s', '-e', a.exe] + ['%x' % (int(s, 16) + base) for s in sites],
                         capture_output=True, text=True).stdout.split('\n')
    for i, s in enumerate(sites):
        fn = out[2 * i] if 2 * i < len(out) else '?'
        loc = out[2 * i + 1] if 2 * i + 1 < len(out) else '?'
        fn = re.sub(r'\(.*', '', fn)
        names[s] = '%s (%s)' % (fn, loc)
if a.census:
    # A site missing from a report's top twelve counts 0 there.
    print('%d reports; in-place recording sites, mean per flip' % reports)
    for site, n in total.most_common(a.top):
        print('%8.1f /flip  %s' % (n / reports, names.get(site, site)))
    raise SystemExit(0)
grand = sum(total.values())
print('%d reports, %.0f ms of GPU time outside profiled draws in all' % (reports, grand))
for key, ms in total.most_common(a.top):
    label = ' + '.join(names.get(s, s) for s in key.split('+'))
    print('%8.0f ms %5.1f%% %8d gaps  %s' % (ms, 100 * ms / grand if grand else 0, count[key], label))
