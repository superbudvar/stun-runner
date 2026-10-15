# Sources

Every external claim in this repository should be traceable to a numbered source
here. This file is the register; the numbers (S1, S2, …) are what documents
cite.

## The rule

1. **Tag every external claim.** A sentence about what the hardware, the manual,
   or the original team did carries `[S#]`. A sentence about what we measured
   carries `[ROM]` and is reproducible from a tool.
2. **Prefer the primary source.** For what the game *says*, the ROM wins. For
   what the board *is*, the manual wins. For what the emulator *does*, the
   pinned MAME source wins.
3. **A source is not evidence until it has been read.** Cite the document, not
   the topic. "Per Wikipedia" is not a citation; "per the KLOV staff listing for
   this title" is.
4. **Name what is inference.** Where a source is being interpreted, say so at the
   interpretation site.
5. **Record access dates** for anything on the open web, and re-check
   volatile claims before relying on them.
6. **A wrong citation is worse than none**, because it launders a guess into
   something that looks verified. If you cannot source it, mark it `UNKNOWN`.

## Register

| # | Source | Tier | What it is authoritative for | Where it lives |
|---|---|---|---|---|
| **S1** | `:mainpcb:maincpu` program ROM, `stunrun` rev 6 | primary | What the game says: strings, credit table, addresses, reported landmarks | local, `roms/stunrun/` (gitignored); assemble with `tools/build-maincpu-image` |
| **S2** | [Killer List of Videogames — S.T.U.N. Runner](https://www.arcade-history.com/game/2289/s.t.u.n.-runner) | secondary | Real job titles behind the in-game pun credits; software revisions and build dates | web, accessed 2026-10-14 |
| **S3** | [Jed Margolin — Hard Drivin'/Race Drivin' schematics and notes](https://jmargolin.com/schem/schems.htm) | primary (his own hardware) | `harddriv` board lineage, VRAM organisation, MSP meaning, `34012`, credit-screen access | web, accessed 2026-10-14 |
| **S4** | [Wikipedia — S.T.U.N. Runner](https://en.wikipedia.org/wiki/S.T.U.N._Runner) | tertiary | Designer/programmer infobox, release dates, regional publishers | web, accessed 2026-10-14 |
| **S5** | [The Strong National Museum of Play](https://www.museumofplay.org/) | tertiary | Preservation context for surviving cabinets | web |
| **S6** | SP-338 / TM-238 S.T.U.N. Runner operator manual, 1st printing 1989 | primary (vendor) | The board: memory map, `SW1` word, VRAM array, board identifiers | `reference/manuals/stunrunner.pdf` |
| **S7** | MAME `src/mame/atari/harddriv.cpp` at `/mnt/build/mame-ref`, build 0.289 | primary (emulator) | What the emulator does: machine config, device tags, address maps | `/mnt/build/mame-ref/` (outside this repo) |
| **S8** | MAME TMS34010 device implementation | primary (emulator) | Host-port semantics, register behaviour | `/mnt/build/mame-ref/src/devices/cpu/tms34010/` |

### Tiers, and what each one outranks

- **primary — the artifact itself.** S1, S6, S7, S8. These are the thing being
  studied or the thing doing the studying. When S6 and S7 disagree about the
  hardware, the manual wins and the driver has a bug.
- **primary — the maker's own account.** S3. Authoritative about what its author
  built and saw. It is about Hard Drivin'/Race Drivin', so it does not transfer
  to our board automatically; every transfer has to be argued.
- **secondary.** S2. Useful, and the only source for some facts (the job titles
  behind the pun credits), but a compiled record rather than a document.
- **tertiary.** S4, S5. Orientation and context only. Never the last source a
  claim rests on.

## Where each tier is already used

| Document | Uses |
|---|---|
| `CREDITS.md` | S1 for the credits table; S2/S4 for job titles; S3 for the team |
| `analysis/hardware-lineage.md` | S3 against S1/S6/S7 for the VRAM result |
| `docs/hardware-contract.md` | S6 `[MANUAL]`, S7 `[MAME]`, S1 `[ROM]`, S3 `[MARGOLIN]` |
| `docs/mame-machine-reference.md` | S7 `[MAME]` + S8 throughout; S6 and S1 for the cross-checks |
| `docs/mc68010-boot-findings.md` | S1 `[ROM]`, S7 `[MAME]`, S6 `[MANUAL]` |
| `analysis/rom-text-strings.md` | S1 throughout |

## Adding a source

Add a row, give it the next `S#`, and cite it by number. If it is a document we
hold locally, say where the local copy is and whether it is committed — ROMs,
NVRAM and snapshots stay out of this repository, so a source that only exists on
one machine has to be marked as such.