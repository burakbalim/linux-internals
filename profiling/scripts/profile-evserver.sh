#!/bin/bash
# Profiles the evserver backends with perf, so "where does the time go" is answered
# by sampling rather than by guessing.
#
# Run from the evserver directory, with evserver already built:
#   docker run --rm --privileged -v "$PWD":/work -w /work evserver-dev \
#     ../profiling/scripts/profile-evserver.sh epoll
set -u

BACKEND=${1:-epoll}
PORT=${2:-9500}
SECONDS_RUN=${3:-5}

cleanup() { kill "$SRV" 2>/dev/null; wait "$SRV" 2>/dev/null; }
trap cleanup EXIT

./build/evserver --backend="$BACKEND" --port="$PORT" >/tmp/srv.log 2>&1 &
SRV=$!
sleep 0.5

echo "=== perf stat: what the CPU actually did (backend=$BACKEND) ==="
# Software events only: hardware counters are usually unavailable inside a VM.
perf stat -e task-clock,context-switches,cpu-migrations,page-faults \
    -p "$SRV" -- sleep "$SECONDS_RUN" 2>/tmp/stat.log &
PERF=$!
sleep 0.2
./build/loadgen --port="$PORT" --threads=8 --seconds="$SECONDS_RUN" 2>&1 | grep -E 'throughput|p50'
wait "$PERF" 2>/dev/null
grep -E 'task-clock|context-switches|cpu-migrations|page-faults|elapsed' /tmp/stat.log

echo
echo "=== perf record: where the time is spent ==="
perf record -F 499 -e cpu-clock -g -o /tmp/perf.data -p "$SRV" -- sleep "$SECONDS_RUN" \
    >/dev/null 2>&1 &
PERF=$!
sleep 0.2
./build/loadgen --port="$PORT" --threads=8 --seconds="$SECONDS_RUN" >/dev/null 2>&1
wait "$PERF" 2>/dev/null

perf report -i /tmp/perf.data --stdio --no-children --percent-limit 1 2>/dev/null \
  | grep -vE '^#|^$' | head -15
