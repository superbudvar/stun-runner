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
