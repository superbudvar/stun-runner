# Bounded GSP display-list capture

## Capture

The pinned harness was run for 600 frames with the `drive` input mode and a
100,000-word FIFO cap:

```text
./tools/mame-display-list-trace roms /tmp/opencode/gsp-display-list-trace-600-100k \
  --mame /mnt/build/mame-ref/mame --frames 600 --start-frame 1 \
  --end-frame 600 --max-calls 64 --max-fifo-words 100000 --input drive
```

MAME is `/mnt/build/mame-ref/mame`, SHA-256
`75b09ee422732ebce54d4c641e3bf38ddc9de4976963844691c60c361fb46b06`.
The complete artifact hashes and command provenance are in
`reference/experiments/stunrun/gsp-display-list-trace-600/metadata.json`.

## Observed calls

Seven display-list compiler calls were captured, with callers:

```text
02D7D6, 02D780, 02D792, 02D7A4, 02D7B6, 02D7F2, 02DB98
```

Calls 4 and 5 were complete and count-matched against the existing stream
decoder:

| call | frame | caller | stream | FIFO words |
|---:|---:|---:|---:|---:|
| 4 | 101 | `$02D7A4` | `$09841C` | 40 |
| 5 | 101 | `$02D7B6` | `$098457` | 38 |

The other calls were truncated by the FIFO cap or did not match the bounded
stream decoder. Call 4's 40 words are exactly divisible by eight, but this is
only a grouping observation; it is not evidence that the words are eight
five-word geometry records. Call 5 has 38 words and therefore rejects that
interpretation for the adjacent call family.

The GSP consumer execute tap was installed and reported `observed`, but it
reported zero events at `$FFF48C20` during this capture. This is an
instrumentation result, not evidence that the GSP did not consume the FIFO:
the GSP program selector or tap semantics may not expose the consumer in this
run. FIFO-to-call ownership and field semantics remain UNKNOWN.

## Rendering consequence

This capture is sufficient to preserve exact bounded FIFO payloads and to
drive a future replay decoder, but not to create a trustworthy GL mesh. The
indexed scanout path is therefore the current renderer boundary; geometry
rendering must wait for a consumer-correlated record decode or an equivalent
oracle comparison.

## Destination-window check

The same frame was sampled through the scripted memory-snapshot harness at
GSP program-space address `$FFF3F000`, covering `$1000` values. At frame 90
the window was zero; at frame 101 it contained 976 nonzero values and at
frame 110 it still contained 976. The frame-101 snapshot is preserved under
`reference/experiments/stunrun/gsp-vram-window-101/`.

The nonzero runs include the destinations observed for the two complete
uploads:

```text
$FFF3FB20  — call 5 destination
$FFF3FD80  — call 4 destination
```

This establishes that those uploads land in the GSP mapped shared-memory
window during the same early display phase. It does **not** establish whether
the data is a glyph, sprite, texture, command buffer, or geometry; the
semantic role remains UNKNOWN. It does, however, rule out treating these two
small uploads as FIFO-only records and makes them useful candidates for a
future indexed/2bpp texture decoder.

## Frame-101 visual correlation

An independent harness capture at frame 101 with the same `drive` input mode
shows the visible screen text `STUN RUNNER` and `PROCESSING BACKGROUNDS`.
The screenshot and provenance are preserved under
`reference/experiments/stunrun/gsp-frame101-screen/`.

This is a strong temporal correlation between the small shared-memory uploads
and a text-rendering phase. It does not yet prove that calls `$02D7A4` and
`$02D7B6` contain the glyph pixels: the display-list compiler may be copying
font data, a text command, or an intermediate tile record, and the GSP/scanout
consumer has not been correlated. The existing `$234B6` 2bpp row expander is
therefore a candidate comparison implementation, not a decoded format.

## Entry-state capture

The bounded trace also preserves the MC68010 entry state for each `$225E4`
display-list call. The two frame-101 calls are:

```text
caller  frame  stream    host destination  entry stack (first 16 bytes)
$2D7A4  101    $09841C  $FFF3FD80        0002D7A60009841C00097F4B00094D09
$2D7B6  101    $098457  $FFF3FB20        0002D7B8000984570009841C00097F4B
```

The first longword is the return address and the next longword is the stream
pointer, consistent with the existing display-list entry contract. The
remaining stack words are preserved evidence only; they have not been given
argument names. This is enough to reproduce the exact source/destination
pairing without inventing a texture or glyph structure.

## Late-drive GSP execution

The first consumer tap did not fire, but that is an instrumentation limitation,
not evidence that the GSP was idle. The trace now records bounded per-frame
GSP state samples. In a `late_drive` run covering frames 600–1200, 512 samples
showed active PCs including:

```text
$FFF41060  sentinel dispatch       88 samples
$FFF41650  dispatch/worker path    55 samples
$FFF454E0  known FILL XY writer   89 samples
$FFF414C0  worker path             47 samples
```

The result is preserved at
`reference/experiments/stunrun/display-list-gsp-state-late-1200/result.json`.
This establishes that the GSP executes the known rasterizer path during
late-drive gameplay. The next correlation should tap GSP VRAM writes and pair
their GSP state/PC landmarks with the preceding MC68010 host writes; the
unavailable execute-tap callback must not be treated as evidence that no
consumer exists.
