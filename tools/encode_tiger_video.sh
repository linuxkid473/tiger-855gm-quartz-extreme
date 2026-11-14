#!/bin/sh
# Re-encode a video for a 1.6 GHz Pentium M running QuickTime 7.1.3 on Tiger: Constrained Baseline, no B-frames, CAVLC,
# 360 lines (NOT 384: 640x384 clips hard-freeze the 855GM machine), native frame rate (never resample 30 -> 24).
# usage: encode_tiger_video.sh in.ext out.mov [width]      (width default 640; 4:3 sources: use 480)
W=${3:-640}
ffmpeg -nostdin -i "$1" -vf "scale=$W:360:flags=lanczos,setsar=1" -c:v libx264 -profile:v baseline -level 3.0 -preset slow -crf 20 \
  -maxrate 2800k -bufsize 3000k -x264-params ref=1:bframes=0:cabac=0:weightp=0 -c:a aac -b:a 128k -movflags +faststart "$2"
