#!/bin/bash
# A sampled world walk on Linux for tools/surface.py: guest frames of the main loop and the
# engine's worker threads through perf_events, from the world's first frames to the exit flip.
cd "$(dirname "$0")/.."
export DISPLAY=:78 HELIAS_NO_GAMEPAD=1 HELIAS_SKIP_INTRO=1 HELIAS_NP_TEST=probe
export HELIAS_SAMPLE="main,FD4JobWorker,EzWorkPool,GXRenderThread,GXWorker,MOMainThread,HavokWorkerThread,SpClothVertexUpdate,Core.Res,FaceGenMan,FMOD,NexusRevolution,bb-cp0"
export HELIAS_SAMPLE_PERF=1 HELIAS_SAMPLE_HZ=997 HELIAS_SAMPLE_FROM_FLIP=2200 HELIAS_SAMPLE_OUT=build/samples-surface
HELIAS_AUTOPRESS="f1000:down,f1040:cross,f1150:cross,f1300:cross,f1450:cross,f1600:cross,f1800:cross,f2000:cross,f3300:lup:6000,f3600:lright:4000" \
  HELIAS_EXIT_FLIP=5200 timeout -k 5 420 tmp/np-host-run.sh > build/surface-run.log 2>&1
echo "EXIT=$?" >> build/surface-run.log
