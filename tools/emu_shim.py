"""Shared C++ shim for compiling one of MAME's CPU disassemblers standalone.

MAME's CPU disassemblers are not self-contained: each `.cpp` includes the whole
of `emu.h` and reaches for `util::disasm_interface`, `util::stream_format` and
the `BIT()` field extractor. Compiling one against the real tree means building
all of MAME. Compiling it against a *reimplementation* of those interfaces
means the decode can drift from the oracle, which defeats the point.

This module supplies the smallest stand-in that lets the pinned tree's
disassembler `.cpp` compile unmodified, and builds it. The decoder itself is
never copied or reimplemented here — only the scaffolding around it.

`SHIM` is transcribed from three pinned files and each piece carries its
source line so a reader can check it:

  * `src/lib/util/disasmintf.h:22-80` — `util::disasm_interface`, its
    `data_buffer`, and the flag constants in the returned `offs_t`.
  * `src/osd/osdcomm.h:60-72` — the `osd::u8`..`s64` integer aliases.
  * `src/lib/util/coretmpl.h:601-633` — `util::make_bitmask` and `util::BIT`,
    which `emu/emucore.h:80` pulls into the global namespace with
    `using util::BIT;`. `adsp2100/2100dasm.cpp` calls `BIT()` unqualified more
    than a hundred times, so this is not optional.

WHAT IS STILL NOT HERE — read before trusting a build:

  * `SHIM` supplies no `emu_fatalerror`. A decoder that reaches for it will not
    compile, which is the intended outcome: a new dependency is a signal to
    read MAME's source, not to guess.
  * `disasm_interface::interface_flags()` returns 0 here, so `NONLINEAR_PC` and
    `PAGED` are never claimed. That is correct for `adsp21xx_disassembler` and
    `tms34010_disassembler`, neither of which overrides them; a decoder that
    needs paging must be given a real implementation of `pc_linear_to_real`
    first.
  * `util::stream_format` forwards to `snprintf` through a 512-byte buffer and
    truncates silently past that, where MAME's real one grows a `std::string`.
    No ADSP or TMS34010 instruction MAME can emit reaches 512 bytes, but a
    decoder that can would truncate rather than fail.

DUPLICATION — deliberate, and follow-up work:

  `tools/disasm-gsp` still carries its own private copy of this shim and its
  own copy of the build loop. It was written first and is owned by another
  agent, so it was not touched here. `tools/disasm-adsp` imports this module
  instead. Folding `disasm-gsp` onto `emu_shim` is the intended follow-up;
  the shim text is a superset of the private copy (it adds `BIT`/`make_bitmask`
  and the interface flag enum, which the TMS34010 decoder happens not to need),
  so the fold is a delete, not a rewrite. Until then the two copies must be
  kept in step by hand — this module says so at the top because that is the
  failure mode.
"""

from __future__ import annotations

import hashlib
import os
import pathlib
import shutil
import subprocess
import tempfile

# The pinned MAME checkout. `tools/disasm-gsp` reads the same variable.
MAME_TREE = pathlib.Path(os.environ.get("MAME_SRC", "/mnt/build/mame-ref"))


SHIM = r'''
#ifndef STUNRUN_EMU_SHIM_H
#define STUNRUN_EMU_SHIM_H
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <ostream>

/* src/osd/osdcomm.h:60-72 */
namespace osd {
using u8=std::uint8_t; using u16=std::uint16_t; using u32=std::uint32_t;
using u64=std::uint64_t; using s8=std::int8_t;  using s16=std::int16_t;
using s32=std::int32_t;  using s64=std::int64_t;
}

namespace util {

/* src/lib/util/coretmpl.h:601-604 */
template <typename T, typename U> constexpr T make_bitmask(U n)
{
    return T((n < (8 * sizeof(T)) ? (std::make_unsigned_t<T>(1) << n) : std::make_unsigned_t<T>(0)) - 1);
}

/* src/lib/util/coretmpl.h:617 and :630-633 */
template <typename T, typename U> constexpr T BIT(T x, U n) noexcept { return (x >> n) & T(1); }
template <typename T, typename U, typename V> constexpr T BIT(T x, U n, V w)
{
    return (x >> n) & make_bitmask<T>(w);
}

template <typename... Args>
std::ostream &stream_format(std::ostream &stream, const char *fmt, Args... args)
{
    const auto fill=stream.fill(); const auto flags=stream.flags();
    const auto prec=stream.precision(); const auto width=stream.width();
    char buffer[512];
    const int n=std::snprintf(buffer,sizeof buffer,fmt,args...);
    if(n>0) stream.write(buffer,(n<(int)sizeof buffer)?n:(int)sizeof buffer-1);
    stream.width(width); stream.precision(prec); stream.flags(flags); stream.fill(fill);
    return stream;
}

/* src/lib/util/disasmintf.h:22-80. Flag values are verbatim; they are
   decoded by MAME's debugger and by tools/unidasm.cpp, so a wrong bit here
   is a silently wrong "step over" count, not a compile error. */
class disasm_interface {
public:
    virtual ~disasm_interface()=default;
    using u8=osd::u8; using u16=osd::u16; using u32=osd::u32; using u64=osd::u64;
    using s8=osd::s8; using s16=osd::s16; using s32=osd::s32; using s64=osd::s64;
    using offs_t=u32;
    static constexpr u32 SUPPORTED=0x80000000, STEP_OUT=0x40000000,
        STEP_OVER=0x20000000, STEP_COND=0x10000000,
        OVERINSTMASK=0x0c000000, OVERINSTSHIFT=26, LENGTHMASK=0x0000ffff;
    static inline u32 step_over_extra(u32 x){return x<<OVERINSTSHIFT;}
    /* src/lib/util/disasmintf.h:60-66. Both classes below claim none of
       them, so every default below is the identity. */
    enum {
        NONLINEAR_PC        = 0x00000001,
        PAGED               = 0x00000002,
        PAGED2LEVEL         = 0x00000006,
        INTERNAL_DECRYPTION = 0x00000008,
        SPLIT_DECRYPTION    = 0x00000018
    };
    class data_buffer {
    public:
        virtual ~data_buffer()=default;
        virtual u8 r8(offs_t pc) const=0;
        virtual u16 r16(offs_t pc) const=0;
        virtual u32 r32(offs_t pc) const=0;
        virtual u64 r64(offs_t pc) const=0;
    };
    virtual u32 interface_flags() const {return 0;}
    virtual u32 page_address_bits() const {return 0;}
    virtual u32 page2_address_bits() const {return 0;}
    virtual offs_t pc_linear_to_real(offs_t pc) const {return pc;}
    virtual offs_t pc_real_to_linear(offs_t pc) const {return pc;}
    virtual u8 decrypt8(u8 v,offs_t,bool) const {return v;}
    virtual u16 decrypt16(u16 v,offs_t,bool) const {return v;}
    virtual u32 decrypt32(u32 v,offs_t,bool) const {return v;}
    virtual u64 decrypt64(u64 v,offs_t,bool) const {return v;}
    virtual u32 opcode_alignment() const=0;
    virtual offs_t disassemble(std::ostream&,offs_t,const data_buffer&,const data_buffer&)=0;
};
}

/* src/emu/emucore.h:80 -- `using util::BIT;`. adsp2100/2100dasm.cpp calls
   BIT() unqualified 156 times. */
using util::BIT;

using u8=osd::u8; using u16=osd::u16; using u32=osd::u32; using u64=osd::u64;
using s8=osd::s8; using s16=osd::s16; using s32=osd::s32; using s64=osd::s64;
using uint8_t=std::uint8_t; using uint16_t=std::uint16_t;
using uint32_t=std::uint32_t; using uint64_t=std::uint64_t;
using int8_t=std::int8_t; using int16_t=std::int16_t;
using int32_t=std::int32_t; using int64_t=std::int64_t;
using offs_t=util::disasm_interface::offs_t;
#endif
'''


def compiler() -> str:
    """The C++ compiler to use, or SystemExit naming what is missing."""
    cxx = os.environ.get("CXX", "g++")
    if shutil.which(cxx) is None:
        raise SystemExit(
            f"no C++ compiler: {cxx}\n"
            f"disassembling with MAME's own decoder requires compiling "
            f"{MAME_TREE}/src/devices/cpu/*/ against the shim; install a C++ "
            f"compiler or set CXX")
    return cxx


def build_disassembler(workdir: pathlib.Path, tag: str, sources: list[pathlib.Path],
                       driver: str, inputs: list[pathlib.Path] | None = None) -> pathlib.Path:
    """Compile MAME's disassembler `sources` against the shim and return the binary.

    `sources` are the translation units to compile: `.cpp` files only. A header
    is not a translation unit, and passing one here compiles it as its own TU
    where `util::disasm_interface` is undeclared -- a real failure this module
    hit, recorded because it is an easy mistake to repeat.

    `inputs` is every file whose contents should force a rebuild, headers
    included; it defaults to `sources`. The cache key is the SHA-256 of every
    input plus the shim plus the driver, so editing any of them rebuilds and
    nothing else does.

    Give each tool its own `tag` so two tags cannot overwrite each other's
    scratch files.
    """
    missing = [source for source in (inputs or sources) if not source.is_file()]
    if missing:
        wanted = ", ".join(str(path) for path in missing)
        raise SystemExit(
            f"no MAME source at {wanted}\n"
            f"set MAME_SRC to a MAME checkout that has "
            f"src/devices/cpu/<cpu>/ (currently {MAME_TREE})")
    cxx = compiler()
    hashed = inputs if inputs is not None else sources

    key = hashlib.sha256()
    for source in hashed:
        key.update(source.read_bytes())
    key.update(SHIM.encode())
    key.update(driver.encode())
    digest = key.hexdigest()[:16]

    # Everything for one build lives in one directory named after it, so two
    # tags (or two revisions of one tag) cannot overwrite each other's
    # emu_shim.h / driver.cpp / emu.h while one of them is compiling.
    build_dir = workdir / f"{tag}-{digest}"
    cached = build_dir / tag
    if cached.is_file():
        return cached

    build_dir.mkdir(parents=True, exist_ok=True)
    (build_dir / "emu.h").write_text('#include "emu_shim.h"\n')
    (build_dir / "emu_shim.h").write_text(SHIM)
    (build_dir / "driver.cpp").write_text(driver)

    cmd = [cxx, "-O2", "-std=c++17", "-Wno-format-security",
           "-I", str(build_dir), "-I", str(sources[0].parent),
           str(build_dir / "driver.cpp"), *[str(s) for s in sources],
           "-o", str(cached)]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        raise SystemExit(
            f"failed to build MAME's {tag} disassembler:\n{proc.stderr[:4000]}")
    return cached


def default_cache(cache_dir_env: str, fallback_name: str) -> pathlib.Path:
    """The build cache directory: `$cache_dir_env` if set, else `<tmp>/fallback_name`.

    Give each tool its own default name so two tools that share this module do
    not also share one directory of scratch files. Each build further gets its
    own `<tag>-<hash>/` subdirectory, so even one tool never overwrites a
    previous build's scratch files.
    """
    return pathlib.Path(os.environ.get(cache_dir_env, pathlib.Path(tempfile.gettempdir()) / fallback_name))