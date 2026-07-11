#!/bin/bash

# Update the main repo
git checkout master
git fetch
git pull

# Init the subrepos
git submodule update --init --recursive
git submodule foreach --recursive 'git remote set-head origin --auto >/dev/null 2>&1; b=$(git symbolic-ref --short refs/remotes/origin/HEAD 2>/dev/null); case $b in origin/*) git checkout ${b#origin/} ;; *) echo no default branch in $displaypath ;; esac'

# Update the subrepos
git submodule foreach --recursive "git pull"