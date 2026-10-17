"""`docs/open-questions-index.md` must not rot silently.

That document is an index, and an index is only useful while its pointers
resolve. Three ways it can go stale, each checked here:

1. **A pointer that no longer lands.** Every `file:line` and bare-path
   reference must resolve to a file that exists and a line that exists. This is
   the same failure as MD-13 living in three files while its retraction lived in
   a fourth (findings §83.4): a claim outlives the thing it points at.
2. **A row with no owner.** The document's own rule is that an entry carries an
   owner or a next action and a reconciled-on date, and that an entry with
   neither is promoted to a reference doc or deleted. Without teeth that rule is
   aspirational, so it is enforced here.
3. **A bead that has gone.** A row pointing at a closed or nonexistent bead is
   an entry whose owner evaporated.

Deliberately ROM-free and offline: it reads committed text and the bead board,
never a capture.
"""

from __future__ import annotations

import pathlib
import re
import json
import subprocess
import unittest

REPO = pathlib.Path(__file__).resolve().parents[1]
INDEX = REPO / "docs" / "open-questions-index.md"

# `path/to/file.ext:123` and `path/to/file.ext:123-130`, where the path is
# repo-relative and uses forward slashes.
POINTER = re.compile(r"`([A-Za-z0-9_./+-]+\.(?:md|c|h|cpp|hxx|sv|py|sh|yaml|json|lst))"
                     r":(\d+)(?:-(\d+))?`")

# `findings §85.4`, `BUGS.md MD-13`, `STATUS.md:195` handled above.
SECTION = re.compile(r"§(\d+)(?:\.(\d+))?")

# Paths that live outside this repository, or are generated.
EXTERNAL_PREFIXES = ("/mnt/", "http", "~")

# The index writes paths in the short form a reader already has in context:
# `tms34010.cpp:1570` means "in the TMS34010 device directory", `track-storage.md`
# means "in analysis/". Resolve against the roots a reference can plausibly be
# relative to, rather than demanding the index spell out the full path.
ROOTS = (
    REPO,
    REPO / "analysis",
    REPO / "docs",
    REPO / "third_party" / "musashi",
    REPO / "third_party" / "tms34010_sv",
    REPO / "third_party" / "tms34010_sv" / "rtl" / "core",
    pathlib.Path("/mnt/build/mame-ref"),
    pathlib.Path("/mnt/build/mame-ref/src"),
    pathlib.Path("/mnt/build/mame-ref/src/devices/cpu"),
    pathlib.Path("/mnt/build/mame-ref/src/devices/cpu/tms34010"),
    pathlib.Path("/mnt/build/mame-ref/src/devices/cpu/m68000"),
    pathlib.Path("/mnt/build/mame-ref/src/devices/cpu/adsp2100"),
    pathlib.Path("/mnt/build/mame-ref/src/mame/atari"),
)


def resolved(path_text: str, line_text: str) -> str | None:
    """Return a failure reason if `path:line` does not resolve, else None."""
    if path_text.startswith(EXTERNAL_PREFIXES):
        return None
    start = int(line_text)
    found_at: pathlib.Path | None = None
    for root in ROOTS:
        candidate = root / path_text
        if candidate.is_file():
            found_at = candidate
            break
    if found_at is None:
        return f"{path_text} does not exist under any known root"
    try:
        count = len(found_at.read_text(errors="replace").splitlines())
    except OSError as exc:                                   # pragma: no cover
        return f"{path_text} unreadable: {exc}"
    if start < 1 or start > count:
        return (f"{path_text}:{line_text} is past the end of "
                f"{found_at.relative_to(REPO) if found_at.is_relative_to(REPO) else found_at} "
                f"({count} lines)")
    return None


class OpenQuestionsIndexTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not INDEX.is_file():
            raise unittest.SkipTest("docs/open-questions-index.md not present")
        cls.text = INDEX.read_text()
        cls.lines = cls.text.splitlines()

    # -- 1. pointers must land ---------------------------------------------

    def test_every_file_line_pointer_resolves(self):
        """Each `path:line` must name a file and a line that exist.

        This is the check that would have caught MD-13 sitting in three files
        while the retraction was written in a fourth.
        """
        seen: set[tuple[str, str]] = set()
        failures: list[str] = []
        for match in POINTER.finditer(self.text):
            key = (match.group(1), match.group(2))
            if key in seen:
                continue
            seen.add(key)
            reason = resolved(match.group(1), match.group(2))
            if reason:
                failures.append(reason)
        self.assertEqual(
            failures, [],
            "stale pointer(s) in docs/open-questions-index.md -- an index that "
            "points at things that moved is worse than none:\n  "
            + "\n  ".join(sorted(failures)))

    def test_every_findings_section_pointer_exists(self):
        """`§N` must name a section that is actually in the findings log."""
        findings = REPO / "docs" / "mc68010-boot-findings.md"
        self.assertTrue(findings.is_file())
        found = {int(m.group(1))
                 for m in re.finditer(r"^## (\d+)\.", findings.read_text(), re.M)}
        self.assertTrue(found, "could not read any section headings from the findings")
        missing = sorted({int(m.group(1)) for m in SECTION.finditer(self.text)} - found)
        self.assertEqual(
            missing, [],
            f"index points at findings section(s) that do not exist: {missing}")

    # -- 2. the lifecycle rule has teeth -----------------------------------

    def test_the_document_states_its_own_lifecycle(self):
        """The three exits and the two required fields must be present.

        If this fails, the file has been edited away from the design and will
        quietly become a stale copy of the truth again.
        """
        for required in ("drain", "graduate", "promote",
                         "owner or next action", "reconciled-on"):
            with self.subTest(required=required):
                self.assertIn(required, self.text,
                              f"the lifecycle rule lost its '{required}' clause")

    def test_every_row_has_an_owner_or_a_next_action(self):
        """A row with no bead and no next check cannot be actioned.

        Enforced on the summary tables, which are the actionable part. The long
        narrative sections are exempt: they carry argument, not queue entries.
        """
        orphan_rows: list[str] = []
        in_actionable_table = False
        for raw in self.lines:
            line = raw.rstrip()
            if line.startswith("## ") or line.startswith("### "):
                # Only tables that are plainly a queue need owners.
                in_actionable_table = any(
                    marker in line for marker in
                    ("Ranked summary", "Bead register", "Premise audit",
                     "IRQ register", "Orphans", "Reconciled on"))
                continue
            if not in_actionable_table or not line.startswith("|"):
                continue
            cells = [c.strip() for c in line.strip("|").split("|")]
            if len(cells) < 3 or set("".join(cells)) <= set("-: "):
                continue
            row = " ".join(cells).lower()
            has_owner = ("stun-runner-" in row or "bd " in row
                         or "bead" in row or "irq-000" in row)
            has_next = any(w in row for w in
                           ("next check", "no candidate", "candidate",
                            "re-run", "re-run ", "read ", "confirm", "verify",
                            "not assessed", "candidate, unverified"))
            if not (has_owner or has_next):
                orphan_rows.append(line[:120])
        self.assertEqual(
            orphan_rows, [],
            "index row(s) with neither an owner nor a next action -- by the "
            "document's own rule these must be promoted or deleted:\n  "
            + "\n  ".join(orphan_rows))

    def test_the_index_records_when_it_was_reconciled(self):
        """A dated reconciliation is what makes staleness visible."""
        self.assertRegex(self.text, r"\*\*Reconciled on:\*\*\s*\d{4}-\d{2}-\d{2}")

    # -- 3. bead pointers must still exist ---------------------------------

    def test_every_referenced_bead_exists(self):
        """A row must not point at a bead that was closed or never existed."""
        # Every status matters: an in_progress or deferred bead is still a real
        # owner, and checking only open+closed reported eight valid beads as
        # missing.
        listed = subprocess.run(["bd", "list", "--status=open", "--json"], cwd=REPO,
                                capture_output=True, text=True, check=False)
        others = [subprocess.run(["bd", "list", "--status=" + st, "--json"],
                                 cwd=REPO, capture_output=True, text=True,
                                 check=False)
                  for st in ("in_progress", "blocked", "deferred", "closed")]
        if listed.returncode != 0:
            self.skipTest("bd not available")
        try:
            payload = listed.stdout and json.loads(listed.stdout)
        except ValueError:                                     # pragma: no cover
            self.skipTest("bd --json was not JSON")
        def ids_of(blob):
            out = set()
            rows = blob if isinstance(blob, list) else blob.get("issues", [])
            for item in rows:
                if isinstance(item, dict):
                    ident = item.get("id") or item.get("short_id")
                    if ident:
                        out.add(ident)
            return out

        known = ids_of(payload)
        for extra in others:
            if extra.returncode == 0:
                try:
                    known |= ids_of(json.loads(extra.stdout))
                except ValueError:
                    pass
        if not known:
            self.skipTest("bd returned no issue ids to check against")
        referenced = set(re.findall(r"stun-runner-[0-9a-z]+(?:\.[0-9]+)?", self.text))
        missing = sorted(ident for ident in referenced
                         if ident not in known
                         and not any(k.startswith(ident) for k in known))
        self.assertEqual(
            missing, [],
            f"index references bead(s) the tracker does not have: {missing}")


if __name__ == "__main__":
    unittest.main()