"""ROM-free checks for the MAME display-list producer trace."""

import importlib.machinery
import importlib.util
import pathlib
import os
import shutil
import subprocess
import unittest
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "mame-display-list-trace"
LUA = ROOT / "mame" / "lua" / "display-list-trace.lua"


def load_tool():
    loader = importlib.machinery.SourceFileLoader("display_list_trace", str(TOOL))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


class DisplayListTraceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tool = load_tool()

    def test_lua_compiles(self):
        luac = shutil.which("luac")
        if luac is None:
            self.skipTest("luac not installed")
        proc = subprocess.run([luac, "-p", str(LUA)], capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)

    def test_wrapper_help(self):
        proc = subprocess.run([str(TOOL), "--help"], capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("--raw-bytes", proc.stdout)

    def test_decodes_group_boundaries_and_pending_state(self):
        table = bytes(range(16))
        raw = b"\x00" + table + b"\x03\xAA\xBB\xCC\x82\x55\xC2\x6F\x40"
        decoded = self.tool.decode_stream(0x1000, raw)
        self.assertEqual(decoded["status"], "complete")
        self.assertEqual(decoded["end"], "opcode-40")
        self.assertEqual([group["family"] for group in decoded["groups"]],
                         ["copy", "fill", "table-fill", "end-40"])
        self.assertEqual(decoded["groups"][0]["operand_hex"], "AABBCC")
        self.assertEqual(decoded["groups"][0]["expected_fifo_words"], 1)
        self.assertTrue(decoded["groups"][0]["pending_after"])
        self.assertEqual(decoded["groups"][1]["expected_fifo_words"], 1)
        # The pending flush consumes one of the fill's two units, then the odd
        # remainder re-arms D2 via `subx.w D2,D2` at 0x226EA.
        self.assertTrue(decoded["groups"][1]["pending_after"])
        self.assertEqual(decoded["groups"][2]["expected_fifo_words"], 2)

    def test_reports_truncated_operand_without_guessing(self):
        raw = b"\x00" + bytes(range(16)) + b"\x05\x11\x22"
        decoded = self.tool.decode_stream(0x2000, raw)
        self.assertEqual(decoded["status"], "truncated")
        self.assertTrue(decoded["groups"][-1]["truncated"])

    def test_parses_call_and_attributes_fifo_words(self):
        raw = (b"\x00" + bytes(range(16)) + b"\x82\x55\x80").hex().upper()
        log = (
            f"M1_GSP_DL_CALL id=1 frame=600 return=00024AB4 caller=00024AAE "
            f"call_size=6 call_opcode=4EB9 stream=00FF9200 raw={raw}\n"
            "M1_GSP_DL_FIFO id=1 frame=600 pc=000226F8 addr=00C0000C "
            "data=00005555 mask=0000FFFF\n"
            "M1_GSP_DL_ENTRY id=1 sp=00FF8000 a6=00FF8100 d0=00000001 "
            "d1=00000002 d2=00000003 d3=00000004 d4=00000005 "
            "stack=12345678ABCDEF00\n"
            "M1_GSP_DL_CONSUMER frame=600 pc=FFF48C20\n"
            "M1_GSP_DL_CONSUMER_STATUS status=observed\n"
            "M1_GSP_DL_DONE frames=600 calls=1 fifo_words=1 "
            "calls_truncated=false fifo_truncated=false output=/tmp/x\n"
        )
        parsed = self.tool.parse_log(log)
        call = parsed["calls"][0]
        self.assertEqual(call["caller_pc"], 0x24AAE)
        self.assertEqual(call["call_opcode"], 0x4EB9)
        self.assertEqual(call["fifo_writes"][0]["data"], 0x5555)
        self.assertTrue(call["fifo_count_matches_decode"])
        self.assertEqual(call["raw_fifo_words"], [0x5555])
        self.assertEqual(call["raw_fifo_hex"], "5555")
        self.assertEqual(call["entry_state"]["d4"], 5)
        self.assertEqual(call["entry_state"]["stack_hex"], "12345678ABCDEF00")
        self.assertEqual(parsed["consumer_capture_status"], "observed")
        self.assertEqual(call["consumer_evidence"]["status"], "observed-same-frame")
        self.assertIn("ownership is unknown", call["consumer_evidence"]["meaning"])

    def test_preserves_unknown_consumer_selector(self):
        parsed = self.tool.parse_log(
            "M1_GSP_DL_CALL id=1 frame=4 return=10 caller=0 call_size=0 "
            "call_opcode=0 stream=100 raw=\n"
            "M1_GSP_DL_DONE frames=4 calls=1 fifo_words=0 "
            "calls_truncated=false fifo_truncated=false\n")
        self.assertEqual(parsed["consumer_capture_status"], "unknown")
        self.assertEqual(parsed["calls"][0]["consumer_evidence"]["status"],
                         "not-observed-same-frame")

    def test_maps_unavailable_consumer_selector_to_unknown(self):
        parsed = self.tool.parse_log(
            "M1_GSP_DL_CONSUMER_STATUS status=unavailable\n")
        self.assertEqual(parsed["consumer_capture_status"], "unknown")

    def test_exports_exact_oracle_bytes_and_preview(self):
        calls = [{"id": 7, "stream_address": 0x9841C,
                  "fifo_count_matches_decode": True, "fifo_word_count": 2,
                  "fifo_writes": [{"data": 0x1234}, {"data": 0xABCD}]}]
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory)
            result = self.tool.export_small_upload(calls, output)
            self.assertEqual(result["pixel_format"], "unknown")
            self.assertEqual((output / "small-upload.bin").read_bytes(),
                             bytes.fromhex("1234ABCD"))
            self.assertEqual((output / "small-upload.pgm").read_bytes(),
                             b"P5\n2 2\n255\n" + bytes.fromhex("1234ABCD"))

    def test_does_not_export_incomplete_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory)
            self.assertEqual(self.tool.export_small_upload([], output)["status"],
                             "no-complete-upload")
            self.assertEqual(list(output.iterdir()), [])

    def test_lua_confirms_execution_not_duplicate_fetches(self):
        lua = shutil.which("lua")
        if lua is None:
            self.skipTest("lua not installed")
        harness = r'''
local taps = {}
local pc = {value = 0x225e4}
local space = {}
function space:install_read_tap(lo, hi, name, callback) taps.entry = callback end
function space:install_write_tap(lo, hi, name, callback)
    if lo == 0x608000 then taps.execution = callback else taps.fifo = callback end
end
function space:read_u32(addr) return addr == 0x1000 and 0x2d782 or 0x98457 end
function space:read_u16(addr)
    if addr == 0x2d780 then return 0x4e92 end
    if addr == 0xc00000 then return 0xfff3 end
    if addr == 0xc00004 then return 0xfb20 end
    if addr == 0xc00008 then return 0xb800 end
    return 0
end
function space:read_u8(addr) return 0 end
local callback
emu = {register_frame_done = function(fn) callback = fn end}
manager = {machine = {devices = {[':mainpcb:maincpu'] = {
    spaces = {program = space}, state = {CURPC = pc, A7 = {value = 0x1000}}}},
    exit = function() end}}
dofile(arg[1])
for i = 1, 10 do callback() end
taps.entry() -- prefetch candidate, not yet an execution
pc.value = 0x226f8
taps.fifo(0xc0000c, 0xffff, 0xffff) -- excluded without watchdog
pc.value = 0x225e4
taps.entry() -- duplicate fetch, still only a candidate
pc.value = 0x22608
taps.execution()
taps.execution() -- repeated loop iteration must not start another call
pc.value = 0x226f8
taps.fifo(0xc0000c, 0x1234, 0xffff)
callback() -- frame 11 is outside capture window
taps.fifo(0xc0000c, 0xabcd, 0xffff)
callback() -- frame 12 emits records
'''
        env = dict(os.environ, STUNRUN_GSP_DL_TRACE_OUT="fixture",
                   STUNRUN_GSP_DL_TRACE_INPUT="none", STUNRUN_GSP_DL_TRACE_FRAMES="12",
                   STUNRUN_GSP_DL_TRACE_START="10", STUNRUN_GSP_DL_TRACE_END="10",
                   STUNRUN_GSP_DL_TRACE_RAW_BYTES="18")
        with tempfile.TemporaryDirectory() as directory:
            script = pathlib.Path(directory) / "fixture.lua"
            script.write_text(harness)
            proc = subprocess.run([lua, str(script), str(LUA)], env=env,
                                  capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        calls = self.tool.parse_log(proc.stdout)["calls"]
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0]["host_address"], 0xFFF3FB20)
        self.assertEqual(calls[0]["host_control"], 0xB800)
        self.assertEqual(calls[0]["fifo_word_count"], 1)
        self.assertEqual(calls[0]["fifo_writes"][0]["data"], 0x1234)
        self.assertEqual(calls[0]["fifo_writes"][0]["destination_latch"], 0xFFF3FB20)


if __name__ == "__main__":
    unittest.main()
