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
The `thumbnailer` script is exercised by a functional test under `tests/`.
It synthesizes a one-second video with `ffmpeg -f lavfi` (no fixtures on
disk), runs `thumbnailer` against it inside a sandboxed `$HOME`, and
asserts the cache layout, stdout contract, cache reuse, and regeneration
on stale cache.

Run from the build directory once `qmake6` has been invoked:
- make -C build test

The test exercises the full `thumbnailer` pipeline, so it needs the
runtime dependencies above (`mpv`, `mediainfo`, `imagemagick`). The one
additional tool the test itself requires — for synthesizing the sample
video — is `ffmpeg`:
- apt install ffmpeg
