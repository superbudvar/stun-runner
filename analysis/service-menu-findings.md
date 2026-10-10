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
