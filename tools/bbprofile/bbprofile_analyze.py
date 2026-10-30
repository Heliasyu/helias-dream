#!/usr/bin/env python3
"""Box-side analysis of a bbprofile.sh session tarball (profiling kit, after rc5b).

  bbprofile_analyze.py ~/bbprofile-20261021-190000.tar.gz [--out DIR] [--symbols heliasdream-prof.debug]
  bbprofile_analyze.py DIR                        an extracted session folder
  bbprofile_analyze.py pgo-merge SESSION OUT.profdata [--llvm-profdata llvm-profdata-19]

Writes REPORT.md (and cp0.folded / hot.folded for flame graphs) into the session
folder and prints the report. Sections: system facts; the A/B table (each step's
two 30 s windows, against a1 and the a6 drift); M4 (the flip trace's slow frames
split by stage, the main-loop waits, the perf sched trace around each slow frame);
the CPU profile (bb-cp0's self/inclusive functions, stage buckets, hot chain);
sysmon; PGO profiles. Standard library only (plus perf output files, no perf needed).
"""
import argparse, collections, glob, gzip, io, os, re, shutil, statistics as st, subprocess, sys, tarfile

STEPS = ["sys", "a1", "a2", "a3", "a4", "m4", "pgo", "a5", "a6"]
LABEL = {"a1": "plain", "a2": "TABLES_VRAM=0", "a3": "PARAMS_WC=1", "a4": "PARAMS_VRAM=1", "m4": "M4 (traced)",
         "a5": "PS_WAVE32=32", "a6": "plain again", "pgo": "PGO (instrumented)"}
HOT = ["main", "heliasdream.bin", "GXRenderThread", "bb-cp0", "bb-record", "bb-submit", "bb-present", "bb-vblank"]

out_lines = []
def p(s=""):
    out_lines.append(s)

def opener(path):
    return gzip.open(path, "rt", errors="replace") if path.endswith(".gz") else open(path, errors="replace")

def mean(a):
    return sum(a) / len(a) if a else float("nan")

def pct(a, q):
    if not a:
        return float("nan")
    a = sorted(a)
    return a[min(len(a) - 1, int(q * len(a)))]

def f2(x, nd=2):
    return "-" if x is None or x != x else f"{x:.{nd}f}"

# ---------------------------------------------------------------- snapshots
def parse_snap(path):
    d = {"path": path, "sections": collections.defaultdict(list), "kv": {}, "seconds": [], "frames": [], "log": [], "flip": [], "flip_hdr": None,
         "base_ns": None}
    sect = None
    for line in open(path, errors="replace"):
        line = line.rstrip("\n")
        if line.startswith("[") and not line.startswith("[heliasdream]"):
            sect = line.split("]")[0][1:]
            continue
        if sect is None:
            if ": " in line:
                k, v = line.split(": ", 1)
                d["kv"][k.strip()] = v
            continue
        if sect == "seconds" and line[:1].isdigit():
            parts = [x.strip() for x in line.split(",")]
            try:
                thr = {m.group(1).strip(): float(m.group(2)) for m in re.finditer(r"([A-Za-z][\w:.\- ]*?)=(\d+)", parts[5])}
                d["seconds"].append(dict(t=float(parts[0]), busy=float(parts[2]), cmd=float(parts[3]), thr=thr))
            except (ValueError, IndexError):
                pass
        elif sect == "frames" and line[:1].isdigit():
            try:
                d["frames"].append([float(x) for x in line.split(",")])
            except ValueError:
                pass
        elif sect == "log":
            m = re.match(r"(-?\d+\.\d+) (.*)", line)
            if m:
                d["log"].append((float(m.group(1)), m.group(2)))
        elif sect == "flip_trace":
            if line.startswith("# base_steady_ns="):
                d["base_ns"] = int(line.split("=", 1)[1])
            elif line.startswith("# id,"):
                d["flip_hdr"] = line[2:].split(",")
            elif line[:1].isdigit() and d["flip_hdr"]:
                try:
                    d["flip"].append(dict(zip(d["flip_hdr"], [float(x) for x in line.split(",")])))
                except ValueError:
                    pass
        else:
            d["sections"][sect].append(line)
    return d

def kvs(line):
    return {k: v for k, v in re.findall(r"(\w+)=([-\d.]+)", line)}

def sect_line(d, sect, prefix):
    for l in d["sections"].get(sect, []):
        if l.startswith(prefix):
            return l
    return ""

def snap_stats(d):
    s = {}
    summ = kvs(d["kv"].get("summary", ""))
    for k in ("avg_fps", "low1", "low01", "over8.5", "over9", "over12", "max_ms"):
        try:
            s[k] = float(summ.get(k, "nan"))
        except ValueError:
            s[k] = float("nan")
    iv = [f[1] for f in d["frames"] if f[1] < 250]
    s["frames"] = len(iv)
    s["mean_ms"] = mean(iv)
    s["sd_ms"] = st.pstdev(iv) if len(iv) > 1 else float("nan")
    s["p99_ms"] = pct(iv, 0.99)
    s["over11"] = sum(1 for x in iv if x > 11)
    s["pct_over8.5"] = 100.0 * sum(1 for x in iv if x > 8.5) / len(iv) if iv else float("nan")
    fps_s = collections.Counter(int(f[0] // 1000) for f in d["frames"] if f[1] < 250)
    secs = d["seconds"]
    s["gpu_busy"] = mean([x["busy"] for x in secs])
    s["cmd_ms_s"] = mean([x["cmd"] for x in secs])
    g = [x["cmd"] / fps_s[int(x["t"])] for x in secs if fps_s.get(int(x["t"]))]
    s["gpu_ms_frame"] = mean(g)
    for t in ("bb-cp0", "bb-record", "bb-submit", "heliasdream.bin", "GXRenderThread"):
        s["cpu_" + t] = mean([x["thr"].get(t, 0.0) for x in secs])
    q, late, busy = [], [], []
    for t, l in d["log"]:
        if "gpu idle:" in l and "queued" in l:
            m = re.search(r"queued ([\d.]+) ms/s", l)
            m2 = re.search(r"late ([\d.]+) ms/s", l)
            if m:
                q.append(float(m.group(1)))
            if m2:
                late.append(float(m2.group(1)))
    s["queued_idle_ms_s"] = mean(q)
    s["late_idle_ms_s"] = mean(late)
    iq = kvs(sect_line(d, "gpu_idle", "idle_with_queued:"))
    s["iwq_total_ms"] = float(iq.get("ms", "nan"))
    cc = sect_line(d, "cpu", "cp_cost:")
    m = re.search(r"since_last_snapshot: draws=(\d+) exec_ns_per_draw=(\d+)", cc)
    s["exec_ns_draw"] = float(m.group(2)) if m else float(kvs(cc).get("exec_ns_per_draw", "nan"))
    # rc6: draws per frame in the window - the fixed-view check (the same
    # spot and camera should draw the same scene within a few percent)
    s["draws_frame"] = float(m.group(1)) / s["frames"] if m and s["frames"] else float("nan")
    pt = sect_line(d, "walks", "page_tables:")
    s["page_tables"] = pt.split("(")[0].replace("page_tables:", "").strip() if pt else "-"
    s["env"] = [l for l in d["sections"].get("env", []) if l.strip()]
    s["area"] = d["kv"].get("area", "?")
    s["version"] = d["kv"].get("version", "?")
    return s

def step_snaps(sd):
    files = sorted(glob.glob(os.path.join(sd, "snaps", "snap-*.txt")))
    snaps = [parse_snap(f) for f in files]
    # the measurement windows are the ones the driver requested (trigger 'bbtest snap'); else the last two
    meas = [x for x in snaps if x["kv"].get("trigger", "").startswith("bbtest")]
    return snaps, (meas[-2:] if len(meas) >= 2 else snaps[-2:])

# ---------------------------------------------------------------- report parts
def section_system(root):
    S = os.path.join(root, "steps", "sys")
    p("## System")
    if not os.path.isdir(S):
        p("(no sys step)"); p(); return
    def read(n):
        f = os.path.join(S, n)
        return open(f, errors="replace").read() if os.path.exists(f) else ""
    mem = read("dmidecode-memory.txt")
    if mem:
        speeds = collections.Counter(re.findall(r"^\s*Speed: (.+)$", mem, re.M))
        conf = collections.Counter(re.findall(r"Configured (?:Memory|Clock) Speed: (.+)$", mem, re.M))
        parts = collections.Counter(x.strip() for x in re.findall(r"Part Number: (.+)$", mem, re.M) if x.strip() not in ("Unknown", "Not Specified"))
        sizes = [x for x in re.findall(r"^\s*Size: (.+)$", mem, re.M) if "No Module" not in x]
        p(f"- RAM: {len(sizes)} modules {dict(collections.Counter(sizes))}; rated {dict(speeds)}; running {dict(conf)}; parts {dict(parts)}")
        try:
            rated = max(int(re.match(r"(\d+)", k).group(1)) for k in speeds if re.match(r"\d+", k))
            run = max(int(re.match(r"(\d+)", k).group(1)) for k in conf if re.match(r"\d+", k))
            p(f"  -> {'running at the rated speed (EXPO/XMP likely on)' if run >= rated else f'running {run} below the rated {rated}: EXPO probably OFF'}"
              " (dmidecode's rated speed is the SPD/JEDEC or EXPO profile the board reports; check the part number's spec)")
        except ValueError:
            pass
    else:
        p("- RAM: no dmidecode (no sudo?)")
    cpu = read("cpu-power.txt")
    for key in ("cpu0 scaling_driver", "governors", "epp (all", "amd_pstate status", "boost", "platform_profile", "power-profiles-daemon", "sched_ext", "thp",
                "service scx", "service scx_loader", "service ananicy-cpp", "service gamemoded"):
        for l in cpu.splitlines():
            if l.startswith(key):
                p(f"- {l.strip()}")
                break
    gpu = read("gpu.txt")
    for pat in (r"power1_cap: (\d+)", r"power1_cap_max: (\d+)"):
        m = re.search(pat, gpu)
        if m:
            p(f"- GPU {pat.split(':')[0]}: {int(m.group(1)) / 1e6:.0f} W")
    for l in gpu.splitlines():
        if re.search(r"BAR \d+: current size|Resizable BAR|LnkSta:", l):
            p(f"- GPU {l.strip()}")
    m = re.search(r"== power_dpm_force_performance_level\n(.*)", gpu)
    if m:
        p(f"- GPU power_dpm_force_performance_level: {m.group(1)}")
    m = re.search(r"== pp_power_profile_mode\n((?:.*\n){1,12})", gpu)
    if m:
        act = [l for l in m.group(1).splitlines() if "*" in l]
        if act:
            p(f"- GPU power profile: {act[0].strip()}")
    vk = read("vulkaninfo-summary.txt")
    m = re.search(r"driverInfo\s*=\s*(.+)", vk)
    if m:
        p(f"- Vulkan driver: {m.group(1).strip()}")
    disp = read("display.txt")
    for l in disp.splitlines():
        if re.search(r"^Output:|\*", l) and len(l) < 200:
            p(f"- display: {l.strip()}")
    pk = read("packages.txt").split("\n")
    if any(pk):
        p("- packages: " + "; ".join(x for x in pk if x))
    p()

def section_ab(root):
    p("## A/B at the Old Yharnam spot (two 30 s windows each)")
    rows = {}
    for sname in ("a1", "a2", "a3", "a4", "m4", "a5", "a6"):
        sd = os.path.join(root, "steps", sname)
        if not os.path.isdir(sd):
            continue
        snaps, meas = step_snaps(sd)
        if not meas:
            p(f"- {sname}: no snapshots"); continue
        ws = [snap_stats(x) for x in meas]
        r = {}
        for k in ws[0]:
            v = [w[k] for w in ws if isinstance(w.get(k), float)]
            r[k] = mean(v) if v else ws[-1][k]
        if len(meas) == 2:
            t0 = meas[0]["kv"].get("time", ""); t1 = meas[1]["kv"].get("time", "")
            dms = ws[1]["iwq_total_ms"] - ws[0]["iwq_total_ms"]
            r["iwq_delta_ms_s"] = dms / 30.0 if dms == dms else float("nan")
        else:
            r["iwq_delta_ms_s"] = float("nan")
        r["areas"] = sorted({w["area"] for w in ws})
        r["windows"] = len(ws)
        rows[sname] = r
    if not rows:
        p("(no A/B data)"); p(); return rows
    cols = [("avg_fps", "fps", 1), ("low1", "1% low", 1), ("pct_over8.5", "%>8.5ms", 1), ("over11", ">11ms/30s", 1), ("p99_ms", "p99 ms", 2),
            ("gpu_busy", "GPU busy%", 1), ("gpu_ms_frame", "GPU ms/frame", 2), ("queued_idle_ms_s", "queued idle ms/s", 1),
            ("iwq_delta_ms_s", "queued idle (totals) ms/s", 1), ("late_idle_ms_s", "late idle ms/s", 1), ("exec_ns_draw", "CP ns/draw", 0),
            ("draws_frame", "draws/frame", 0), ("cpu_bb-cp0", "bb-cp0 ms/s", 0), ("cpu_bb-record", "bb-record ms/s", 0), ("cpu_heliasdream.bin", "game thr ms/s", 0)]
    p("| step | " + " | ".join(c[1] for c in cols) + " |")
    p("|" + "---|" * (len(cols) + 1))
    for sname, r in rows.items():
        p(f"| {sname} {LABEL.get(sname, '')} | " + " | ".join(f2(r.get(k), nd) for k, _, nd in cols) + " |")
    p()
    for sname, r in rows.items():
        p(f"- {sname}: area {', '.join(r['areas'])}; page tables: {r.get('page_tables')}; env in snapshot: {'; '.join(r.get('env') or []) or '(none)'}")
    base = rows.get("a1")
    if base:
        drift = rows.get("a6")
        p()
        p("Against a1 (plain). 'drift' = a6 - a1 (same binary, same spot, end of the session):")
        keys = [("gpu_ms_frame", "GPU ms/frame", -1), ("queued_idle_ms_s", "queued idle ms/s", -1), ("gpu_busy", "GPU busy%", -1),
                ("pct_over8.5", "%>8.5ms", -1), ("low1", "1% low", 1), ("exec_ns_draw", "CP ns/draw", -1)]
        # rc6: a step whose view was not the same scene (draws per frame more
        # than 5% from a1's) compares different work: say so first.
        bd = base.get("draws_frame")
        for sname, r in rows.items():
            dd = r.get("draws_frame")
            if sname != "a1" and isinstance(bd, float) and isinstance(dd, float) and bd == bd and dd == dd and bd > 0 and abs(dd - bd) / bd > 0.05:
                p(f"- WARNING: {sname} drew {dd:.0f} draws/frame against a1's {bd:.0f} ({(dd - bd) / bd * 100:+.1f}%): not the same view "
                  "(spot, camera or what is on screen differs) - its A/B numbers compare different work")
        for sname, r in rows.items():
            if sname in ("a1", "a6"):
                continue
            parts = []
            for k, name, sign in keys:
                a, b = base.get(k), r.get(k)
                if isinstance(a, float) and isinstance(b, float) and a == a and b == b:
                    dlt = b - a
                    dr = abs(drift[k] - a) if drift and isinstance(drift.get(k), float) and drift[k] == drift[k] else None
                    tag = ""
                    if dr is not None:
                        tag = " (within drift)" if abs(dlt) <= dr else (" BETTER" if dlt * sign > 0 else " WORSE")
                    parts.append(f"{name} {dlt:+.2f}{tag}")
            p(f"- {sname} {LABEL.get(sname, '')}: " + "; ".join(parts))
        if drift:
            p(f"- drift a6-a1: GPU ms/frame {drift['gpu_ms_frame'] - base['gpu_ms_frame']:+.2f}, queued idle {drift['queued_idle_ms_s'] - base['queued_idle_ms_s']:+.1f} ms/s, "
              f"%>8.5ms {drift['pct_over8.5'] - base['pct_over8.5']:+.1f}")
        p("Reading: the game is capped at 120 fps, so fps barely moves; judge by GPU ms/frame, queued idle and the frame-time tail (%>8.5 ms, 1% low).")
    p()
    return rows

def load_threads(sd):
    tids = {}
    for f in (os.path.join(sd, "perf", "threads-start.txt"), os.path.join(sd, "perf", "threads-end.txt")):
        if os.path.exists(f):
            for l in open(f, errors="replace"):
                m = re.match(r"(\d+) (.+?) cpu=", l)
                if m:
                    tids[int(m.group(1))] = m.group(2)
    return tids

def section_m4(root):
    sd = os.path.join(root, "steps", "m4")
    p("## M4: the periodic ~12 ms frame")
    if not os.path.isdir(sd):
        p("(m4 not run)"); p(); return
    snaps, meas = step_snaps(sd)
    rows = []
    for s in meas:
        rows += s["flip"]
    if not rows:
        p("no [flip_trace] rows (was HELIAS_FLIP_TRACE=1 picked up?)")
    else:
        slow = [r for r in rows if r.get("interval_ms", 0) >= 11 and r.get("interval_ms", 0) < 250]
        norm = [r for r in rows if 0 < r.get("interval_ms", 0) < 9.5]
        span_s = (sum(r["interval_ms"] for r in rows if r["interval_ms"] < 250)) / 1000
        p(f"- {len(rows)} presented flips traced over {span_s:.0f} s; {len(slow)} slow (>= 11 ms) = {len(slow) / span_s:.2f}/s")
        ts = sorted(r["t_ms"] for r in slow)
        gaps = [b - a for a, b in zip(ts, ts[1:]) if b > a]
        if gaps:
            p(f"- spacing between slow frames: median {st.median(gaps):.0f} ms, p10 {pct(gaps, .1):.0f}, p90 {pct(gaps, .9):.0f} ms"
              f" ({'periodic' if pct(gaps, .9) - pct(gaps, .1) < 0.3 * st.median(gaps) else 'irregular'})")
        comps = ["submit_gap_ms", "main_frame_ms", "main_end_to_submit_ms", "backpressure_ms", "queue_ms", "cp_walk_ms", "to_video_ms", "to_present_ms",
                 "cp_busy_ms", "cp_idle_ms", "jobs", "backlog"]
        p()
        p("| component | normal median | slow median | slow - normal |")
        p("|---|---|---|---|")
        excess = {}
        for c in comps:
            a = [r[c] for r in norm if r.get(c, -1) >= 0]
            b = [r[c] for r in slow if r.get(c, -1) >= 0]
            if a and b:
                excess[c] = st.median(b) - st.median(a)
                p(f"| {c} | {st.median(a):.2f} | {st.median(b):.2f} | {excess[c]:+.2f} |")
        thr = [h[:-7] for h in (meas[0]["flip_hdr"] or []) if h.endswith("_run_ms")]
        for t in thr:
            for kind in ("run", "wait"):
                c = f"{t}_{kind}_ms"
                a = [r[c] for r in norm if r.get(c, -1) >= 0]
                b = [r[c] for r in slow if r.get(c, -1) >= 0]
                if a and b:
                    excess[c] = st.median(b) - st.median(a)
                    p(f"| {t} {'run-queue' if kind == 'wait' else 'run'} | {st.median(a):.2f} | {st.median(b):.2f} | {excess[c]:+.2f} |")
        p()
        stage = {k: v for k, v in excess.items() if k in ("submit_gap_ms", "backpressure_ms", "queue_ms", "cp_walk_ms", "to_video_ms", "to_present_ms")}
        if stage:
            top = max(stage, key=lambda k: stage[k])
            hint = {"submit_gap_ms": "the game thread submitted late (its own frame: see main_frame_ms and the main-loop waits below)",
                    "backpressure_ms": "the game thread waited on the CP backlog (CP behind)",
                    "queue_ms": "the CP picked the flip up late (busy with earlier jobs)",
                    "cp_walk_ms": "the CP walk itself took longer (more work in that frame, or bb-cp0 preempted: see its run-queue)",
                    "to_video_ms": "the hand-off to video was slow", "to_present_ms": "the presenter/pacer side"}[top]
            p(f"- biggest stage excess in slow frames: **{top} {stage[top]:+.2f} ms** -> {hint}")
            waits = {k: v for k, v in excess.items() if k.endswith("_wait_ms")}
            if waits:
                tw = max(waits, key=lambda k: waits[k])
                p(f"- biggest run-queue excess: {tw[:-8]} {waits[tw]:+.2f} ms (scheduler delay; > ~0.5 ms points at preemption/placement)")
        # slow frames: the stage per slow frame
        p()
        p("Slow frames (first 15): interval, submit gap, main frame, back-pressure, queue, CP walk (jobs), cp0 run/run-queue")
        for r in slow[:15]:
            p(f"- t={r['t_ms']:.0f} ms {r['interval_ms']:.2f}: gap {r['submit_gap_ms']:.2f} main {r['main_frame_ms']:.2f} bp {r['backpressure_ms']:.2f} "
              f"q {r['queue_ms']:.2f} walk {r['cp_walk_ms']:.2f} ({r['jobs']:.0f}) cp0 {r.get('bb-cp0_run_ms', -1):.2f}/{r.get('bb-cp0_wait_ms', -1):.2f}")
    # the game log: slow-frame lines, main-loop waits
    log = os.path.join(sd, "game.log")
    slow_t = []
    if os.path.exists(log):
        txt = open(log, errors="replace").read()
        for m in re.finditer(r"fliptrace: slow frame ([\d.]+) ms \(>= [\d.]+\) at ([\d.]+) s monotonic", txt):
            slow_t.append((float(m.group(2)), float(m.group(1))))
        mains = re.findall(r"main loop: a ([\d.]+) ms frame before flip (\d+), ([\d.]+) ms of it waiting(?::(.*))?", txt)
        p()
        p(f"- game.log: {len(slow_t)} `fliptrace: slow frame` lines; {len(mains)} `main loop: a N ms frame` lines (main-loop frames >= 11 ms)")
        if mains:
            fr = [float(m[0]) for m in mains]; wt = [float(m[2]) for m in mains]
            p(f"  main-loop slow frames: median {st.median(fr):.1f} ms, of it waiting median {st.median(wt):.1f} ms "
              f"({'mostly waiting: the game thread is blocked on something' if st.median(wt) > 0.5 * st.median(fr) else 'mostly running: the game thread itself is slow'})")
            sites = collections.Counter(); nsite = collections.Counter()
            for m in mains:
                for piece in (m[3] or "").split(";"):
                    mm = re.match(r"\s*(.*?)\s*([\d.]+) ms\s*$", piece)
                    if mm and mm.group(1):
                        sites[mm.group(1)] += float(mm.group(2)); nsite[mm.group(1)] += 1
            if sites:
                p("  their waits by call site (ms summed, frames): " + "; ".join(f"{k} {v:.1f} ({nsite[k]})" for k, v in sites.most_common(8)))
        wl = re.findall(r"wait: (\S+) waited ([\d.]+) ms[^\n]*", txt)
        if wl:
            byt = collections.Counter(w[0] for w in wl)
            p(f"- HELIAS_WAIT_LOG (>= 8 ms waits): {len(wl)} lines; by thread {dict(byt.most_common(5))}; longest {max(float(w[1]) for w in wl):.1f} ms")
    # perf sched around the slow frames
    th = os.path.join(sd, "perf", "sched-timehist.txt.gz")
    if os.path.exists(th) and slow_t:
        sched_correlate(th, slow_t, load_threads(sd))
    elif not os.path.exists(th):
        p("- no perf sched trace")
    p()

def sched_correlate(path, slow_t, tids):
    rx = re.compile(r"^\s*(\d+\.\d+)\s+\[(\d+)\]\s+(.+?)\[(\d+)(?:/(\d+))?\]\s+(\d+\.\d+)\s+(\d+\.\d+)\s+(\d+\.\d+)")
    slices = []  # (end, cpu, comm, tid, wait, schdelay, run)
    for l in opener(path):
        m = rx.match(l)
        if m:
            slices.append((float(m.group(1)), int(m.group(2)), m.group(3).strip(), int(m.group(4)), float(m.group(6)), float(m.group(7)), float(m.group(8))))
    if not slices:
        p("- perf sched timehist: no lines parsed"); return
    t0, t1 = slices[0][0], slices[-1][0]
    inside = [(t, iv) for t, iv in slow_t if t0 <= t <= t1]
    p(f"- perf sched: {len(slices)} slices over {t1 - t0:.1f} s; {len(inside)} slow frames inside the trace")
    if not inside:
        p("  (no overlap: the slow-frame times are CLOCK_MONOTONIC like perf -k CLOCK_MONOTONIC; check record-sched.log)"); return
    ours = set(tids)
    hot_tids = {t for t, n in tids.items() if n in HOT}
    slices.sort()
    ends = [s[0] for s in slices]
    import bisect
    def window(a, b):
        i = bisect.bisect_left(ends, a)
        out = []
        while i < len(slices) and slices[i][0] - slices[i][6] / 1000 <= b:
            out.append(slices[i]); i += 1
            if i < len(slices) and slices[i][0] > b + 0.05:
                break
        return out
    def summarize(wins):
        delay = collections.Counter(); run = collections.Counter(); other = collections.Counter()
        for a, b in wins:
            ws = window(a, b)
            cpus = {s[1] for s in ws if s[3] in hot_tids}
            for s in ws:
                if s[3] in hot_tids:
                    delay[tids[s[3]]] += s[5]; run[tids[s[3]]] += s[6]
                elif s[3] not in ours and s[1] in cpus and s[2] not in ("<idle>", "swapper"):
                    other[s[2]] += s[6]
        n = max(1, len(wins))
        return ({k: v / n for k, v in delay.items()}, {k: v / n for k, v in run.items()}, {k: v / n for k, v in other.items()})
    slow_w = [(t - iv / 1000, t) for t, iv in inside]
    import random
    random.seed(1)
    norm_w = []
    for _ in range(400):
        t = random.uniform(t0 + 0.02, t1)
        if all(abs(t - s) > 0.02 for s, _ in inside):
            norm_w.append((t - 0.00833, t))
    sd_, sr, so = summarize(slow_w)
    nd, nr, no = summarize(norm_w)
    p("  per frame window (ms): thread: sched delay slow vs normal; run slow vs normal")
    for k in sorted(set(sd_) | set(nd), key=lambda k: -(sd_.get(k, 0) - nd.get(k, 0))):
        p(f"  - {k}: delay {sd_.get(k, 0):.2f} vs {nd.get(k, 0):.2f}; run {sr.get(k, 0):.2f} vs {nr.get(k, 0):.2f}")
    top = sorted(so.items(), key=lambda kv: -(kv[1] - no.get(kv[0], 0)))[:8]
    if top:
        p("  other tasks on our hot threads' CPUs (ms per window, slow vs normal): " + "; ".join(f"{k} {v:.2f} vs {no.get(k, 0):.2f}" for k, v in top))

# ---------------------------------------------------------------- CPU profile
STAGE_HINTS = [("buffers", r"buffer|stage_buffers|resolve_.*buf|vsharp|shadow"), ("prefetch", r"prefetch"), ("programs", r"program|shader|translat|pipeline_for|variant"),
               ("targets", r"target|render_target|rt_|color_|depth_"), ("key", r"key|hash|lookup|find"), ("descriptors/sets", r"descriptor|set_|sets|update"),
               ("record", r"record|cmd|vkCmd"), ("pm4 parse", r"pm4|packet|parse|gnm_exec|cp_"), ("alloc/copy", r"memcpy|memmove|memset|malloc|free|operator new"),
               ("locks", r"mutex|lock|futex|pthread")]

def parse_script(path):
    """perf script -F comm,tid,time,ip,sym,dso: yields (comm, tid, [(ip, sym, dso) leaf first])."""
    comm = tid = None; frames = []
    hdr = re.compile(r"^(\S.*?)\s+(\d+)\s+(\d+\.\d+):")
    fr = re.compile(r"^\s+([0-9a-f]+)\s+(.*?)\s+\((.*)\)\s*$")
    for l in opener(path):
        if not l.strip():
            if comm is not None:
                yield comm, tid, frames
            comm = None; frames = []
            continue
        if not l[0].isspace():
            if comm is not None:
                yield comm, tid, frames
            m = hdr.match(l)
            comm, tid, frames = (m.group(1), int(m.group(2)), []) if m else (None, None, [])
            continue
        m = fr.match(l)
        if m and comm is not None:
            frames.append((int(m.group(1), 16), m.group(2), os.path.basename(m.group(3))))
    if comm is not None:
        yield comm, tid, frames

def clean_sym(s):
    s = re.sub(r"\+0x[0-9a-f]+$", "", s)
    return s if s and s != "[unknown]" else "?"

def section_cpu(root, symbols):
    sd = os.path.join(root, "steps", "a1", "perf")
    p("## CPU profile (a1)")
    if not os.path.isdir(sd):
        p("(no perf data)"); p(); return
    summ = os.path.join(sd, "summary.txt")
    if os.path.exists(summ):
        p("### whole process (flat, perf_cpu_summary.py)")
        p("```"); p(open(summ, errors="replace").read()[:6000].rstrip()); p("```")
    st_ = os.path.join(sd, "stat-per-thread.txt")
    if os.path.exists(st_):
        lines = [l for l in open(st_, errors="replace") if re.search(r"bb-cp0|heliasdream.bin|GXRender|bb-record|bb-submit", l) and re.search(r"instructions|cycles|task-clock", l)]
        if lines:
            p("### perf stat (hot threads)"); p("```"); p("".join(lines[:40]).rstrip()); p("```")
    for name in ("cp0", "hot"):
        f = os.path.join(sd, f"{name}-dwarf.script.gz")
        if not os.path.exists(f):
            continue
        self_c = collections.Counter(); incl = collections.Counter(); folded = collections.Counter(); per_comm = collections.Counter()
        leaf_ip = collections.Counter(); stage = collections.Counter(); n = 0
        incl_by_comm = collections.defaultdict(collections.Counter)
        for comm, tid, frames in parse_script(f):
            if not frames:
                continue
            n += 1; per_comm[comm] += 1
            syms = [clean_sym(s) for _, s, _ in frames]
            self_c[(comm, syms[0])] += 1
            leaf_ip[frames[0][0]] += 1
            for s in set(syms) - {"?"}:
                incl[(comm, s)] += 1
                incl_by_comm[comm][s] += 1
            folded[";".join([comm] + syms[::-1])] += 1
            joined = " ".join(syms).lower()
            for b, rx in STAGE_HINTS:
                if re.search(rx, syms[0].lower()) or (b in ("locks", "alloc/copy") and re.search(rx, joined.split(" ")[0])):
                    stage[b] += 1; break
            else:
                stage["other"] += 1
        with open(os.path.join(root, f"{name}.folded"), "w") as fo:
            for k, v in folded.most_common():
                fo.write(f"{k} {v}\n")
        p(f"### {name} DWARF call graphs: {n} samples ({', '.join(f'{c} {v}' for c, v in per_comm.most_common(8))}); {name}.folded for flamegraph.pl")
        for comm, cnt in per_comm.most_common(6 if name == "hot" else 1):
            mine = [x for x in self_c.most_common() if x[0][0] == comm][:15]
            p(f"- **{comm}** top self: " + "; ".join(f"{s} {100 * v / cnt:.1f}%" for (c, s), v in mine))
            p("  top inclusive: " + "; ".join(f"{s} {100 * v / cnt:.1f}%" for s, v in incl_by_comm[comm].most_common(25)))
        if name == "cp0":
            p("- leaf buckets by name (rough; the cp_split counters in the snapshot are exact): " + "; ".join(f"{b} {100 * v / max(1, n):.1f}%" for b, v in stage.most_common()))
            if symbols:
                symbolize(sd, symbols, leaf_ip, n)
    p()

def symbolize(sd, symbols, leaf_ip, n):
    tool = shutil.which("llvm-symbolizer") or shutil.which("llvm-symbolizer-19")
    maps = os.path.join(sd, "maps.txt")
    if not tool or not os.path.exists(maps):
        p("- (no llvm-symbolizer or maps.txt: no inline view)"); return
    base = None
    for l in open(maps):
        parts = l.split()
        if len(parts) >= 6 and parts[5].endswith("heliasdream.bin") and int(parts[2], 16) == 0:
            base = int(parts[0].split("-")[0], 16); break
    if base is None:
        p("- (heliasdream.bin not in maps.txt)"); return
    top = leaf_ip.most_common(60)
    addrs = "\n".join(hex(ip - base) for ip, _ in top)
    r = subprocess.run([tool, "--obj=" + symbols, "--inlining", "--functions=short", "--relative-address"], input=addrs, capture_output=True, text=True)
    blocks = r.stdout.strip().split("\n\n")
    agg = collections.Counter()
    for (ip, cnt), b in zip(top, blocks):
        lines = b.strip().splitlines()
        if lines:
            fn = lines[0]; loc = lines[1] if len(lines) > 1 else ""
            outer = lines[-2] if len(lines) >= 4 else ""  # --inlining: innermost first, the real function last
            key = f"{fn} ({os.path.basename(loc.split(':')[0]) if loc else '?'})"
            agg[key + (f" in {outer}" if outer and outer != fn else "")] += cnt
    p("- bb-cp0 leaf code with inlining (the top 60 addresses, " + f"{100 * sum(c for _, c in top) / max(1, n):.0f}% of samples): "
      + "; ".join(f"{k} {100 * v / max(1, n):.1f}%" for k, v in agg.most_common(20)))

def section_sysmon(root):
    p("## sysmon (1 Hz, whole run incl. loading)")
    p("| step | GPU busy% avg | GPU W avg/max | sclk MHz avg | mclk MHz | GPU edge/junction C max | CPU MHz avg/max | Tctl max | game CPU% avg |")
    p("|---|---|---|---|---|---|---|---|---|")
    for s in STEPS:
        f = os.path.join(root, "steps", s, "sysmon.csv")
        if not os.path.exists(f):
            continue
        rows = list(csv_rows(f))
        if len(rows) < 3:
            continue
        def col(k):
            return [float(r[k]) for r in rows if r.get(k) not in (None, "")]
        def m(k):
            return mean(col(k))
        def mx(k):
            c = col(k); return max(c) if c else float("nan")
        p(f"| {s} | {f2(m('gpu_busy_pct'), 0)} | {f2(m('gpu_power_w'), 0)}/{f2(mx('gpu_power_w'), 0)} | {f2(m('gpu_sclk_mhz'), 0)} | {f2(m('gpu_mclk_mhz'), 0)} | "
          f"{f2(mx('gpu_edge_c'), 0)}/{f2(mx('gpu_junction_c'), 0)} | {f2(m('cpu_mhz_avg'), 0)}/{f2(mx('cpu_mhz_max'), 0)} | {f2(mx('tctl_c'), 0)} | {f2(m('game_cpu_pct'), 0)} |")
    p()

def csv_rows(f):
    with open(f) as fh:
        hdr = fh.readline().strip().split(",")
        for l in fh:
            yield dict(zip(hdr, l.strip().split(",")))

def section_pgo(root):
    p("## PGO")
    files = sorted(glob.glob(os.path.join(root, "steps", "pgo", "pgo", "*.profraw")))
    if not files:
        p("(no .profraw)"); p(); return
    for f in files:
        p(f"- {os.path.basename(f)} {os.path.getsize(f) / 1e6:.1f} MB")
    log = os.path.join(root, "steps", "pgo", "game.log")
    if os.path.exists(log):
        w = re.findall(r"pgo: profile written[^\n]*", open(log, errors="replace").read())
        p(f"- {len(w)} `pgo: profile written` lines logged (first three and the final one are logged)")
    p("- merge here (llvm-profdata 19, the version of the clang that built the instrumented binary):")
    p(f"  `python3 tools/bbprofile/bbprofile_analyze.py pgo-merge {root} heliasdream.profdata`")
    p()

def section_captures(root):
    caps = sorted(glob.glob(os.path.join(root, "steps", "*", "captures", "*.png")))
    if caps:
        p("## Presented-frame captures (reference pictures, not bit-identical between runs)")
        for c in caps:
            p(f"- {os.path.relpath(c, root)} {os.path.getsize(c) / 1e6:.1f} MB")
        p()

def pgo_merge(session, out, tool):
    files = sorted(glob.glob(os.path.join(session, "**", "*.profraw"), recursive=True))
    if not files:
        sys.exit("no .profraw files under " + session)
    tool = tool or shutil.which("llvm-profdata-19") or shutil.which("llvm-profdata")
    v = subprocess.run([tool, "--version"], capture_output=True, text=True).stdout
    print(f"{tool}: {v.strip().splitlines()[-1] if v.strip() else '?'}")
    if "version 19" not in v:
        print("warning: the instrumented binary was built with clang 19; another llvm-profdata may reject its raw format")
    subprocess.run([tool, "merge", "-o", out] + files, check=True)
    print(f"merged {len(files)} files -> {out}")
    subprocess.run([tool, "show", "--topn=25", out])
    print("\nbuild with it (same flags as the release build, from the source tree):")
    print(f"  cmake -S . -B build-ci/build-pgo -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH=$PWD/build-ci/prefix "
          f"-DVulkan_INCLUDE_DIR=$PWD/build-ci/prefix/include -DCMAKE_EXE_LINKER_FLAGS='-static-libstdc++ -static-libgcc -fuse-ld=lld' "
          f"-DCMAKE_MODULE_LINKER_FLAGS='-static-libgcc -fuse-ld=lld' -DHELIAS_RELEASE=ON -DHELIAS_PGO_USE={os.path.abspath(out)}")
    print("  ninja -C build-ci/build-pgo heliasdream && objcopy --strip-debug build-ci/build-pgo/heliasdream heliasdream.bin")

def extract(tgz, out):
    os.makedirs(out, exist_ok=True)
    with tarfile.open(tgz) as t:
        try:
            t.extractall(out, filter="data")
        except TypeError:
            t.extractall(out)
    subs = [d for d in glob.glob(os.path.join(out, "*")) if os.path.isdir(d)]
    return subs[0] if len(subs) == 1 else out

def main():
    if len(sys.argv) > 1 and sys.argv[1] == "pgo-merge":
        ap = argparse.ArgumentParser(prog="bbprofile_analyze.py pgo-merge")
        ap.add_argument("cmd"); ap.add_argument("session"); ap.add_argument("out"); ap.add_argument("--llvm-profdata")
        a = ap.parse_args()
        sess = a.session
        if sess.endswith(".tar.gz"):
            sess = extract(sess, sess[:-7])
        pgo_merge(sess, a.out, a.llvm_profdata)
        return
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("session", help="the tarball or an extracted session folder")
    ap.add_argument("--out", help="where to extract a tarball (default: beside it)")
    ap.add_argument("--symbols", help="the unstripped heliasdream-prof (symbols/heliasdream-prof.debug) for bb-cp0's inline view")
    a = ap.parse_args()
    root = a.session
    if os.path.isfile(root):
        root = extract(root, a.out or re.sub(r"\.tar\.gz$", "", root))
    if not os.path.isdir(os.path.join(root, "steps")):
        sys.exit(f"{root}: not a bbprofile session folder (no steps/)")
    p(f"# bbprofile session {os.path.basename(root.rstrip('/'))}")
    sess = os.path.join(root, "session.txt")
    if os.path.exists(sess):
        p("```"); p("\n".join(l for l in open(sess).read().splitlines() if not re.match(r"^[0-9a-f]{64} ", l))); p("```")
    p()
    section_system(root)
    section_ab(root)
    section_m4(root)
    section_cpu(root, a.symbols)
    section_sysmon(root)
    section_pgo(root)
    section_captures(root)
    rep = "\n".join(out_lines) + "\n"
    with open(os.path.join(root, "REPORT.md"), "w") as f:
        f.write(rep)
    sys.stdout.write(rep)
    print(f"\n(report written to {os.path.join(root, 'REPORT.md')})")

if __name__ == "__main__":
    main()
