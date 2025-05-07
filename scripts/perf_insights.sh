#!/bin/bash

cd ..
make GEN=ninja
cd build/release/test


# === Configuration ===
CMD="./unittest --test-dir ../../../ test/path_length_profiler_bidirectional.test"
OUTDIR="perf_logs"
TIMESTAMP=$(date +'%Y%m%d_%H%M%S')
LOGFILE="${OUTDIR}/perf_log_${TIMESTAMP}.txt"
mkdir -p "$OUTDIR"

# === Cache and Instruction Metrics ===
echo "▶ Running perf stat: Cache & instruction metrics..."
perf stat \
  -e cycles \
  -e instructions \
  -e cache-references \
  -e cache-misses \
  -e L1-dcache-loads \
  -e L1-dcache-load-misses \
  -e LLC-loads \
  -e LLC-load-misses \
  -e dTLB-loads \
  -e dTLB-load-misses \
  $CMD 2>&1 | tee -a "$LOGFILE"

echo "▶ Running command: $CMD"

# === Top-down Metrics ===
echo -e "\n▶ Running perf stat: Top-down pipeline metrics (if supported)..."
perf stat \
  -e branches \
  -e branch-misses \
  -e machine_clears.memory_ordering \
  -e uops_issued.any \
  -e uops_retired.retire_slots \
  $CMD 2>&1 | tee -a "$LOGFILE" 

# === Memory Load Events (Intel only) ===
echo -e "\n▶ Running perf stat: Memory load breakdown (if supported)..."
perf stat \
  -e mem_load_retired.l1_hit \
  -e mem_load_retired.l2_hit \
  -e mem_load_retired.l3_hit \
  -e mem_load_retired.l3_miss \
  -e mem_load_l3_miss_retired.local_dram \
  -e mem_load_l3_miss_retired.remote_dram \
  $CMD 2>&1 | tee -a "$LOGFILE" 

# === Memory Latency Hotspots ===
#echo -e "\n▶ (Optional) Memory latency profiling with perf mem..."
#if perf mem record $CMD; then
#  echo -e "\n▶ Running perf mem report..."
#  perf mem report | tee -a "$LOGFILE"
#else
#  echo "⚠️ perf mem not supported or needs sudo access." | tee -a "$LOGFILE"
#fi

echo -e "\n✅ Results saved to: $LOGFILE"
