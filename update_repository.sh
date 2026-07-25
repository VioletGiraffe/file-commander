#!/bin/bash
set -eu

# Update the main repo
remote="${1:-origin}"

git remote set-head "$remote" -a >/dev/null

default_branch="$(git symbolic-ref --short "refs/remotes/$remote/HEAD")"
default_branch="${default_branch#"$remote/"}"
current_branch="$(git symbolic-ref --short HEAD)"

if [ "$current_branch" != "$default_branch" ]; then
	>&2 echo "On branch $current_branch, expected $default_branch; aborting."
	exit 1
fi

git pull --ff-only "$remote" "$default_branch"

# Init the subrepos
git submodule update --init --recursive
git submodule foreach --recursive 'git remote set-head origin --auto >/dev/null 2>&1; b=$(git symbolic-ref --short refs/remotes/origin/HEAD 2>/dev/null); case $b in origin/*) git checkout ${b#origin/} ;; *) echo no default branch in $displaypath ;; esac'

# Update the subrepos
git submodule foreach --recursive "git pull"