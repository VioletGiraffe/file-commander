#!/bin/sh

git submodule foreach --recursive "git push"
git push