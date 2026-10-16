import json
import hashlib
import pathlib
import struct
import subprocess
import tempfile
import unittest
import zlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


class PublicToolTests(unittest.TestCase):
    def run_tool(self, name, *args):
        return subprocess.run(
            [str(ROOT / "tools" / name), *map(str, args)],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )

    def test_dump_glyph_font_decodes_big_endian_nibble_rows(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            roms = temp / "roms"
            roms.mkdir()
            even = bytearray(0x2000)
            odd = bytearray(0x2000)
            # A is at 0x212C. Values are words (big-endian), eight rows;
            # the rendered pixels are the low four nibbles, low-first.
            glyph = [0x0005, 0x0041, 0x0555, 0, 0, 0, 0, 0]
            base = 0x1FBC + 16 * (ord("A") - 0x2A)
            for row, word in enumerate(glyph):
                address = base + 2 * row
                even[address // 2] = word >> 8
                odd[address // 2] = word & 0xFF
            (roms / "even.bin").write_bytes(even)
            (roms / "odd.bin").write_bytes(odd)
            manifest = temp / "manifest.json"
            manifest.write_text(json.dumps({
                "schema": "stunrun-rom-manifest/v1",
                "roms": [
                    {"filename": "even.bin", "region": "mainpcb:maincpu",
                     "size": len(even), "offset": 0},
                    {"filename": "odd.bin", "region": "mainpcb:maincpu",
                     "size": len(odd), "offset": 1},
                ],
            }), encoding="utf-8")
            png = temp / "font.png"
            result = subprocess.run(
                [str(ROOT / "tools/dump-glyph-font"), "--roms", str(roms),
                 "--manifest", str(manifest), "--chars", "A", "--pixel-size", "1",
                 "--output", str(png), "--json"],
                cwd=ROOT, capture_output=True, text=True, check=True,
            )
            report = json.loads(result.stdout)
            self.assertEqual(report["schema"], "stunrun-glyph-font-v1")
            self.assertEqual(report["glyphs"][0]["rom_address"], 0x212C)
            self.assertEqual(report["glyphs"][0]["row_words"], glyph)
            self.assertEqual(report["glyphs"][0]["pixels"][1], [1, 4, 0, 0])

            data = png.read_bytes()
            self.assertTrue(data.startswith(b"\x89PNG\r\n\x1a\n"))
            offset = 8
            idat = bytearray()
            while offset < len(data):
                size = struct.unpack_from(">I", data, offset)[0]
                kind = data[offset + 4:offset + 8]
                payload = data[offset + 8:offset + 8 + size]
                if kind == b"IHDR":
                    width, height = struct.unpack_from(">II", payload)
                if kind == b"IDAT":
                    idat.extend(payload)
                offset += size + 12
            self.assertEqual((width, height), (60, 14))
            raw = zlib.decompress(idat)
            # Filter byte 0; glyph starts at x=1,y=6. Nibble 5 lights pixel 0.
            pixel_offset = 6 * (1 + width * 3) + 1 + 1 * 3
            self.assertEqual(raw[pixel_offset:pixel_offset + 3], b"\xff\xff\xff")
            self.assertEqual(raw[pixel_offset + 3:pixel_offset + 6], b"\x00\x00\x00")

    def test_dump_glyph_font_rejects_wrong_sized_rom(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            roms = temp / "roms"
            roms.mkdir()
            (roms / "even.bin").write_bytes(b"\0" * 4)
            (roms / "odd.bin").write_bytes(b"\0" * 4)
            manifest = temp / "manifest.json"
            manifest.write_text(json.dumps({
                "schema": "stunrun-rom-manifest/v1",
                "roms": [
                    {"filename": "even.bin", "region": "mainpcb:maincpu",
                     "size": 8, "offset": 0},
                    {"filename": "odd.bin", "region": "mainpcb:maincpu",
                     "size": 8, "offset": 1},
                ],
            }), encoding="utf-8")
            result = subprocess.run(
                [str(ROOT / "tools/dump-glyph-font"), "--roms", str(roms),
                 "--manifest", str(manifest), "--chars", "A", "--output",
                 str(temp / "font.png")],
                cwd=ROOT, capture_output=True, text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("ROM size mismatch", result.stderr)

    def test_manifest_parser_without_roms(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            xml = temp / "sample.xml"
            output = temp / "manifest.json"
            xml.write_text(
                '<mame><machine name="stunrun" sourcefile="atari/harddriv.cpp">'
                '<description>synthetic</description>'
                '<rom name="a.bin" size="2" crc="12345678" sha1="abc" region="maincpu" offset="0"/>'
                '</machine></mame>',
                encoding="utf-8",
            )
            self.run_tool("mame-manifest", xml, "--mame-version", "test", "-o", output)
            manifest = json.loads(output.read_text())
            self.assertEqual(manifest["roms"][0]["filename"], "a.bin")
            self.assertEqual(manifest["roms"][0]["size"], 2)

    def test_analyze_musashi_gsp_writes_groups_callers(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            log = temp / "gsp_host_writes.bin"
            output = temp / "summary.json"
            log.write_bytes(b"".join([
                struct.pack("<IH2xI", 0xFF896040, 0xDFDF, 0x25E8),
                struct.pack("<IH2xI", 0xFF896050, 0x0000, 0x25E8),
                struct.pack("<IH2xI", 0xC00000, 0x1234, 0x2D762),
            ]))
            self.run_tool("analyze-musashi-gsp-writes", log, "-o", output)
            report = json.loads(output.read_text())
            self.assertEqual(report["schema"],
                             "stunrun-musashi-gsp-write-summary/v1")
            writer = report["writers"]["0x25E8"]
            self.assertEqual(writer["event_count"], 2)
            self.assertEqual(writer["regions"], {"vram": 2})
            self.assertEqual(writer["common_address_strides"][0],
                             {"stride": 16, "count": 1})

    def test_analyze_gsp_vram_write_trace_groups_writers(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            trace = temp / "writes.json"
            output = temp / "summary.json"
            trace.write_text(json.dumps({
                "schema": "stunrun-ram-write-trace-result/v1",
                "device": ":mainpcb:gsp", "space": "program",
                "base": 0x2000000, "end": 0x20000FF,
                "capture_start_frame": 10, "capture_end_frame": 12,
                "events": [
                    {"frame": 10, "pc": 0xFFF46590, "address": 0x2000000,
                     "data": 1, "mask": 0xFFFF},
                    {"frame": 10, "pc": 0xFFF46590, "address": 0x2000010,
                     "data": 2, "mask": 0xFFFF},
                    {"frame": 12, "pc": 0xFFF43030, "address": 0x2000040,
                     "data": 3, "mask": 0xFFFF},
                ],
            }), encoding="utf-8")
            self.run_tool("analyze-gsp-vram-write-trace", trace,
                          "--label", "0xFFF46590=PIXBLT", "-o", output)
            report = json.loads(output.read_text())
            self.assertEqual(report["schema"],
                             "stunrun-gsp-vram-write-summary/v1")
            self.assertEqual(report["writers"]["0xFFF46590"]["label"], "PIXBLT")
            self.assertEqual(report["writers"]["0xFFF46590"]["event_count"], 2)
            self.assertEqual(report["writers"]["0xFFF46590"]["address_ranges"], [
                {"start": 0x2000000, "end": 0x2000000, "count": 1},
                {"start": 0x2000010, "end": 0x2000010, "count": 1},
            ])

    def test_analyze_gsp_read_write_correlation(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            writes = temp / "writes.json"
            reads = temp / "reads.json"
            output = temp / "correlation.json"
            writes.write_text(json.dumps({
                "schema": "stunrun-ram-write-trace-result/v1",
                "device": ":mainpcb:gsp", "events": [
                    {"frame": 8, "pc": 0x100, "address": 0x2000,
                     "data": 1, "mask": 0xFFFF},
                    {"frame": 16, "pc": 0x110, "address": 0x2000,
                     "data": 2, "mask": 0xFFFF},
                ]
            }), encoding="utf-8")
            reads.write_text(json.dumps({
                "schema": "stunrun-ram-read-trace-result/v1",
                "device": ":mainpcb:gsp", "events": [
                    {"frame": 11, "pc": 0xFFF45A10, "address": 0x2000,
                     "data": 1, "mask": 0xFFFF},
                    {"frame": 16, "pc": 0xFFF45A10, "address": 0x2000,
                     "data": 2, "mask": 0xFFFF},
                    {"frame": 11, "pc": 0xFFF45A10,
                     "address": 0xFFF45A10, "data": 0, "mask": 0xFFFF},
                    {"frame": 11, "pc": 0xFFF45A10, "address": 0x3000,
                     "data": 3, "mask": 0xFFFF},
                ]
            }), encoding="utf-8")
            self.run_tool("analyze-gsp-read-write-correlation", writes, reads,
                          "--read-pc", "0xFFF45A10", "--output", output)
            report = json.loads(output.read_text())
            self.assertEqual(report["schema"],
                             "stunrun-gsp-read-write-correlation/v1")
            self.assertEqual(report["read_event_count"], 3)
            self.assertEqual(report["source_address_count"], 2)
            self.assertEqual(report["overlap_address_count"], 1)
            self.assertEqual(report["same_frame_overlap_read_count"], 1)
            self.assertEqual(report["nearest_write_delta_frames"],
                             {"-3": 1, "0": 1})

    def test_analyze_gsp_read_write_correlation_uses_event_order(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            writes = temp / "writes.json"
            reads = temp / "reads.json"
            output = temp / "correlation.json"
            writes.write_text(json.dumps({
                "schema": "stunrun-ram-write-trace-result/v1",
                "device": ":mainpcb:gsp", "events": [
                    {"frame": 8, "pc": 0x100, "address": 0x2000,
                     "data": 1, "mask": 0xFFFF, "order": 1},
                    {"frame": 16, "pc": 0x110, "address": 0x2000,
                     "data": 2, "mask": 0xFFFF, "order": 5},
                ]
            }), encoding="utf-8")
            reads.write_text(json.dumps({
                "schema": "stunrun-ram-read-trace-result/v1",
                "device": ":mainpcb:gsp", "events": [
                    {"frame": 11, "pc": 0xFFF45A10, "address": 0x2000,
                     "data": 1, "mask": 0xFFFF, "order": 4},
                ]
            }), encoding="utf-8")
            self.run_tool("analyze-gsp-read-write-correlation", writes, reads,
                          "--read-pc", "0xFFF45A10", "--output", output)
            report = json.loads(output.read_text())
            self.assertEqual(report["nearest_write_order_delta"], {"1": 1})
            self.assertEqual(report["nearest_write_order_before_read_count"], 0)
            self.assertEqual(report["nearest_write_order_after_read_count"], 1)
            self.assertEqual(report["nearest_write_pc_counts"], {"0x110": 1})
            self.assertEqual(report["same_address_and_value_read_count"], 1)
            self.assertEqual(report["same_value_with_prior_write_count"], 1)
            self.assertEqual(report["same_value_with_later_write_count"], 0)
            self.assertEqual(report["nearest_same_value_write_pc_counts"],
                             {"0x100": 1})

    def test_analyze_gsp_read_write_correlation_accepts_same_run_result(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            trace = temp / "same-run.json"
            output = temp / "correlation.json"
            trace.write_text(json.dumps({
                "schema": "stunrun-ram-read-write-trace-result/v1",
                "write": {
                    "schema": "stunrun-ram-write-trace-result/v1",
                    "device": ":mainpcb:gsp", "events": [
                        {"frame": 8, "pc": 0x100, "address": 0x2000,
                         "data": 1, "mask": 0xFFFF, "order": 1},
                    ]
                },
                "read": {
                    "schema": "stunrun-ram-read-trace-result/v1",
                    "device": ":mainpcb:gsp", "events": [
                        {"frame": 8, "pc": 0xFFF45090, "address": 0x2000,
                         "data": 1, "mask": 0xFFFF, "order": 2},
                    ]
                }
            }), encoding="utf-8")
            self.run_tool("analyze-gsp-read-write-correlation", trace, trace,
                          "--read-pc", "0xFFF45090", "--output", output)
            report = json.loads(output.read_text())
            self.assertEqual(report["write_event_count"], 1)
            self.assertEqual(report["read_event_count"], 1)
            self.assertEqual(report["same_frame_overlap_read_count"], 1)
            self.assertEqual(report["same_order_overlap_read_count"], 0)

    def test_same_run_trace_exposes_recorded_playback(self):
        tool = (ROOT / "tools" / "mame-ram-read-write-trace").read_text(
            encoding="utf-8")
        self.assertIn('parser.add_argument("--playback"', tool)
        self.assertIn('"--playback", str(args.playback)', tool)
        self.assertIn("--load-state and --playback are mutually exclusive", tool)
        self.assertIn('parser.add_argument("--mame"', tool)
        self.assertIn('["--mame", args.mame]', tool)

    def test_analyze_packed_text_trace_decodes_words_and_filters_fetches(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            trace = temp / "reads.json"
            output = temp / "text.json"
            trace.write_text(json.dumps({
                "schema": "stunrun-ram-read-trace-result/v1",
                "events": [
                    {"frame": 10, "pc": 0x464E0, "address": 0x464E0,
                     "data": 0x9405, "mask": 0xFFFF},
                    {"frame": 10, "pc": 0x464E0, "address": 0x1000,
                     "data": 0x7243, "mask": 0xFFFF},
                    {"frame": 10, "pc": 0x464E0, "address": 0x1000,
                     "data": 0x7243, "mask": 0xFFFF},
                    {"frame": 10, "pc": 0x464E0, "address": 0x1010,
                     "data": 0x6465, "mask": 0xFFFF},
                    {"frame": 10, "pc": 0x464E0, "address": 0x464E0,
                     "data": 0x9405, "mask": 0xFFFF},
                    {"frame": 10, "pc": 0x464E0, "address": 0x1020,
                     "data": 0x7469, "mask": 0xFFFF},
                    {"frame": 10, "pc": 0x464E0, "address": 0x464E0,
                     "data": 0x9405, "mask": 0xFFFF},
                    {"frame": 10, "pc": 0x464E0, "address": 0x1030,
                     "data": 0x3A73, "mask": 0xFFFF},
                ],
            }), encoding="utf-8")
            self.run_tool("analyze-packed-text-trace", trace, "--pc",
                          hex(0x464E0), "--output", output)
            report = json.loads(output.read_text())
            self.assertEqual(report["schema"],
                             "stunrun-packed-text-analysis/v1")
            self.assertEqual(report["decoded_record_count"], 4)
            self.assertEqual(report["decoded_text"], "Credits:")
            self.assertEqual(report["records"][0]["bytes_hex"], "4372")
            self.assertEqual(report["lane_text"], {"low": "Ceis", "high": "rdt:"})

    def test_analyze_gsp_text_cursor_preserves_register_cursor(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            trace = temp / "cursor.json"
            output = temp / "cursor-analysis.json"
            trace.write_text(json.dumps({
                "schema": "stunrun-ram-read-trace-result/v1",
                "events": [
                    {"frame": 1793, "pc": 0x464E0, "address": 0x464E0,
                     "data": 0x9405, "mask": 0xffff, "a0": 0x1000,
                     "a1": 0xfd0040, "a5": 2, "a10": 0xdbc0, "a11": 64},
                    {"frame": 1793, "pc": 0x464E0, "address": 0x2000,
                     "data": 0x7243, "mask": 0xffff, "a0": 0x2000,
                     "a1": 0xfd0040, "a5": 2, "a10": 0xdbc0, "a11": 64},
                    {"frame": 1793, "pc": 0x464E0, "address": 0x464E0,
                     "data": 0x9405, "mask": 0xffff, "a0": 0x2008,
                     "a1": 0xfd0048, "a5": 0, "a10": 0xdbc0, "a11": 64},
                    {"frame": 1793, "pc": 0x464E0, "address": 0x2000,
                     "data": 0x7243, "mask": 0xffff, "a0": 0x2008,
                     "a1": 0xfd0048, "a5": 0, "a10": 0xdbc0, "a11": 64},
                ],
            }), encoding="utf-8")
            self.run_tool("analyze-gsp-text-cursor", trace, "--pc",
                          hex(0x464E0), "--output", output)
            report = json.loads(output.read_text())
            self.assertEqual(report["decoded_record_count"], 1)
            self.assertEqual(report["decoded_text"], "Cr")
            self.assertEqual(report["records"][0]["a0_before"], 0x2000)
            self.assertEqual(report["records"][0]["a0_after"], 0x2008)
            self.assertEqual(report["records"][0]["a1"], 0xFD0040)
            self.assertEqual(report["glyph_count"], 2)
            self.assertEqual(report["glyphs"], [
                {"frame": 1793, "descriptor_address": 0x2000,
                 "lane": 0, "glyph_code": ord("C"), "a0": 0x2000,
                 "a1": 0xFD0040, "text": "C"},
                {"frame": 1793, "descriptor_address": 0x2000,
                 "lane": 1, "glyph_code": ord("r"), "a0": 0x2008,
                 "a1": 0xFD0048, "text": "r"},
            ])

    def test_export_gsp_text_fixture_writes_little_endian_inputs(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            snapshot = temp / "table.json"
            table = [0] * 512
            table[0x43 * 4] = 0x633E
            snapshot.write_text(json.dumps({
                "schema": "stunrun-memory-snapshot/v1",
                "base": 0xFFF5DBC0, "count": 512, "width": 16,
                "values": table,
            }), encoding="utf-8")
            table_out = temp / "table.bin"
            words_out = temp / "words.bin"
            result = self.run_tool(
                "export-gsp-text-fixture", snapshot,
                "--table-out", table_out, "--words-out", words_out,
                "--word", "0x7243", "--word", "0x0000")
            self.assertEqual(json.loads(result.stdout)["descriptor_words"], 2)
            self.assertEqual(table_out.stat().st_size, 1024)
            self.assertEqual(table_out.read_bytes()[0x43 * 8:0x43 * 8 + 2],
                             struct.pack("<H", 0x633E))
            self.assertEqual(words_out.read_bytes(),
                             struct.pack("<HH", 0x7243, 0x0000))

    def test_export_gsp_text_record_fixture_extracts_records(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            trace = temp / "writes.json"
            output = temp / "records.bin"
            events = []
            words = [0x4013, 0, 0x40, 0xFD, 0x3A30, 0x3533, 0x302E, 0]
            for index, word in enumerate(words):
                events.append({"frame": 1788, "pc": 0xFFF454E0,
                                "address": 0xA480 + index * 0x10,
                                "data": word, "mask": 0xFFFF})
            trace.write_text(json.dumps({
                "schema": "stunrun-ram-write-trace-result/v1",
                "events": events,
            }), encoding="utf-8")
            result = self.run_tool(
                "export-gsp-text-record-fixture", trace, "--frame", "1788",
                "--record-base", "0xA480", "--output", output)
            report = json.loads(result.stdout)
            self.assertEqual(report["record_count"], 1)
            self.assertEqual(output.read_bytes(), b"".join(
                struct.pack("<H", value) for value in words))

    def test_analyze_gsp_text_record_writes_finds_stride_records(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            trace = temp / "writes.json"
            output = temp / "records.json"
            words = [0x4013, 0, 0x40, 0xFD, 0x3A30, 0x3533, 0x302E, 0]
            events = [{"frame": 1788, "pc": 0xFFF454E0,
                       "address": 0xA480 + index * 0x10,
                       "data": word, "mask": 0xFFFF}
                      for index, word in enumerate(words)]
            trace.write_text(json.dumps({
                "schema": "stunrun-ram-write-trace-result/v1",
                "events": events,
            }), encoding="utf-8")
            self.run_tool("analyze-gsp-text-record-writes", trace,
                          "--frame", "1788", "--output", output)
            report = json.loads(output.read_text())
            self.assertEqual(report["candidate_count"], 1)
            candidate = report["candidates"][0]
            self.assertEqual(candidate["descriptor_address"], "0xA4C0")
            self.assertEqual(candidate["a1"], "0x00FD0040")
            self.assertEqual(candidate["payload_text"], "0:35.0\\x00\\x00")

    def test_track_table_analyzer_snapshot_comparison_without_roms(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            rompath = temp / "roms"
            romdir = rompath / "stunrun"
            romdir.mkdir(parents=True)
            manifest = temp / "manifest.json"
            output = temp / "analysis.json"
            snapshot = temp / "snapshot.json"
            bases = [0x20, 0x320, 0x620, 0x920]
            region = bytearray(0x47406 + 23 * 4)
            for slot, base in enumerate(bases):
                for word in range(384):
                    value = 0x1000 + word
                    if 48 <= word <= 143:
                        value += slot << 8
                    if slot == 0 and word == 24:
                        value ^= 0x00FF
                    region[base + word * 2:base + word * 2 + 2] = struct.pack(
                        ">H", value)
            selector = [0x44630, 0x44930, 0x45230] + [0x44630] * 20
            for index, value in enumerate(selector):
                region[0x47406 + index * 4:0x4740A + index * 4] = struct.pack(
                    ">I", value)
            (romdir / "even.bin").write_bytes(bytes(region[0::2]))
            (romdir / "odd.bin").write_bytes(bytes(region[1::2]))
            manifest.write_text(json.dumps({
                "roms": [
                    {"filename": "even.bin", "region": "mainpcb:maincpu",
                     "size": len(region) // 2, "offset": 0},
                    {"filename": "odd.bin", "region": "mainpcb:maincpu",
                     "size": len(region) // 2, "offset": 1},
                ]
            }), encoding="utf-8")
            selected = bytes(region[0x620:0x920])
            selected = bytearray(selected)
            selected[-48:] = bytes((value ^ 0x55 for value in selected[-48:]))
            snapshot.write_text(json.dumps({
                "schema": "stunrun-memory-snapshot/v1",
                "base": 0xFF9584,
                "width": 8,
                "values": list(selected) * 2,
            }), encoding="utf-8")
            base_args = [argument for base in bases
                         for argument in ("--base", hex(base))]
            result = self.run_tool(
                "analyze-track-tables", rompath, "--manifest", manifest,
                *base_args, "--snapshot", snapshot, "--output", output,
            )
            self.assertEqual(result.returncode, 0)
            report = json.loads(output.read_text())
            comparison = report["snapshot_comparisons"][0]
            self.assertEqual(report["common_tail_sha256"],
                             "d54e3b376eb4d0fc96fd4af38c020e99c1e4b54d38d65edc4e97e2e72eeec18d")
            self.assertEqual(report["state_table_selector"]["entry_count"], 23)
            self.assertEqual(report["state_table_selector"]["state_to_table"][10],
                             {"state": 10, "rom_base": "0x44630"})
            self.assertEqual(comparison["best"]["slot"], "0x00620")
            self.assertEqual(comparison["best"]["different_word_ranges"], ["360-383"])
            self.assertEqual(comparison["settled_tail_sha256"],
                             "00c560922d5d536424b96f2d92e6cdda6055330024c3bd693e41cb1734a801a8")

    def test_rom_read_sequence_analyzer_collapses_lane_duplicates(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            trace = temp / "reads.json"
            output = temp / "analysis.json"
            trace.write_text(json.dumps({
                "schema": "stunrun-rom-read-trace-result/v1",
                "events": [
                    {"frame": 4, "pc": 0x29760, "address": 0x44630,
                     "data": 1, "mask": 0xFFFF},
                    {"frame": 4, "pc": 0x29760, "address": 0x44630,
                     "data": 1, "mask": 0x00FF},
                    {"frame": 4, "pc": 0x29760, "address": 0x44632,
                     "data": 2, "mask": 0xFFFF},
                    {"frame": 4, "pc": 0x29760, "address": 0x44634,
                     "data": 3, "mask": 0xFFFF},
                    {"frame": 5, "pc": 0x29760, "address": 0x44640,
                     "data": 4, "mask": 0xFFFF},
                    {"frame": 5, "pc": 0x29760, "address": 0x44644,
                     "data": 5, "mask": 0xFFFF},
                ],
            }), encoding="utf-8")
            self.run_tool("analyze-rom-read-sequence", trace,
                          "--output", output)
            report = json.loads(output.read_text())
            self.assertEqual(report["schema"],
                             "stunrun-rom-read-sequence-analysis/v1")
            self.assertEqual(report["source_event_count"], 6)
            self.assertEqual(report["non_sequential_group_count"], 1)
            first = report["groups"][0]
            self.assertEqual(first["deduplicated_address_count"], 3)
            self.assertEqual(first["runs"][0], {
                "start": 0x44630, "end": 0x44634,
                "count": 3, "step": 2,
            })
            second = report["groups"][1]
            self.assertFalse(second["sequential"])

    def test_native_visible_bridge_runner(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            expected = temp / "expected.ppm"
            actual = temp / "actual.ppm"
            vram = temp / "vram.bin"
            palette = temp / "palette.rgb"
            native = temp / "native"
            payload = bytes([0x12, 0x34, 0x56])
            expected.write_bytes(b"P6\n1 1\n255\n" + payload)
            vram.write_bytes(b"\x00\x00")
            palette.write_bytes(bytes(768))
            native.write_text(
                "#!/bin/sh\ncp '" + str(expected) + "' \"$STUNRUN_RENDER_PPM\"\n",
                encoding="utf-8")
            native.chmod(0o755)
            result = self.run_tool("run-native-visible-bridge", native, vram,
                                   palette, expected, "--actual", actual)
            report = json.loads(result.stdout)
            self.assertEqual(report["schema"],
                             "stunrun-native-visible-bridge-result/v1")
            self.assertEqual(report["comparison"]["changed_pixels"], 0)

    def test_machine_map_reconciliation_without_roms(self):
        with tempfile.TemporaryDirectory() as temp:
            inventory = pathlib.Path(temp) / "inventory.json"
            inventory.write_text(
                json.dumps({
                    "schema": "test",
                    "system": {"name": "stunrun", "description": "test"},
                    "mame": {"version": "test"},
                    "devices": [
                        {"tag": ":mainpcb:maincpu", "shortname": "m68010", "name": "main"},
                        {"tag": ":mainpcb:gsp", "shortname": "tms34010", "name": "gsp"},
                        {"tag": ":mainpcb:adsp", "shortname": "adsp2100", "name": "adsp"},
                        {"tag": ":mainpcb:jsa:cpu", "shortname": "m6502", "name": "sound"},
                    ],
                }),
                encoding="utf-8",
            )
            result = self.run_tool(
                "check-machine-map",
                ROOT / "analysis/driver-mining/stunrun.machine-map.yaml",
                inventory,
            )
            self.assertIn('"ok": true', result.stdout)

    def test_trace_to_lcov_without_mame(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            listing = temp / "sample.lst"
            trace = temp / "sample.trace"
            output = temp / "sample.info"
            listing.write_text("000000: nop\n000002: rts\n", encoding="utf-8")
            trace.write_text("000000: nop\n000000: nop\n", encoding="utf-8")
            self.run_tool(
                "trace-to-lcov", trace, listing,
                "--source", str(listing), "--test-name", "synthetic", "-o", output,
            )
            report = output.read_text()
            self.assertIn("TN:synthetic", report)
            self.assertIn("DA:1,2", report)
            self.assertIn("LH:1", report)

    def test_diff_memory_snapshot_reports_ranges(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            before = temp / "before.json"
            after = temp / "after.json"
            common = {
                "schema": "stunrun-memory-snapshot/v1",
                "device": ":mainpcb:adsp", "space": "data",
                "base": 0x10, "count": 6, "width": 16,
            }
            before.write_text(json.dumps({**common, "frame": 1,
                                          "values": [0, 1, 2, 3, 4, 5]}))
            after.write_text(json.dumps({**common, "frame": 2,
                                         "values": [0, 1, 9, 8, 4, 5]}))
            result = self.run_tool("diff-memory-snapshot", before, after)
            diff = json.loads(result.stdout)
            self.assertEqual(diff["changed_count"], 2)
            self.assertEqual(diff["changed_ranges"], [{"start": 0x12, "end": 0x13, "count": 2}])
            self.assertEqual(diff["changes"][0]["before"], 2)

    def test_memory_snapshot_exposes_address_stride(self):
        tool = (ROOT / "tools" / "mame-memory-snapshot").read_text()
        lua = (ROOT / "mame" / "lua" / "memory-snapshot.lua").read_text()
        self.assertIn("--address-stride", tool)
        self.assertIn("STUNRUN_MEMORY_STRIDE", tool)
        self.assertIn("offset * address_stride", lua)

    def test_memory_snapshot_exposes_isolated_nvram_directory(self):
        tool = (ROOT / "tools" / "mame-memory-snapshot").read_text()
        self.assertIn("--nvram-directory", tool)
        self.assertIn("-nvram_directory", tool)
        self.assertIn('"nvram_directory"', tool)

    def test_gsp_state_snapshot_can_pair_screen_output(self):
        tool = (ROOT / "tools" / "mame-gsp-state-snapshot").read_text()
        lua = (ROOT / "mame" / "lua" / "gsp-state-snapshot.lua").read_text()
        self.assertIn("--screen", tool)
        self.assertIn("fork_hold_left", tool)
        self.assertIn("fork_hold_right", tool)
        self.assertIn("fork_center", tool)
        self.assertIn("input_mode == 'fork_hold_left'", lua)
        self.assertIn("input_mode == 'fork_hold_right'", lua)
        self.assertIn("input_mode == 'fork_center'", lua)
        self.assertIn("field:set_value() takes", lua)
        self.assertIn("frame = 60, port = ':mainpcb:IN0', field = 'Service Mode', action = 'press'", lua)
        self.assertIn("frame = 120, port = ':mainpcb:IN0', field = 'Service Mode', action = 'release'", lua)
        self.assertIn("STUNRUN_GSP_STATE_SCREEN", lua)
        self.assertIn("screen:snapshot", lua)
        self.assertIn("fine_scroll", lua)

    def test_gsp_vram_dump_reads_the_share_consecutively(self):
        tool = (ROOT / "tools" / "mame-gsp-vram-dump").read_text(encoding="utf-8")
        lua = (ROOT / "mame" / "lua" / "gsp-vram-dump.lua").read_text(encoding="utf-8")
        # The whole point: a consecutive read, not the stride sweep that
        # gsp-state-snapshot.lua uses for its checksum.
        self.assertIn("space:read_u16(base + index * 0x10)", lua)
        self.assertIn("local base = 0xff800000", lua)
        self.assertIn("STUNRUN_GSP_VRAM_WORDS", tool)
        self.assertIn("STUNRUN_GSP_VRAM_OUT", tool)
        self.assertIn('"stunrun-gsp-vram-dump-result/v2"', tool)
        # Zero is valid content during the boot clear; nonzero is descriptive,
        # never a success criterion for a VRAM capture.
        self.assertIn("nonzero", lua)
        self.assertIn("is reported as an observation", tool)
        self.assertIn("dump_sha256", tool)

    def test_gsp_vram_dump_defaults_to_the_physical_512kib_share(self):
        # The TMS34010 address window is 4 Mi bit-address units, but addrshift=3
        # makes that 512 KiB of backing bytes. The old +2 read stride sampled
        # overlapping bit-address windows and produced a bogus 4 MiB file.
        tool = (ROOT / "tools" / "mame-gsp-vram-dump").read_text(encoding="utf-8")
        lua = (ROOT / "mame" / "lua" / "gsp-vram-dump.lua").read_text(encoding="utf-8")
        self.assertIn('default=0x40000', tool)
        self.assertIn("or '262144'", lua)
        self.assertIn("args.words > 0x40000", tool)
        self.assertIn("physical 512 KiB", tool)

    def test_direct_mame_share_capture_is_physical_512kib(self):
        capture = (ROOT / "reference" / "experiments" / "stunrun" /
                   "gsp-direct-share-frame600-service")
        metadata = json.loads((capture / "metadata.json").read_text(encoding="utf-8"))
        blob = capture / "gsp_vram.bin"
        self.assertEqual(metadata["share"]["bytes"], 524288)
        self.assertEqual(blob.stat().st_size, 524288)
        self.assertEqual(hashlib.sha256(blob.read_bytes()).hexdigest(),
                         metadata["share"]["sha256"])
        self.assertEqual(metadata["share"]["words"], 262144)
        self.assertIn("memory_share pointer", metadata["authority"])

    def test_lua_word_stride_matches_mame_direct_share_pointer(self):
        capture = (ROOT / "reference" / "experiments" / "stunrun" /
                   "gsp-share-reader-crosscheck-service")
        metadata = json.loads((capture / "metadata.json").read_text(encoding="utf-8"))
        blob = capture / "gsp_vram.bin"
        self.assertEqual(metadata["comparison"]["direct_share_bytes"], 524288)
        self.assertEqual(metadata["comparison"]["lua_reader_bytes"], 524288)
        self.assertEqual(metadata["comparison"]["differing_bytes"], 0)
        self.assertEqual(hashlib.sha256(blob.read_bytes()).hexdigest(),
                         metadata["comparison"]["sha256_both"])

    def test_gsp_vram_dump_always_sets_an_nvram_directory(self):
        # -nonvram_save stops MAME writing NVRAM but not loading it, so a run
        # without an explicit directory inherits ~/.mame/nvram/stunrun/ and the
        # share reads as 262144 zero words at the correct size.
        tool = (ROOT / "tools" / "mame-gsp-vram-dump").read_text(encoding="utf-8")
        self.assertIn('"-nvram_directory"', tool)
        self.assertIn('"-cfg_directory"', tool)
        self.assertNotIn('"/tmp/stunrun-mame-cfg"', tool)
        self.assertIn("mame-state", tool)

    def test_gsp_vram_dump_agrees_with_state_snapshot_input_modes(self):
        wrapper = (ROOT / "tools" / "mame-gsp-vram-dump").read_text(encoding="utf-8")
        lua = (ROOT / "mame" / "lua" / "gsp-vram-dump.lua").read_text(encoding="utf-8")
        sibling = (ROOT / "mame" / "lua" / "gsp-state-snapshot.lua").read_text(
            encoding="utf-8")
        for mode in ("service", "sw_off_prestart", "drive", "late",
                     "late_drive", "fork_hold_left", "fork_hold_right",
                     "fork_center"):
            self.assertIn(f"input_mode == '{mode}'", lua)
            self.assertIn(f'"{mode}"', wrapper)
        # 'none' is the fallback, not a branch, in both scripts.
        self.assertIn('} or {}', lua)
        self.assertIn('} or {}', sibling)
        self.assertIn('"none"', wrapper)
        # Same timing as the sibling tool, or a dump and a snapshot would
        # silently describe different machine states.
        self.assertIn("frame = 60, port = ':mainpcb:IN0', field = 'Service Mode'", lua)
        self.assertIn("emu.register_prestart", lua)
        self.assertIn("emu.register_frame_done", lua)
        self.assertIn("machine:exit()", lua)

    def test_gsp_vram_dump_refuses_target_after_run_bound(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            output = temp / "out"
            # --target past --frames is rejected before MAME is spawned.
            failed = subprocess.run(
                [str(ROOT / "tools" / "mame-gsp-vram-dump"), "roms", str(output),
                 "--frames", "60", "--target", "600"],
                cwd=ROOT, capture_output=True, text=True)
            self.assertNotEqual(failed.returncode, 0)
            self.assertIn("--target 600 is past --frames 60", failed.stderr)
            self.assertFalse(output.exists())

    def test_export_gsp_native_state_writes_little_endian_fixture(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            common = {
                "schema": "stunrun-memory-snapshot/v1",
                "device": ":mainpcb:gsp", "space": "program",
                "base": 0, "count": 4, "width": 16,
            }
            vram = temp / "vram.json"
            lo = temp / "lo.json"
            hi = temp / "hi.json"
            vram.write_text(json.dumps({**common, "values": [1, 0x2345, 3, 4]}), encoding="utf-8")
            lo.write_text(json.dumps({**common, "values": [0x1000] * 256}), encoding="utf-8")
            hi.write_text(json.dumps({**common, "values": [0x0020] * 256}), encoding="utf-8")
            vram_out = temp / "vram.bin"
            palette_out = temp / "palette.rgb"
            low_out = temp / "palette-low.bin"
            high_out = temp / "palette-high.bin"
            self.run_tool("export-gsp-native-state", vram, lo, hi,
                          "--vram-out", vram_out, "--palette-out", palette_out)
            self.assertEqual(vram_out.read_bytes(), b"".join(
                struct.pack("<H", value) for value in (1, 0x2345, 3, 4)))
            self.assertEqual(palette_out.read_bytes(), bytes([0x10, 0, 0x20]) * 256)
            self.run_tool("export-gsp-native-state", vram, lo, hi,
                          "--vram-out", vram_out, "--palette-out", palette_out,
                          "--palette-low-out", low_out,
                          "--palette-high-out", high_out)
            self.assertEqual(low_out.read_bytes(),
                             struct.pack("<H", 0x1000) * 256)
            self.assertEqual(high_out.read_bytes(),
                             struct.pack("<H", 0x0020) * 256)

    def test_export_geometry_native_state_validates_twin_and_writes_words(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            values = list(range(256)) * 6
            snapshot = temp / "road.json"
            snapshot.write_text(json.dumps({
                "schema": "stunrun-memory-snapshot/v1",
                "device": ":mainpcb:maincpu", "space": "program",
                "base": 0xFF9584, "count": 0x600, "width": 8,
                "frame": 30, "values": values,
            }), encoding="utf-8")
            output = temp / "geometry.bin"
            result = self.run_tool("export-geometry-native-state", snapshot,
                                   "--output", output)
            metadata = json.loads(result.stdout)
            self.assertEqual(metadata["copies"], "match")
            self.assertEqual(metadata["bytes"], 0x300)
            self.assertEqual(output.stat().st_size, 0x300)
            self.assertEqual(output.read_bytes()[:4], b"\x00\x01\x02\x03")

    def test_analyze_memory_candidates_ranks_persistent_diffs(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            baseline, variant = temp / "baseline", temp / "variant"
            baseline.mkdir()
            variant.mkdir()
            for frame, base_values, variant_values in (
                (10, [0, 0, 1, 2], [0, 0, 1, 2]),
                (20, [0, 0, 1, 2], [4, 0, 1, 3]),
                (30, [0, 0, 1, 2], [4, 0, 1, 3]),
            ):
                for directory, values in ((baseline, base_values), (variant, variant_values)):
                    (directory / f"snapshot-{frame}.json").write_text(json.dumps({
                        "schema": "stunrun-memory-snapshot/v1",
                        "frame": frame, "base": 0x100, "count": len(values),
                        "width": 8, "device": ":mainpcb:maincpu",
                        "space": "program", "values": values,
                    }), encoding="utf-8")
            report = temp / "report.json"
            self.run_tool("analyze-memory-candidates", baseline, variant,
                          "--output", report, "--top", "10")
            result = json.loads(report.read_text())
            self.assertEqual(result["common_frames"], [10, 20, 30])
            self.assertEqual(result["candidates"][0]["address"], 0x100)
            self.assertEqual(result["candidates"][0]["address_hex"], "0x100")
            self.assertEqual(result["candidates"][0]["first_diff_frame"], 20)
            self.assertEqual(result["candidates"][0]["persistence_after_first"], 1.0)

    def test_analyze_memory_series_finds_monotonic_word(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            for frame, word in ((10, 100), (20, 90), (30, 80), (40, 80)):
                values = [word >> 8, word & 0xFF, 0xAA, 0x55]
                (temp / f"snapshot-{frame}.json").write_text(json.dumps({
                    "schema": "stunrun-memory-snapshot/v1", "frame": frame,
                    "base": 0x200, "count": 4, "width": 8,
                    "device": ":mainpcb:maincpu", "space": "program",
                    "values": values,
                }), encoding="utf-8")
            report = temp / "report.json"
            self.run_tool("analyze-memory-series", temp, "--output", report, "--top", "10")
            result = json.loads(report.read_text())
            candidate = next(item for item in result["candidates"]
                             if item["address"] == 0x200 and item["width_bytes"] == 2)
            self.assertEqual(candidate["direction"], "decreasing")
            self.assertEqual(candidate["monotonicity"], 1.0)

    def test_diff_ram_write_trace_reports_first_mismatch(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            before = temp / "before.json"
            after = temp / "after.json"
            common = {
                "schema": "stunrun-ram-write-trace-result/v1",
                "device": ":mainpcb:maincpu", "space": "program",
                "base": 0xFF9000, "end": 0xFF9FFF,
                "capture_start_frame": 680, "capture_end_frame": 705,
            }
            before.write_text(json.dumps({**common, "input": "none", "events": [
                {"frame": 680, "pc": 0x100, "address": 0xFF9000, "data": 1, "mask": 0xFF}
            ]}))
            after.write_text(json.dumps({**common, "input": "late_drive", "events": [
                {"frame": 680, "pc": 0x100, "address": 0xFF9000, "data": 2, "mask": 0xFF},
                {"frame": 681, "pc": 0x200, "address": 0xFF9006, "data": 1, "mask": 0xFF}
            ]}))
            result = self.run_tool("diff-ram-write-trace", before, after)
            diff = json.loads(result.stdout)
            self.assertEqual(diff["first_event_mismatch"]["index"], 0)
            self.assertEqual(diff["after_only_writer_pcs"], [0x200])

    def test_ram_write_trace_exposes_early_tap_frame(self):
        wrapper = (ROOT / "tools" / "mame-ram-write-trace").read_text(
            encoding="utf-8")
        lua = (ROOT / "mame" / "lua" / "ram-write-trace.lua").read_text(
            encoding="utf-8")
        self.assertIn('"--tap-frame"', wrapper)
        self.assertIn("STUNRUN_RAM_TRACE_TAP_FRAME", wrapper)
        self.assertIn("STUNRUN_RAM_TRACE_TAP_FRAME", lua)
        self.assertIn("frame == tap_frame", lua)
        self.assertIn('"--delay-frame"', wrapper)
        self.assertIn('"--delay-seconds"', wrapper)
        self.assertIn("STUNRUN_RAM_TRACE_DELAY_FRAME", lua)
        self.assertIn("STUNRUN_RAM_TRACE_DELAY_SECONDS", lua)
        self.assertIn("input_mode == 'service_f2'", lua)

    def test_ram_read_trace_exposes_pc_filter(self):
        wrapper = (ROOT / "tools" / "mame-ram-read-trace").read_text(
            encoding="utf-8")
        lua = (ROOT / "mame" / "lua" / "ram-read-trace.lua").read_text(
            encoding="utf-8")
        self.assertIn('"--pc"', wrapper)
        self.assertIn("STUNRUN_RAM_READ_PC", wrapper)
        self.assertIn("pc_filter", lua)

    def test_ram_read_trace_supports_service_f2_schedule(self):
        wrapper = (ROOT / "tools" / "mame-ram-read-trace").read_text(
            encoding="utf-8")
        lua = (ROOT / "mame" / "lua" / "ram-read-trace.lua").read_text(
            encoding="utf-8")
        self.assertIn('"service_f2"', wrapper)
        self.assertIn("input_mode == 'service_f2'", lua)
        self.assertIn("frame = 60, port = ':mainpcb:IN0'", lua)
        self.assertIn("frame = 120, port = ':mainpcb:IN0'", lua)

    def test_ram_read_write_trace_exposes_same_run_capture(self):
        wrapper = (ROOT / "tools" / "mame-ram-read-write-trace").read_text(
            encoding="utf-8")
        lua = (ROOT / "mame" / "lua" / "ram-write-trace.lua").read_text(
            encoding="utf-8")
        self.assertIn('"--read-base"', wrapper)
        self.assertIn('"--read-end"', wrapper)
        self.assertIn("STUNRUN_RAM_READ_BASE", lua)
        self.assertIn("install_read_tap", lua)
        self.assertIn('"stunrun-ram-read-write-trace-result/v1"', wrapper)
        self.assertIn('"--order"', wrapper)
        self.assertIn("STUNRUN_RAM_TRACE_ORDER", lua)
        self.assertIn('action="append"', wrapper)
        self.assertIn("read_pc_filters", lua)

    def test_capture_adsp_som_transaction_extracts_somclk_to_gint(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            roms = temp / "roms"
            roms.mkdir()
            (roms / "stunrun.manifest.json").write_text("{}\n", encoding="utf-8")
            mame = temp / "mame-stub"
            mame.write_text(
                "#!/usr/bin/env python3\n"
                "print('M1_ADSP_TX_BANK seq=1 frame=134 address=00818016 offset=B bank=1 pc=0002C24E')\n"
                "print('M1_ADSP_TX_EVENT seq=10 frame=550 cpu=adsp access=W address=00002003 data=00000000 mask=0000FFFF pc=00000034 bank=1')\n"
                "print('M1_ADSP_TX_EVENT seq=11 frame=550 cpu=adsp access=W address=00002002 data=00000030 mask=0000FFFF pc=00000515 bank=1')\n"
                "print('M1_ADSP_TX_EVENT seq=12 frame=550 cpu=adsp access=W address=00002006 data=00000045 mask=0000FFFF pc=0000004C bank=1')\n"
                "print('M1_ADSP_TX_EVENT seq=13 frame=550 cpu=maincpu access=W address=00818060 data=00000000 mask=0000FFFF pc=000213FE bank=1')\n"
                "print('M1_ADSP_TX_EVENT seq=14 frame=551 cpu=maincpu access=R address=00810000 data=00000030 mask=0000FFFF pc=0002D000 bank=1')\n"
                "print('M1_ADSP_TX_EVENT seq=15 frame=551 cpu=maincpu access=W address=00C0000C data=00000030 mask=0000FFFF pc=0002D010 bank=1')\n"
                "print('M1_ADSP_TX_DONE frames=556 events=6 truncated=false bank_events=1 tx_start_frame=550 tx_start_seq=10 tx_end_frame=550 tx_end_seq=12')\n",
                encoding="utf-8")
            mame.chmod(0o755)
            output = temp / "capture"
            result = subprocess.run(
                [str(ROOT / "tools" / "capture-adsp-som-transaction"),
                 str(roms), str(output), "--frames", "556", "--mame", str(mame)],
                cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads((output / "result.json").read_text())
            tx = report["transaction"]
            self.assertEqual(report["input"], "none")
            self.assertEqual(tx["somclk"]["seq"], 10)
            self.assertEqual(tx["gint"]["seq"], 12)
            self.assertEqual(tx["host_bank"], 1)
            self.assertEqual(tx["adsp_producer_bank"], 0)
            self.assertEqual(tx["som_words"], [{
                "address_word": 0, "value": 0x30, "pc": 0x515, "seq": 11
            }])
            self.assertEqual(tx["maincpu_irq_acks"][0]["pc"], 0x213FE)
            self.assertEqual(tx["post_window_ferry_observation"]["hstdat_write_count"], 1)

    def test_capture_adsp_som_transaction_rejects_missing_gint_boundary(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            roms = temp / "roms"
            roms.mkdir()
            (roms / "stunrun.manifest.json").write_text("{}\n", encoding="utf-8")
            mame = temp / "mame-stub"
            mame.write_text(
                "#!/usr/bin/env python3\n"
                "print('M1_ADSP_TX_EVENT seq=10 frame=550 cpu=adsp access=W address=00002003 data=00000000 mask=0000FFFF pc=00000034 bank=0')\n"
                "print('M1_ADSP_TX_EVENT seq=11 frame=550 cpu=adsp access=W address=00002002 data=00000030 mask=0000FFFF pc=00000515 bank=0')\n"
                "print('M1_ADSP_TX_DONE frames=556 events=2 truncated=false bank_events=0 tx_start_frame=550 tx_start_seq=10 tx_end_frame=nil tx_end_seq=nil')\n",
                encoding="utf-8")
            mame.chmod(0o755)
            output = temp / "capture"
            result = subprocess.run(
                [str(ROOT / "tools" / "capture-adsp-som-transaction"),
                 str(roms), str(output), "--frames", "556", "--mame", str(mame)],
                cwd=ROOT, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("no complete SOMCLK->GINT transaction", result.stderr)

    # ---- MC68010 -> GSP halt protocol -------------------------------
    #
    # These two guard the two failure modes that produced real dead ends:
    # assembling the ROM image without the even/odd interleave (a silently
    # undecodable image), and reading a bus-event stream at the wrong record
    # stride (a silently plausible one).

    def test_build_maincpu_image_interleaves_even_and_odd_roms(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            roms = temp / "roms"
            roms.mkdir()
            # 4 bytes each: even supplies MSB of each word, odd the LSB.
            (roms / "e.rom").write_bytes(bytes([0x11, 0x33, 0x55, 0x77]))
            (roms / "o.rom").write_bytes(bytes([0xAA, 0xBB, 0xCC, 0xDD]))
            manifest = temp / "m.json"
            manifest.write_text(json.dumps({"roms": [
                {"filename": "e.rom", "region": "mainpcb:maincpu",
                 "size": 4, "offset": 0},
                {"filename": "o.rom", "region": "mainpcb:maincpu",
                 "size": 4, "offset": 1},
            ]}))
            out = temp / "maincpu.bin"
            self.run_tool("build-maincpu-image", "--roms", roms,
                          "--manifest", manifest, "-o", out)
            image = out.read_bytes()
            # Interleaved, not concatenated: 11 AA 33 BB 55 CC 77 DD.
            self.assertEqual(image[:8],
                             bytes([0x11, 0xAA, 0x33, 0xBB,
                                    0x55, 0xCC, 0x77, 0xDD]))

    def test_build_maincpu_image_reports_reset_vector(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            roms = temp / "roms"
            roms.mkdir()
            # Big-endian SP=0xFFFFC000, PC=0x00000216 at address 0.
            sp = bytes([0xFF, 0xFF, 0xC0, 0x00])
            pc = bytes([0x00, 0x00, 0x02, 0x16])
            (roms / "e.rom").write_bytes(sp[0::2] + pc[0::2])
            (roms / "o.rom").write_bytes(sp[1::2] + pc[1::2])
            manifest = temp / "m.json"
            manifest.write_text(json.dumps({"roms": [
                {"filename": "e.rom", "region": "mainpcb:maincpu",
                 "size": 8, "offset": 0},
                {"filename": "o.rom", "region": "mainpcb:maincpu",
                 "size": 8, "offset": 1},
            ]}))
            out = temp / "s.json"
            r = self.run_tool("build-maincpu-image", "--roms", roms,
                              "--manifest", manifest, "--json", "-o", out)
            report = json.loads(r.stdout)
            self.assertEqual(report["schema"], "stunrun-maincpu-image/v1")
            self.assertEqual(report["reset_vector"],
                             {"sp": 0xFFFFC000, "pc": 0x00000216})
            # -o is the image, and the reset vector must be readable from it.
            self.assertEqual(int.from_bytes(out.read_bytes()[0:4], "big"),
                             0xFFFFC000)

    def test_analyze_gsp_hstctl_separates_halt_changing_from_preserving(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            image = bytearray(0x2000)
            target = 0x00C00008
            def put(off, opcode, imm):
                image[off:off + 8] = opcode + struct.pack(">HI", imm, target)
            put(0x100, b"\x33\xfc", 0xB800)   # move.w -> SETS HLT
            put(0x200, b"\x33\xfc", 0x3800)   # move.w -> CLEARS HLT
            put(0x300, b"\x02\x79", 0x7FFF)   # andi.w #$7fff -> CLEARS HLT
            put(0x400, b"\x00\x79", 0x1800)   # ori.w #$1800 -> preserves
            put(0x500, b"\x02\x79", 0xE7FF)   # andi.w #$e7ff -> preserves
            img = temp / "image.bin"
            img.write_bytes(bytes(image))
            out = temp / "s.json"
            r = self.run_tool("analyze-gsp-hstctl", "--image", img,
                              "--json", "-o", out)
            report = json.loads(r.stdout)
            self.assertEqual(report["schema"],
                             "stunrun-gsp-hstctl-sites/v1")
            self.assertEqual(report["site_count"], 5)
            self.assertEqual([s["addr"] for s in report["hlt_setting_sites"]],
                             [0x100])
            self.assertEqual(sorted(s["addr"]
                                    for s in report["hlt_clearing_sites"]),
                             [0x200, 0x300])
            self.assertEqual(sorted(s["addr"]
                                    for s in report["hlt_preserving_sites"]),
                             [0x400, 0x500])

    def test_analyze_gsp_hstctl_rejects_wrong_stride_stream(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            img = temp / "image.bin"
            img.write_bytes(bytes(0x2000))
            # 32-byte records: the stride of a stream captured before
            # `cycles` was added to musashi_gsp_bus_event_t. Must be
            # rejected, not silently misparsed -- see native/musashi_cpu.h.
            stream = temp / "ev.bin"
            stream.write_bytes(bytes(32 * 4))
            r = subprocess.run(
                [str(ROOT / "tools" / "analyze-gsp-hstctl"),
                 "--image", str(img), "--stream", str(stream)],
                cwd=ROOT, capture_output=True, text=True)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("40-byte record", r.stdout + r.stderr)

    def test_analyze_gsp_hstctl_cross_references_stream(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            image = bytearray(0x2000)
            target = 0x00C00008
            # A halt release at 0x200; the recorded PC is the instruction
            # AFTER the store, so the stream carries 0x208.
            image[0x200:0x208] = b"\x33\xfc" + struct.pack(">HI", 0x3800, target)
            img = temp / "image.bin"
            img.write_bytes(bytes(image))

            def rec(seq, cycles, addr, pc, data, kind):
                return (struct.pack("<QQIIHHB", seq, cycles, addr, pc, data,
                                    0xFFFF, 16)
                        + b"\0" * 3
                        + struct.pack("<iB", kind, 0) + b"\0" * 3)
            stream = temp / "ev.bin"
            stream.write_bytes(rec(0, 1000, 0xC00008, 0x208, 0xA000, 1)
                               + rec(1, 9000, 0xC00008, 0x208, 0x3800, 1))
            out = temp / "s.json"
            r = self.run_tool("analyze-gsp-hstctl", "--image", img,
                              "--stream", stream, "--json", "-o", out)
            st = json.loads(r.stdout)["stream"]
            self.assertEqual(st["records"], 2)
            self.assertEqual(st["hstctl_asserts"], 1)
            self.assertEqual(st["hstctl_releases"], 1)
            self.assertEqual(st["first_release_cycle"], 9000)
            # The release must be attributed back to the ROM site.
            self.assertEqual(st["hstctl_writes"][1]["rom_site"], 0x200)

    def test_compare_gsp_host_events_reports_first_semantic_divergence(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            oracle = temp / "oracle.txt"
            native = temp / "native.bin"
            oracle.write_text(
                "1 1 HSTDAT 00C0000C 00001234 0000FFFF 00000000 "
                "00000100 00C0000C 00C00002 00C0000C\n"
                "2 1 HSTCTL 00C00008 0000B800 0000FFFF 00000001 "
                "00000110 00C0000C 00C00002 00000000\n"
                "3 2 GSPRES 0060400C 00000000 0000FFFF 00000001 "
                "00000120 00000000 00000000 00000000\n",
                encoding="ascii")

            def record(seq, cycles, address, pc, data, mask, width, kind,
                       released=0):
                return struct.pack("<QQIIHHB3xIB3x", seq, cycles, address, pc,
                                   data, mask, width, kind, released)

            # Native boot has two events before the MAME tap's first row. The
            # matching PC differs by +2 because REG_PC in the memory callback
            # points at the next unconsumed word; the comparator should use it
            # to align, not to declare the streams unequal.
            native.write_bytes(
                record(0, 0, 0x0060400C, 0x402, 0, 0xFFFF, 16, 2, 0) +
                record(1, 100, 0x00C00008, 0x414, 0xA000, 0xFFFF, 16, 1) +
                record(2, 200, 0x00C0000C, 0x102, 0x1234, 0xFFFF, 16, 1) +
                record(3, 300, 0x00C00008, 0x112, 0xB800, 0xFFFF, 16, 1) +
                record(4, 400, 0x0060400C, 0x122, 0, 0xFFFF, 16, 2, 0))
            result = subprocess.run(
                [str(ROOT / "tools" / "compare-gsp-host-events"),
                 str(oracle), str(native), "--json"],
                cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual(report["schema"], "stunrun-gsp-host-event-compare/v1")
            self.assertEqual(report["status"], "match")
            self.assertEqual(report["native_start_seq"], 2)
            self.assertEqual(report["matched_events"], 3)

            # A broken HSTCTL value must stop at the first mismatch and show
            # both sides, not return a vague final-count difference.
            data = bytearray(native.read_bytes())
            struct.pack_into("<H", data, 3 * 40 + 24, 0x3800)
            native.write_bytes(data)
            result = subprocess.run(
                [str(ROOT / "tools" / "compare-gsp-host-events"),
                 str(oracle), str(native), "--json"],
                cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual(report["status"], "diverged")
            self.assertEqual(report["matched_events"], 1)
            self.assertEqual(report["first_divergence"]["oracle"]["data"], 0xB800)
            self.assertEqual(report["first_divergence"]["native"]["data"], 0x3800)

            # Wrong raw ABI stride is a malformed capture, not an empty match.
            native.write_bytes(native.read_bytes() + b"x")
            result = subprocess.run(
                [str(ROOT / "tools" / "compare-gsp-host-events"),
                 str(oracle), str(native), "--json"],
                cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(json.loads(result.stdout)["status"], "invalid_capture")

    def test_compare_gsp_host_events_keeps_mame_unmapped_host_writes_visible(self):
        """The two producers do not record the same events; do not hide it.

        A real native stream carries 262,766 writes to $00C0000E that the MAME
        tap never records, because MAME's driver only decodes word offsets 0-6 of
        the $C00000-$C03FFF host aperture. Treating that address as an ordinary
        HSTDAT would let a later mismatch look like a data difference when it is
        really a producer difference, so it is named distinctly in the report.
        """
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            oracle = temp / "oracle.txt"
            native = temp / "native.bin"
            oracle.write_text(
                "1 1 HSTDAT 00C0000C 00000000 0000FFFF 00000000 "
                "00000100 00C0000C 00C00002 00C0000C\n",
                encoding="ascii")
            native.write_bytes(
                struct.pack("<QQIIHHB3xIB3x", 0, 0, 0x00C0000C, 0x100, 0, 0xFFFF,
                            16, 1, 0)
                + struct.pack("<QQIIHHB3xIB3x", 1, 10, 0x00C0000E, 0x104, 0, 0xFFFF,
                              16, 1, 0))
            result = subprocess.run(
                [str(ROOT / "tools" / "compare-gsp-host-events"),
                 str(oracle), str(native), "--json"],
                cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual(report["status"], "diverged")
            # The oracle has no further rows, so the mismatch is the exhausted
            # oracle tail, not a fabricated equal record.
            self.assertIsNone(report["first_divergence"]["oracle"])
            self.assertEqual(report["first_divergence"]["native"]["kind"],
                             "HSTDAT-UNMAPPED-0E")

    # ---- tools/mame-gsp-mirror-probe -------------------------------------
    #
    # The VRAM share is 4 MiB, not the 512 KiB both native/ models assume, and
    # 745,384 of the 843,688 captured 0xFFxxxxxx writes land in a different
    # cell as a result (docs/mc68010-boot-findings.md section 19).  These tests
    # exercise the classifier against a recorded probe log by pointing --mame
    # at a stub that emits that log, so the verdict logic is checked without
    # needing MAME or ROMs in the public suite.

    # A real recorded probe log, not a hand-written one: this is the M1_MIRROR
    # output of `tools/mame-gsp-mirror-probe roms <dir> --frames 600 --target
    # 600` against the pinned driver, verbatim, committed under reference/ per
    # the repository's raw-evidence rule.  Using the recording rather than a
    # synthetic one means the test cannot pass by agreeing with a misreading
    # baked into a literal.
    MIRROR_PROBE_LOG_PATH = (ROOT / "reference" / "experiments" / "stunrun"
                             / "gsp-vram-mirror-probe-600" / "mame.log")

    def _mirror_probe_log(self):
        return self.MIRROR_PROBE_LOG_PATH.read_text(encoding="utf-8")

    def _run_mirror_probe_stub(self, temp, log=None, extra=()):
        """Drive mame-gsp-mirror-probe with a stubbed MAME.

        The stub ignores every argument and prints a recorded probe log, so the
        tool's parse/classify/model path runs for real -- MAME is not stubbed
        out inside the tool, only the emulator binary it launches.
        """
        stub = temp / "fake-mame"
        stub.write_text("#!/bin/sh\ncat <<'EOF'\n" +
                        (log if log is not None else self._mirror_probe_log()) +
                        "\nEOF\n", encoding="utf-8")
        stub.chmod(0o755)
        out = temp / "out"
        r = self.run_tool("mame-gsp-mirror-probe", ROOT / "roms", out,
                          "--mame", stub, "--frames", "600", "--target", "600",
                          *extra)
        return r, out / "result.json"

    def test_mirror_probe_reports_share_size_and_flags_shipped_mask(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            r, result_path = self._run_mirror_probe_stub(temp)
            result = json.loads(result_path.read_text(encoding="utf-8"))
            self.assertEqual(result["schema"],
                             "stunrun-gsp-mirror-probe-result/v1")
            self.assertEqual(result["share"]["bytes"], 4194304)
            self.assertEqual(result["share"]["why"], "aliases_offset_0")
            agree = result["offset_agreement"]
            # The declared decode must agree with MAME on every mapped probe.
            self.assertTrue(agree["native_model_correct"]["declared"])
            self.assertEqual(agree["disagree"]["declared"], [])
            # The shipped 512 KiB constant must be shown to be wrong, and wrong
            # exactly at the bit-21 probes.  If this ever passes, the tool has
            # stopped measuring the thing it exists to measure.
            self.assertFalse(agree["native_model_correct"]["as_shipped_512k"])
            bad = agree["disagree"]["as_shipped_512k"]
            self.assertEqual([e["addr"] for e in bad],
                             ["0xffa00000", "0xffe00000"])
            # Addresses MAME does not map at all are reported as such rather
            # than compared: 0xff000000-0xff7fffff is outside the window.
            self.assertEqual([e["addr"] for e in agree["not_mapped"]],
                             ["0xff000000", "0xff200000",
                              "0xff400000", "0xff600000"])
            self.assertIn("WRONG at 2/4", r.stdout)

    def test_mirror_probe_measure_share_is_not_a_naive_round_trip(self):
        # A marker written at base+off and read back at base+off round-trips
        # just as happily when base+off is the bit-22 alias of offset 0, so a
        # plain round-trip cannot bound the share.  The probe must distinguish
        # real storage from a mirror by re-reading offset 0 afterwards.
        lua = (ROOT / "mame" / "lua" / "gsp-mirror-probe.lua").read_text(
            encoding="utf-8")
        self.assertIn("read(space, base) == cand", lua)
        self.assertIn("aliases_offset_0", lua)
        # The reported size must come from the sentinel probe, not from the
        # declaration.  Checking the assignment rather than searching for a
        # number, because the declaration is legitimately named in the
        # surrounding comment.
        self.assertRegex(lua, r"local share_bytes[^=]*=\s*measure_share\(space\)")

    def test_mirror_probe_always_sets_an_nvram_directory(self):
        # Same trap as mame-gsp-vram-dump: -nonvram_save stops MAME writing
        # NVRAM but not loading it, so an unpinned directory silently
        # contaminates the machine state under measurement.
        tool = (ROOT / "tools" / "mame-gsp-mirror-probe").read_text(encoding="utf-8")
        self.assertIn('"-nvram_directory"', tool)
        self.assertIn('"-cfg_directory"', tool)
        self.assertNotIn('"/tmp/stunrun-mame-cfg"', tool)

    def test_mirror_probe_fails_when_the_probe_did_not_run(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            stub = temp / "fake-mame"
            stub.write_text("#!/bin/sh\necho 'no probe output here'\n",
                            encoding="utf-8")
            stub.chmod(0o755)
            r = subprocess.run(
                [str(ROOT / "tools" / "mame-gsp-mirror-probe"),
                 str(ROOT / "roms"), str(temp / "out"),
                 "--mame", str(stub), "--frames", "600", "--target", "600"],
                cwd=ROOT, capture_output=True, text=True)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("GSP mirror probe failed", r.stdout + r.stderr)


# ---- register capture must survive a CPU that lacks a register --------
    #
    # ram-write-trace.lua used to read device.state['A10'].value and
    # device.state['A11'].value unconditionally.  A10/A11 exist on the
    # TMS34010 but not on the MC68010, so indexing nil raised inside the tap
    # callback and every event was dropped: --registers on the 68010 reported
    # "no traffic" instead of "unsupported register".  That is the machine the
    # GSP host window lives on, so it silently blocked every 68010 register
    # trace -- including the text path at $25E8.

    RAM_TRACE_LOG = """\
M1_RAM_WRITE frame=10 pc=00002656 addr=00C00002 data=FF896040 mask=FFFF a0=00FF8960 a1=00001FBC a5=00000000
M1_RAM_WRITE frame=10 pc=00002656 addr=00C00004 data=00000000 mask=FFFF a0=00FF8960 a1=00001FBC a5=00000000
M1_RAM_WRITE frame=11 pc=00002666 addr=00C0000C data=000000DF mask=FFFF a0=00FF8960 a1=00001FBC a5=00000000
M1_RAM_WRITE frame=11 pc=FFF46590 addr=00C0000C data=0000ABCD mask=FFFF a0=00FF1234 a1=00FD0040 a5=00000002 a10=0000DBC0 a11=00000040
M1_RAM_WRITE_DONE frames=12 events=4 truncated=false output=/dev/null
"""

    def _run_ram_write_trace_stub(self, temp, log):
        stub = temp / "fake-mame"
        stub.write_text("#!/bin/sh\ncat <<'EOF'\n" + log + "\nEOF\n",
                        encoding="utf-8")
        stub.chmod(0o755)
        out = temp / "out"
        r = subprocess.run(
            [str(ROOT / "tools" / "mame-ram-write-trace"),
             str(ROOT / "roms"), str(out), "--device", ":mainpcb:maincpu",
             "--base", "0xC00000", "--end", "0xC00020", "--frames", "12",
             "--registers", "--mame", str(stub)],
            cwd=ROOT, capture_output=True, text=True)
        return r, out / "result.json"

    def test_ram_write_trace_register_capture_without_a10_a11(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            r, result_path = self._run_ram_write_trace_stub(
                temp, self.RAM_TRACE_LOG)
            result = json.loads(result_path.read_text(encoding="utf-8"))
            self.assertEqual(result["event_count"], 4,
                             "--registers dropped every event")
            mc68010, gsp = result["events"][0], result["events"][3]
            # The 68010 has A0-A7 only: the keys must be absent, not zero,
            # because a fabricated 0 would read as a real register sample.
            self.assertEqual(mc68010["a0"], 0x00FF8960)
            self.assertNotIn("a10", mc68010)
            self.assertNotIn("a11", mc68010)
            # The GSP line must still parse with all five, unchanged shape.
            self.assertEqual(gsp["a10"], 0x0000DBC0)
            self.assertEqual(gsp["a11"], 0x00000040)

    def test_ram_write_trace_accepts_a_log_without_the_a10_a11_group(self):
        # Covers the wrapper's end-to-end handling of a log whose a10/a11 group
        # is absent.  It does NOT cover the Lua half: the stub MAME never runs
        # ram-write-trace.lua, so reintroducing device.state['A10'].value there
        # still passes here.  That half is verified by a real MAME run against
        # :mainpcb:maincpu, recorded in the commit for this fix; keep this test
        # scoped to what it can actually prove.
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            log = ("M1_RAM_WRITE frame=1 pc=00002566 addr=00C0000C data=00AB "
                   "mask=FFFF a0=00FF8960 a1=00001FBC a5=00000000\n"
                   "M1_RAM_WRITE_DONE frames=1 events=1 truncated=false "
                   "output=/dev/null\n")
            r, result_path = self._run_ram_write_trace_stub(temp, log)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            result = json.loads(result_path.read_text(encoding="utf-8"))
            self.assertEqual(result["events"][0]["data"], 0x00AB)
            self.assertNotIn("a10", result["events"][0])


class OracleFixtureTests(unittest.TestCase):
    """The committed oracle fixture must be the thing its name claims.

    This path has held two wrong files. The first was 1 MiB, named VRAM, and
    started with the MC68010's reset SP/PC -- so it was main RAM. The second was
    4 MiB and read MAME's GSP program address space, which is a span of BIT
    addresses, not physical bytes; the share behind it is 512 KiB.

    The second failure is the one that matters here, because it was committed
    WITH this checker already enforcing its size. A checker that blesses the
    wrong unit conversion is worse than no checker, so these tests pin 512 KiB
    and keep a negative control on the 4 MiB shape specifically.
    """

    FIXTURE = ROOT / "fixtures" / "oracle" / "frame_600" / "frame_600_gsp_vram.bin"
    META = ROOT / "fixtures" / "oracle" / "frame_600" / "metadata.json"
    # SP-338 page 10: 'VRAM Array (16) 64K x 4' = 16 x 256 Kbit = 512 KiB.
    # MAME's memory_share reports the same: share bytes=524288.
    VRAM_BYTES = 0x80000

    def test_committed_gsp_vram_fixture_is_the_512kib_physical_share(self):
        """The shipped fixture passes its own check, with no ROM and no MAME."""
        r = subprocess.run(
            [str(ROOT / "tools" / "verify-oracle-fixture"), str(self.FIXTURE)],
            cwd=ROOT, capture_output=True, text=True,
        )
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

        blob = self.FIXTURE.read_bytes()
        self.assertEqual(len(blob), self.VRAM_BYTES)
        # The specific tell: a file claiming to be the GSP VRAM share must not
        # begin with the MC68010 reset vector, which lives in main RAM.
        self.assertNotEqual(blob[:8], bytes.fromhex("ffffc000 00000216"))
        self.assertTrue(any(blob), "an all-zero capture looks successful but is not")

    def test_checker_rejects_the_4mib_program_space_sample(self):
        """Negative control for the unit error, which is the one that recurred.

        A 4194304-byte file of GSP program addresses is not a VRAM image: the
        program space is configured datawidth 16, addrshift 3, so one address
        unit is one BIT and 4 MiB of addresses is 512 KiB of physical bytes.
        This control is what stops the checker being relaxed back to 4 MiB to
        accommodate a capture that was never VRAM.
        """
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            too_big = temp / "gsp_vram.bin"
            too_big.write_bytes(bytes(0x400000))
            r = subprocess.run(
                [str(ROOT / "tools" / "verify-oracle-fixture"), str(too_big),
                 "--metadata", str(self.META)],
                cwd=ROOT, capture_output=True, text=True,
            )
            self.assertEqual(r.returncode, 1, "a 4 MiB program-space sample must fail")
            self.assertIn("524288", r.stderr)

    def test_metadata_records_the_address_conversion(self):
        """The conversion is the trap; it belongs in the provenance, not in prose."""
        meta = json.loads(self.META.read_text(encoding="utf-8"))
        self.assertEqual(meta["share"]["bytes"], self.VRAM_BYTES)
        self.assertIn(">> 3", meta["addressing"]["conversion"])
        # Both superseded files must stay on the record.
        self.assertEqual(meta["supersedes"]["first"]["first_8_bytes"],
                         "ffffc000 00000216")
        self.assertEqual(meta["supersedes"]["second"]["size_bytes"], 0x400000)

    def test_checker_rejects_the_old_main_ram_fixture(self):
        """Negative control: the superseded file must fail, loudly.

        Reconstructed from the shape it had, not from git, so the test keeps
        its meaning even if the history is rewritten. What matters is that each
        failure mode is caught independently.
        """
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            bad = temp / "frame_600_gsp_vram.bin"
            # Right name, wrong content: main RAM's reset vector at offset 0.
            bad.write_bytes(bytes.fromhex("ffffc00000000216") + bytes(1048576 - 8))
            r = subprocess.run(
                [str(ROOT / "tools" / "verify-oracle-fixture"), str(bad),
                 "--metadata", str(self.META)],
                cwd=ROOT, capture_output=True, text=True,
            )
            self.assertEqual(r.returncode, 1, "wrong-sized main-RAM file must fail")
            self.assertIn("4194304", r.stderr)

    def test_checker_rejects_a_correctly_sized_but_wrong_blob(self):
        """Negative control: size alone is not enough.

        A silent single-bit corruption passes every size and content-shape
        check; only the recorded sha256 catches it. Without this test the
        checker could pass for the wrong reason on a truncated or substituted
        capture.
        """
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            blob = bytearray(self.FIXTURE.read_bytes())
            blob[0x2000] ^= 0xFF
            tampered = temp / "vram.bin"
            tampered.write_bytes(bytes(blob))
            r = subprocess.run(
                [str(ROOT / "tools" / "verify-oracle-fixture"), str(tampered),
                 "--metadata", str(self.META)],
                cwd=ROOT, capture_output=True, text=True,
            )
            self.assertEqual(r.returncode, 1, "tampered blob must fail on sha256")
            self.assertIn("sha256", r.stderr)

    def test_checker_rejects_an_all_zero_capture(self):
        """Negative control: a misconfigured read yields the right size, all zeros.

        This is the documented failure of running MAME without -nvram_directory,
        and it produces a file that every size check accepts.
        """
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            zeros = temp / "vram.bin"
            zeros.write_bytes(bytes(self.VRAM_BYTES))
            r = subprocess.run(
                [str(ROOT / "tools" / "verify-oracle-fixture"), str(zeros),
                 "--metadata", str(self.META)],
                cwd=ROOT, capture_output=True, text=True,
            )
            self.assertEqual(r.returncode, 1, "all-zero capture must fail")
            self.assertIn("zero", r.stderr)

    def test_metadata_records_the_command_and_the_superseded_files(self):
        """Provenance is the part that was missing, so it is pinned too.

        The command must be the DIRECT-SHARE capture, not mame-gsp-vram-dump.
        The latter reads GSP program addresses and produced the 4 MiB
        address-space sample; naming it here would quietly re-license the unit
        error this fixture was re-pointed to escape.
        """
        meta = json.loads(self.META.read_text(encoding="utf-8"))
        self.assertEqual(meta["schema"], "stunrun-oracle-fixture/v1")
        self.assertTrue(meta["capture"]["command"])
        self.assertIn("mame-gsp-state-snapshot", meta["capture"]["command"])
        self.assertNotIn("mame-gsp-vram-dump", meta["capture"]["command"])
        self.assertEqual(meta["share"]["bytes"], self.VRAM_BYTES)
        # Both superseded files' mislabels must stay on the record.
        self.assertEqual(meta["supersedes"]["first"]["first_8_bytes"],
                         "ffffc000 00000216")
        self.assertEqual(meta["supersedes"]["second"]["size_bytes"], 0x400000)


class GspFirstOutputTests(unittest.TestCase):
    """tools/check-gsp-first-output must decide the question, not just run.

    The claim under test is narrow: inside the window that the pre/post-release
    pair itself defines, the GSP core's output is byte-identical to MAME's and
    touches exactly the words MAME touched. These are synthetic images -- the
    real ones need MAME, a ROM set and 200k GSP cycles -- so the tests here are
    about the decision logic, and the real run is recorded in findings 36.
    """

    SHARE = 0x80000

    def _images(self, temp, core_words=None):
        """A pre/post pair with a known first output, and a matching core dump."""
        pre = bytearray(self.SHARE)
        post = bytearray(self.SHARE)
        core = bytearray(self.SHARE)
        lo = 0x1234
        for i in range(4):
            w = 0x2000 + i
            post[lo + 2 * i] = w & 0xFF
            post[lo + 2 * i + 1] = w >> 8
        if core_words is None:
            core[lo:lo + 8] = post[lo:lo + 8]   # the core produced the same thing
        else:
            for i, w in enumerate(core_words):
                core[lo + 2 * i] = w & 0xFF
                core[lo + 2 * i + 1] = w >> 8
        p = pathlib.Path(temp)
        for name, buf in (("pre.bin", pre), ("post.bin", post), ("core.bin", core)):
            (p / name).write_bytes(bytes(buf))
        return p / "core.bin", p / "pre.bin", p / "post.bin"

    def test_matching_core_passes(self):
        with tempfile.TemporaryDirectory() as temp:
            core, pre, post = self._images(temp)
            out = pathlib.Path(temp) / "r.json"
            r = subprocess.run(
                [str(ROOT / "tools" / "check-gsp-first-output"), str(core),
                 str(pre), str(post), "--json", str(out)],
                cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            report = json.loads(out.read_text(encoding="utf-8"))
            self.assertTrue(report["ok"])
            self.assertEqual(report["mame_changed_words"], 4)
            self.assertEqual(report["core"]["changed_only_core"], 0)
            self.assertEqual(report["core"]["changed_only_mame"], 0)

    def test_wrong_value_in_the_core_fails(self):
        """Negative control: matching byte COUNTS is not the claim.

        A core that writes the right number of words with the wrong values is
        the failure this check exists to catch, so a count-based test would
        pass it.
        """
        with tempfile.TemporaryDirectory() as temp:
            core, pre, post = self._images(
                temp, core_words=[0x2000, 0x2001, 0x2002, 0xBEEF])
            r = subprocess.run(
                [str(ROOT / "tools" / "check-gsp-first-output"), str(core),
                 str(pre), str(post)],
                cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
            self.assertIn("RESULT FAIL", r.stdout)

    def test_core_writing_nothing_fails(self):
        """Negative control: a halted core must not pass.

        This is the specific false pass that matters -- the core staying in
        reset produces zero writes, which trivially matches the pre-release
        image, and only the MAME-changed set catches it.
        """
        with tempfile.TemporaryDirectory() as temp:
            core, pre, post = self._images(temp, core_words=[])
            r = subprocess.run(
                [str(ROOT / "tools" / "check-gsp-first-output"), str(core),
                 str(pre), str(post)],
                cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
            self.assertIn("by MAME only: 4", r.stdout)

    def test_refuses_a_4mib_address_space_image(self):
        """Negative control: the size guard, on the exact confusion that recurred.

        4194304 bytes is a GSP program-address image, not the 512 KiB physical
        share. A reader without this guard invents plausible data instead of
        failing, and that mislabelled file has been committed here before.
        """
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            too_big = temp / "addrspace.bin"
            too_big.write_bytes(bytes(0x400000))
            r = subprocess.run(
                [str(ROOT / "tools" / "check-gsp-first-output"), str(too_big),
                 str(too_big), str(too_big)],
                cwd=ROOT, capture_output=True, text=True)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("524288", r.stderr)

    def test_identical_pre_and_post_is_an_error_not_a_pass(self):
        """No boundary means no first output; that is a failed capture."""
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            same = temp / "same.bin"
            same.write_bytes(bytes(self.SHARE))
            r = subprocess.run(
                [str(ROOT / "tools" / "check-gsp-first-output"), str(same),
                 str(same), str(same)],
                cwd=ROOT, capture_output=True, text=True)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("no boundary", r.stderr)


if __name__ == "__main__":
    unittest.main()
