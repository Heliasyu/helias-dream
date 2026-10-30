#!/bin/bash
# heliasdream-vk CPU profile of a running game (proposed `bbtest cpuprofile`).
# Run it from a second terminal while heliasdream is in gameplay at the spot to
# measure (Old Yharnam outside, then the Hunter's Dream). It only reads:
# perf samples of the heliasdream process, its /proc maps and thread names.
#
#   cpuprofile.sh [OUTDIR]          ~3 minutes; prints where the results are
#   CPUPROF_SCHED=0 cpuprofile.sh   skip the system-wide scheduler/GPU-scheduler trace (needs sudo)
#
# Needs: perf (sudo pacman -S --needed perf), and for this session
#   sudo sysctl kernel.perf_event_paranoid=1     (2 = no kernel samples of our own process)
# Put it back afterwards: sudo sysctl kernel.perf_event_paranoid=2
set -eu
PID=$(pgrep -x heliasdream.bin | head -1 || true)
[ -n "$PID" ] || { echo "heliasdream.bin is not running"; exit 1; }
OUT=${1:-$HOME/.local/share/bbtest/cpuprofile-$(date +%Y%m%d-%H%M%S)}
mkdir -p "$OUT"
cd "$OUT"
echo "pid $PID -> $OUT"
{
  echo "date: $(date)"; uname -a
  echo "paranoid: $(cat /proc/sys/kernel/perf_event_paranoid) kptr_restrict: $(cat /proc/sys/kernel/kptr_restrict)"
  echo "governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor) epp: $(cat /sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference 2>/dev/null)"
  echo "ppd: $(powerprofilesctl get 2>/dev/null || echo none)"
  echo "thp: $(cat /sys/kernel/mm/transparent_hugepage/enabled) defrag: $(cat /sys/kernel/mm/transparent_hugepage/defrag)"
  echo "sched_ext: $(cat /sys/kernel/sched_ext/state 2>/dev/null)"
  echo "affinity: $(taskset -pc "$PID")"
  for c in /sys/devices/system/cpu/cpu[0-9]*; do
    echo "$(basename "$c") l3=$(cat "$c/cache/index3/shared_cpu_list") smt=$(cat "$c/topology/thread_siblings_list") prefcore=$(cat "$c/cpufreq/amd_pstate_prefcore_ranking" 2>/dev/null || cat "$c/acpi_cppc/highest_perf" 2>/dev/null)"
  done
} > env.txt 2>&1
cp /proc/"$PID"/maps maps.txt
threads() { for t in /proc/"$PID"/task/*; do echo "$(basename "$t") $(cat "$t/comm") cpu=$(awk '{print $39}' "$t/stat") $(awk '{print "utime="$14" stime="$15}' "$t/stat")"; done; }
threads > threads-start.txt

echo "1/4 perf stat per thread, 20 s"
perf stat --per-thread -p "$PID" \
  -e task-clock,context-switches,cpu-migrations,cycles,instructions,branch-misses,cache-misses,stalled-cycles-frontend \
  -- sleep 20 2> stat-per-thread.txt || true

echo "2/4 flat profile, 60 s at 1999 Hz"
perf record -q -F 1999 -p "$PID" -o cpu-flat.data -- sleep 60 2> record-flat.log || true

echo "3/4 call graphs (DWARF unwinding: heliasdream has no frame pointers), 15 s at 499 Hz"
perf record -q -F 499 --call-graph dwarf,12288 -p "$PID" -o cpu-dwarf.data -- sleep 15 2> record-dwarf.log || true

if [ "${CPUPROF_SCHED:-1}" = 1 ]; then
  echo "4/4 scheduler + GPU-scheduler tracepoints, system-wide 8 s (sudo)"
  sudo perf record -q -a -o sched.data \
    -e sched:sched_switch -e sched:sched_wakeup -e sched:sched_migrate_task \
    -e 'gpu_scheduler:*' -e 'amdgpu:amdgpu_cs_ioctl' -e 'amdgpu:amdgpu_sched_run_job' \
    -- sleep 8 2> record-sched.log || true
  sudo chown "$(id -u):$(id -g)" sched.data 2>/dev/null || true
fi
threads > threads-end.txt

echo "reports"
perf report -i cpu-flat.data --stdio --no-children --sort comm,dso,sym --percent-limit 0.2 > flat-report.txt 2>/dev/null || true
perf script -i cpu-flat.data -F comm,tid,ip,sym,dso > flat.script 2>/dev/null || true
perf report -i cpu-dwarf.data --stdio --children --sort comm,sym --percent-limit 0.5 > dwarf-report.txt 2>/dev/null || true
perf report -i cpu-dwarf.data --stdio --no-children --comm bb-cp0 -G --percent-limit 0.5 > dwarf-bb-cp0.txt 2>/dev/null || true
if [ -f sched.data ]; then
  perf sched timehist -i sched.data -s > sched-timehist-summary.txt 2>/dev/null || true
  perf sched latency -i sched.data --sort max > sched-latency.txt 2>/dev/null || true
  perf script -i sched.data -F comm,tid,cpu,time,event,trace 2>/dev/null | zstd -q -o sched.script.zst || true
fi
[ -f "$(dirname "$0")/perf_cpu_summary.py" ] && python3 "$(dirname "$0")/perf_cpu_summary.py" flat.script maps.txt > summary.txt || true
rm -f flat.script
tar --zstd --exclude="*.data" -cf "../$(basename "$OUT").tar.zst" -C .. "$(basename "$OUT")" 2>/dev/null || true
echo "done: $OUT (the .data files stay there; send ../$(basename "$OUT").tar.zst)"
