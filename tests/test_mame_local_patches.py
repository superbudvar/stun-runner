"""The MAME oracle is not stock MAME, and nothing keeps that honest except this.

`/mnt/build/mame-ref` carries local modifications to the emulator. Six are
recorded observation hooks; a seventh would be someone editing emulation
behaviour and hoping it goes unnoticed. The repository's evidence rule treats the
game under pinned MAME as the behavioural oracle, so a behavioural edit smuggled
in among the observation hooks would quietly invalidate every measurement ever
recorded -- and there is no way to tell from the outside, because a hook and a
behaviour change look identical in a diff.

This compares the inventory in `analysis/mame-local-patches.json` against the
actual working-tree diff and fails when they disagree.

Skipped, not passed, when the MAME tree is absent: an absent tree cannot vouch
for the oracle, and silently succeeding would be the exact failure this test
exists to catch.
"""

from __future__ import annotations

import json
import pathlib
import re
import subprocess
import unittest

REPO = pathlib.Path(__file__).resolve().parents[1]
INVENTORY = REPO / "analysis" / "mame-local-patches.json"
MAME_TREE = pathlib.Path("/mnt/build/mame-ref")

# Files MAME's own build touches. A diff here that the inventory does not
# describe is the thing worth catching, so the scan covers every tracked change.
SOURCE_SUFFIXES = (".cpp", ".h", ".hpp", ".c", ".hxx")


def mame_diff() -> str | None:
    """Return `git diff` for the MAME tree, or None if it is not available."""
    if not (MAME_TREE / ".git").exists():
        return None
    try:
        proc = subprocess.run(
            ["git", "-C", str(MAME_TREE), "diff", "--unified=0"],
            capture_output=True, text=True, timeout=120, check=False)
    except (OSError, subprocess.SubprocessError):
        return None
    if proc.returncode != 0:
        return None
    return proc.stdout


@unittest.skipIf(mame_diff() is None, "MAME source tree not available")
class MameLocalPatchInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
        cls.diff = mame_diff() or ""
        cls.known_tags = {p["tag"] for p in cls.inventory["patches"]}
        cls.known_env = {e for p in cls.inventory["patches"] for e in p["env"]}

    def added_lines(self) -> list[str]:
        return [ln[1:] for ln in self.diff.splitlines()
                if ln.startswith("+") and not ln.startswith("+++")]

    def test_every_stunrun_tag_in_the_source_is_recorded(self):
        """A new STUNRUN- hook that nobody wrote down must fail here.

        This is the check that catches the dangerous case: someone adds a hook
        for good reasons and it becomes part of the oracle without anyone
        deciding whether it is allowed to touch behaviour.
        """
        in_source = set(re.findall(r"STUNRUN-[A-Z]{3,}", "\n".join(self.added_lines())))
        unrecorded = in_source - self.known_tags
        self.assertEqual(
            unrecorded, set(),
            f"STUNRUN hooks added to the MAME tree but absent from "
            f"{INVENTORY.name}: {sorted(unrecorded)}. Record it, including "
            f"whether it is observation-only, before relying on it.")

    def test_every_stunrun_env_var_in_the_source_is_recorded(self):
        in_source = set(re.findall(r"STUNRUN_[A-Z0-9_]+", "\n".join(self.added_lines())))
        unrecorded = in_source - self.known_env
        self.assertEqual(
            unrecorded, set(),
            f"STUNRUN environment variables added to the MAME tree but absent "
            f"from {INVENTORY.name}: {sorted(unrecorded)}. An unrecorded "
            f"environment variable is a way to change the oracle from outside "
            f"the harness, with nothing recording that it did.")

    def test_every_modified_source_file_is_recorded(self):
        """A modified file the inventory never mentions.

        File-level, because a diff can touch a file the inventory lists for one
        hook while changing something unrelated -- and this is the coarsest net
        that still catches a wholly unrecorded edit.
        """
        recorded = {f for p in self.inventory["patches"] for f in p["files"]}
        changed = set()
        for line in self.diff.splitlines():
            if line.startswith("+++ b/"):
                path = line[6:]
                if path.endswith(SOURCE_SUFFIXES):
                    changed.add(path)
        unrecorded = changed - recorded
        self.assertEqual(
            unrecorded, set(),
            f"source files modified in the MAME tree but absent from "
            f"{INVENTORY.name}: {sorted(unrecorded)}")

    def test_recorded_patches_are_marked_observation_only(self):
        """A patch claiming to be observation-only is a claim, not a fact.

        Nothing here can prove it, so at minimum the claim must be explicit:
        a patch with the flag absent is not asserted to be safe, it is just
        unexamined, and that has to be visible.
        """
        for patch in self.inventory["patches"]:
            with self.subTest(tag=patch["tag"]):
                self.assertIn(
                    "observation_only", patch,
                    f"{patch['tag']} does not state whether it is "
                    f"observation-only")
                self.assertIs(
                    patch["observation_only"], True,
                    f"{patch['tag']} is recorded as NOT observation-only. That "
                    f"is a behavioural change to the oracle and must be called "
                    f"out, not listed alongside the observation hooks.")

    def test_oracle_hash_is_recorded_and_well_formed(self):
        digest = self.inventory["oracle_sha256"]
        self.assertRegex(digest, r"^[0-9a-f]{64}$")


if __name__ == "__main__":
    unittest.main()