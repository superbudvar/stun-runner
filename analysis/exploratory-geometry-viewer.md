# Exploratory geometry viewer

`tools/geometry-viewer` is a fast, dependency-free SVG visualizer for the
currently unresolved road/FIFO word format. It is intentionally **not** an
original-game renderer: it renders several signed 16-bit field/stride
hypotheses side by side and labels the output accordingly.

## Usage

For a reconstructed big-endian MC68010 image containing the observed course-0
table at `$044630`:

```sh
tools/geometry-viewer stunrun/mc68010_rom.bin /tmp/stunrun-geometry.svg \
  --offset 0x44630 --count 384
```

Open the SVG in a browser or image viewer. The four panels compare adjacent
table words, stride-2 words, two alternate lanes, and the derived even-word
FIFO stream. The input must contain the reconstructed image; separate ROM
byte lanes cannot be passed directly.

The viewer is useful for quickly rejecting visual hypotheses. It does not
establish the GSP field meanings, polygon topology, or the relationship of
the `$2F470` primitive stream to the road-table transfer.

## First result

The first render of the reconstructed image's `$044630` table did **not** look
like a road under any of the four default signed-word projections. This is a
useful negative observation, not a format decode: the table may be an
intermediate command/parameter table, the plotted lanes may be wrong, or the
values may require the GSP's transform before becoming screen geometry. The
road-table label therefore remains a provenance label from the observed
 runtime path, not a claim about the table's field layout.

## Reassessment after `$2F470`/`$2B7E8` decoding

The newly decoded `$2F470` primitive emitter does not validate any panel in
this SVG. It emits five FIFO halfwords per call: a `$2000 + selector` word
followed by four low argument words. Its caller `$2B7E8` emits four such
records from D2/D3/D4, with `$80` and `$38` offsets. No evidence currently
connects that caller to the 384-word road-table transfer at `$02248E`.

The arithmetic is also a useful negative check: the observed road transfer
contains 192 FIFO halfwords, which is not divisible by the five-halfword
primitive record size (`192 mod 5 = 2`). Therefore the current road FIFO and
the decoded primitive stream should not be merged into one display-list
format. The SVG remains useful only as a deliberately labeled hypothesis
rejection tool; it is not a track reconstruction and should not be used as
the basis for an OpenGL mesh.

The reassessment render used `stunrun/mc68010_rom.bin`, offset `$044630`,
count 384, and was written to `/tmp/opencode/stunrun-track-reassessment.svg`
(SHA-256
`2ea8fe0f0699c1336c4dd78e8e255ef3bceb3977786418053b076fce8d7c2369`). It
again produces tangled projections rather than a road-like path.
