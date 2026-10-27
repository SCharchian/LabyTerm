#!/usr/bin/env python3
"""Starts labyterm -t in a pseudo-terminal that answers its startup queries the way different terminals do
(or not at all), sends keys, and checks what the game says and draws: no questions on a known terminal, the glyph check on an unknown
one (and that the answer is remembered), the true-color and Linux-console notices, and the environment overrides.
  python3 -I tools/test_term_startup.py [path/to/labyterm]"""
import os, pty, sys, time, select, struct, fcntl, termios, re, tempfile, shutil
EXE = sys.argv[1] if len(sys.argv) > 1 else "build/labyterm"
def run(name, env_extra, reply, keys=(), settle=1.2, state=None, rows=30, cols=100):
    state = state or tempfile.mkdtemp()
    pid, fd = pty.fork()
    if pid == 0:
        env = {"PATH": os.environ["PATH"], "HOME": os.environ["HOME"], "TERM": "xterm-256color", "LABYTERM_STATE_DIR": state, "LABYTERM_SKIP_TITLE": ""}
        env.pop("LABYTERM_SKIP_TITLE")
        env.update(env_extra)
        os.execve(EXE, [EXE, "-t", "7"], env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    buf = b""; answered = False; t0 = time.time(); keyi = 0; last_key = 0
    def pump(t):
        nonlocal buf, answered
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.03)
            if r:
                try: d = os.read(fd, 1 << 20)
                except OSError: return
                buf += d
            if not answered and b"\x1b[c" in buf and reply is not None:
                os.write(fd, reply); answered = True
    pump(1.0)
    for k in keys:
        os.write(fd, k); pump(0.6)
    pump(settle)
    text = re.sub(rb"\x1b\[[0-9;?<>]*[A-Za-z]|\x1bP[^\x1b]*\x1b\\\\|\x1b\\\\", b"", buf).decode("utf8", "replace")
    os.write(fd, b"\x03"); pump(0.6)
    try: os.waitpid(pid, os.WNOHANG)
    except ChildProcessError: pass
    return buf, text, state
def octants(b): return sum(1 for ch in b.decode("utf8", "replace") if 0x1CD00 <= ord(ch) <= 0x1CDE5)
def quads(b): return sum(1 for ch in b.decode("utf8", "replace") if 0x2596 <= ord(ch) <= 0x259F)
GHOSTTY = b"\x1bP1$r0;48:2::1:2:3m\x1b\\\x1bP>|ghostty 1.3.1\x1b\\\x1b[?62;22c"
UNKNOWN_TC = b"\x1b[?62c"
ok = True
def check(c, m):
    global ok
    print(("PASS " if c else "FAIL ") + m); ok = ok and c
# 1. a ghostty-like terminal: no screens, octants
b, t, st = run("ghostty", {}, GHOSTTY)
check("Terminal check" not in t and "True color" not in t and octants(b) > 100, "ghostty-like: no questions, octant glyphs")
# 2. unknown terminal with COLORTERM: asks about glyphs; N -> quadrants, remembered
b, t, st = run("unknown-n", {"COLORTERM": "truecolor"}, UNKNOWN_TC, keys=[b"n"])
check("Terminal check" in t and "Octants (sharper)" in t, "unknown terminal: the glyph check is shown")
tail = b[b.rfind(b"Quadrants"):]
check(quads(tail) > 100 and octants(tail[tail.find(b"\x1b[2J"):]) < 5, "answer N: the game then draws quadrants")
check(os.path.exists(st + "/terminal.json") and '"quadrant"' in open(st + "/terminal.json").read(), "the answer is saved")
b2, t2, _ = run("unknown-again", {"COLORTERM": "truecolor"}, UNKNOWN_TC, state=st)
check("Terminal check" not in t2 and quads(b2) > 100, "next start: not asked again, quadrants")
b3, t3, _ = run("unknown-y", {"COLORTERM": "truecolor"}, UNKNOWN_TC, keys=[b"y"])
check("Terminal check" in t3 and octants(b3[b3.rfind(b"Quadrants"):]) > 100, "answer Y: octants")
# 3. no truecolor anywhere: the colour screen, Enter continues
b, t, st = run("no-tc", {"LABYTERM_GLYPHS": "octant"}, UNKNOWN_TC, keys=[b"\r"])
check("True color not detected" in t and octants(b) > 100, "no truecolor: the warning is shown, Enter carries on")
# 4. overrides skip everything
b, t, st = run("override", {"LABYTERM_TRUECOLOR": "1", "LABYTERM_GLYPHS": "quadrant"}, None)
check("True color" not in t and "Terminal check" not in t and octants(b) == 0 and quads(b) > 100, "overrides: no questions, quadrants forced")
# 5. Esc on a question quits
b, t, st = run("quit", {"COLORTERM": "truecolor"}, UNKNOWN_TC, keys=[b"\x1b"], settle=0.3)
check(b"\x1b[?1049l" in b, "Esc on the check leaves cleanly")
# 6. the Linux text console
b, t, st = run("linux", {"TERM": "linux", "COLORTERM": "truecolor", "LABYTERM_GLYPHS": "octant"}, UNKNOWN_TC, keys=[b"\r"])
check("Linux text console" in t and octants(b) > 100, "TERM=linux: the notice is shown, Enter carries on")
# 7. inside tmux: it swallows the colour query, so the game asks tmux what it passes on (skipped without tmux)
import subprocess
if shutil.which("tmux"):
    def in_tmux(outer_env):
        st = tempfile.mkdtemp()
        cmd = f"env -u COLORTERM LABYTERM_GLYPHS=octant LABYTERM_STATE_DIR={st} {os.path.abspath(EXE)} -t 7"
        pid, fd = pty.fork()
        if pid == 0:
            env = {"PATH": os.environ["PATH"], "HOME": os.environ["HOME"], "SHELL": "/bin/sh"}
            env.update(outer_env)
            os.execve(shutil.which("tmux"), ["tmux", "-L", "labytest", "-f", "/dev/null", "new-session", cmd], env)
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 32, 110, 0, 0))
        end = time.time() + 4.0
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.1)
            if r:
                try: os.read(fd, 1 << 20)
                except OSError: break
        screen = subprocess.run(["tmux", "-L", "labytest", "capture-pane", "-p", "-t", "0"], capture_output=True, text=True).stdout
        subprocess.run(["tmux", "-L", "labytest", "kill-server"], capture_output=True)
        return screen
    screen = in_tmux({"TERM": "xterm-256color", "COLORTERM": "truecolor"})
    check("True color not detected" not in screen, "tmux that passes RGB on: no warning")
    screen = in_tmux({"TERM": "xterm-ghostty"})
    check("True color not detected" in screen and "terminal-features" in screen, "tmux without RGB: the warning, with the tmux tip")
else:
    print("SKIP tmux (not installed)")
print("term_startup: ok" if ok else "term_startup: FAILED")
sys.exit(0 if ok else 1)
