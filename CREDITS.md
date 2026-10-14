# Credits

S.T.U.N. Runner was made by 25 people at Atari Games in 1989. This file records
who they were, from the strongest source available, and where every claim comes
from.

The credits are not a scrap of a flyer or a wiki infobox. **The game ships its
own credits table in its program ROM**, and that table is the primary source
here. Everything else in this file is secondary and is labelled as such.

---

## The credits, as the ROM states them

The credits live in the `:mainpcb:maincpu` region, whose SHA-1 is
`75b721654691d5589c3a1d2df72340b657aa6917`. Region offset equals CPU address.

```sh
tools/build-maincpu-image -o /tmp/maincpu.bin
# credits block is 0x046BFA .. 0x046F87
```

One caveat, because it will otherwise look like a bug: that tool emits the
**768 KiB populated** image (6 × 128 KiB banks), not the 1 MiB region, so its
SHA-1 will not equal the one above. The region's last 0x40000 bytes are
unpopulated zeros. Every address below is under `0xC0000`, so both artifacts
answer identically.

The ROM stores them as 25 NUL-terminated records. Each is a 21-character role
field — `ROLE:` then spaces out to a fixed column — followed by the person's
person's name:

```text
0x046BFA  "STUN RUNNER TEAM"   header, then 0x00 0x00
0x046C0C  "COMMISSIONER:        Ed Rotberg"
...
0x046F3A  "THE ANCIENT ONE:     Rick Moncrief"
0x046F5E  "Copyright 1989 ATARI GAMES"
0x046F7A  "Credits: %s %s"      format string for the HUD line
```

The in-game roles are puns. They were not the job titles.

| In-game role (verbatim) | Name (verbatim) | Actual job | Role source |
|---|---|---|---|
| COMMISSIONER | **Ed Rotberg** | Design | ROM + KLOV |
| TRANSIT AUTHORITY | **Andrew Burgess** | Programmer | ROM + KLOV |
| TUNNEL ARCHITECT | **Sam Comstock** | Lead artist | ROM + KLOV |
| VEHICLE ENGINEER | **Will Noble** | Artist | ROM + KLOV |
| CITY PLANNING | **Kris Moser** | Supporting artist | ROM + KLOV |
| COMMUNICATIONS | **Don Diekneite** | Music | ROM + KLOV |
| ACOUSTICS | **Brad Fuller** | Sound effects | ROM + KLOV |
| PROPULSION ENGINEER | **Jed Margolin** | Hardware design | ROM + KLOV |
| PATRIARCH | **John Ray** | Team leader | ROM + KLOV |
| PUBLIC RELATIONS | **Jerry Momoda** | Product manager | ROM + KLOV |
| BLUE SKY | **Carole Cameron** | Technical support | ROM + KLOV |
| DAMAGE CONTROL | **Glenn McNamara** | Technician | ROM + KLOV |
| PHYSICIST | **Max Behensky** | Software design | ROM + KLOV |
| OPTICS | **Stephanie Mott** | Lead software support | ROM + KLOV |
| AUXILIARY SYSTEMS | **Gary Stempler** | Hardware engineer | ROM + KLOV |
| MATHEMATICS | **Jim Morris** | Math software | ROM + KLOV |
| CHIEF MECHANIC | **Milt Loper** | Mechanical design | ROM + KLOV |
| BACKHOE OPERATOR | **Erik Durfey** | Design support | ROM + KLOV |
| OVERLORD | **Mike Hally** | Assistant design support | ROM + KLOV |
| CHAINSAW | **Ralph Perez** | Cabinet | ROM + KLOV |
| BONDO MAN | **Ken Hata** | Cabinet design | ROM + KLOV |
| XACTO BLADERUNNER | **Kyoko Dougherty** | Technical support | ROM + KLOV |
| PUB RUNNER | **Andrea Dencker** | Technical support | ROM + KLOV |
| WIDGET ARTIST | **Mary Sumner** | Management | ROM + KLOV |
| THE ANCIENT ONE | **Rick Moncrief** | Support | ROM + KLOV |

**Name and in-game role are ROM-confirmed.** **The "Actual job" column is
secondary** — it is the decoding published by the Killer List of Videogames
(KLOV, `arcade-history.com`). The decode is consistent and obviously correct
(a "COMMISSIONER" who designed the game; an "ACOUSTICS" credit that is sound
effects), and no entry contradicts the ROM, but it is not in the ROM. Treat it
as a well-attested reading, not as evidence.

The three names a reader is most likely to know — Ed Rotberg, Sam Comstock and
Jed Margolin — are exactly the ones the ROM leads with, and they are the ones
the secondary sources single out.

## How to see the credits yourself

Per Jed Margolin, the credit screen is reached by **pressing the `Abort` switch
while the game is in Attract Mode**. Margolin describes this for Hard Drivin'/
Race Drivin'; it follows that the same attract-mode convention applies here, and
the credits table is in this ROM — but we have not run that specific
interaction, so treat the control detail as secondary until someone does.

## Who is who

Short notes on the people, so the table above is more than surnames. **Only
claims I could source are here**; anything I could not verify is left out rather
than filled in.

- **Jed Margolin** — hardware design. Atari Games engineer on the `harddriv`
  board family, and the author of the schematics and design notes this project
  leans on. He names himself "Engineer" on the Hard Drivin'/Race Drivin' team.
  See [`analysis/hardware-lineage.md`](analysis/hardware-lineage.md).
- **Rick Moncrief** — Margolin names him **Project Leader** for Hard Drivin'/
  Race Drivin'. Here he is credited "THE ANCIENT ONE" / support.
- **Max Behensky** and **Stephanie Mott** — Margolin names both as
  **Programmers** on that same team. Here: "PHYSICIST" (software design) and
  "OPTICS" (lead software support).
- **Erik Durfey** — Margolin names him **Technician** on that team; Margolin
  separately credits him with "the moving dashboard contributed by Erik Durfey".
  Here: "BACKHOE OPERATOR" / design support.
- **Ed Rotberg** — designer of S.T.U.N. Runner (Wikipedia infobox). Note that
  *Gauntlet* was designed by Ed **Logg**; Rotberg is a different person, and
  conflating the two is an easy and wrong guess.
- **Andrew Burgess** — programmer of S.T.U.N. Runner (Wikipedia infobox), and
  the only name the two independent sources agree on without decoding.
- **Sam Comstock**, **Will Noble**, **Ken Hata** and **Brad Fuller** — all four
  appear on *Gauntlet* (1985) as well, as video graphics, video graphics,
  cabinet designer and sound respectively. So the art, cabinet and audio
  continuity between the two Atari Games titles is documented, not assumed.
- **Jerry Momoda** — product manager at Atari Games from 1987. In his own
  account he lists Stun Runner among the Atari hits he launched, alongside
  Cyberball and Pit Fighter. Credited here as "PUBLIC RELATIONS".
- **Carole Cameron** — KLOV notes the game was "originally based upon an idea
  that Carol Cameron submitted", which is consistent with her being credited at
  all. Credited here as "BLUE SKY" / technical support.

The **"Actual job" column in the table above is the weakest evidence in this
file.** It is a published decoding, not a ROM fact. Where this file says a job
title and Margolin independently names someone in a different job, trust
Margolin: he was on the team.

## Other people who made the hardware possible

Two names belong here because they are not Atari staff but the reason the
emulation can be checked against something other than guesswork:

- **Jed Margolin** designed the graphics hardware of the `harddriv` family and
  later published the schematics and engineering notes.
- **Don Paauw** was the Atari engineer who put the `34012` pixel expander into a
  gate array, per Margolin.

## Third-party components we build on

Our native target links code we did not write. Licences read from the vendored
copies in `third_party/`, not from memory:

| Component | Role | Licence | Read from |
|---|---|---|---|
| [Musashi](https://github.com/kstenerud/Musashi) (`third_party/musashi`) | MC68010 core | MIT-style ("use, copy, modify, …"), Karl Stenerud | `third_party/musashi/readme.txt` |
| [ymfm](https://github.com/blarggs/ymfm) (`third_party/ymfm`) | YM2151 synthesis | BSD 3-Clause, Aaron Giles | `third_party/ymfm/LICENSE` |
| [stb](https://github.com/nothings/stb) (`third_party/stb`) | image read/write | public domain | headers of both files |
| [TMS34010_sv](https://github.com/birdybro/TMS34010_sv) (`third_party/tms34010_sv`) | TMS34010 SystemVerilog core, pinned at `94a258e` | MIT | `third_party/tms34010_sv/LICENSE` |

## What this project is

An independent reverse-engineering and reimplementation effort. It is not
affiliated with, endorsed by, or connected to Atari Games, Atari Corporation,
Midway, or any of the people named above. "S.T.U.N. Runner" and the game itself
remain the property of their rights holders.

---

## Sources

Every external claim in this file is traceable to one of these. Access dates are
2026-10-14. Full register and policy in [`docs/sources.md`](docs/sources.md).

| # | Source | Used for |
|---|---|---|
| S1 | This repository's ROM — `:mainpcb:maincpu`, extracted by `tools/build-maincpu-image` | names, in-game roles, offsets, `Copyright 1989 ATARI GAMES` |
| S2 | [Killer List of Videogames — S.T.U.N. Runner](https://www.arcade-history.com/game/2289/s.t.u.n.-runner) | real job titles behind each pun; software revision/build dates; "idea submitted by Carole Cameron" |
| S3 | [Jed Margolin, Hard Drivin'/Race Drivin' schematics and design notes](https://jmargolin.com/schem/schems.htm) | `Abort`-switch credit screen; `harddriv` board lineage; VRAM organisation; Atari Games → Midway Games West |
| S4 | [Wikipedia — S.T.U.N. Runner](https://en.wikipedia.org/wiki/S.T.U.N._Runner) | designer/programmer infobox; release dates and regional publishers |
| S5 | [The Strong National Museum of Play — arcade preservation](https://www.museumofplay.org/) | preservation context (Margolin's Race Drivin' cabinet) |