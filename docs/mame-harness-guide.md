# MAME Oracle Harness Guide

**MAME binary**: `~/projects/decomp/stun-runner/mame-ref/mame`
**MAME version**: MAME v0.289 (from `mame -help`)
**OSD**: SDL2 (confirmed by SDL function symbols in binary — not SDL3)
**Driver**: `stunrun` (from `scripts/target/mame/stunrun.lua` — builds against `src/mame/atari/harddriv.cpp`)

---

## 1. Invoking MAME with Lua Scripts

### `-autoboot_script` (runs at machine startup, before reset)

```bash
./mame stunrun -autoboot_script /path/to/script.lua
```

The script is loaded and executed **before** the machine is reset. Use `emu.register_prestart()` to set up inputs, then `emu.register_frame_done()` for per-frame actions. This is the approach used by all the project's capture scripts.

### `-console` (interactive/repl mode)

```bash
./mame stunrun -console
```

Opens the MAME console plugin and an interactive Lua REPL. Scripts can be pasted or loaded with `dofile()`. The `-console` flag alone does **not** auto-run scripts; it just enables the interactive console. For headless automation, `-autoboot_script` is preferred.

### Inline `-autoprofile` / plugin loading

```bash
./mame stunrun -plugin console -autoboot_script /path/to/script.lua
```

The `console` plugin provides the interactive REPL; `-autoboot_script` still runs its own script independently.

---

## 2. Key MAME Lua API Calls

### `screen:snapshot(path)` — Screen capture

**Signature** (from `src/frontend/mame/luaengine.cpp:1997–2024`):

```lua
screen:snapshot(filename: string?) -> nil  -- on success
screen:snapshot(filename: string?) -> error  -- on failure (returns error object)
```

**Behaviour**:
- If `filename` is `nil` or `""`, MAME auto-generates a filename in its configured snapshot directory (default: `./snapshots/`) using the pattern `姓_MAME.png`.
- If `filename` is a **relative path**, it is appended to MAME's snapshot directory.
- If `filename` is an **absolute path**, it is used as-is.
- Format is **PNG** (confirmed by `machine().video().open_next(file, "png")` call path).
- The snapshot captures the **drawn frame content** — it fires inside `video_manager::save_snapshot()` which is called after the frame is rendered, so the output reflects the completed frame.
- Returns `nil` on success; on error returns a sol2 error object (falsy, so `assert(screen:snapshot(...))` works).

**Example** (from `gsp-state-snapshot.lua:145–148`):

```lua
local screen = assert(machine.screens[':mainpcb:screen'], 'screen not found')
assert(screen:snapshot(screen_output) == nil, 'screen snapshot failed')
print('M1_GSP_SCREEN frame=' .. frame .. ' path=' .. screen_output)
```

**Failure modes**:
- `'screen not found'` — device tag `:mainpcb:screen` does not exist (check with `print(inspect(machine.screens))`).
- Screen snapshot failed — permission issue or disk full.

**Note**: `checkpoint.lua` explicitly notes `screen:snapshot()` requires MAME `-snap` or external capture; the current implementation does not call it. `gsp-state-snapshot.lua` uses it when `STUNRUN_GSP_STATE_SCREEN` is set.

---

### `manager.machine.screens` — Screen enumeration

```lua
manager.machine.screens  -- screen_device_enumerator
```

Iterate with `pairs()` or `next()`:

```lua
for tag, screen in pairs(manager.machine.screens) do
    print(tag, screen.width, screen.height)
end
```

For STUN Runner the screen device tag is `:mainpcb:screen`.

Properties available on each screen device:
- `width`, `height` — visible area dimensions in pixels
- `refresh` — frame rate in Hz
- `frame_number` — count of frames displayed
- `frame_period` — attotime interval per frame
- `pixel()`, `pixels()` — direct pixel read
- `time_until_vblank_start()`, `time_until_vblank_end()` — timing helpers

---

### `manager.machine.devices` — Device enumeration and lookup

```lua
manager.machine.devices  -- map<tag_string, device_t>
```

**Key STUN Runner device tags**:
- `:mainpcb:maincpu` — MC68010 main CPU
- `:mainpcb:gsp` — Atari GSP (TMS340X0) graphics processor
- `:mainpcb:adsp` — Analog Devices ADSP-2106x audio DSP
- `:mainpcb:jsa:cpu` — Z80 sound CPU (JSA)
- `:mainpcb:SW1` — Dip switch bank 1
- `:mainpcb:IN0` — Input port 0 (coins, service)
- `:mainpcb:a80000` — Input port for player controls (start, buttons)
- `:mainpcb:8BADC.0`, `:mainpcb:8BADC.2` — ADC channels for analog sticks

**Example** (from `gsp-state-snapshot.lua:4`):

```lua
local gsp = assert(machine.devices[':mainpcb:gsp'])
```

**Device sub-apis**:

| Property | Type | Example |
|---|---|---|
| `device.spaces['program']` | addr_space | `gsp.spaces['program']:read_u16(addr)` |
| `device.state['PC'].value` | u64 | `gsp.state['PC'].value` |
| `device.tag` | string | `'maincpu'` |
| `device.started` | bool | — |
| `device.memshare` | share | — |

**`device.state`** — access CPU/DSP internal registers. Available keys vary by device type; common ones for MC68010: `PC`, `SR`, `SP`, `D0`–`D7`, `A0`–`A7`. For GSP (TMS340X0): `PC`, `ST`. For ADSP-2106x: `PC`, `ASTAT`, `MSTAT`.

---

### `manager.machine.memory` — Memory subsystem

```lua
manager.machine.memory  -- memory_manager
```

**Three sub-collections**:

```lua
memory.regions   -- map<name, memory_region>   ROM blobs, RAM regions
memory.shares    -- map<name, memory_share>    shared memory buffers
memory.banks     -- map<name, memory_bank>     banked memory
```

**Reading device address spaces** (the primary mechanism used by all capture scripts):

```lua
local cpu = machine.devices[':mainpcb:maincpu']
local space = cpu.spaces['program']
local value_u8  = space:read_u8(0xff9000)
local value_u16 = space:read_u16(0xff9000)
local value_u32 = space:read_u32(0xff9000)
```

**Widths available**: `read_u8`, `read_u16`, `read_u32`, `read_i8`, `read_i16`, `read_i32`

**Writing**: `space:write_u8(addr, val)`, `space:write_u16`, `space:write_u32`, etc.

**Read a range as a binary string**:

```lua
space:read_range(first, last, width_bits, [step]) -> sol.buffer
-- width_bits must be 8, 16, 32, or 64
```

**Memory taps** (from `gsp-boundary.lua:79–81` and `adsp-window.lua:79–99`):

```lua
-- Read tap
space:install_read_tap(start_addr, end_addr, name, callback)
-- Write tap
space:install_write_tap(start_addr, end_addr, name, callback)

-- Callback signature (from luaengine_mem.cpp:270–283):
function(offset, data, mask)
    -- offset: address within the range
    -- data:   data value read/written
    -- mask:   byte mask (e.g. 0xFF for byte access)
end
```

**Tap returns a handler**; to remove: `tap:reset()` (from `luaengine.cpp:1100`, the `unsubscribe` method on notifier subscriptions).

**Reading raw memory regions** (non-device, e.g. ROM):

```lua
local rom = machine.memory.regions['rom']
rom:read_u8(offset)
rom:read_u16(offset)  -- 16-bit words
rom:size              -- size in bytes
```

---

### `emu.register_frame_done(callback)` — Per-frame hook

```lua
emu.register_frame_done(function()
    -- called once per frame after the frame is drawn to screen
end)
```

**When it fires**: Called from `lua_engine::frame_hook()` (`luaengine.cpp:2490`), which is invoked **after** `video_manager::frame_update()` renders the frame. The frame is fully drawn; all CPUs have executed for that frame.

**Return value**: A `notifier_subscription` object. Call `.reset()` to unsubscribe.

**The frame loop pattern** used by all project scripts:

```lua
local frame = 0
emu.register_frame_done(function()
    frame = frame + 1
    -- apply input events for this frame
    while next_event <= #events and events[next_event].frame == frame do
        apply_event(events[next_event])
        next_event = next_event + 1
    end
    -- capture on target frames
    if targets[frame] then capture() end
    -- exit on limit
    if frame >= limit then machine:exit() end
end)
```

**Important**: `frame` starts at 1 in the first callback invocation. Scripts that need to act at frame 1 must use `emu.register_prestart()` for pre-reset setup and then rely on `frame == 1` in the frame_done callback.

**There is no `machine:frameemitter():subscribe()`** in the MAME Lua API. The equivalent is `emu.register_frame_done`. The `machine:add_machine_frame_notifier()` (`luaengine.cpp:993`) is the C++ side notifier, but the Lua binding for `frameemitter` subscription does not exist.

---

### `emu.register_prestart(callback)` — Pre-reset setup

```lua
emu.register_prestart(function()
    -- called before machine reset
    -- safe place to set input defaults (dip switches, etc.)
end)
```

**Use case**: Set up dip switch states before the machine starts. The `checkpoint.lua` and `gsp-state-snapshot.lua` scripts use this to set all `SW1` bits to ON (1) before the machine resets.

---

### `emu.app_build()` — Version string

```lua
emu.app_build()   -- returns full build version string
-- e.g. "MAME v0.289 (unknown)"
```

Also available: `emu.app_name` (`"mame"`), `emu.app_version` (bare version `"0.289"`).

---

### Output: `print()` vs `emu.print_*()`

```lua
print('hello')                          -- goes to stdout / MAME console
emu.print_error('error message')        -- goes to osd_printf_error (red console)
emu.print_warning('warning message')    -- goes to osd_printf_warning
emu.print_info('info message')         -- goes to osd_printf_info
emu.print_verbose('verbose message')   -- goes to osd_printf_verbose
emu.print_debug('debug message')        -- goes to osd_printf_debug
```

For harness automation, `print()` is sufficient — stdout is redirected and captured by the shell harness. The `emu.print_*` family is useful to route messages to the appropriate MAME logging level.

---

## 3. Environment Variables Used by Capture Scripts

All scripts use `os.getenv()` for configuration. Variables are divided into categories:

### Global / checkpoint

| Variable | Purpose | Example |
|---|---|---|
| `STUNRUN_CHECKPOINT_FRAME` | Frame number at which to capture checkpoint | `600` |
| `STUNRUN_CHECKPOINT_INPUT` | Input sequence preset name | `drive`, `late`, `sw_off` |
| `STUNRUN_CHECKPOINT_OUTPUT_DIR` | Directory for binary dumps | `/tmp` |

### GSP state snapshot

| Variable | Purpose | Example |
|---|---|---|
| `STUNRUN_GSP_STATE_OUT` | Output base path | `/tmp/gsp` |
| `STUNRUN_GSP_STATE_FRAMES` | Max frames to run | `1800` |
| `STUNRUN_GSP_STATE_TARGETS` | Comma-separated capture frame numbers | `600,1800` |
| `STUNRUN_GSP_STATE_INPUT` | Input preset | `drive`, `late_drive` |
| `STUNRUN_GSP_STATE_DETAIL` | Enable verbose control-region dump | `1` |
| `STUNRUN_GSP_STATE_SCREEN` | Screen snapshot output path | `/tmp/gsp_screen.png` |

### Main CPU state snapshot

| Variable | Purpose | Example |
|---|---|---|
| `STUNRUN_MAIN_STATE_OUT` | Output base path | `/tmp/main` |
| `STUNRUN_MAIN_STATE_FRAMES` | Max frames | `1800` |
| `STUNRUN_MAIN_STATE_TARGETS` | Comma-separated capture frames | `600,750,900,1200,1800` |
| `STUNRUN_MAIN_STATE_INPUT` | Input preset | `late`, `late_drive` |

### Memory snapshot

| Variable | Purpose | Example |
|---|---|---|
| `STUNRUN_MEMORY_DEVICE` | Device tag | `:mainpcb:maincpu` |
| `STUNRUN_MEMORY_SPACE` | Space name | `program` |
| `STUNRUN_MEMORY_BASE` | Base address (hex) | `0xFF8000` |
| `STUNRUN_MEMORY_COUNT` | Element count | `524288` |
| `STUNRUN_MEMORY_WIDTH` | Bit width per element | `8`, `16`, `32` |
| `STUNRUN_MEMORY_STRIDE` | Address stride per element | `1` |
| `STUNRUN_MEMORY_FRAMES` | Max frames | `600` |
| `STUNRUN_MEMORY_TARGETS` | Comma-separated capture frames | `600` |
| `STUNRUN_MEMORY_OUT` | Output directory | `/tmp/mem` |
| `STUNRUN_MEMORY_INPUT` | Input preset | `fork_hold_left` |
| `STUNRUN_MEMORY_PLAYBACK` | Input playback file (empty = scripted) | `""` |
| `STUNRUN_MEMORY_SAVE_STATE` | MAME save state path after capture | `/tmp/state.sta` |
| `STUNRUN_MEMORY_LOAD_STATE` | MAME save state to load at frame 0 | `/tmp/state.sta` |

### ADSP program snapshot / dump

| Variable | Purpose | Example |
|---|---|---|
| `STUNRUN_ADSP_SNAPSHOT_FRAMES` | Max frames | `600` |
| `STUNRUN_ADSP_SNAPSHOT_TARGETS` | Capture frame numbers | `1,10,122,136,600` |
| `STUNRUN_ADSP_DUMP_FRAMES` | Max frames | `136` |
| `STUNRUN_ADSP_DUMP_TARGET` | Dump target frame | `136` |

### ADSP window (watchpoint)

| Variable | Purpose | Example |
|---|---|---|
| `STUNRUN_ADSP_WINDOW_OUT` | Output path | `/tmp/adsp_win` |
| `STUNRUN_ADSP_WINDOW_FRAMES` | Max frames | `600` |
| `STUNRUN_ADSP_WINDOW_BASE` | Watchpoint start address | `0x808000` |
| `STUNRUN_ADSP_WINDOW_LENGTH` | Watchpoint length | `0x4000` |
| `STUNRUN_ADSP_WINDOW_LABEL` | Label for output lines | `data_ram` |
| `STUNRUN_ADSP_WINDOW_INPUT` | Input preset | `late` |
| `STUNRUN_ADSP_WINDOW_ACCESS` | `read` or `write` | `write` |

### GSP boundary (FIFO tap)

| Variable | Purpose | Example |
|---|---|---|
| `STUNRUN_GSP_BOUNDARY_OUT` | Output path | `/tmp/gsp_boundary` |
| `STUNRUN_GSP_BOUNDARY_FRAMES` | Max frames | `600` |
| `STUNRUN_GSP_BOUNDARY_INPUT` | Input preset | `drive` |
| `STUNRUN_GSP_BOUNDARY_CAPTURE_START` | Frame to start recording FIFO writes | `132` |

### Display-list producer trace

| Variable | Purpose | Example |
|---|---|---|
| `STUNRUN_GSP_DL_TRACE_OUT` | Output path | `/tmp/gsp_dl_trace` |
| `STUNRUN_GSP_DL_TRACE_FRAMES` | Max frames | `1800` |
| `STUNRUN_GSP_DL_TRACE_START` / `_END` | Inclusive capture window | `500` / `1800` |
| `STUNRUN_GSP_DL_TRACE_INPUT` | Input preset (`none`, `drive`, `late_drive`, or `service_f2`) | `drive` |
| `STUNRUN_GSP_DL_TRACE_MAX_CALLS` | Maximum `0x225E4` entries | `256` |
| `STUNRUN_GSP_DL_TRACE_RAW_BYTES` | Bounded stream snapshot per call | `1024` |
| `STUNRUN_GSP_DL_TRACE_MAX_FIFO` | Maximum attributed FIFO words | `16384` |

### Human-play capture

| Variable | Purpose | Example |
|---|---|---|
| `STUNRUN_PLAY_OUT` | Output directory | `/tmp/play` |
| `STUNRUN_PLAY_EVERY` | Snapshot cadence in frames | `600` |
| `STUNRUN_PLAY_FRAMES` | Auto-exit frame (0 = run until closed) | `21600` |
| `STUNRUN_PLAY_STATES` | `1` = save MAME states, `0` = snapshots only | `1` |

---

## 4. Input Event Model

All scripts share the same input event structure:

```lua
{ frame = N, port = ':mainpcb:IN0', field = 'Coin 1', action = 'press' | 'release' | 'set', value = V }
```

- `press` → `field:set_value(1)` then `field:set_value(0)` on the next frame's release event
- `release` → `field:set_value(0)`
- `set` → `field:set_value(value)` directly (used for analog/stick values)

**Applying events** (from `checkpoint.lua:100–104`):

```lua
local function apply_event(event)
    local port = assert(machine.ioport.ports[event.port], 'unknown input port: ' .. event.port)
    local field = assert(port.fields[event.field], 'unknown input field: ' .. event.port .. '/' .. event.field)
    field:set_value(event.action == 'press' and 1 or event.action == 'release' and 0 or event.value)
end
```

**Input presets** (common across scripts):
- `none` — no scripted input
- `drive` — coin at frame 120, start at frame 300, button+stick at frame 600
- `late` — coin at frame 650, start at frame 750 (game already in progress)
- `late_drive` — like `late` but with active driving input
- `sw_off` — all SW1 dip switches OFF before coin
- `sw_off_prestart` — SW1 OFF and coin+start at 120/300 (pre-reset dip switch state)
- `fork_hold_left`, `fork_hold_right`, `fork_center` — joystick held direction
- `fork_delayed_left`, `fork_delayed_right` — direction change at frame 80
- `course_sweep`, `course_coin2`, `course_preface_left`, `course_preface_right` — course-hunting sequences
- `weapon_probe`, `collision_probe_*`, `damage_probe_sweep` — gameplay probes
- `service_probe` — enters service mode at frame 100
- `service_f2` — presses the logical `Service Mode` field at frame 60 and
  releases it at frame 120; used by the display-list trace to capture the
  service transition without raw-port writes

---

## 5. The MAME → Native Harness Handoff

Each script produces different artifacts:

### `checkpoint.lua` — Binary memory dumps + JSON summary

**Output files** (written to `$STUNRUN_CHECKPOINT_OUTPUT_DIR`, default `/tmp`):

| File | Size | Content |
|---|---|---|
| `maincpu_ram.bin` | 64 KB | MC68010 RAM `0x000000–0x00FFFF` |
| `gsp_vram.bin` | 1 MB | GSP VRAM via bus-mapped address `0x02000000` |
| `gsp_palette.bin` | 1 KB | GSP palette RAM `0xF5000000–0xF50003FF` (256 × 4-byte entries) |
| `adsp_data.bin` | 16 KB | ADSP data memory `0x808000–0x80BFFF` |

**JSON summary** printed to stdout:

```
MAME_CHECKPOINT_JSON {"schema":"stunrun-checkpoint/v1","system":"stunrun","frame":N,"processors":{"maincpu":{"pc":...,"sr":...},"gsp":{"pc":...,"st":...},"adsp":{"pc":...},"soundcpu":{"pc":...}},"regions":{"adsp_program":{...}}}
```

### `gsp-state-snapshot.lua` — Structured GSP state prints

**Output**: Printed to stdout, no files (unless `STUNRUN_GSP_STATE_SCREEN` is set):

```
M1_GSP_STATE frame=N pc=XXXXXXXX st=XXXXXXXX lo_sum=N hi_sum=N low_sum=N high_sum=N
M1_GSP_DISPLAY frame=N dpyadr=... dpytap=... dpystart=... heblnk=... hsblnk=... dpyctl=... fine_scroll=...
M1_GSP_CONTROL frame=N lo=... hi=...    (only if STUNRUN_GSP_STATE_DETAIL=1)
M1_GSP_SCREEN frame=N path=/path/to/screenshot.png  (only if screen snapshot requested)
```

### `memory-snapshot.lua` — Per-frame JSON snapshots

**Output files** (written to `$STUNRUN_MEMORY_OUT`):

```
snapshot-N.json   -- one per target frame
```

JSON schema `stunrun-memory-snapshot/v1`:
```json
{"schema":"stunrun-memory-snapshot/v1","frame":N,"device":":mainpcb:maincpu","space":"program",
 "base":0,"count":524288,"width":8,"nonzero":N,"values":[...]}
```

Also prints: `MAME_MEMORY_INPUT`, `MAME_MEMORY_SNAPSHOT frame=N path=... nonzero=N`, `MAME_MEMORY_STATE_SAVE path=...`, `MAME_MEMORY_SNAPSHOT_DONE`.

### `play-capture.lua` — Human-play memory + state + course log

**Output files** (written to `$STUNRUN_PLAY_OUT`):

| File | Content |
|---|---|
| `course-log.txt` | `frame course score_lo` per change |
| `course-writes.txt` | All writes to course word with PC and data |
| `snapshot-N.json` | Full work-RAM dump every `$STUNRUN_PLAY_EVERY` frames |
| `state-N.sta` | MAME save state every `$STUNRUN_PLAY_EVERY` frames (if enabled) |

**Key stdout lines**: `MAME_PLAY_START`, `MAME_PLAY_CHANGE frame=N course=N (was M) score=N (was O)`, `MAME_PLAY_SNAPSHOT`, `MAME_PLAY_STATE_SAVE`, `MAME_PLAY_DONE`.

### `adsp-window.lua` — Watchpoint trace

**Output**: Printed to stdout only:

```
M1_ADSP_BLOCK frame=N sequence=N count=N terminator=XXXX fifo_writes=N
M1_ADSP_READ kind=data_ram pc=XXXXXXXX addr=XXXXXXXX data=XXXXXXXX mask=XXXXXXXX
M1_ADSP_WRITE kind=data_ram pc=XXXXXXXX addr=XXXXXXXX data=XXXXXXXX
M1_ADSP_SOURCE pc=XXXXXXXX addr=XXXXXXXX data=XXXXXXXX mask=XXXXXXXX
M1_ADSP_WINDOW_DONE
```

### `gsp-boundary.lua` — GSP FIFO write trace

**Output**: Printed to stdout only:

```
M1_GSP_WRITE_FIRST frame=N pc=XXXXXXXX addr=XXXXXXXX data=XXXXXXXX mask=XXXXXXXX post_init=true|false
M1_GSP_COMMAND_FIRST ...
M1_GSP_BOUNDARY_DONE
```

### `display-list-trace.lua` — display-list producer and group trace

The probe taps instruction fetch at `0x225E4`. It reads the return address from
`(A7)`, classifies the preceding JSR/BSR encoding to recover its true size and
call site, and reads the stream pointer from `4(A7)`. This matters because live
code uses both six-byte `jsr $225e4.l` and two-byte `jsr (A2)`. Entry fetches are
only candidates: a watchdog write with compiler PC `0x22608` confirms the
opcode loop executed. Duplicate prefetches are excluded, as are nonzero-header
early exits which never enter that loop. Taps are installed at frame 10.
FIFO writes are
attributed only while the write PC is inside `0x225E4–0x22748`.
`tools/mame-display-list-trace` converts the
bounded raw snapshot into group records (`copy`, `table-run`, `fill`,
`table-fill`, table reload, or terminator) and preserves every operand as hex.

**Output files**: `mame.log` (raw oracle lines) and `result.json` (structured
calls, producer counts, group boundaries, operands, host address/control at the
first watchdog write, and actual FIFO words with their pre-write destination
latches). `small-upload.bin` contains big-endian oracle words from the shortest
complete count-matched call. `small-upload.pgm` is a **byte-intensity preview**:
two columns (high/low byte), one FIFO word per row. It is not a decoded game
bitmap: palette, pixel format, stride and destination semantics remain unknown.
No export is made if no complete count-matched call is available.

Each call also contains `raw_fifo_words` and `raw_fifo_hex`, preserving the
captured 16-bit FIFO payload without assigning a format. The trace installs a
bounded execute tap at the observed GSP consumer selector `0xFFF48C20` and
exports `consumer_events` plus `consumer_capture_status`. A same-frame event is
only execution evidence; it does **not** prove FIFO ownership or field
semantics. If the GSP selector is unavailable, the status remains `unknown`.
The result records the configured call/FIFO/consumer limits and bounded
artifact names.

```
M1_GSP_DL_CALL id=N frame=N return=XXXXXXXX caller=XXXXXXXX call_size=N call_opcode=XXXX stream=XXXXXXXX raw=...
M1_GSP_DL_HOST id=N hi=XXXX lo=XXXX control=XXXX
M1_GSP_DL_FIFO id=N frame=N pc=XXXXXXXX addr=XXXXXXXX data=XXXXXXXX mask=XXXXXXXX destination=XXXXXXXX
M1_GSP_DL_CONSUMER frame=N pc=FFF48C20
M1_GSP_DL_CONSUMER_STATUS status=observed|unavailable
M1_GSP_DL_DONE frames=N calls=N fifo_words=N calls_truncated=false fifo_truncated=false ...
```

For the complete bounded startup capture (run from repository root):

```bash
stun-runner/tools/mame-display-list-trace roms \
  /tmp/opencode/stunrun-display-list-confirmed-full \
  --mame mame-ref/mame --frames 180 --end-frame 180 --input drive \
  --raw-bytes 65536 --max-fifo-words 200000
```

---

## 6. Common Failure Modes

| Error | Cause | Fix |
|---|---|---|
| `unknown input port: :mainpcb:IN0` | Port name wrong; check with MAME debugger (`info dumps`) | Use exact device tags from `machine.ioport.ports` |
| `unknown input field: Coin 1` | Field name wrong; check `port.fields` table | List available fields with `for k,v in pairs(port.fields) do print(k) end` |
| `screen snapshot failed` | `screen_output` path not writable; `screen:snapshot()` returned error | Check `assert(screen:snapshot...)` result; use absolute path |
| `device not found: :mainpcb:gsp` | Tag path wrong; device not present on this board variant | Verify with `for k,v in pairs(machine.devices) do print(k) end` |
| `screen not found: :mainpcb:screen` | Screen device tag wrong | Enumerate with `for tag,screen in pairs(machine.screens) do print(tag) end` |
| `unknown space: program` | Device has no program space (not a CPU/DSP) | Check `device.spaces` table |
| `width must be 8, 16, or 32` | `STUNRUN_MEMORY_WIDTH` set to invalid value | Must be 8, 16, or 32 |
| Save/load state fails | Path permission or format issue | Use absolute paths; `.sta` extension convention |
| Script runs but no output | `frame` never reaches target; input preset not triggering | Add `print('frame=' .. frame)` in frame loop; verify `limit` and `targets` |

---

## 7. Exact Command Lines for Oracle Capture

### Checkpoint capture (title/reset state)

```bash
STUNRUN_CHECKPOINT_FRAME=600 \
STUNRUN_CHECKPOINT_INPUT=drive \
STUNRUN_CHECKPOINT_OUTPUT_DIR=/tmp/checkpoint \
  ~/projects/decomp/stun-runner/mame-ref/mame stunrun \
  -autoboot_script ~/projects/decomp/stun-runner/mame/lua/checkpoint.lua
```

Exits at frame 600, produces `maincpu_ram.bin`, `gsp_vram.bin`, `gsp_palette.bin`, `adsp_data.bin` in `/tmp/checkpoint/`, and prints `MAME_CHECKPOINT_JSON` to stdout.

### GSP state at two frame landmarks

```bash
STUNRUN_GSP_STATE_OUT=/tmp/gsp \
STUNRUN_GSP_STATE_FRAMES=1800 \
STUNRUN_GSP_STATE_TARGETS=600,1800 \
STUNRUN_GSP_STATE_INPUT=late_drive \
STUNRUN_GSP_STATE_SCREEN=/tmp/gsp_screen.png \
  ~/projects/decomp/stun-runner/mame-ref/mame stunrun \
  -autoboot_script ~/projects/decomp/stun-runner/mame/lua/gsp-state-snapshot.lua
```

### ADSP program RAM snapshot

```bash
STUNRUN_ADSP_SNAPSHOT_FRAMES=600 \
STUNRUN_ADSP_SNAPSHOT_TARGETS=1,10,122,136,600 \
  ~/projects/decomp/stun-runner/mame-ref/mame stunrun \
  -autoboot_script ~/projects/decomp/stun-runner/mame/lua/adsp-program-snapshot.lua
```

### ADSP window watchpoint (write tap on data RAM)

```bash
STUNRUN_ADSP_WINDOW_OUT=/tmp/adsp_win \
STUNRUN_ADSP_WINDOW_FRAMES=600 \
STUNRUN_ADSP_WINDOW_BASE=0x808000 \
STUNRUN_ADSP_WINDOW_LENGTH=0x4000 \
STUNRUN_ADSP_WINDOW_LABEL=data_ram \
STUNRUN_ADSP_WINDOW_INPUT=late \
STUNRUN_ADSP_WINDOW_ACCESS=write \
  ~/projects/decomp/stun-runner/mame-ref/mame stunrun \
  -autoboot_script ~/projects/decomp/stun-runner/mame/lua/adsp-window.lua
```

### GSP FIFO boundary tap

```bash
STUNRUN_GSP_BOUNDARY_OUT=/tmp/gsp_fifo \
STUNRUN_GSP_BOUNDARY_FRAMES=600 \
STUNRUN_GSP_BOUNDARY_INPUT=drive \
STUNRUN_GSP_BOUNDARY_CAPTURE_START=132 \
  ~/projects/decomp/stun-runner/mame-ref/mame stunrun \
  -autoboot_script ~/projects/decomp/stun-runner/mame/lua/gsp-boundary.lua
```

### Display-list producer and grouped-opcode trace

```bash
stun-runner/tools/mame-display-list-trace \
  /home/budvar/projects/decomp/stun-runner/roms \
  /tmp/gsp-display-list-trace \
  --mame /home/budvar/projects/decomp/stun-runner/mame-ref/mame \
  --frames 1800 --start-frame 500 --end-frame 1800 \
  --input drive --max-calls 256 --raw-bytes 1024
```

Inspect `result.json` first by `caller_counts`, then compare each caller's
`decode.groups[*].operand_hex` with `fifo_writes`. A `truncated` decode is an
explicit request to rerun with a larger `--raw-bytes`, not permission to infer
the missing group boundary.

### Memory snapshot (main CPU work RAM)

```bash
STUNRUN_MEMORY_DEVICE=:mainpcb:maincpu \
STUNRUN_MEMORY_SPACE=program \
STUNRUN_MEMORY_BASE=0xFF8000 \
STUNRUN_MEMORY_COUNT=524288 \
STUNRUN_MEMORY_WIDTH=8 \
STUNRUN_MEMORY_STRIDE=1 \
STUNRUN_MEMORY_FRAMES=600 \
STUNRUN_MEMORY_TARGETS=600 \
STUNRUN_MEMORY_OUT=/tmp/mem \
STUNRUN_MEMORY_INPUT=sw_off_prestart \
STUNRUN_MEMORY_SAVE_STATE=/tmp/state.sta \
  ~/projects/decomp/stun-runner/mame-ref/mame stunrun \
  -autoboot_script ~/projects/decomp/stun-runner/mame/lua/memory-snapshot.lua
```

### Human-play capture (with MAME state saves)

```bash
STUNRUN_PLAY_OUT=/tmp/play \
STUNRUN_PLAY_EVERY=600 \
STUNRUN_PLAY_FRAMES=21600 \
STUNRUN_PLAY_STATES=1 \
  ~/projects/decomp/stun-runner/mame-ref/mame stunrun \
  -autoboot_script ~/projects/decomp/stun-runner/mame/lua/play-capture.lua
```

### Interactive REPL (debugging)

```bash
~/projects/decomp/stun-runner/mame-ref/mame stunrun -console
```

Then at the `>` prompt:
```lua
> dofile('/path/to/script.lua')
> print(inspect(manager.machine.devices))
```

---

## 8. Frame Loop Timing Reference

```
CPU executes frame N
    → video_manager::frame_update() renders frame
        → screen/s present results to OSD
        → lua_engine::frame_hook() fires
            → LUA_ON_FRAME_DONE callbacks (emu.register_frame_done)
            → LUA_ON_PERIODIC callbacks
```

`screen:snapshot()` captures the frame that has just been rendered. If you call it from a `register_frame_done` callback, you get the current frame's output.

---

## 9. Address Space Summary (STUN Runner)

| Device | Tag | Program Space Range | Notes |
|---|---|---|---|
| MC68010 (main) | `:mainpcb:maincpu` | 0x000000–0xFFFFFF | 24-bit address space |
| GSP (TMS340X0) | `:mainpcb:gsp` | 0x000000–0x03FFFFFF | Internal VRAM at 0x02000000 |
| ADSP-2106x | `:mainpcb:adsp` | 0x0000–0x1FFFF | Program RAM |
| Z80 (JSA sound) | `:mainpcb:jsa:cpu` | — | Separate address space |
| GSP control regs | — | 0xF4000000 (lo), 0xF4800000 (hi) | 256 bytes each |
| GSP palette | — | 0xF5000000 | 1 KB |
| ADSP data RAM | — | 0x808000–0x80BFFF (via main CPU bus) | 16 KB window |
| Course word | — | 0xFF9578 | 16-bit word in main RAM |
| Score | — | 0xFF9532 | 32-bit longword in main RAM |

---

## 10. Key Source References

| File | Purpose |
|---|---|
| `src/frontend/mame/luaengine.cpp:915` | `register_frame_done` doc comment |
| `src/frontend/mame/luaengine.cpp:997–1000` | `emu.print_error/warning/info/debug` |
| `src/frontend/mame/luaengine.cpp:1009` | `emu.app_build` binding |
| `src/frontend/mame/luaengine.cpp:1036` | `emu.register_frame_done` registration |
| `src/frontend/mame/luaengine.cpp:1612` | `machine.memory` property |
| `src/frontend/mame/luaengine.cpp:1634` | `machine.screens` property |
| `src/frontend/mame/luaengine.cpp:1700–1779` | `device_t` binding (spaces, state, items) |
| `src/frontend/mame/luaengine.cpp:1888–2052` | `screen_device` binding (snapshot, pixels, etc.) |
| `src/frontend/mame/luaengine.cpp:1997–2024` | `screen:snapshot` implementation |
| `src/frontend/mame/luaengine.cpp:2323–2368` | `video_manager` type binding |
| `src/frontend/mame/luaengine.cpp:2490–2497` | `frame_hook` → `LUA_ON_FRAME_DONE` |
| `src/frontend/mame/luaengine_mem.cpp:270–315` | `install_read_tap`, `install_write_tap` |
| `src/frontend/mame/luaengine_mem.cpp:321–356` | `addr_space::mem_read` (read_u8/u16/etc.) |
| `src/frontend/mame/luaengine_mem.cpp:730–733` | `memory.regions`, `memory.shares`, `memory.banks` |
| `src/frontend/mame/luaengine_mem.cpp:740–778` | `memory_region` read/write methods |
| `src/emu/video.cpp:332–360` | `video_manager::save_snapshot` |
| `src/mame/atari/harddriv.cpp` | STUN Runner driver (stunrun) |
| `scripts/target/mame/stunrun.lua` | Minimal build recipe for STUN Runner |
