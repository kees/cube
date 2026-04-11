#!/bin/bash
#
# Functional test for the `thumbnailer` script.
#
# Synthesizes a tiny video with `ffmpeg -f lavfi` (no fixture files on disk),
# runs the thumbnailer against it with a sandboxed $HOME so the real cache
# under ~/.cache/playback is untouched, and asserts:
#   1. The PNG + JSON sidecar are created at the expected sha256-derived path.
#   2. stdout contains exactly one line and that line is a real file path
#      (catches stderr leaks — the `2>&1 >> LOG` bug we've hit before).
#   3. A second invocation reuses the cache (thumb mtime unchanged).
#   4. Touching the media file forward in time invalidates the cache and
#      triggers regeneration.
#
# Requires: bash, ffmpeg, mpv, ImageMagick (`convert`), mediainfo, sha256sum,
#           realpath, awk, cut, GNU coreutils `stat` and `touch -d`.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
THUMBNAILER="$SCRIPT_DIR/../thumbnailer"

fail() {
	echo "FAIL: $*" >&2
	exit 1
}

# Dependency check.
for cmd in ffmpeg mpv convert mediainfo sha256sum realpath stat touch; do
	command -v "$cmd" >/dev/null 2>&1 || fail "missing dependency: $cmd"
done
[ -x "$THUMBNAILER" ] || fail "thumbnailer script not executable: $THUMBNAILER"

# Sandbox via fake HOME so the script's hard-coded ~/.cache/playback is
# redirected into $TESTDIR. Clean up on any exit.
TESTDIR=$(mktemp -d)
trap 'rm -rf "$TESTDIR"' EXIT
export HOME="$TESTDIR"
mkdir -p "$HOME/.cache/playback"

# Synthesize a one-second test clip with SMPTE colour bars (>= 8 distinct
# colours, so the thumbnailer's colour-count heuristic accepts frame 0) and
# a sine-wave audio track (so mediainfo has an Audio track to report on).
MEDIA="$TESTDIR/sample.mp4"
ffmpeg -loglevel error \
	-f lavfi -i testsrc=duration=1:size=320x240:rate=30 \
	-f lavfi -i sine=frequency=440:duration=1 \
	-c:v libx264 -preset ultrafast -pix_fmt yuv420p \
	-c:a aac -shortest -y "$MEDIA" \
	|| fail "ffmpeg failed to synthesize $MEDIA"
[ -s "$MEDIA" ] || fail "synthesized media is empty"

# Compute the expected cache path the same way the script does.
MEDIA_REAL=$(realpath "$MEDIA")
HASH=$(echo -n "$MEDIA_REAL" | sha256sum | awk '{print $1}')
EXPECT="$HOME/.cache/playback/thumbnails/${HASH:0:2}/$HASH.png"

# --- Test 1: first invocation populates the cache and returns the path. ---
THUMB=$("$THUMBNAILER" "$MEDIA")
[ "$THUMB" = "$EXPECT" ] || fail "stdout path mismatch: got '$THUMB' expected '$EXPECT'"
[ -f "$THUMB" ] || fail "thumbnail PNG not created at $THUMB"
[ -f "$THUMB.json" ] || fail "JSON sidecar not created at $THUMB.json"
[ -s "$THUMB" ] || fail "thumbnail PNG is empty"
[ -s "$THUMB.json" ] || fail "JSON sidecar is empty"

# --- Test 2: stdout is exactly one line (catches stderr-on-stdout leaks). ---
RAW=$("$THUMBNAILER" "$MEDIA")
LINES=$(printf '%s\n' "$RAW" | wc -l)
[ "$LINES" -eq 1 ] || fail "thumbnailer stdout has $LINES lines, expected 1: $RAW"
[ -f "$RAW" ] || fail "thumbnailer stdout is not a valid file path: $RAW"

# --- Test 3: second invocation is a cache hit (thumb mtime unchanged). ---
# Use nanosecond-resolution mtime (%.9Y) so a regeneration within the same
# wall-clock second still shows as a distinct value. tmpfs and ext4 both
# record nanosecond mtimes, so this works on any sane Linux test host.
BEFORE=$(stat -c '%.9Y' "$THUMB")
"$THUMBNAILER" "$MEDIA" >/dev/null
AFTER=$(stat -c '%.9Y' "$THUMB")
[ "$BEFORE" = "$AFTER" ] || fail "cache not reused: thumb mtime changed $BEFORE -> $AFTER"

# --- Test 4: advancing media mtime past the thumb invalidates and regenerates. ---
touch -d "2030-01-01 00:00:00" "$MEDIA"
"$THUMBNAILER" "$MEDIA" >/dev/null
REGEN=$(stat -c '%.9Y' "$THUMB")
[ "$REGEN" != "$BEFORE" ] || fail "stale media did not trigger regeneration"

echo "OK: test_thumbnailer.sh"
