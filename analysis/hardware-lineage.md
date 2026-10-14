# Hardware lineage: where S.T.U.N. Runner's board comes from

## Question this answers

S.T.U.N. Runner runs on Atari's `harddriv` board family, but the family shipped
in several variants and none of them are ours. Which parts of what other people
documented about that family transfer to this machine — and which do not?

## Why this matters here

Most of what is publicly known about this hardware was written about **Hard
Drivin' and Race Drivin'**, not about S.T.U.N. Runner. Those boards share the
68010, the TMS34010 GSP, the ADSP-2100 and the JSA II sound board with us, and
they differ in the places that are easiest to get wrong: VRAM size, and whether
a second TMS34010 is fitted.

Jed Margolin was the hardware engineer on that team and has published the
schematics and his engineering notes at
[jmargolin.com/schem/schems.htm](https://jmargolin.com/schem/schems.htm). That
page is a primary source written by someone who was in the room. It is the only
external document in this repository we treat as hardware evidence rather than
folklore.

Caveat that governs this whole file: **the page is about Hard Drivin'/Race
Drivin'.** Nothing on it mentions S.T.U.N. Runner. Everything below is either
(a) an independent corroboration of a conclusion we already reached from the
manual and MAME, or (b) explicitly marked as a family-level fact we have not yet
confirmed for our board.

---

## Which board are we on?

MAME models S.T.U.N. Runner through `stunrun_board_device_state`, which calls
`multisync_nomsp()` (`harddriv.cpp:1859`), i.e. the **MultiSync board without the
MSP**. Margolin confirms S.T.U.N. Runner's siblings differ in exactly this way:
the MSP was optional, and the *compact* Hard Drivin' had one while the cockpit
did not.

### "MSP" means Model Signal Processor

Margolin: *"MSP stands for Model Signal Processor to distinguish it from the
first TMS34010, the Graphics Signal Processor."* The naming explains the
ambiguity we have been working around — "the second TMS34010" is not another
graphics processor, it is the vehicle-physics processor, which is exactly why
`multisync_nomsp` exists as a distinct configuration.

His board used two TMS34010s; ours has one. Our `analysis/driver-mining/` map
already recorded the exclusion and the runtime inventory confirms only four
programmable devices are active.

---

## The load-bearing one: 512 KiB VRAM

**This independently corroborates our central hardware finding, and it is worth
more than the rest of this page combined.**

We concluded — from the SP-338 schematic, from a direct MAME share-pointer dump,
and from the TMS34010 reference manual — that the GSP's physical VRAM is
**512 KiB**, and that the 4 MiB figure in MAME's map is the address *window*, not
the array. That is `docs/mc68010-boot-findings.md` §12.1 and §34, and getting
it wrong cost this project several days.

Margolin describes the MultiSync GSP memory without ever mentioning our game:

> *"The GSP system in the MultiSync Main Board used a memory organization of only
> 8 banks of 64Kx8 (16 64Kx4 VRAMs) in order to save money. In addition to having
> less memory, the system could only fill 8 pixels at time instead of 16."*

Which is the same board we are on. And the arithmetic is unambiguous:

```text
16 chips × 65536 words × 4 bits
  = 4,194,304 bits
  = 524,288 bytes
  = 512 KiB
  = 262,144 words
```

That last line is exactly what MAME reports. Re-measured on the frozen oracle
binary while writing this page:

```text
STUNRUN-DUMP wrote 524288 bytes
  BASE off=0 (0x000000) byte=0x000000 vram_words=262144 mask=0x003ffff
```

Three independent lines now agree — SP-338's `VRAM Array (16) 64K x 4`, Margolin's
chip count, and MAME's own share size — and MAME's `map(0xff800000,
0xffbfffff).mirror(0x0400000)` is confirmed to be the window, not the array.

The **8 pixels per VRAM word** figure is also consistent with our corrected
dumper reading one word per `0x10` of TMS34010 address units. That consistency
is *suggestive*, not proven: we have not derived the address-to-pixel mapping
from Margolin's description, and we should not until someone does the work.

**Status: CONFIRMED, three sources.**

---

## Other things Margolin settles or contextualises

**The `34012` is Atari silicon wearing a TI name.** The schematic calls it a
Texas Instruments part; Margolin explains it is a gate array built by Atari
engineer **Don Paauw** and deliberately named to look like a TI device, so that
pirates ordering it would be told no such part exists. Our hardware contract
treats `34012` as a pixel processor/expander in the modelled device set; this is
where that name comes from.

**The credit screen is one switch press away.** Margolin: press **Abort** during
Attract Mode and the credit screen appears. That is how the S.T.U.N. Runner
credits in [`../CREDITS.md`](../CREDITS.md) were found to be reachable in-game —
though we found them in the ROM first. Our service-mode work has been pressing
different switches; this is a lead worth trying.

**ROM part numbers encode version and region.** Atari's scheme is
`136xxx-<version><region><nn>`; our set is **`136070`-xxxx**, e.g.
`136070-0303.210s`. Margolin documents the scheme in detail for the `harddriv`
family. This is useful for reasoning about our "rev 6" label without guessing.

**Atari Games did not survive as Atari Games.** Margolin, writing in 2002:
*"Atari Games no longer exists except as a game development group for home games.
It isn't even called Atari Games; it's called Midway Games West."* His Race
Drivin' cabinet went to the Strong National Museum of Play. Relevant to anyone
looking for surviving physical boards.

**He is generous with credit.** Margolin's page names his Hard Drivin'/Race
Drivin' team — Rick Moncrief, Max Behensky, Stephanie Mott, Jed Margolin, Erik
Durfey — and notes *"We also listed all the other people at Atari Games who
helped us, which was about half the company."* Four of those five also appear in
the S.T.U.N. Runner credits.

**Doug Milliken's role.** Margolin reveals that the "Test Driver" credit on that
team was Doug Milliken, the car-modelling physicist who consulted with Max
Behensky on the vehicle model. A nice illustration of the kind of thing this page
carries that a credit screen cannot.

---

## Schematics worth pulling

From Margolin's site, if a question needs them:

| Document | When it helps |
|---|---|
| [HardDrivinSchem.pdf](https://jmargolin.com/schem/HardDrivinSchem.pdf) | full main board, 11×17, already in that format |
| [gspms.pdf](https://jmargolin.com/schem/gspms.pdf) | **MultiSync GSP section — the board we are on** |
| [mspms.pdf](https://jmargolin.com/schem/mspms.pdf) | the MSP we do *not* have |
| [gspmain.pdf](https://jmargolin.com/schem/gspmain.pdf) | full-resolution (non-MultiSync) GSP, for the contrast |
| [HD_Sound.pdf](https://jmargolin.com/schem/HD_Sound.pdf) | the JSA II sound board |
| [Hard Drivin' Manual TM-327](https://jmargolin.com/schem/HD_TM-327_3rd.pdf) | operator manual, mirroring our SP-338 |

Not yet pulled, and not yet used for anything: the ADSP board, DSK board and
motor amp. Our ADSP-2100 is on the main board, not the ADSP board, so that PDF
is unlikely to help.

---

## What remains open

- **UNVERIFIED** that the `harddriv` schematics' part numbers and net names match
  our board's. We have not cross-walked them. SP-338 is our authority.
- **UNVERIFIED** the 8-pixels-per-word mapping as applied to our address space.
  Suggested by Margolin's prose, not derived from it.
- **NOT ATTEMPTED** the `Abort`-switch credit screen in MAME. Worth one bounded
  experiment.
- **UNKNOWN** whether S.T.U.N. Runner ran a modified Main Board. Margolin never
  mentions our title, and our MAME model treats it as stock MultiSync-nomsp.
  Nothing in SP-338 or our traces suggests otherwise, but absence of evidence
  from a source that never discussed the game is weak.

## Sources

- **S3** — [Jed Margolin, *Schematics For Hard Drivin'/Race Drivin'*](https://jmargolin.com/schem/schems.htm),
  accessed 2026-10-14. Primary: written by the hardware engineer.
- **S2** — [Killer List of Videogames — S.T.U.N. Runner](https://www.arcade-history.com/game/2289/s.t.u.n.-runner)
- **S1** — our own ROM (`tools/build-maincpu-image`)
- **S6** — [SP-338 / TM-238 operator manual](https://www.arcade-museum.com/Videogame/s-t-u-n-runner),
  local copy at `reference/manuals/stunrunner.pdf`

Full register in [`../docs/sources.md`](../docs/sources.md).