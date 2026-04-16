#!/usr/bin/env bash

set -euo pipefail

INPUT_DIR="${1:-input}"
OUTPUT_DIR="${2:-output}"
THREADS=(1 6 12 18)

mkdir -p "$OUTPUT_DIR"
rm -f "$OUTPUT_DIR/summary_runs.csv" "$OUTPUT_DIR/task_runs.csv" "$OUTPUT_DIR/experiment_report.csv"

make

for t in "${THREADS[@]}"; do
  echo "Ejecutando corrida con ${t} thread(s)..."
  ./image_analyzer --input-dir "$INPUT_DIR" --output-dir "$OUTPUT_DIR" --threads "$t"
done

awk -F, '
BEGIN {
  OFS=",";
}
NR == 1 {
  next;
}
NR == 2 {
  baseline = $5 + 0.0;
  print "threads,total_time_seconds,speedup,efficiency";
}
{
  time = $5 + 0.0;
  speedup = baseline / time;
  efficiency = speedup / $1;
  printf "%s,%.6f,%.6f,%.6f\n", $1, time, speedup, efficiency;
}
' "$OUTPUT_DIR/summary_runs.csv" > "$OUTPUT_DIR/experiment_report.csv"

echo
echo "Resultados consolidados en:"
echo "  $OUTPUT_DIR/summary_runs.csv"
echo "  $OUTPUT_DIR/task_runs.csv"
echo "  $OUTPUT_DIR/experiment_report.csv"
