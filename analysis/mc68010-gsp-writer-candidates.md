# MC68010 GSP writer candidates

The native Musashi state writer records each TMS34010 HSTDAT write as a
12-byte little-endian record (`<IH2xI`: GSP address, data, MC68010 PC). The
reusable classifier is `tools/analyze-musashi-gsp-writes`; it groups records by
caller PC, classifies the destination region, and reports common address
strides. It is deliberately descriptive: a PC is not promoted to a semantic
symbol without a trace/disassembly check.

## First attribution pass

Source artifact: `/tmp/opencode/stunrun-writer-attribution/state/gsp_host_writes.bin`,
SHA-256 `24e86c6355a03b8a656d1aedecea59e9419cce505176cb20e10bc0bda5006d37`.
The file contains 962,546 records. The following results are from the
classifier, not from a MAME run:

| PC | writes | destination | dominant stride | initial interpretation |
|---|---:|---|---:|---|
| `$22418` | 524,288 | main VRAM | `$10` (524,287) | large sequential VRAM fill/copy candidate |
| `$069C` | 230,400 | main VRAM | `$10` (230,399) | early/startup fill candidate; needs ROM context |
| `$22450` | 65,536 | other (`0x000000–0x0FFFF0`) | `$10` (65,535) | non-VRAM host transfer candidate |
| `$226FA` | 39,438 | main VRAM | `$10` (36,251) | structured direct-VRAM writer candidate |
| `$22732` | 28,248 | main VRAM | `$10` (27,258) | structured direct-VRAM writer candidate |
| `$066A` | 24,576 | 2bpp window | `$10` (24,575) | 2bpp fill/copy candidate |
| `$21734` | 24,576 | 2bpp window | `$10` (24,575) | 2bpp fill/copy candidate |
| `$2FE6E` | 6,091 | main VRAM | `$20` (6,088) | paired/strided pixel writer candidate |
| `$2FE76` | 6,091 | main VRAM | `$20` (6,088) | paired/strided pixel writer candidate |

These are caller PCs captured in an existing native run, not yet proof that
each routine owns a display-list record type. In particular, `$25E8` is absent
from this run, so this artifact cannot classify the frame-120 text writer.

## Why this narrows the next reverse-engineering pass

The direct host-port writers and the GSP display-list/FIFO path can now be
separated by destination:

- addresses in `$FF800000–$FFFFFFFF` are direct main-VRAM writes;
- `$02000000–$020FFFFF` is the 2bpp expander window;
- `$F5000000`/`$F5800000` are palette planes;
- other addresses must not be called display-list traffic without an
  additional trace of the target handler.

The next bounded investigation should disassemble `$22418`, `$22450`,
`$226FA`, `$22732`, `$2FE6E`, and `$2FE76`, starting with the two large fill
candidates. For each candidate, capture its entry/exit and the host-address
setup, then add a tested C slice only after the observable write pattern is
stable. `$25E8` should be traced separately with the frame-120 boot path.
