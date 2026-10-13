# 68010 source attribution for bounded GSP uploads

## Result

A fresh bounded MAME capture ties two complete 68010 display-list compiler calls
to both their source streams in main-program ROM and their GSP host destinations.
This demonstrates that we can preserve source provenance through the 68010 call
boundary; it does **not** establish that either destination contains executable
GSP code or identify the payload format.

Experiment and artifact hashes: `reference/experiments/stunrun/gsp-source-
attribution.metadata.json`. The full regenerable JSON result and MAME log were
left local under `/tmp/opencode/gsp-src-correlation-600-final/` because the raw
result contains more than 20 MiB of repetitive FIFO events.

| Call | Frame | 68010 caller | Source stream | Bytes consumed by decoder | GSP destination | FIFO words | Decoder count |
|---:|---:|---:|---:|---:|---:|---:|---|
| 4 | 376 | `$02D7A4` | `$09841C` | 59 | `$FFF3FD80` | 40 | 40/40 |
| 5 | 376 | `$02D7B6` | `$098457` | 55 | `$FFF3FB20` | 38 | 38/38 |

Both source pointers are in the 68010 program-ROM region in the machine map.
The pointers are contiguous: the decoder's 59-byte call-4 consumption ends
exactly at call 5's source address. The pointer and source bytes are directly
captured at call entry; the consumed length is derived by the existing stream
decoder, then corroborated by the exact FIFO word count. We did not install a
per-read MC68010 tap for these ROM bytes. The established `$225E4` compiler
translates each stream into its ordered FIFO word sequence. Source/FIFO SHA-256
values are in the experiment metadata.
The payloads are transformed by the compiler, so equality of source and FIFO
bytes is not expected; the meaningful check here is complete decoding and exact
agreement between expected and observed FIFO word counts.

The same run reports the GSP halted at PC `$00000000` through frame 406 and
sampled executing in the `$FFF4...` range beginning at frame 407. That timing
does not prove whether the two `$FFF3...` destinations are code or data. The
GSP execute tap is unavailable through this MAME Lua API and GSP-originated
writes were not captured. Their semantic type remains **UNKNOWN**.

## Instrumentation correction

The first fresh attempt exposed a harness false-success: this MAME build's GSP
program-space object has no `install_execute_tap` method. The Lua error occurred
after the 68010 call/FIFO taps had been installed, so those producer-side taps
could still collect events, but `consumer_capture_status=observed` was not
trustworthy because the script set it before attempting the missing method.
The runner also accepted a completion marker despite `[LUA ERROR]` output.

The trace now treats execute taps as optional, marks consumer evidence
`unknown` when unavailable, records GSP writes as `not-captured` rather than an
empty successful trace, and rejects any run containing a MAME Lua error. Its
MAME config and NVRAM directories are isolated under the output directory.
The broad GSP write tap was removed after it caused MAME to segfault in this
build; no conclusion about game behavior is drawn from that instrumentation
failure. This is our harness limitation, not an original-game or MAME-driver
defect.

## Next discriminator

Apply the same source-attribution method to the initial GSP program upload and
the `/GSPRES` release boundary. Capture the complete host-register event stream,
recover the source ranges and pre-release VRAM image, then verify whether GSP
execution enters those uploaded ranges. Keep the display-list payloads above as
known source/destination examples, not as evidence that the GSP has multiple
programs.
