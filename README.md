cube
====

simple directory tree based media player

configuration
-------------
- ~/.config/Outflux/playback-walker.conf

cache
-----
- ~/.cache/playback/

dependencies
------------
Run
- apt install mpv mediainfo imagemagick
- apt install qt6-qpa-plugins libqt6widgets6 libqt6concurrent6 libqt6gui6

Build
- apt install qtcreator qt6-base-dev qt6-base-dev-tools qmake6
		qml-qt6 qt6-qmltooling-plugins

deployment
----------
Release build
- export LANG=C.UTF-8
- mkdir -p build
- qmake6 -o build/Makefile cube.pro
- make -C build

tests
-----
Two test suites live under `tests/`, both invoked via `make test`:

1. `test_thumbnailer.sh` — functional test for the `thumbnailer` shell
   script. Synthesizes a one-second video with `ffmpeg -f lavfi` (no
   fixtures on disk), runs `thumbnailer` against it inside a sandboxed
   `$HOME`, and asserts the cache layout, stdout contract, cache reuse,
   and regeneration when media mtime advances past the cached thumb.
2. `tst_mediainfo` — QTest binary built from `tests/tst_mediainfo.pro`.
   Exercises `parseMediaInfo()` in `mediainfo.cpp` (the JSON walk that
   produces metadata-table rows) with a matrix of fixtures covering
   file-size formatting, duration formatting, video/audio/subtitle
   branches, and degenerate input. Pins the current behaviour so
   refactors of metadata handling can be done with confidence.

Run from the build directory once `qmake6` has been invoked:
- make -C build test

The tests exercise the full `thumbnailer` pipeline and build a small
Qt binary, so they need the runtime dependencies above (`mpv`,
`mediainfo`, `imagemagick`) plus the build dependencies. The one
additional tool the tests themselves require — for synthesizing the
sample video — is `ffmpeg`:
- apt install ffmpeg
