#!/usr/bin/env bash
# Release go/no-go in one command: the content-freeze checks that must pass before a tag or a plugin push.
#   tools/release_check.sh          full run (a fresh Release build in a temp directory: a few minutes)
#   tools/release_check.sh --quick  skip the fresh build and the tests
# Prints PASS or FAIL per check and exits 1 if any FAIL. Changes nothing in the repository.
set -uo pipefail
cd "$(dirname "$0")/.."
for d in "$HOME"/.local/share/mise/installs/cmake/latest/*/bin "$HOME"/.local/share/mise/installs/ninja/latest; do
  [[ -d "$d" ]] && PATH="$d:$PATH"
done
export PATH
quick=0; [[ ${1:-} == --quick ]] && quick=1
fail=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; [[ -n ${2:-} ]] && printf '%s\n' "$2" | sed 's/^/        /' | head -n 12; fail=1; }
check() { local name=$1; shift; local out; if out=$("$@" 2>&1); then ok "$name"; else bad "$name" "$out"; fi; }

# 1. nothing uncommitted (tracked or untracked)
st=$(git status --short)
[[ -z $st ]] && ok "git status is clean" || bad "git status is not clean" "$st"

# 2. every commit is by the public noreply address
authors=$(git log --format='%an <%ae>' | sort -u)
[[ $authors == "Sam Charchian <147100012+SCharchian@users.noreply.github.com>" ]] && ok "commit author is the noreply address" \
  || bad "unexpected commit author(s)" "$authors"

# 3. no private addresses, paths or leftover markers in tracked files
leaks=$(git grep -I -n -i -E "hotmail|TODO: SAM|/home/sam" -- . ':!private' ':!tools/release_check.sh' || true)
[[ -z $leaks ]] && ok "no private address, path or TODO: SAM marker" || bad "private-looking text found" "$leaks"

# 4. the versions agree, and the plugin launcher's pin is the tag's commit
check "versions agree and the launcher pin matches the tag" tools/check_version.sh

# 5. the Omarchy plugin passes its own validator
if command -v omarchy >/dev/null 2>&1; then check "omarchy plugin validate" omarchy plugin validate packaging/omarchy-plugin
else bad "omarchy not found: cannot validate the plugin"; fi

# 6. relative links and images in the READMEs and docs resolve, and in-page anchors exist
links=$(python3 -I - <<'PY'
import os, re, sys
files = ["README.md", "KNOWN-ISSUES.md", "LICENSING.md", "CHANGELOG.md", "packaging/omarchy-plugin/README.md"]
files += [os.path.join("docs", f) for f in sorted(os.listdir("docs")) if f.endswith(".md")]
def slug(h):
    h = re.sub(r"[`*_]", "", h.strip().lower())
    return re.sub(r"\s", "-", re.sub(r"[^\w\s-]", "", h))
bad = []
for f in files:
    if not os.path.exists(f): continue
    text = open(f, encoding="utf-8").read()
    anchors = {slug(m.group(1)) for m in re.finditer(r"^#{1,6}\s+(.+?)\s*$", text, re.M)}
    for m in re.finditer(r"!?\[[^\]]*\]\(([^)\s]+)", text):
        t = m.group(1)
        if re.match(r"[a-z][a-z0-9+.-]*:", t, re.I): continue          # external URL: not checked here
        path, _, frag = t.partition("#")
        if not path:
            if frag and frag not in anchors: bad.append(f"{f}: missing anchor #{frag}")
            continue
        if not os.path.exists(os.path.normpath(os.path.join(os.path.dirname(f), path))): bad.append(f"{f}: missing {t}")
print("\n".join(bad)); sys.exit(1 if bad else 0)
PY
) && ok "relative links, images and anchors resolve" || bad "broken relative link or anchor" "$links"

if ((quick)); then
  echo "SKIP  fresh Release build, tests (--quick)"
else
  # 7. a fresh Release build has no warnings, and every test passes
  tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
  if cmake -S . -B "$tmp/build" -G Ninja -DCMAKE_BUILD_TYPE=Release >"$tmp/cfg.log" 2>&1 \
     && cmake --build "$tmp/build" >"$tmp/build.log" 2>&1; then
    w=$(grep -ci 'warning' "$tmp/build.log" || true)
    ((w == 0)) && ok "fresh Release build: 0 warnings" || bad "fresh Release build has $w warning line(s)" "$(grep -i warning "$tmp/build.log")"
    if t=$(ctest --test-dir "$tmp/build" 2>&1); then ok "ctest: $(grep -o '100% tests passed.*' <<<"$t" | head -1)"; else bad "ctest failed" "$(tail -n 12 <<<"$t")"; fi
  else
    bad "fresh Release build failed" "$(tail -n 12 "$tmp/build.log" 2>/dev/null || tail -n 12 "$tmp/cfg.log")"
  fi
fi

echo
if ((fail)); then echo "NO-GO: fix the FAIL lines above."; exit 1; else echo "GO: all checks passed."; fi
