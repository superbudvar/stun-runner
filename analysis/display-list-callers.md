# MC68010 display-list caller inventory

The display-list writer is the ROM interpreter at `$225E4`; it consumes a
byte stream and emits words through `$C0000C` after the caller latches a GSP
address through `$C00002`. The static listing for `136070-2102.200r` contains
the following direct call sites:

| Call site | Address setup / stream source | Current status |
|---|---|---|
| `$024AAE`, `$024ACE` | `$FFE00000`, `$FFED0000`; streams from `$BFE64` or indexed `$BFE00` | caller shape catalogued; semantics open |
| `$024F20`, `$024F5C`, `$024F98` | indexed `$BFE00`; destinations `$FFE5CFE0`, `$FFEB9FC0`, and preceding setup | caller shape catalogued; semantics open |
| `$02996A` | `$FFE00000`; indexed `$BFE00` | caller shape catalogued; semantics open |
| `$029C44`, `$029C64` | `$FFE00000`, `$FFED0000`; `$BFE64` / indexed `$BFE00` | caller shape catalogued; semantics open |
| `$02AF8E`, `$02AFAE` | `$FFE00000`, `$FFED0000`; `$BFE64` / indexed `$BFE00` | caller shape catalogued; semantics open |
| `$02D7D6`, `$02D7F2` | `$FFDE0000`, `$FFE00000`; `$BFE04` / `$BFE08` | C++ caller slice now covers surrounding routine `$2D7C2` |
| `$02DB38`, `$02DB68`, `$02DB98` | `$FFE00000`; `$BFE0C`, `$BFF14`, `$BFE08` | `$2DB84` surrounding caller is C++-converted; adjacent wrappers remain |
| `$02DC10` | `$FFE00000`; indexed `$BFE00` | caller shape catalogued; semantics open |
| `$040586`, `$0407BC`, `$040822` | `$FFE00000` or `$FFE30060`; `$BFE74`, `$BFF04`, `$BFF08` | caller shape catalogued; semantics open |

The table is an inventory, not a semantic claim. Several streams are selected
from `$BFE00` and may be text, setup commands, or other display-list assets.
The destination address alone does not identify the rendered primitive.

## C++ conversion boundary

`native/gsp_display_list.cpp` now contains the complete existing tested
transcription of `$225B0`, `$225E4`, `$2418`, `$0BC4`, `$0C28`, and `$0C46`.
`native/gsp_upload_callers.cpp` contains the already-tested C++ conversion of
the upload/caller cluster around `$2D762`, `$2D7C2`, `$2DB84`, `$2D80E`, and
`$2D858`.

The remaining rows need individual caller slices before they can be safely
converted: each has different stream-selection state and post-call writes.
The next conversion wave should start with `$024AAE/$024ACE` and
`$029C44/$029C64`, because their repeated `$BFE64`/`$BFE00` pattern makes the
stream shape easiest to compare. Do not infer that every direct `$225E4`
caller is geometry; the display-list interpreter is also the transport used
by text and diagnostic assets.
