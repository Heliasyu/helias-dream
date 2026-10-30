#!/usr/bin/env bash
# heliasdream-vk: the Vulkan tests and the stream selftest under the Khronos
# validation layer (synchronization validation on), on whatever device the
# loader gives (lavapipe on a box without a GPU). Needs the layer installed
# (Debian/Ubuntu: vulkan-validationlayers). Prints each run's message IDs.
#   tools/vvl_check.sh [build dir]    (default build-ci/build)
set -u
build=${1:-build-ci/build}
logs=${VVL_LOGS:-/tmp/heliasdream-vvl}
mkdir -p "$logs"
export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
export VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true
export VK_KHRONOS_VALIDATION_REPORT_FLAGS=error,warn,perf
status=0
run() {
    local name=$1; shift
    "$@" >"$logs/$name.log" 2>&1
    local rc=$?
    local ids
    ids=$(grep -oE 'VUID-[A-Za-z0-9_-]+|SYNC-HAZARD-[A-Z_-]+|WARNING-[A-Za-z0-9_-]+|BestPractices-[A-Za-z0-9_-]+' "$logs/$name.log" | sort | uniq -c | tr -s ' ' | tr '\n' ',')
    printf '%-22s exit %-3s %s\n' "$name" "$rc" "${ids:-clean}"
    [ "$rc" -eq 0 ] || [ "$rc" -eq 77 ] || status=1
}
for t in fsr3_upscaler_test fsr411_fallback_test motion_shader_test gcn_sampling_test; do
    [ -x "$build/$t" ] && run "$t" "$build/$t"
done
[ -x "$build/heliasdream" ] && run stream_selftest "$build/heliasdream" --stream-selftest
echo "logs: $logs"
exit $status
