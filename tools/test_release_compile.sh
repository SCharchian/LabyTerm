#!/usr/bin/env bash
# Syntax-checks every source that mentions LABYTERM_DEBUG_KEYS with the define REMOVED, so a release build
# (-DLABYTERM_DEBUG_KEYS=OFF) cannot break unnoticed while the default build keeps the debug keys on.
#   usage: test_release_compile.sh <source dir> <build dir with compile_commands.json>
set -euo pipefail
src=$1; build=$2
python3 -I - "$src" "$build" <<'PY'
import json, re, subprocess, sys, os, concurrent.futures as cf
src, build = sys.argv[1], sys.argv[2]
cmds = json.load(open(os.path.join(build, "compile_commands.json")))
def mentions(path):
    try: return "LABYTERM_DEBUG_KEYS" in open(path, errors="replace").read()
    except OSError: return False
# a .cpp is checked when it, or a project header, mentions the define (headers change what every includer sees)
hdrs = any(mentions(os.path.join(d, f)) for d, _, fs in os.walk(os.path.join(src, "src")) for f in fs if f.endswith(".hpp"))
todo = [c for c in cmds if c["file"].startswith(os.path.join(src, "src")) and (hdrs or mentions(c["file"]))]
def run(c):
    cmd = re.sub(r"-DLABYTERM_DEBUG_KEYS(=\S*)?", "", c["command"])
    cmd = re.sub(r" -o \S+", " -o /dev/null", cmd) + " -fsyntax-only"
    r = subprocess.run(cmd, shell=True, cwd=c["directory"], capture_output=True, text=True)
    return c["file"], r.returncode, r.stderr
bad = 0
with cf.ThreadPoolExecutor(os.cpu_count()) as ex:
    for f, rc, err in ex.map(run, todo):
        if rc: bad += 1; print("FAIL (no LABYTERM_DEBUG_KEYS):", f); print(err[:1500])
print(f"checked {len(todo)} files without LABYTERM_DEBUG_KEYS, {bad} failed")
sys.exit(1 if bad else 0)
PY
