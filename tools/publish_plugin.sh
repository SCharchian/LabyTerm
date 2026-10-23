#!/usr/bin/env bash
# Publish the Omarchy plugin repo, which is a mirror of packaging/omarchy-plugin: split, check, push. Never edit the mirror by hand.
#   tools/publish_plugin.sh          dry run: checks everything and shows what would be pushed
#   tools/publish_plugin.sh --push   push it (fast-forward only), then validate a fresh clone of the result
# LABYTERM_PLUGIN_REMOTE overrides the target (default below); useful to rehearse against a local bare repository.
# Remember first (docs: CLAUDE.md "Plugin updates"): after tagging a release, set PIN in labyterm-launch to the tag's commit.
set -euo pipefail
cd "$(dirname "$0")/.."
REMOTE=${LABYTERM_PLUGIN_REMOTE:-https://github.com/SCharchian/omarchy-labyterm.git}
push=0; [[ ${1:-} == --push ]] && push=1
die() { echo "STOP: $*" >&2; exit 1; }

[[ -z $(git status --short) ]] || die "the working tree is not clean; commit first"
tools/check_version.sh >/dev/null || { tools/check_version.sh >&2 || true; die "versions or the launcher pin do not match (see above)"; }
omarchy plugin validate packaging/omarchy-plugin || die "the plugin does not validate"

split=$(git subtree split --prefix=packaging/omarchy-plugin 2>/dev/null | tail -n 1)
[[ $split =~ ^[0-9a-f]{40}$ ]] || die "git subtree split did not give a commit"
remote=$(git ls-remote "$REMOTE" refs/heads/main | cut -f1)

echo "target:  $REMOTE"
echo "remote:  ${remote:-<no main yet>}"
echo "to push: $split"
if [[ -z $remote ]]; then
  echo "plan:    first push (creates main)"
elif [[ $remote == "$split" ]]; then
  echo "plan:    nothing to do, the plugin repository is already up to date"; exit 0
elif git cat-file -e "${remote}^{commit}" 2>/dev/null && git merge-base --is-ancestor "$remote" "$split"; then
  echo "plan:    fast-forward, new commits:"; git log --format='           %h %s' "$remote..$split"
else
  die "the remote main is not an ancestor of this split (not a fast-forward); do not force-push, find out why"
fi

# The plugin builds a commit of the game repository, so that repository must have been pushed too.
if upstream=$(git rev-parse -q --verify '@{upstream}' 2>/dev/null) && [[ $(git rev-parse HEAD) != "$upstream" ]]; then
  echo "WARNING: this branch is not pushed to its upstream yet; push the game repository first."
fi

((push)) || { echo; echo "dry run only: re-run with --push to publish."; exit 0; }

git push "$REMOTE" "$split:refs/heads/main"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
git clone -q --branch main "$REMOTE" "$tmp/check"
[[ $(git -C "$tmp/check" rev-parse HEAD) == "$split" ]] || die "the clone's HEAD is not the pushed commit"
omarchy plugin validate "$tmp/check" || die "the published plugin does not validate"
echo "published and verified: $split"
