#!/bin/sh
# Regenerates the generated test clips for the library check.
#
# These are generated rather than committed: they are large, they belong to nobody, and real music
# must never land in the repository. Drop any MP3 into data/ to hear actual music (_that_ you do not
# commit either - see .gitignore).
#
# A square wave is used for audibility because a sine radiates almost nothing from a small driver at
# levels safe on 3V3; the sine is kept as the clean reference for judging distortion.
set -eu
cd "$(dirname "$0")"
mkdir -p data

# warble, harmonically rich: proves the chain end to end
ffmpeg -y -v error -f lavfi \
  -i "aevalsrc=0.3*sgn(sin(2*PI*(220+110*sin(2*PI*1.3*t))*t)):d=4:s=44100" \
  -ac 2 -c:a pcm_s16le data/test.wav

# pure sine: anything rough is then the connection, not the waveform
ffmpeg -y -v error -f lavfi -i "aevalsrc=0.3*sin(2*PI*220*t):d=4:s=44100" \
  -ac 2 -c:a pcm_s16le data/sine.wav

# the same content compressed, to exercise the MP3 decoder
ffmpeg -y -v error -f lavfi \
  -i "aevalsrc=0.3*sgn(sin(2*PI*(220+110*sin(2*PI*1.3*t))*t)):d=15:s=44100" \
  -c:a libmp3lame -b:a 128k data/test.mp3

ls -lh data/
echo "done - the SPIFFS partition is ~3.1 MB, so keep the total under that"
