# Synthetic media fixture

`h264-aac.mp4` is a three-second loop of FFmpeg's generated test pattern and a
quiet sine tone. It contains no third-party recording or personal data. The
repository's licence applies to this generated fixture.

It exercises real H.264 Constrained Baseline video and AAC-LC audio decoding in
control/native Chromium. The smoke test requires decoded video frames, explicit
pause retention through tab switches, and continuing playback in the background.
It does not stand in for live Twitch, HLS, DRM or the actual Pie extension.

Generation command (FFmpeg with libx264 and the native AAC encoder):

```sh
ffmpeg -hide_banner -loglevel error \
  -f lavfi -i 'testsrc2=size=160x90:rate=12' \
  -f lavfi -i 'sine=frequency=440:sample_rate=48000' \
  -t 3 -c:v libx264 -preset veryslow -crf 32 -profile:v baseline -level:v 3.0 \
  -pix_fmt yuv420p -filter:a volume=0.0625 -c:a aac -b:a 32k \
  -fflags +bitexact -flags:v +bitexact -flags:a +bitexact \
  -map_metadata -1 -movflags +faststart tests/chromium/media/h264-aac.mp4
```

The committed file has SHA-256
`e22e1a2c812b67dfa10cd6ced85d857fe912696c8c31ff237d2220a4d0a13b6f`.
Encoder updates may change the output bytes; inspect the streams and refresh the
record deliberately when regenerating it.
