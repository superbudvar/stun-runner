# Service-F2 display-list trace

## Provenance

The bounded run used the scripted `tools/mame-display-list-trace` harness with
MAME `/mnt/build/mame-ref/mame`, ROMPATH `roms`, `--frames 180`, capture window
frames 60–180, `--input service_f2`, `--max-calls 32`, `--max-fifo-words 8192`,
and `--raw-bytes 128`. The MAME log SHA-256 is
`5895d7ee8a7a52137bd6f038e80c9831dfd91eccffbdd57227260829de41842c`.
The preserved result is
`reference/experiments/stunrun/display-list-service-f2-60-180/result.json`;
its SHA-256 is
`69ed7854de2bdd572565e3c1c3f1a7d2cc1408743767fbaec79e1ab63d9b3ed6`.

The new `service_f2` selector presses the driver's logical `Service Mode` field
at frame 60 and releases it at frame 120. This is deliberately a field-level
input, not a raw port write.

## Observations

The run captured seven calls to the `$225E4` display-list compiler, with caller
PCs:

```text
$02D7D6  frame 95
$02D780  frame 98
$02D792  frame 99
$02D7A4  frame 101
$02D7B6  frame 101
$02D7F2  frame 101
$02DB98  frame 145
```

The decoder observed `copy`, `fill`, `table-fill`, `table-run`, and `end-40`
families. The largest first capture hit the configured 8192-word limit, and
the result marks those captures as truncated. Therefore this is evidence of
the producer/caller path and its command-family vocabulary, not a complete
record dump or proof that the records are geometry.

GSP consumer events were not observed in the same run, and the GSP write tap
was empty. GSP state samples briefly showed nonzero PCs around frames 132–170,
then returned to `PC=0`, `ST=0x10` by frame 171. This confirms that the service
selector reaches the MC68010 producer path and gives a useful timing window,
but does not establish sustained GSP consumption or field semantics.

## Follow-up

Repeat with a larger bounded FIFO limit and a consumer-specific tap once the
GSP execution/write observation path is fixed. Keep the seven caller PCs as
the candidate correlation set; do not route them into the OpenGL primitive
renderer until a same-frame consumer record is captured.

The same selector was then run to frame 600 with capture through frame 300,
`--max-fifo-words 65536`, and `--raw-bytes 256`. It reproduced the same seven
caller PCs and the same command-family set (`copy`, `fill`, `table-fill`,
`table-run`, `end-40`), with aggregate family counts 45, 130, 140, 86, and 2
respectively. The FIFO was still truncated, while no GSP execution or write
events were captured. The preserved log SHA-256 is
`581bace8fcd6bd7c907620b6d66e5284ba6700f969875f54b31aac76ff8c077c` and the
result is under
`reference/experiments/stunrun/display-list-service-f2-60-300/`.
