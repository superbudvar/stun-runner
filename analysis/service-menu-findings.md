# STUN Runner service/self-test entry and first display trace

## Status

This is a runtime finding from MAME, not a claim that the service display is
implemented in the native port.  MAME binary: `/mnt/build/mame-ref/mame`,
SHA-256 `75b09ee422732ebce54d4c641e3bf38ddc9de4976963844691c60c361fb46b06`.

## Earlier attribution was wrong

The earlier attribution of this screen to service mode is **refuted**.  The
same PNG is produced by the ordinary no-input run at frame 120 and by the
current candidate service run.  The frame-120 screenshot was:

`/tmp/opencode/stunrun-service-frame1/service.png`

It displays `STUN RUNNER` and `PROCESS BACKGROUNDS` on a black screen.  SHA-256
`8ad6fc75382f1502ead6f00278e7a9d990b4b24ea7aeae243899e7b690f659d5` matches
the normal run's frame-120 PNG.  This is evidence of a brief ordinary
boot/title-path screen, not evidence of service mode entry.  The active-low
service timing and a distinct service screen still require a separately
verified experiment.

## Display ownership observed in the simple path

Tracing the MC68010 program window `0xC00000–0xC0000F` for frames 1–180 with
the service input asserted recorded zero MC68010 host-port writes.  Therefore
this particular service screen is not a useful first MC68010-to-GSP FIFO
conversion target.

Tracing the TMS34010 program space `0xFF800000–0xFF80FFFF` for the same run
recorded exactly 4096 writes, all at frame 38, filling that 64 KiB window at
16-byte intervals (`0xFF800000`, `0xFF800010`, …, `0xFF80FFF0`) with zero
words.  The generic Lua tap reported GSP PC as `0x00000000`; that PC field is
not usable evidence of the GSP routine yet.  The observed result is still
useful: the simple service display is produced by TMS34010-side VRAM activity,
not by a main-CPU host-port write burst.

## Next bounded experiment

Enumerate the MAME GSP state keys or add a GSP-specific execution trace so the
frame-38 zero-fill can be attributed to a TMS34010 PC.  Then capture the first
non-zero writes that create the service text.  Do not convert the zero-fill
until its GSP address-space semantics and observable output are tested.

Confidence: **KNOWN** for the identical normal/service candidate screenshots
and the write observations in the stated MAME runs; **UNKNOWN** for whether
the service switch was actually entered, the GSP routine address, and the
meaning of the 16-byte write spacing.

## Confirmed F2 service entry

The earlier reset-time/raw-port experiment was the wrong interaction model.
MAME declares `PORT_SERVICE(0x20, IP_ACTIVE_LOW)`; its standard logical service
input is F2. The harness now models that key as a logical field press at frame
60 and release at frame 120, rather than writing the active-low electrical bit
before reset.

Run used:

```text
./tools/mame-gsp-state-snapshot roms /tmp/opencode/stunrun-service-f2 \
  --mame /mnt/build/mame-ref/mame --input service --frames 600 \
  --targets 1,60,120,180,300,600 \
  --screen-dir /tmp/opencode/stunrun-service-f2/screens
```

MAME exited with code 0 and the process-group cleanup left no running MAME.
Frame 600 (`/tmp/opencode/stunrun-service-f2/screens/frame-000600.png`)
shows `PROGRAM ROM OK`, `200`, `210`, and `PRESS START TO BYPASS SELFTEST`.
Its SHA-256 is
`b9f1f0ccc9b7a34cd34c94da624c64d921c69d824fdf35900200930afe4d3010`.
This is the first confirmed service-path oracle snapshot.

Confidence: **KNOWN** for F2-style entry and the frame-600 display in this run.

## Extended service-path run

The same scripted F2 input was run to frame 3600 with periodic screen and GSP
state snapshots:

```text
./tools/mame-gsp-state-snapshot roms /tmp/opencode/stunrun-service-f2-3600 \
  --mame /mnt/build/mame-ref/mame --input service --frames 3600 \
  --targets 600,1200,1800,2400,3000,3600 \
  --screen-dir /tmp/opencode/stunrun-service-f2-3600/screens
```

MAME exited with code 0 and no MAME process remained. The observed screens
were:

- frame 600: service/self-test (`PROGRAM ROM OK`, `200`, `210`, and
  `PRESS START TO BYPASS SELFTEST`);
- frame 1200: rendered 3D driving scene;
- frame 1800: later driving/attract state;
- frames 2400 and 3000: additional rendered states;
- frame 3600: attract/instruction screen (`INSERT 1 COIN`).

The GSP high-VRAM checksum was non-zero at every sample:
`15789268`, `20738900`, `23009008`, `10244640`, `7584010`, and `7584010`
for frames 600 through 3600 respectively. The low-VRAM checksum remained zero
in this byte-view snapshot, so the high-VRAM result is the useful indicator;
it does not by itself identify the drawing routine or data format.

The frame-1200 PNG SHA-256 is
`a624257a73f4938ea417335ff50a13076391a0407ef9b853b3070943ae8f2470` and the
frame-3600 PNG SHA-256 is
`207112d454c29b45cea97c8785f738ae68c792a6c9e5e8730eca799d3137d073`.
Confidence: **KNOWN** for the observed frame progression and non-zero GSP
state; **UNKNOWN** for whether the service path itself or the subsequent
automatic attract loop owns each rendered scene.

## Corrected staged capture

The preceding extended run is superseded for stage selection: its frame-600
classification did not match the confirmed service capture. A fresh run used
the same harnessed F2 input but explicitly captured frame 120, frame 300, and
frame 600, then every 300 frames through 3600:

```text
./tools/mame-gsp-state-snapshot roms /tmp/opencode/stunrun-service-f2-3600-stages \
  --mame /mnt/build/mame-ref/mame --input service --frames 3600 \
  --targets 120,300,600,900,1200,1500,1800,2100,2400,2700,3000,3300,3600 \
  --screen-dir /tmp/opencode/stunrun-service-f2-3600-stages/screens
```

The selected early-stage images are:

- frame 120: `STUN RUNNER / PROCESS BACKGROUNDS`, SHA-256
  `8ad6fc75382f1502ead6f00278e7a9d990b4b24ea7aeae243899e7b690f659d5`;
- frame 300: `PRESS START TO BYPASS SELFTEST`, SHA-256
  `6c754821135a5ed1f5d3e3130f8ba3e126a0ffd6287e8bdbabbf051f21b531ec`;
- frame 600: `PROGRAM ROM OK`, `200`, `210`, and `PRESS START TO BYPASS
  SELFTEST`, SHA-256
  `b9f1f0ccc9b7a34cd34c94da624c64d921c69d824fdf35900200930afe4d3010`.

All thirteen requested snapshots were produced and the MAME process was
reaped. Later frames remain available under the same output directory for
manual selection.
