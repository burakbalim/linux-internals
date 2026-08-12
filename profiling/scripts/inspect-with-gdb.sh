#!/bin/bash
# Attaches gdb to a running evserver and asks where the process actually is.
# The answer differs per backend in exactly the way the I/O model predicts.
#
#   docker run --rm --privileged -v "$PWD":/work -v "$PWD/../profiling":/prof \
#     -w /work evserver-dev bash /prof/scripts/inspect-with-gdb.sh epoll
set -u

BACKEND=${1:-epoll}
PORT=${2:-9700}

cleanup() {
    kill "$LG" 2>/dev/null
    kill "$SRV" 2>/dev/null
    wait "$SRV" 2>/dev/null
}
trap cleanup EXIT

./build/evserver --backend="$BACKEND" --port="$PORT" >/tmp/srv.log 2>&1 &
SRV=$!
sleep 0.5

# Hold connections open so there is state worth looking at.
./build/loadgen --port="$PORT" --threads=4 --idle=20 --seconds=8 >/dev/null 2>&1 &
LG=$!
sleep 2

echo "=== backend=$BACKEND ==="
echo "--- how many threads, and what are they doing? ---"
gdb -p "$SRV" -batch -ex 'info threads' 2>/dev/null |
    grep -E '^\*?\s+[0-9]+\s+' | head -8
echo "  (thread count: $(gdb -p "$SRV" -batch -ex 'info threads' 2>/dev/null |
    grep -cE '^\*?\s+[0-9]+\s+'))"

echo "--- stack of the thread gdb stopped on ---"
gdb -p "$SRV" -batch -ex 'bt 6' 2>/dev/null |
    grep -E '^#' | head -6
