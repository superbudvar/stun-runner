import importlib.machinery
import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LOADER = importlib.machinery.SourceFileLoader("geometry_viewer", str(ROOT / "tools/geometry-viewer"))
SPEC = importlib.util.spec_from_loader("geometry_viewer", LOADER)
VIEWER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VIEWER)


class GeometryViewerTests(unittest.TestCase):
    def test_loads_big_endian_table_and_writes_svg(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "table.bin"
            output = Path(directory) / "view.svg"
            source.write_bytes(b"\x00" * 4 + struct.pack(">4H", 0xFFFF, 2, 3, 4))
            words = VIEWER.load_words(source, 4, 4)
            self.assertEqual(words, [0xFFFF, 2, 3, 4])
            VIEWER.render(words * 96, output)
            text = output.read_text(encoding="utf-8")
            self.assertIn("table adjacent", text)
            self.assertIn("exploratory only", text)
            self.assertIn("#66ccff", text)

    def test_rejects_short_input(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "short.bin"
            source.write_bytes(b"\x00")
            with self.assertRaises(ValueError):
                VIEWER.load_words(source, 0, 2)


if __name__ == "__main__":
    unittest.main()
