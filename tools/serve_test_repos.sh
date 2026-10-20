#!/usr/bin/env bash
# Serve the COMMITTED state of this repo to a test machine (a fresh Omarchy VM) over git://, while the real
# GitHub repo is still private. Serves two repos:
#   plugin.git  packaging/omarchy-plugin as its own repo (manifest.json at the root, as the marketplace wants)
#   game.git    this repo (what the plugin's first-run build clones, and what the PKGBUILD builds from)
# Commit first: uncommitted changes are not served. Stop with Ctrl-C. Needs the VM to reach this machine (port 9418).
#   usage: tools/serve_test_repos.sh [bind-address]
set -euo pipefail
cd "$(dirname "$0")/.."
root=$(mktemp -d "${TMPDIR:-/tmp}/labyterm-serve.XXXXXX")
trap 'rm -rf "${root:?}"' EXIT
git clone -q --bare . "$root/game.git"
# the plugin builds the release tag that matches its version (v<version>), so serve one
git -C "$root/game.git" tag -f "v$(sed -n 's/^project(LabyTerm VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)" HEAD >/dev/null
sha=$(git subtree split --prefix=packaging/omarchy-plugin 2>/dev/null)
git init -q --bare -b main "$root/plugin.git"
git push -q "$root/plugin.git" "$sha:refs/heads/main"
ip=${1:-$(ip -4 route get 1.1.1.1 | sed -n 's/.* src \([0-9.]*\).*/\1/p')}
cat <<MSG
Serving on git://$ip/ (Ctrl-C to stop). In the VM:

  omarchy plugin add git://$ip/plugin.git --enable
  LABYTERM_REPO=git://$ip/game.git ~/.config/omarchy/plugins/io.github.scharchian.labyterm/labyterm-launch

  # or test the AUR recipe (labyterm-git) the way an AUR user gets it:
  git clone git://$ip/game.git g && mkdir t && cp g/packaging/arch/PKGBUILD-git t/PKGBUILD && cd t
  LABYTERM_GIT_URL=git://$ip/game.git makepkg -si
MSG
git daemon --base-path="$root" --export-all --listen="$ip" --reuseaddr "$root"
