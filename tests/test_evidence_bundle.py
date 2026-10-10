import json
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "evidence-bundle"


class EvidenceBundleTests(unittest.TestCase):
    def test_create_and_validate_hashes_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            artifact = root / "trace.log"
            artifact.write_text("frame=1 pc=1234\n", encoding="utf-8")
            bundle = root / "evidence.json"
            subprocess.run([
                str(TOOL), "create", str(bundle), "--root", str(root),
                "--source-kind", "mame", "--source-id", "stunrun-frame-1",
                "--confidence", "KNOWN", "--claim", "screen captured",
                "--artifact", "trace.log=runtime-trace",
            ], check=True, capture_output=True, text=True)
            result = subprocess.run([
                str(TOOL), "validate", str(bundle), "--root", str(root),
            ], check=True, capture_output=True, text=True)
            self.assertTrue(json.loads(result.stdout)["valid"])
            data = json.loads(bundle.read_text(encoding="utf-8"))
            self.assertEqual(data["artifacts"][0]["path"], "trace.log")
            self.assertEqual(data["artifacts"][0]["role"], "runtime-trace")

    def test_validate_rejects_changed_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            artifact = root / "dump.bin"
            artifact.write_bytes(b"original")
            bundle = root / "evidence.json"
            subprocess.run([
                str(TOOL), "create", str(bundle), "--root", str(root),
                "--source-kind", "rom", "--source-id", "rev6",
                "--artifact", "dump.bin=raw-dump",
            ], check=True, capture_output=True, text=True)
            artifact.write_bytes(b"changed")
            result = subprocess.run([
                str(TOOL), "validate", str(bundle), "--root", str(root),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("changed", result.stderr)


if __name__ == "__main__":
    unittest.main()
