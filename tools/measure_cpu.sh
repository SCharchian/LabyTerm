#!/usr/bin/env bash
# Measures a process's CPU use while it sits idle (all-day running is a goal: an idle LabyTerm should stay near 0%).
#   tools/measure_cpu.sh <seconds> -- ./build/labyterm --walk
# Starts the command, waits 2 s for the startup burst, then samples its CPU time over <seconds>.
secs=$1; shift; shift
"$@" > /dev/null 2>&1 &
pid=$!
sleep 2
read -r _ _ _ _ _ _ _ _ _ _ _ _ _ u1 s1 _ < /proc/$pid/stat
t1=$(date +%s.%N)
sleep "$secs"
read -r _ _ _ _ _ _ _ _ _ _ _ _ _ u2 s2 _ < /proc/$pid/stat
t2=$(date +%s.%N)
rss=$(awk '/VmRSS/{print $2}' /proc/$pid/status)
hz=$(getconf CLK_TCK)
python3 - "$u1" "$s1" "$u2" "$s2" "$t1" "$t2" "$hz" "$rss" <<'PY'
import sys
u1,s1,u2,s2,t1,t2,hz,rss = map(float, sys.argv[1:])
cpu = ((u2-u1)+(s2-s1))/hz/(t2-t1)*100
print(f"CPU {cpu:.1f}% of one core over {t2-t1:.1f}s, RSS {rss/1024:.0f} MB")
PY
kill $pid 2>/dev/null; wait $pid 2>/dev/null
