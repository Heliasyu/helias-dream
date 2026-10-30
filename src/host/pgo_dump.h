#pragma once

// Profiling kit: the PGO-instrumented build (cmake -DHELIAS_PGO_GENERATE=ON,
// which defines HELIAS_PGO_GEN and compiles pgo_dump.cpp). heliasdream ends with
// _exit on most paths (the window closed, SIGTERM, an exit flip), where the
// profile runtime's own atexit write never runs, so a helper thread (bb-pgo)
// writes the counters every HELIAS_PGO_DUMP_S seconds (30) and once more on
// the way out, to $HELIAS_PGO_DIR/heliasdream-<pid>.profraw (default ./pgo beside
// the game, i.e. the folder heliasdream runs in), through a .tmp and a rename so
// a run killed mid-write keeps the last whole file. In any other build these
// are empty inline functions.

#if defined(HELIAS_PGO_GEN)
void pgo_dump_start();
// Writes the counters now (the helper thread does it; waits up to 5 s).
// Async-signal-safe: an atomic flag and sleeps (on_term calls it).
void pgo_dump_final();
#else
inline void pgo_dump_start() {}
inline void pgo_dump_final() {}
#endif
