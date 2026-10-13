# S.T.U.N. Runner Reverse Engineering Project

![project logo](promo/logo-small.png)

Experimental, evidence-driven reconstruction of Atari Games' **S.T.U.N. Runner** using a three-agent workflow: Investigator, Implementer, and Verifier.

The project aims to incrementally produce both a reproduction build for the original/emulated hardware environment and a native Linux port, with MAME execution serving as the behavioral reference.

See `PROCESS.md` for the workflow and `PROJECT.md` / `STATUS.md` / `MILESTONES.md` for current project state.

## Quick start

With the local STUN Runner ROM chips in `roms/stunrun/`, build and launch the
live native comparison windows from the project root:

```sh
./build.sh
./run.sh
```

`run.sh` also builds on first use. Set `STUNRUN_STUNRUN_DIR` for a different
ROM directory, or `STUNRUN_FRAMES=120 ./run.sh` for a bounded run. The default
live SDL/OpenGL mode opens the CPU reference and OpenGL scanout in separate
windows.

## Promo kit

Watch the [sizzle reel](promo/sizzle-reel.mp4) (21 s), check the
[gameplay screenshots](promo/screenshots/), or spin the procedurally
modeled player craft:

![craft turntable](promo/craft3d/craft-turntable.gif)

All shareable art lives in [`promo/`](promo/) — logo, poster, social
card, wallpaper, sticker, ASCII banner, and the 3D turntable sources.
`promo/FACTS.md` is the one-page hype sheet.
