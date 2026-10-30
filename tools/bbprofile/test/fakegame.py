#!/usr/bin/env python3
"""Box-side stand-in for heliasdream.bin, for dry runs of bbprofile.sh (BBPROFILE_LAUNCH).

Named threads like the game's (heliasdream.bin, bb-cp0, GXRenderThread, bb-record,
bb-submit, bb-present) burn a little CPU so perf has something to sample;
snap.request / capture.request files are answered like heliasdream does (snapshots
copied from FAKE_SNAPS, a [flip_trace] section made up when HELIAS_FLIP_TRACE=1);
HELIAS_PGO_DIR gets FAKE_PROFRAW copies. SIGTERM ends it like the game (exit 0).
"""
import ctypes, glob, os, random, shutil, signal, sys, threading, time

libc = ctypes.CDLL(None)
def name_thread(n):
    libc.prctl(15, ctypes.c_char_p(n.encode()[:15]), 0, 0, 0)  # PR_SET_NAME

stop = threading.Event()
signal.signal(signal.SIGTERM, lambda *a: stop.set())

def burner(n, duty):
    name_thread(n)
    x = 0
    while not stop.is_set():
        t = time.perf_counter()
        while time.perf_counter() - t < duty * 0.004:
            x += 1
        time.sleep(0.004 * (1 - duty))

for n, d in (("bb-cp0", 0.6), ("GXRenderThread", 0.4), ("bb-record", 0.3), ("bb-submit", 0.1), ("bb-present", 0.05)):
    threading.Thread(target=burner, args=(n, d), daemon=True).start()
name_thread("heliasdream.bin")

snap_dir = os.environ.get("HELIAS_SNAP_DIR", "snaps")
cap_dir = os.environ.get("HELIAS_VK_CAPTURE_DIR", "") if os.environ.get("HELIAS_VK_CAPTURE") == "1" else ""
pgo_dir = os.environ.get("HELIAS_PGO_DIR", "")
src_snaps = sorted(glob.glob(os.path.join(os.environ.get("FAKE_SNAPS", "."), "snap-*.txt")))
profraw = os.environ.get("FAKE_PROFRAW", "")
trace = os.environ.get("HELIAS_FLIP_TRACE") == "1"
print("[heliasdream] fake game: pid %d snap %s capture %s pgo %s trace %s" % (os.getpid(), snap_dir, cap_dir or "-", pgo_dir or "-", trace), flush=True)
for k in sorted(os.environ):
    if k.startswith("HELIAS_"):
        print("[heliasdream] env %s=%s" % (k, os.environ[k]), flush=True)

def flip_trace_section():
    now = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
    base = now - 30_000_000_000
    rows, t, fid = [], 0.0, 1000
    threads = ["main", "GXRenderThread", "bb-cp0", "bb-record", "bb-submit", "bb-present", "bb-vblank"]
    while t < 30000:
        slow = random.random() < 1 / 120
        iv = 12.4 if slow else random.gauss(8.33, 0.15)
        walk = 11.8 if slow else random.gauss(6.9, 0.3)
        t += iv; fid += 1
        th = []
        for n in threads:
            th += [random.uniform(1, 7), (2.5 if (slow and n == "bb-cp0") else random.uniform(0, 0.2))]
        rows.append([fid, t, iv, iv, 6.9, 0.1, 0.0, 3, 0.05, walk, 0.02, 0.6, 2, walk, 1.2] + th)
        if slow:
            print("[heliasdream] fliptrace: slow frame %.2f ms (>= 11.0) at %.6f s monotonic: flip %d: ..." % (iv, (base + t * 1e6) / 1e9, fid), flush=True)
    out = ["[flip_trace]", "# fake", "# base_steady_ns=%d" % base,
           "# id,t_ms,interval_ms,submit_gap_ms,main_frame_ms,main_end_to_submit_ms,backpressure_ms,backlog,queue_ms,cp_walk_ms,to_video_ms,to_present_ms,jobs,cp_busy_ms,cp_idle_ms," +
           ",".join("%s_run_ms,%s_wait_ms" % (n, n) for n in threads)]
    out += [",".join(("%d" % v) if isinstance(v, int) else ("%.3f" % v) for v in r) for r in rows]
    return "\n".join(out) + "\n"

nsnap, last_pgo = 0, time.time()
period = max(5, int(os.environ.get("HELIAS_PGO_DUMP_S", "30")))
def dump_pgo():
    if pgo_dir and profraw:
        os.makedirs(pgo_dir, exist_ok=True)
        shutil.copy(profraw, os.path.join(pgo_dir, "heliasdream-%d.profraw" % os.getpid()))
        print("[heliasdream] pgo: profile written", flush=True)

while not stop.is_set():
    time.sleep(0.25)
    req = os.path.join(snap_dir, "snap.request")
    if os.path.exists(req):
        note = open(req).read().strip(); os.remove(req)
        os.makedirs(snap_dir, exist_ok=True)
        name = "snap-%s-%02d-m23_00_00_00.txt" % (time.strftime("%Y%m%d-%H%M%S"), nsnap)
        body = open(src_snaps[nsnap % len(src_snaps)], errors="replace").read() if src_snaps else "[present]\n"
        if trace:
            body = body.replace("\n[log]", "\n" + flip_trace_section() + "\n[log]", 1) if "\n[log]" in body else body + flip_trace_section()
        with open(os.path.join(snap_dir, name), "w") as f:
            f.write(body)
        nsnap += 1
        print("[heliasdream] snap: %s (bbtest snap: %s)" % (name, note), flush=True)
    if cap_dir and os.path.exists(os.path.join(cap_dir, "capture.request")):
        os.remove(os.path.join(cap_dir, "capture.request"))
        with open(os.path.join(cap_dir, "capture-%d.png" % int(time.time() * 1000)), "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n")
    if pgo_dir and time.time() - last_pgo >= period:
        last_pgo = time.time(); dump_pgo()
dump_pgo()
print("[heliasdream] fake game: SIGTERM, exit", flush=True)
sys.exit(0)
