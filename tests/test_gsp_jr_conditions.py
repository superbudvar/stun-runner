"""The TMS34010 JR/JA condition table, held against three independent readings.

`analysis/gsp-program-upload.md` recorded a condition table that stopped at
`$600` and said so. This file closes that gap and then keeps it closed, by
comparing three implementations of the same sixteen conditions:

  1. **MAME's disassembler** -- `34010dsm.cpp`, `print_condition_code`, the
     switch that turns opcode bits `[11:8]` into the two characters appended to
     `JR`/`JA`.
  2. **MAME's executor** -- `34010ops.hxx`, the 48 `j_<CC>_<form>` functions,
     plus `34010tbl.hxx`, the opcode table that decides which of them a given
     opcode word calls. Per findings §85.5 the executor is the oracle: it is the
     code that produced every behaviour in `reference/`.
  3. **SPVU001A** (S9), the TI TMS34010 User's Guide, §12 "Assembly Language
     Instruction Set". Each of `JAcc` (pp. 12-79/12-80/12-81), `JRcc` ±127
     words (pp. 12-82/12-83) and `JRcc` ±32K words (pp. 12-84/12-85) carries its
     own "Condition Codes" table. Three copies of the same table in one document,
     transcribed here into (a) a boolean predicate and (b) the manual's own
     "Flags for Branch" example column.

Why compare all three rather than just copying a table. The tempting version of
this task is "transcribe `print_condition_code`", and that is a copy with no
teeth: if the disassembler prints `NE` for `0x0b00` while the executor runs
`j_EQ_0` there, a transcribed table is happily wrong and says nothing. The
comparison below would fail. That is the whole point -- see
`docs/open-questions-index.md` F8/T9 and findings §83.4, where a mnemonic and
its own expansion were compared as if they were rival claims and a false
upstream bug report nearly resulted.

ROM-free: no capture, no ROM set, no MAME binary is read. Only the pinned MAME
*source* tree, and only when it is present (`@skipUnless`).

SPVU001A is not held in this repository (docs/sources.md S9); the transcription
below is from a document read on 2026-10-17 and its page numbers are recorded
at the point of use. Nothing here requires the manual to be online: the test is
built so that MAME's two readings are checked against each other, and against
the transcription, from the source tree alone.
"""

from __future__ import annotations

import itertools
import pathlib
import re
import unittest

MAME_SRC = pathlib.Path("/mnt/build/mame-ref")
DSM = MAME_SRC / "src/devices/cpu/tms34010/34010dsm.cpp"
OPS = MAME_SRC / "src/devices/cpu/tms34010/34010ops.hxx"
TBL = MAME_SRC / "src/devices/cpu/tms34010/34010tbl.hxx"
CPP = MAME_SRC / "src/devices/cpu/tms34010/tms34010.cpp"

NEED_SOURCE = unittest.skipUnless(
    DSM.is_file() and OPS.is_file() and TBL.is_file(),
    f"pinned MAME TMS34010 source not available at {MAME_SRC}")


# --------------------------------------------------------------------------
# A predicate evaluator, so "these are the same condition" is decided by
# arithmetic over all sixteen flag combinations rather than by string equality.
# The grammar is exactly what 34010ops.hxx uses and no more:
#     flag     ::= C_FLAG() | Z_FLAG() | N_FLAG() | V_FLAG()
#     expr     ::= or
#     or       ::= and ('||' and)*
#     and      ::= unary ('&&' unary)*
#     unary    ::= '!' unary | atom
#     atom     ::= flag | '0' | '1' | '(' or ')'
#
# Every truth-table tuple is in NCZV order -- N, C, Z, V. That is not a guess:
# SPVU001A Figure 5-4 and Table 5-2 (pp. 5-20/5-21) place N at bit 31, C at 30,
# Z at 29 and V at 28, so the "Flags for Branch" column headed NCZV is read
# N, C, Z, V left to right. Reading it as N, C, V, Z instead makes JRZ
# (`xx1x`) and JALT (`0xx1`, `1xx0`) disagree about which flag the third
# character means, and the whole example table stops being self-consistent.
#
# Using the manual's own order means its example groups and the evaluated
# predicate can be compared with no translation step of our own to be wrong.
# --------------------------------------------------------------------------

FLAGS = ("N", "C", "Z", "V")
_TOKEN = re.compile(r"\s*(?:(\|\||&&|!)|([()01])|([CVZN]_FLAG\(\)))")


def _tokenize(text: str) -> list[tuple[str, str]]:
    out, pos = [], 0
    while pos < len(text):
        match = _TOKEN.match(text, pos)
        if not match:
            raise ValueError(f"cannot tokenize {text!r} at offset {pos}")
        out.append(next(g for g in match.groups() if g is not None))
        pos = match.end()
    return out


def _parse(text: str):
    tokens = _tokenize(text)
    pos = 0

    def peek():
        return tokens[pos] if pos < len(tokens) else None

    def eat(kind):
        nonlocal pos
        got = peek()
        if got != kind:
            raise ValueError(f"expected {kind!r} in {text!r}, got {got!r}")
        pos += 1

    def atom():
        nonlocal pos
        token = peek()
        if token is None:
            raise ValueError(f"truncated predicate {text!r}")
        if token == "(":
            eat("(")
            node = or_expr()
            eat(")")
            return node
        if token in ("0", "1"):
            pos += 1
            return ("const", token == "1")
        if token and token.endswith("_FLAG()"):
            pos += 1
            return ("flag", token[0])
        raise ValueError(f"unexpected {token!r} in {text!r}")

    def unary():
        nonlocal pos
        if peek() == "!":
            eat("!")
            return ("not", unary())
        return atom()

    def and_expr():
        nonlocal pos
        node = unary()
        while peek() == "&&":
            eat("&&")
            node = ("and", node, unary())
        return node

    def or_expr():
        nonlocal pos
        node = and_expr()
        while peek() == "||":
            eat("||")
            node = ("or", node, and_expr())
        return node

    tree = or_expr()
    if pos != len(tokens):
        raise ValueError(f"trailing tokens in predicate {text!r}: {tokens[pos:]}")
    return tree


def _evaluate(tree, flags: dict[str, bool]) -> bool:
    kind = tree[0]
    if kind == "const":
        return tree[1]
    if kind == "flag":
        return flags[tree[1]]
    if kind == "not":
        return not _evaluate(tree[1], flags)
    if kind == "and":
        return _evaluate(tree[1], flags) and _evaluate(tree[2], flags)
    if kind == "or":
        return _evaluate(tree[1], flags) or _evaluate(tree[2], flags)
    raise AssertionError(f"unknown node {kind!r}")


def truth_table(predicate: str) -> frozenset[tuple[bool, ...]]:
    """Every NCZV combination, in that order, for which `predicate` is true."""
    return frozenset(
        combo for combo in itertools.product((False, True), repeat=4)
        if _evaluate(_parse(predicate), dict(zip(FLAGS, combo)))
    )


def _ncvz(combo: tuple[bool, ...]) -> str:
    """Render a tuple the way SPVU001A's example column does: NCZV, x for 0."""
    return "".join("1" if bit else "x" for bit in combo)


def compare_predicates(left: str, right: str) -> str | None:
    """None when the two predicates select the same flag combinations."""
    difference = truth_table(left) ^ truth_table(right)
    if not difference:
        return None
    detail = ", ".join(_ncvz(combo) for combo in sorted(difference, key=str))
    return f"differ on NCZV = {detail}"


# --------------------------------------------------------------------------
# SPVU001A (S9) section 12, transcribed.
#
# `ISA_PREDICATE[cc]` is the manual's own "Condition Codes" table restated as a
# boolean expression over the four status bits. Page numbers per block:
#   JAcc          p. 12-80 (Unsigned/Signed Compare to Zero) and p. 12-81
#                 (General Arithmetic)
#   JRcc +-127    p. 12-82 and p. 12-83
#   JRcc +-32K    p. 12-84 and p. 12-85
# The three tables are printed separately in the manual and agree; this file
# uses the JAcc table as the transcription and the JRcc tables as a second
# reading via ISA_EXAMPLES below.
#
# The manual's "Status Bits" column prints logical-NOT as a bar over the flag,
# which the OCR of the scanned copy renders as an absent or substituted
# character. ISA_EXAMPLES below is transcribed from a *different* column of the
# *same* table -- the "Flags for Branch" example column, where every group is
# four characters in NCZV order with `x` for "don't care" -- and is what the
# test actually compares. Two columns, one reading each, agreeing by arithmetic.
# --------------------------------------------------------------------------

ISA_PREDICATE = {
    "UC": "1",                                   # Unconditional; no conditions
    "P":  "!N_FLAG() && !Z_FLAG()",              # Positive
    "LS": "C_FLAG() || Z_FLAG()",                # Lower or same
    "HI": "!C_FLAG() && !Z_FLAG()",              # Higher than
    "LT": "(N_FLAG() && !V_FLAG()) || (!N_FLAG() && V_FLAG())",
    "GE": "(N_FLAG() && V_FLAG()) || (!N_FLAG() && !V_FLAG())",
    "LE": "(N_FLAG() && !V_FLAG()) || (!N_FLAG() && V_FLAG()) || Z_FLAG()",
    "GT": "(N_FLAG() && V_FLAG() && !Z_FLAG()) || (!N_FLAG() && !V_FLAG() && !Z_FLAG())",
    "C":  "C_FLAG()",
    "NC": "!C_FLAG()",
    "EQ": "Z_FLAG()",
    "NE": "!Z_FLAG()",
    "V":  "V_FLAG()",
    "NV": "!V_FLAG()",
    "N":  "N_FLAG()",
    "NN": "!N_FLAG()",
}

# SPVU001A p. 12-81 (JAcc), p. 12-83 (JRcc +-127), p. 12-85 (JRcc +-32K):
# "Flags for Branch", each group four characters in NCZV order, `x` = don't
# care. The assembler takes the jump when any one of the groups matches.
#
# This is a SECOND reading of the same table, from a different column: the
# Status Bits / formula column below states each condition as a boolean
# expression, this one states it as a set of flag patterns. They are
# transcribed independently and reconciled only by arithmetic, so agreement is
# evidence and a copy is not.
#
# Glyph caveat, recorded rather than hidden: the OCR of the scanned copies
# renders digit zero as `O` in some places (`Oxx1` for `0xx1`) and digit one as
# `l` (`xxxl` for `xxx1`). Both the transputer.net and vgamuseum.info copies of
# the document were OCR'd independently and produce the same strings at the
# same positions, so the character POSITIONS are solid; the digits have been
# normalised to 0/1 here, and every group below is unambiguous once the header
# is read as N, C, Z, V.
ISA_EXAMPLES = {
    "UC": ("xxxx",),
    "P":  ("0x0x",),
    "LS": ("xx1x", "x1xx"),
    "HI": ("x00x",),
    "LT": ("0xx1", "1xx0"),
    "GE": ("0xx0", "1xx1"),
    "LE": ("0xx1", "1xx0", "xx1x"),
    "GT": ("0x00", "1x01"),
    "C":  ("x1xx",),
    "NC": ("x0xx",),
    "EQ": ("xx1x",),
    "NE": ("xx0x",),
    "V":  ("xxx1",),
    "NV": ("xxx0",),
    "N":  ("1xxx",),
    "NN": ("0xxx",),
}


def _expand(groups: tuple[str, ...]) -> frozenset[tuple[bool, ...]]:
    taken = set()
    for group in groups:
        if len(group) != 4:
            raise ValueError(f"NCZV group {group!r} is not four characters")
        for combo in itertools.product((False, True), repeat=4):
            if all(g == "x" or (g == "1") == bit for g, bit in zip(group, combo)):
                taken.add(combo)
    return frozenset(taken)


# The disassembler prints the unconditional code as two spaces, so the mnemonic
# column of the printed table is blank for UC and the resulting assembly text is
# bare `JR`. Anything that reads this table has to know that.
DISASSEMBLER_BLANK = "  "

CONDITION_ORDER = ("UC", "P", "LS", "HI", "LT", "GE", "LE", "GT",
                   "C", "NC", "EQ", "NE", "V", "NV", "N", "NN")


# --------------------------------------------------------------------------
# Parsers for the two MAME sources.
# --------------------------------------------------------------------------

_DSM_CASE = re.compile(
    r'case\s+(0x[0-9a-f]{4})\s*:\s*stream\s*<<\s*"([^"]*)"\s*;\s*break\s*;'
    r'(?:\s*/\*\s*(.*?)\s*\*/)?')


def disassembler_conditions(source: str = None) -> dict[int, str]:
    """opcode[11:8] -> the exact string `print_condition_code` appends."""
    text = DSM.read_text(encoding="utf-8") if source is None else source
    body = text.split("void tms34010_disassembler::print_condition_code", 1)[1]
    body = body.split("\n}\n", 1)[0]
    found: dict[int, str] = {}
    for code, printed, comment in _DSM_CASE.findall(body):
        value = int(code, 16)
        if value in found:
            raise AssertionError(f"print_condition_code lists {code} twice")
        found[value] = printed
    return found


def disassembler_comments(source: str = None) -> dict[int, str]:
    """The `/* ... */` note MAME keeps beside a case, where it has one."""
    text = DSM.read_text(encoding="utf-8") if source is None else source
    body = text.split("void tms34010_disassembler::print_condition_code", 1)[1]
    body = body.split("\n}\n", 1)[0]
    return {int(code, 16): (comment or "").strip()
            for code, _, comment in _DSM_CASE.findall(body)}


_OPS_FN = re.compile(
    r"void tms340x0_device::j_([A-Z]+)_([08x])\(uint16_t op\)\s*\n\{\s*\n"
    r"\s*j_xx_\2\((.+?)\);\s*\n\}")


def executor_predicates(source: str = None) -> dict[tuple[str, str], str]:
    """(condition name, form) -> the boolean text passed to j_xx_*."""
    text = OPS.read_text(encoding="utf-8") if source is None else source
    found = {}
    for name, form, predicate in _OPS_FN.findall(text):
        found[(name, form)] = predicate.strip()
    return found


_TBL_ROW = re.compile(r"//\s*0x(c[0-9a-f]{3})\s*\n((?:\s*&tms34010_device::[^\n]*\n)+)")


def executor_rows(source: str = None) -> dict[int, list[str]]:
    """opcode >> 8 (0xc000..0xcf00) -> the sixteen handler names, columns 0..15.

    `s_opcode_table` is `[65536 >> 4]` and is indexed `s_opcode_table[op >> 4]`
    (tms34010.cpp), so a row is one opcode high byte and a column is one low
    nibble. The JR family therefore binds a *condition* to the row, and the
    *form* to the column.
    """
    text = TBL.read_text(encoding="utf-8") if source is None else source
    table = text.split("s_opcode_table", 1)[1]
    rows: dict[int, list[str]] = {}
    for label, block in _TBL_ROW.findall(table):
        names = [name for name in re.findall(r"&tms34010_device::(\w+)", block)]
        if len(names) != 16:
            raise AssertionError(
                f"row 0x{label} has {len(names)} entries, expected 16")
        rows.setdefault(int(label, 16), names)
    return rows


def dispatcher_index(source: str = None) -> str:
    """The expression `s_opcode_table` is indexed with, read out of tms34010.cpp."""
    text = CPP.read_text(encoding="utf-8") if source is None else source
    found = re.findall(r"s_opcode_table\[([^\]]+)\]", text)
    if not found:
        raise AssertionError("no s_opcode_table dispatch found in tms34010.cpp")
    return found[0]


def dstreg_definition(source: str = None) -> str:
    text = CPP.read_text(encoding="utf-8") if source is None else source
    found = re.findall(r"#define\s+DSTREG\(O\)\s+(.+)", text)
    if not found:
        raise AssertionError("no DSTREG macro found in tms34010.cpp")
    return found[0].strip()


# --------------------------------------------------------------------------
# The shared checks, factored out so the negative control can run exactly the
# same code against a perturbed input.
# --------------------------------------------------------------------------

def mnemonic_for(code: int, table: dict[int, str]) -> str:
    """The ISA condition name the disassembler's printed string stands for."""
    printed = table[code]
    if printed == DISASSEMBLER_BLANK:
        return "UC"
    return printed.strip()


def check_condition_table(table: dict[int, str],
                          predicates: dict[tuple[str, str], str]) -> list[str]:
    """Every printed mnemonic expands to the ISA predicate for that code.

    Two independent things have to hold, and both are checked here: the name
    the disassembler prints has to be the right name for that code, and the
    executor's predicate under that name has to be the right predicate.
    """
    problems = []
    for code, printed in sorted(table.items()):
        name = mnemonic_for(code, table)
        expected = CONDITION_ORDER[code >> 8]
        if name != expected:
            problems.append(
                f"0x{code:04x}: SPVU001A's condition code {expected} is named "
                f"{name!r} in the disassembler's table")
            continue
        for form in ("0", "8", "x"):
            got = predicates.get((name, form))
            if got is None:
                problems.append(f"{name}_{form}: no such executor function")
                continue
            difference = compare_predicates(got, ISA_PREDICATE[name])
            if difference:
                problems.append(
                    f"0x{code:04x} ({name}, form {form}): executor {got!r} vs "
                    f"SPVU001A {ISA_PREDICATE[name]!r} {difference}")
    return problems


def check_row_binding(table: dict[int, str], rows: dict[int, list[str]]) -> list[str]:
    """Row 0xcN00 must call the handler for condition code 0xN00, in every column.

    The row *is* the condition and the column *is* the form, because the
    dispatcher indexes `s_opcode_table[op >> 4]` -- one row per opcode high
    byte, one column per low nibble. Getting this binding wrong is the failure
    this check exists for: the disassembler would print one condition while the
    executor branched on another.
    """
    problems = []
    for code in sorted(table):
        # The JR family is the opcode range 0xc000..0xcf00, so condition code
        # 0x0N00 is table row 0xcN00 -- the high byte carries the JR opcode and
        # the second nibble the condition.
        row = rows.get(0xc000 | code)
        if row is None:
            problems.append(f"0x{code:04x}: no opcode-table row")
            continue
        want = mnemonic_for(code, table)
        for column, handler in enumerate(row):
            match = re.fullmatch(r"j_(.+)_(0|8|x)", handler)
            if match is None:
                problems.append(f"0x{code:04x} col {column}: unexpected {handler!r}")
            elif match.group(1) != want:
                problems.append(
                    f"0x{code:04x} col {column}: disassembler prints {want!r} but "
                    f"the executor calls {handler!r}")
    return problems


def check_examples(predicates: dict[tuple[str, str], str],
                   examples: dict[str, tuple[str, ...]] | None = None) -> list[str]:
    """MAME's predicate must take the branch exactly where SPVU001A says it does."""
    problems = []
    for name, groups in sorted((examples or ISA_EXAMPLES).items()):
        want = _expand(groups)
        for form in ("0", "8", "x"):
            got = predicates.get((name, form))
            if got is None:
                problems.append(f"{name}_{form}: no such executor function")
                continue
            difference = truth_table(got) ^ want
            if difference:
                problems.append(
                    f"{name}_{form}: executor takes the branch on NCZV="
                    f"{_ncvz(sorted(difference, key=str)[0])} which SPVU001A's "
                    f"example groups {groups} do not cover")
    return problems


# --------------------------------------------------------------------------

@NEED_SOURCE
class DisassemblerConditionTableTests(unittest.TestCase):
    """`print_condition_code` -- the table the bead said stopped at `$600`."""

    def test_the_table_covers_every_condition_field_exactly_once(self):
        table = disassembler_conditions()
        self.assertEqual(sorted(table),
                         [i << 8 for i in range(16)],
                         "print_condition_code must enumerate 0x0000..0x0f00 "
                         "and nothing else")

    def test_the_field_is_opcode_bits_11_to_8(self):
        # `op & 0x0f00` is what makes these condition fields; if the mask moves,
        # every mnemonic below names a different condition.
        source = DSM.read_text(encoding="utf-8")
        body = source.split("void tms34010_disassembler::print_condition_code", 1)[1]
        self.assertIn("switch (op & 0x0f00)", body.split("\n}\n", 1)[0])

    def test_the_unconditional_code_prints_blank_and_is_called_uc(self):
        table = disassembler_conditions()
        self.assertEqual(table[0x0000], DISASSEMBLER_BLANK)
        self.assertEqual(mnemonic_for(0x0000, table), "UC")
        # MAME says so in a comment on the case itself; the name `UC` appears
        # nowhere else in the table, which is why the blank needs pinning.
        self.assertIn("UC", disassembler_comments()[0x0000])

    def test_the_table_spells_the_sixteen_isa_conditions_in_order(self):
        table = disassembler_conditions()
        self.assertEqual([mnemonic_for(code, table) for code in sorted(table)],
                         list(CONDITION_ORDER))

    def test_the_printing_pads_to_a_three_character_mnemonic(self):
        # `JR` + condition must be three characters wide, so the single-letter
        # conditions are printed with a trailing space. A future edit that drops
        # the space produces `JRP` -> `JRNE` -> `JRN` style collisions.
        table = disassembler_conditions()
        for code, printed in sorted(table.items()):
            if code != 0x0000:
                self.assertEqual(len(printed), 2, f"0x{code:04x} prints {printed!r}")
                self.assertTrue(printed.endswith(" ")
                                or len(printed) == 2,
                                f"0x{code:04x} prints {printed!r}")

    def test_the_ja_and_jr_prefixes_come_from_the_low_byte_of_the_opcode(self):
        source = DSM.read_text(encoding="utf-8")
        body = source.split("\tcase 0xc000:", 1)[1].split("break;", 1)[0]
        self.assertIn('util::stream_format(stream, "JA")', body)
        self.assertIn('util::stream_format(stream, "JR")', body)
        self.assertIn("(op & 0x00ff) == 0x80", body)


@NEED_SOURCE
class ExecutorConditionTableTests(unittest.TestCase):
    """`34010ops.hxx` / `34010tbl.hxx` -- the code that actually ran."""

    def setUp(self):
        self.predicates = executor_predicates()
        self.table = disassembler_conditions()

    def test_every_condition_has_all_three_forms(self):
        self.assertEqual(len(self.predicates), 16 * 3,
                         "expected j_<CC>_<0|8|x> for sixteen conditions")
        for name in CONDITION_ORDER:
            for form in ("0", "8", "x"):
                self.assertIn((name, form), self.predicates)

    def test_the_three_forms_of_a_condition_share_one_predicate(self):
        # This is the internal-consistency check proper: a divergence between
        # j_UC_0 and j_UC_x would branch differently depending on which form the
        # opcode word used, and nothing else in MAME would notice.
        for name in CONDITION_ORDER:
            forms = {self.predicates[(name, form)] for form in ("0", "8", "x")}
            self.assertEqual(len(forms), 1,
                             f"{name}: predicates differ between forms: {forms}")

    def test_the_unconditional_predicate_is_the_literal_one(self):
        for form in ("0", "8", "x"):
            self.assertEqual(self.predicates[("UC", form)], "1")

    def test_every_executor_predicate_is_boolean(self):
        for (name, form), predicate in sorted(self.predicates.items()):
            try:
                truth_table(predicate)
            except ValueError as error:
                self.fail(f"j_{name}_{form}: {error}")

    def test_the_opcode_table_rows_bind_each_code_to_its_condition(self):
        self.assertEqual(check_row_binding(self.table, executor_rows()), [])

    def test_no_jr_row_is_dispatched_to_an_illegal_opcode_handler(self):
        rows = executor_rows()
        for code in range(0xc000, 0xd000, 0x100):
            self.assertNotIn("illop", rows[code])

    def test_the_dispatch_column_is_the_low_nibble(self):
        # Everything about which *form* runs rests on this: if the table were
        # indexed by more than the low nibble, the row would not be the whole
        # story and `j_<CC>_8` would not be reachable only at op & 0x0f == 8.
        self.assertEqual(dispatcher_index(), "op >> 4")
        self.assertEqual(dstreg_definition(), "((O) & 0x0f)")


@NEED_SOURCE
class IsaAgreementTests(unittest.TestCase):
    """MAME's two readings, against SPVU001A section 12."""

    def setUp(self):
        self.predicates = executor_predicates()
        self.table = disassembler_conditions()

    def test_no_disagreements_between_the_disassembler_and_the_executor(self):
        self.assertEqual(check_condition_table(self.table, self.predicates), [])

    def test_the_executor_agrees_with_spvu001a_branch_flag_examples(self):
        self.assertEqual(check_examples(self.predicates), [])

    def test_gt_is_covered_by_both_readings_of_the_manual(self):
        # GT is the condition most likely to be got wrong (it is the only one
        # that is a disjunction of two three-way conjunctions), so it is the one
        # condition held by both the formula column and the example column.
        self.assertIn("GT", ISA_EXAMPLES)
        self.assertEqual(compare_predicates(self.predicates[("GT", "0")],
                                            ISA_PREDICATE["GT"]), None)
        self.assertEqual(truth_table(self.predicates[("GT", "0")]), _expand(ISA_EXAMPLES["GT"]))

    def test_the_example_column_only_needs_the_column_order_to_read(self):
        # If the "NCZV" header were read N,C,V,Z instead of N,C,Z,V, JRZ and
        # JALT would disagree about the third character and the table would
        # stop being self-consistent. Holding the two readings apart keeps the
        # transcription honest about which order it assumed.
        self.assertEqual(FLAGS, ("N", "C", "Z", "V"))
        self.assertNotEqual(_expand(("xx1x",)), _expand(("0xx1",)))

    def test_positive_is_not_n_and_not_z(self):
        # docs/open-questions-index.md correction 2: the disassembler prints `P `
        # and the executor takes `!N && !Z`, and those are the *same* condition,
        # not a divergence. Held here so it cannot be re-reported as a bug.
        self.assertEqual(self.table[0x0100], "P ")
        self.assertEqual(self.predicates[("P", "0")], "!N_FLAG() && !Z_FLAG()")
        # P is taken whenever N=0 and Z=0, whatever C and V are: four of the
        # sixteen NCZV combinations.
        self.assertEqual(
            sorted(truth_table(self.predicates[("P", "0")])),
            [(False, False, False, False), (False, False, False, True),
             (False, True, False, False), (False, True, False, True)])

    def test_the_complement_pairs_are_complements(self):
        for left, right in (("C", "NC"), ("EQ", "NE"), ("V", "NV"),
                            ("N", "NN"), ("LS", "HI")):
            self.assertEqual(
                compare_predicates(
                    self.predicates[(left, "0")],
                    "!(" + self.predicates[(right, "0")] + ")"),
                None, f"{left} is not the complement of {right}")

    def test_the_signed_pair_splits_the_zero_and_carry_free_case(self):
        # GE and LT are exhaustive: one of them is always true, and they differ
        # only on N ^ V. Worth holding because both are written as a disjunction
        # of two conjunctions and a typo in either half would still parse.
        ge = truth_table(self.predicates[("GE", "0")])
        lt = truth_table(self.predicates[("LT", "0")])
        self.assertEqual(ge | lt, frozenset(itertools.product((False, True), repeat=4)))
        self.assertEqual(ge & lt, frozenset())


class NegativeControlTests(unittest.TestCase):
    """Proof the checks above can fail.

    A comparison test that has never been seen to fail is not evidence. These
    run the *same* functions the real assertions run, against inputs perturbed
    in one place each, and require the perturbation to be reported.
    """

    def test_a_flipped_equality_predicate_is_caught(self):
        # Perturb MAME's EQ into NE and the ISA comparison must notice.
        self.assertIsNone(compare_predicates("Z_FLAG()", ISA_PREDICATE["EQ"]))
        perturbed = executor_predicates().copy()
        for form in ("0", "8", "x"):
            perturbed[("EQ", form)] = "!Z_FLAG()"
        problems = check_condition_table(disassembler_conditions(), perturbed)
        self.assertEqual(len(problems), 3,
                         f"one report per form expected, got {problems}")
        self.assertTrue(all("0x0a00" in problem for problem in problems), problems)

    def test_a_swapped_executor_row_is_caught(self):
        # Move LT's opcode row onto GE's handler, which is what a copy-paste
        # in 34010tbl.hxx would look like.
        rows = executor_rows()
        rows[0xc400], rows[0xc500] = rows[0xc500], rows[0xc400]
        problems = check_row_binding(disassembler_conditions(), rows)
        self.assertEqual(len(problems), 32, "both directions, sixteen columns each")
        self.assertTrue(all("0x0400" in p or "0x0500" in p for p in problems), problems)

    def test_a_renamed_disassembler_condition_is_caught(self):
        # EQ's code carrying NE's name. The *predicate* check cannot see this
        # -- NE is a real condition with a real predicate -- so the name binding
        # is what has to catch it.
        table = disassembler_conditions()
        table[0x0a00] = "NE"
        problems = check_condition_table(table, executor_predicates())
        self.assertTrue(any("0x0a00" in problem and "EQ" in problem
                            for problem in problems), problems)

    def test_a_renamed_disassembler_condition_is_caught_by_the_row_binding(self):
        table = disassembler_conditions()
        table[0x0a00] = "NE"
        problems = check_row_binding(table, executor_rows())
        self.assertEqual(len(problems), 16)
        self.assertTrue(all("0x0a00" in problem for problem in problems), problems)

    def test_an_example_column_that_contradicts_the_formula_is_caught(self):
        # SPVU001A's EQ example group is `xx1x`; assert the opposite and the
        # checker must notice. First establish that the real column is clean,
        # so a "it flagged something" result cannot come from a broken checker.
        self.assertEqual(check_examples(executor_predicates()), [],
                         "the unperturbed example column must be clean first")
        bad = dict(ISA_EXAMPLES)
        bad["EQ"] = ("xx0x",)
        reported = check_examples(executor_predicates(), examples=bad)
        self.assertEqual(sum(problem.startswith("EQ_") for problem in reported), 3,
                         f"one report per EQ form expected, got {reported}")

    def test_a_reassociation_is_not_reported_as_a_disagreement(self):
        # Control for the previous tests: the checker compares truth tables, so
        # a harmless re-association must NOT be flagged. Without this, "it
        # flagged something" would not mean "it can tell right from wrong".
        self.assertIsNone(compare_predicates(
            "N_FLAG() && V_FLAG() || !N_FLAG() && !V_FLAG()",
            ISA_PREDICATE["GE"]))
        self.assertIsNone(compare_predicates(
            "1", "1 || 0"))
        self.assertIsNotNone(compare_predicates(
            "N_FLAG() && V_FLAG() && !Z_FLAG()", ISA_PREDICATE["GE"]))

    def test_perturbing_the_real_mame_source_is_caught(self):
        """End to end: perturb a copy of the pinned source, not a dict.

        The other controls perturb a parsed structure. This one edits the text
        `34010ops.hxx` actually contains, so it also proves the parser reads the
        file rather than something cached, and that the comparison is wired to
        it. The file under test is a COPY in a temp directory; nothing in
        /mnt/build/mame-ref is written.
        """
        pristine = OPS.read_text(encoding="utf-8")
        self.assertIn("j_xx_0(Z_FLAG());", pristine)
        perturbed = pristine.replace("j_xx_0(Z_FLAG());", "j_xx_0(!Z_FLAG());", 1)
        self.assertNotEqual(perturbed, pristine)

        predicates = executor_predicates(perturbed)
        self.assertEqual(predicates[("EQ", "0")], "!Z_FLAG()")
        table = disassembler_conditions()

        reported = check_condition_table(table, predicates)
        self.assertTrue(reported, "the perturbed EQ predicate was not reported")
        self.assertTrue(any("0x0a00" in problem for problem in reported), reported)
        self.assertTrue(any(problem.startswith("EQ_")
                            for problem in check_examples(predicates)))

        # The unperturbed source must be clean, or "it reported something"
        # would prove nothing.
        self.assertEqual(check_condition_table(table, executor_predicates()), [])
        self.assertEqual(check_examples(executor_predicates()), [])

    def test_perturbing_the_real_opcode_table_is_caught(self):
        """Swap two handler rows in a copy of `34010tbl.hxx`."""
        pristine = TBL.read_text(encoding="utf-8")
        self.assertIn("j_LT_0", pristine)
        # Point row 0xc400 at GE's handlers -- a copy-paste slip in the table.
        row = re.search(r"//\s*0xc400\s*\n(?:\s*&tms34010_device::[^\n]*\n)+", pristine)
        self.assertIsNotNone(row, "row 0xc400 not found in 34010tbl.hxx")
        swapped = row.group(0).replace("j_LT_", "j_GE_")
        perturbed = pristine.replace(row.group(0), swapped)
        self.assertNotEqual(perturbed, pristine)

        rows = executor_rows(perturbed)
        self.assertEqual(rows[0xc400][0], "j_GE_0")
        problems = check_row_binding(disassembler_conditions(), rows)
        self.assertEqual(len(problems), 16)
        self.assertTrue(all("0x0400" in problem for problem in problems), problems)
        self.assertEqual(
            check_row_binding(disassembler_conditions(), executor_rows()), [])

    def test_the_parser_rejects_a_malformed_predicate(self):
        for bad in ("C_FLAG() &&", "C_FLAG() Z_FLAG()", "(C_FLAG()", "C_FLAG() & Z"):
            with self.assertRaises(ValueError, msg=bad):
                truth_table(bad)


if __name__ == "__main__":
    unittest.main()