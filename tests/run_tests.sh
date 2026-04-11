#!/bin/bash
#
# Top-level test runner invoked by `make test`. Runs both the shell
# functional test for the `thumbnailer` script and the QTest binary for
# `parseMediaInfo()`. Fails fast on the first error so `make test`
# propagates a non-zero exit.
set -euo pipefail

DIR=$(cd "$(dirname "$0")" && pwd)

# 1. Shell test for the thumbnailer script.
bash "$DIR/test_thumbnailer.sh"

# 2. C++ unit tests for parseMediaInfo(). Build out-of-tree in tests/build
# so artefacts stay out of the main build dir and the source tree. qmake6
# is idempotent; re-running it on every invocation is cheap.
mkdir -p "$DIR/build"
(
	cd "$DIR/build"
	qmake6 ../tst_mediainfo.pro >/dev/null
	make -s
	./tst_mediainfo
)
