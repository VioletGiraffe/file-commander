#!/bin/sh

# Init the subrepos
git submodule update --init --recursive
git submodule foreach --recursive "git checkout master"

# Update the main repo
git checkout master
git fetch
git pull

# Update the subrepos
git submodule foreach --recursive "git pull"