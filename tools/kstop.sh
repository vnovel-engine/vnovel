#!/bin/bash
# Always stop retroarch and confirm it is gone before another run starts.
#
# Prints "stopped" on success and exits 0; prints "STILL RUNNING: <n>" and
# exits 1 otherwise, so a caller can never mistake a stale instance for the
# run under test.
pkill -9 -x retroarch 2>/dev/null
sleep 2
n=$(pgrep -xc retroarch 2>/dev/null)
n=${n:-0}
if [ "$n" -eq 0 ] 2>/dev/null; then echo "stopped"; else echo "STILL RUNNING: $n"; exit 1; fi
