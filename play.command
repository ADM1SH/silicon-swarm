#!/bin/bash
# Double-click this in Finder to build and launch Silicon Swarm.
# (Same as running `make build && make run-gfx` yourself.)
cd "$(dirname "$0")"
make build && make run-gfx
