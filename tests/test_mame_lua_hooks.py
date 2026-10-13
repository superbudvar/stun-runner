"""No MAME Lua script may apply input through emu.register_prestart.

This is a regression guard for a measured defect, not a style rule.
docs/mc68010-boot-findings.md section 31.3 records the measurement:

    machine.register_init   = nil
    machine.register_start  = nil
    emu.register_prestart   = function
    emu.register_preinit    = nil
    emu.register_init       = nil
    emu.register_start      = nil

`emu.register_prestart` is the only pre-frame hook the Lua engine exposes, and
it never fires. MAME registers it in `luaengine.cpp:862`:

    machine().add_notifier(MACHINE_NOTIFY_RESET,
        machine_notify_delegate(&lua_engine::on_machine_prestart, this), true);

The trailing `true` is `one_time`, so the callback fires on the first machine
reset only. `-autoboot_script` is loaded after that reset, so a callback the
script registers is never invoked. Verified with a script that prints from the
callback: no output, under `-seconds_to_run 1`, `-seconds_to_run 3`, and
`-autoboot_wait 2`.

Eight scripts applied a SW1 DIP prefix or `initial_events` through it, so those
settings were silently absent from every capture ever taken with those input
modes. The failure is invisible: the run succeeds, the event count is
plausible, and the missing DIP change reads as a property of the game.

Use `emu.register_frame_done` instead and apply the prefix at the first frame.
That is later than a true prestart, so a schedule that must be in effect before
frame 0 needs a different answer than "apply it at frame 1" -- raise it rather
than silently moving the boundary.
"""

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
LUA_DIR = ROOT / "mame" / "lua"

# Scripts are allowed to *mention* prestart only in a comment explaining why it
# is not used. A real call is what this guard rejects.
PRESTART_CALL = re.compile(r"emu\.register_prestart\s*\(")
PRESTART_COMMENT = re.compile(r"^\s*--")


class NoPrestartInputTest(unittest.TestCase):
    def scripts(self):
        return sorted(LUA_DIR.glob("*.lua"))

    def test_lua_directory_is_not_empty(self):
        # Without this the guard below passes vacuously if the path is wrong.
        self.assertGreater(len(self.scripts()), 5)

    def test_no_script_registers_prestart(self):
        offenders = []
        for path in self.scripts():
            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                if PRESTART_COMMENT.match(line):
                    continue
                if PRESTART_CALL.search(line):
                    offenders.append(f"{path.name}:{number}: {line.strip()}")
        self.assertEqual(
            offenders, [],
            "emu.register_prestart never fires on this build (MAME's one_time "
            "reset notifier fires before -autoboot_script is loaded), so input "
            "applied there is silently dropped. Findings section 31.3.\n"
            + "\n".join(offenders))

    def test_the_defect_is_still_documented(self):
        findings = (ROOT / "docs" / "mc68010-boot-findings.md").read_text(encoding="utf-8")
        self.assertIn("31.3", findings)
        self.assertIn("register_prestart", findings)


if __name__ == "__main__":
    unittest.main()