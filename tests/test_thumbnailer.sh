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

# --- Test 5: square-pixel source produces the expected 720-tall thumbnail. ---
# The synthesized sample.mp4 is 320x240 with SAR=1:1, so scaling to 720 tall
# with auto width gives 960x720 (= 320 * 720/240). Pins the square-pixel path
# of the anamorphic-safe `scale=iw*sar:ih,scale=-1:720` filter chain.
DIMS=$(identify -format '%wx%h' "$THUMB")
[ "$DIMS" = "960x720" ] || fail "square-pixel thumb dims wrong: got '$DIMS' expected 960x720"

# --- Test 6: anamorphic source is corrected to display aspect, not coded. ---
# Synthesize a second clip with the SAR deliberately set to 2:1 so the coded
# frame is 640x480 but the display aspect is 1280x480 (DAR 8:3). With the
# anamorphic-safe filter, the 720-tall thumbnail should be 1920x720. Under
# the old `scale=-1:720` filter (which ignored SAR entirely) it would have
# come out as 960x720 — the squashed look that motivated the fix.
ANA="$TESTDIR/anamorphic.mp4"
ffmpeg -loglevel error \
	-f lavfi -i testsrc=duration=1:size=640x480:rate=30 \
	-f lavfi -i sine=frequency=440:duration=1 \
	-vf setsar=2/1 \
	-c:v libx264 -preset ultrafast -pix_fmt yuv420p \
	-c:a aac -shortest -y "$ANA" \
	|| fail "ffmpeg failed to synthesize anamorphic media"

ANA_THUMB=$("$THUMBNAILER" "$ANA")
[ -f "$ANA_THUMB" ] || fail "anamorphic thumbnail not created at $ANA_THUMB"
ANA_DIMS=$(identify -format '%wx%h' "$ANA_THUMB")
[ "$ANA_DIMS" = "1920x720" ] \
	|| fail "anamorphic thumb dims wrong: got '$ANA_DIMS' expected 1920x720 (would be 960x720 under the old non-SAR-aware filter)"

# --- Test 7: updating the thumbnailer script itself invalidates caches. ---
# When the generating logic changes (as it just did for the anamorphic fix),
# existing cached thumbnails should be regenerated — not silently reused.
# Operate on a *copy* of the script so we can safely `touch` it without
# mutating the real repo file; the copy's $0 resolves to the copy's path so
# the in-script SELF=$(command -v "$0") reads the copy's mtime.
COPY="$TESTDIR/thumbnailer_copy"
cp "$THUMBNAILER" "$COPY"
chmod +x "$COPY"

# Prime: run once via the copy so the cache reflects the copy's mtime baseline.
"$COPY" "$MEDIA" >/dev/null
BEFORE=$(stat -c '%.9Y' "$THUMB")

# Advance the copy's mtime well past the thumb's; re-run and confirm regen.
touch -d "2031-01-01 00:00:00" "$COPY"
"$COPY" "$MEDIA" >/dev/null
REGEN=$(stat -c '%.9Y' "$THUMB")
[ "$REGEN" != "$BEFORE" ] || fail "script mtime change did not invalidate thumb"

# Same check for the JSON sidecar.
BEFORE_JSON=$(stat -c '%.9Y' "$THUMB.json")
touch -d "2032-01-01 00:00:00" "$COPY"
"$COPY" "$MEDIA" >/dev/null
REGEN_JSON=$(stat -c '%.9Y' "$THUMB.json")
[ "$REGEN_JSON" != "$BEFORE_JSON" ] || fail "script mtime change did not invalidate json"

echo "OK: test_thumbnailer.sh"
