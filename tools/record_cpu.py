#!/usr/bin/env python3
"""Records a running LabyTerm's CPU use while you play, then prints a summary.

  tools/record_cpu.py <label> [--seconds N] [--pid PID] [--interval S]

Start the game (any build), run this in another terminal, play normally, and press Ctrl-C (or wait for --seconds) to stop.
It samples the process once per interval (default 1 s): total CPU as a percentage of ONE core, plus each thread's share.
Output: build/cpu/<label>.csv (one row per sample) and a summary (mean, median, p95, max, busiest threads, RSS).

To compare the maze styles, record the same route twice under two labels, switching Options > Dungeon style in between:
  tools/record_cpu.py text      # play ~2 minutes: walk, turn, open a chest, a fight
  tools/record_cpu.py mosaic    # the same again
Standing still counts too (the torch breathes and redraws), so include a pause in each.

IDLE matters most (the game is meant to run all day and stay near 0%). Record it separately, once per style, with the game
focused and then unfocused, hands off the keyboard (the torch redraws a few times a second only while focused):
  tools/record_cpu.py text-idle --seconds 60       # in a dungeon, standing still, window focused
  tools/record_cpu.py text-idle-unfocused --seconds 60   # click another window first
  tools/record_cpu.py mosaic-idle --seconds 60     # same two again after switching Dungeon style
Goal: focused idle <= 2%, unfocused about 0%.
"""
import argparse, os, signal, statistics, subprocess, sys, time

def find_pid():
    out = subprocess.run(["pgrep", "-n", "-x", "labyterm"], capture_output=True, text=True).stdout.split()
    return int(out[0]) if out else None

def thread_times(pid):
    """{tid: (name, ticks)} for every thread of pid."""
    res = {}
    for tid in os.listdir(f"/proc/{pid}/task"):
        try:
            with open(f"/proc/{pid}/task/{tid}/stat") as f:
                s = f.read()
        except OSError:
            continue
        name = s[s.index("(") + 1:s.rindex(")")]
        rest = s[s.rindex(")") + 2:].split()
        res[int(tid)] = (name, int(rest[11]) + int(rest[12]))  # utime + stime
    return res

def rss_mb(pid):
    try:
        for line in open(f"/proc/{pid}/status"):
            if line.startswith("VmRSS"):
                return int(line.split()[1]) / 1024
    except OSError:
        pass
    return 0.0

def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(len(xs) * p))]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("label")
    ap.add_argument("--seconds", type=float, default=0, help="stop after this long (default: until Ctrl-C)")
    ap.add_argument("--pid", type=int)
    ap.add_argument("--interval", type=float, default=1.0)
    a = ap.parse_args()
    pid = a.pid or find_pid()
    if not pid:
        sys.exit("no running labyterm found (start the game first, or pass --pid)")
    hz = os.sysconf("SC_CLK_TCK")
    os.makedirs("build/cpu", exist_ok=True)
    path = f"build/cpu/{a.label}.csv"
    stop = []
    signal.signal(signal.SIGINT, lambda *_: stop.append(1))
    print(f"recording pid {pid} every {a.interval:g}s into {path}; play now, Ctrl-C to stop")
    prev, t_prev = thread_times(pid), time.monotonic()
    totals, names, rows = [], {}, []
    thread_ticks = {}  # name -> summed ticks over the recording
    start = t_prev
    with open(path, "w") as out:
        out.write("t_s,cpu_pct,rss_mb\n")
        while not stop:
            time.sleep(a.interval)
            try:
                cur = thread_times(pid)
            except OSError:
                print("process ended"); break
            t = time.monotonic()
            dt = t - t_prev
            used = 0
            for tid, (name, ticks) in cur.items():
                d = ticks - prev.get(tid, (name, ticks))[1]
                used += d
                thread_ticks[name] = thread_ticks.get(name, 0) + d
            cpu = used / hz / dt * 100
            totals.append(cpu)
            out.write(f"{t - start:.1f},{cpu:.1f},{rss_mb(pid):.0f}\n")
            out.flush()
            prev, t_prev = cur, t
            if a.seconds and t - start >= a.seconds:
                break
    if not totals:
        sys.exit("no samples")
    dur = time.monotonic() - start
    idle = sum(1 for c in totals if c < 1.0)
    print(f"\n== {a.label}: {len(totals)} samples over {dur:.0f}s (CPU as % of one core) ==")
    print(f"mean {statistics.mean(totals):.1f}%  median {statistics.median(totals):.1f}%  p95 {pct(totals, .95):.1f}%  max {max(totals):.1f}%  idle(<1%) {100 * idle / len(totals):.0f}% of samples")
    print(f"RSS {rss_mb(pid):.0f} MB")
    print("busiest threads (share of all CPU the game used):")
    all_ticks = sum(thread_ticks.values()) or 1
    for name, tk in sorted(thread_ticks.items(), key=lambda kv: -kv[1])[:5]:
        print(f"  {name:<16} {tk / hz / dur * 100:5.1f}% of a core  ({100 * tk / all_ticks:.0f}% of the game's CPU)")

main()
