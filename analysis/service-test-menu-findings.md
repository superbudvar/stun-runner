# STUN Runner service/self-test screen findings

## User-observed sequence

The initial self-test is automated; no button presses are needed to reach these
screens. The observed sequence is:

1. `PRESS START TO BYPASS SELFTEST` appears first.
2. The program-ROM test progressively draws a board of small white
   rectangles (rendered textually here as `o` marks): `PROGRAM ROM OK`,
   followed by the `200`/`210` rows and `Y X W V U T S R` labels. These are
   graphical white blocks, not literal `o` glyphs.
3. `PROGRAM RAM OK` appears while the lower dash progress bar advances.
4. The `PRESS START` text changes to yellow and the lower separator line
   appears.
5. The display runs a white top-to-bottom fill, a black bottom-to-top fill, a
   fade-like transition, and several full-screen colour fills.
6. The later status screen reports:

   ```text
   PROGRAM ROM OK
   PROGRAM RAM OK
   BAD GSP VRAM
   GSP COLOR RAM OK
   ADSP BOARD OK
   SA II TEST OK
   PRESS START TO SELECT TEST MENU
   ```

## Investigation plan

The `200` and `210` rows are the two byte lanes of the MC68010 program ROM.
The six white squares under `W V U T S R` correspond to the six populated
even/odd ROM pairs in the rev-6 set: positions `200r/210r` through
`200w/210w`. Their presence on both rows is therefore expected: each ROM
pair supplies the two lanes of one 128 KiB slice. The absent `Y` and `X`
positions are unpopulated sockets in this configuration, not failed tests.

These screens provide progressively simpler checkpoints than gameplay:

- capture the MC68010 PC on every GSP host-port write during the progress
  bars, text, and colour fills;
- capture host-port reads during the `GSP VRAM` test and compare the address,
  data, and readback timing against MAME;
- separate direct host-port VRAM/palette activity from any TMS34010 execution
  by recording whether the GSP CPU advances or consumes a display list;
- convert the smallest confirmed fill/text writer first, with a test asserting
  the emitted host address/data sequence;
- treat `BAD GSP VRAM` as an observed MAME/oracle divergence, not proof that
  the ROM test or our model is wrong until the read/write sequence is captured.

The existing native host-port model already covers writes to the shared VRAM
and the two GSP colour planes. The missing evidence is the self-test's exact
writer/readback routine and the reason MAME reports the VRAM check as bad.
