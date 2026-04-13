cube
====

simple directory tree based media player

configuration
-------------
- ~/.config/Outflux/playback-walker.conf

ratings (optional)
------------------
For files under a `/Movies/` directory whose name (or parent directory)
matches the pattern `Title Name (YYYY)`, the thumbnailer can fetch
ratings from OMDb (Rotten Tomatoes, IMDb, Metacritic). To enable:

1. Get a free OMDb API key at https://www.omdbapi.com/apikey.aspx
2. Add the key to `~/.config/Outflux/playback-walker.conf`:
   `omdb_apikey=your_key_here`

Ratings are cached in `~/.cache/playback/thumbnails/` as `.ratings`
sidecars alongside the existing `.png` and `.json` files, and are
regenerated when the media file or the thumbnailer script changes.

Each rating row in the metadata table shows a small service-logo icon.
Out of the box these are brand-coloured placeholder squares (red for RT,
yellow for IMDb, green for Metacritic, orange for Letterboxd). To use
real logos, drop 32x32 PNG files into `~/.cache/playback/icons/`:
- `rt.png` — Rotten Tomatoes
- `imdb.png` — IMDb
- `metacritic.png` — Metacritic
- `letterboxd.png` — Letterboxd (for future use)

No rebuild needed; the icons are loaded at runtime on first display.

cache
-----
- ~/.cache/playback/

dependencies
------------
Run
- apt install mpv mediainfo imagemagick
- apt install qt6-qpa-plugins libqt6widgets6 libqt6concurrent6 libqt6gui6
- apt install curl jq  (optional, for movie ratings lookups)

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
