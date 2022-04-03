#!/usr/bin/env bash

set -e

echo Running example $@

exec "$NORM_BUILD/$@"
