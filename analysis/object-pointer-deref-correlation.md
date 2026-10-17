# Runtime object-pointer and descriptor correlation

This experiment answers a narrow question: do active 68010 object-record
pointer fields resolve to data that the copy routine reads from maincpu ROM?
It does **not** identify vehicles or prove a complete object catalog.

## Capture

Five independent runs of the same `late_drive` scripted-input schedule were
captured with `md12-fixed` (MAME SHA-256
`55956f2b26c0d5caeb86f346e6247ded04428eae07465d35470dea77d6bf4045`). Each
captured frames 920–930 of a 1000-frame run with event ordering enabled. The
same-run trace watched 68010 reads from `0x00047000–0x00FFDEFF`, selected PCs
`0x02E220`, `0x02E226`, `0x02E22A`, `0x02E230`, `0x02E25A`, `0x02E264`, and
writes from `0x00C00000–0x00FFDEFF`. Full raw logs, parsed results, and concise
summaries are under
`reference/experiments/stunrun/m5-object-pointer-deref-correlation/run-{1..5}/`;
the complete provenance is in that directory's `metadata.json`.

The 1989 maincpu ROM image is 786,432 bytes with SHA-256
`29c911a21d1abe780419cd3a924080756490f2d008776995dd48c7b2bedc5b6c`. The
captured instruction at `$02E226` is `movea.l ($26,A2),A0`. Its paired word
reads expose a 32-bit pointer in each runtime record. The following observed
PCs read descriptor offsets: `$02E22A` at `+0`, `$02E230` at `+2`, `$02E264` at
`+6`, `$02E25A` at `+8/+10`, and `$02E220` at `+12/+14`. The analyzer
`tools/analyze-object-pointer-trace` cross-checks every captured value against
the ROM bytes at the observed effective address.

## Result

Every run produced 23 pointer longwords across 23 record locations, resolving
to 13 unique maincpu-ROM targets. The mapping was identical across all five
runs. Each run also produced 159 descriptor-field reads matching the ROM image
byte-for-byte (zero mismatches), with no read or write trace truncation.
Therefore, for this observed routine/path, the `$FF9C00–$FFDEFF` records do
contain pointer fields consumed as maincpu ROM descriptor addresses; this is a
runtime pointer-to-ROM-data correlation, not a label inferred from nearby
bytes.

The sampled target differences have a greatest common divisor of 20 bytes, and
the observed per-target snippets have recurring content. This is a candidate
20-byte entry spacing only: targets may be sampled from several tables, and
neither boundaries nor exhaustiveness follow from this capture. See the raw
words and observed field offsets in each analyzer JSON output or regenerate
them with:

```bash
tools/analyze-object-pointer-trace \
  reference/experiments/stunrun/m5-object-pointer-deref-correlation/run-1/result.json \
  roms/stunrun/stunrun_maincpu.bin --json
```

There were 23,210 same-run writes to GSP FIFO addresses `$C0000C/$C0000E` in
the capture window, but they came from many 68010 PCs. Event ordering confirms
they coexist in the run, not that a particular FIFO write originated from a
particular pointer target. Do not use this count as an object-to-geometry join.

## Confirmed vs unknown

- **Confirmed [ROM]**: `$02E226` loads a longword from runtime-record offset
  `+$26`; the 13 observed targets are within the maincpu ROM image; the listed
  descriptor reads match the corresponding ROM words in all five runs.
- **Confirmed [ROM]**: the run's observed pointer mapping is stable across five
  repeated scripted-input runs on the same MAME binary and harness.
- **Inference**: some target spacing may represent 20-byte records. This is not
  a proven global stride or a table boundary.
- **UNKNOWN**: the target entries' semantic fields and their vehicle/object
  identities; whether the targets exhaust all entries; and which emitted GSP
  commands or geometry correspond to each entry.
- **UNKNOWN**: `mainpcb:user1` ADSP object-ROM asset boundaries. This experiment
  concerns maincpu program-ROM pointers only and does not identify ADSP assets.

No vehicle names or gameplay properties are assigned. The next useful slice is
to isolate one pointer target at a time through the descriptor-copy routine and
the downstream geometry submission path, using discriminating field values or
an independently interpretable rendered result.
