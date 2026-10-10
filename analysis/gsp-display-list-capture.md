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
