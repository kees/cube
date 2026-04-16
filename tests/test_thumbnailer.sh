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

# --- Test 7: external subtitle discovery ---
# Create a media file inside a directory structure that exercises all
# three phases of vidplay's subtitle logic, then verify the synthetic
# Text tracks appear in the JSON sidecar.

SUBDIR="$TESTDIR/Movies/Test Movie (2020)"
mkdir -p "$SUBDIR/Subs"
SUBMEDIA="$SUBDIR/Test Movie (2020).mp4"
cp "$MEDIA" "$SUBMEDIA"
SUBBASE="Test Movie (2020)"

# Phase 1: Subs/*_Eng*.srt (English SDH, largest first)
echo "large english sdh" > "$SUBDIR/Subs/2_English.srt"
echo "small eng" > "$SUBDIR/Subs/3_Eng.srt"

# Phase 2: basename-matched with language tags
echo "french" > "$SUBDIR/${SUBBASE}.fr.srt"
echo "bare" > "$SUBDIR/${SUBBASE}.srt"

# Phase 3a: VobSub .idx/.sub pair (DVD rip with language info)
cat > "$SUBDIR/Subs/${SUBBASE}.idx" <<'IDXEOF'
# VobSub index file
id: en, index: 0
id: fr, index: 1
IDXEOF
printf 'fake vobsub data' > "$SUBDIR/Subs/${SUBBASE}.sub"

# Phase 3b: non-English SRT in Subs/
echo "spanish" > "$SUBDIR/Subs/4_Spanish.srt"

# Invalidate any prior cache for this file.
SUB_REAL=$(realpath "$SUBMEDIA")
SUB_HASH=$(echo -n "$SUB_REAL" | sha256sum | awk '{print $1}')
SUB_THUMB="$HOME/.cache/playback/thumbnails/${SUB_HASH:0:2}/$SUB_HASH.png"
rm -f "$SUB_THUMB" "$SUB_THUMB.json" "$SUB_THUMB.ratings" 2>/dev/null

"$THUMBNAILER" "$SUBMEDIA" >/dev/null

[ -f "$SUB_THUMB.json" ] || fail "external subs: JSON sidecar not created"

# Count Text tracks in the JSON.
TEXT_COUNT=$(python3 -c "
import json, sys
data = json.load(open('$SUB_THUMB.json'))
tracks = [t for t in data.get('media',{}).get('track',[]) if t.get('@type') == 'Text']
print(len(tracks))
")
# Expect 7 external sub tracks: 2_English, 3_Eng, fr, bare,
# VobSub en + fr (from .idx), 4_Spanish.
# (The media itself has no embedded subs.)
[ "$TEXT_COUNT" = "7" ] \
	|| fail "external subs: expected 7 Text tracks, got $TEXT_COUNT"

# Verify specific languages are present.
LANGS=$(python3 -c "
import json
data = json.load(open('$SUB_THUMB.json'))
tracks = [t for t in data.get('media',{}).get('track',[]) if t.get('@type') == 'Text']
print(' '.join(sorted(t.get('Language','?') for t in tracks)))
")
# English (x2 from SDH), en (bare .srt + DVD), fr (srt + DVD), Spanish
[ "$LANGS" = "English English Spanish en en fr fr" ] \
	|| fail "external subs: unexpected languages: '$LANGS'"

# Verify DVD title tag on VobSub tracks.
DVD_COUNT=$(python3 -c "
import json
data = json.load(open('$SUB_THUMB.json'))
tracks = [t for t in data.get('media',{}).get('track',[])
          if t.get('@type') == 'Text' and t.get('Title') == 'DVD']
print(len(tracks))
")
[ "$DVD_COUNT" = "2" ] \
	|| fail "external subs: expected 2 DVD-tagged tracks, got $DVD_COUNT"

echo "PASS   : test_thumbnailer.sh"
