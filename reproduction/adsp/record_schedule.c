#include "record_schedule.h"
#include "som_source_select.h"

#include <string.h>

/* The MP=1 SIM-address bank base is the one som_source_select.c reads through
 * (harddriv_m.cpp:896, `m_adsp_eprom_base = 0x10000 * data`). Bind to that
 * value rather than restating it: a change there must break this slice too. */
typedef char stunrun_adsp_record_schedule_mp_words_is_0x10000[
    (STUNRUN_ADSP_SOM_SIMBUF_MP_WORDS == 0x10000u) ? 1 : -1];

/* The SIMBUF read port. harddriv_m.cpp:846-850 serves DM($2000) from
 * m_sim_memory[m_adsp_eprom_base + m_adsp_sim_address++] -- one word, then the
 * address advances by one. On the board the same read is the /SIMBUF port at
 * the address /SIMLD (DM($2001)) latched, auto-incrementing; the capture
 * cannot tell the two apart, because $017B latches exactly the address the port
 * is already about to read and $019B advances DM($0034) by the same two words
 * the port consumes. This models the latch -- DM($0034) is the single source of
 * truth -- with the per-read increment the capture does show.
 *
 * MAME's DM($2001) write is a no-op (`hdadsp_special_w` case 1 is /SIMLD and
 * does nothing), so under MAME alone the latch has no effect at all. Both
 * readings agree on every pass of the captured chain and this evidence cannot
 * separate them; the disagreement is recorded in the findings rather than
 * resolved here. */
static int simbuf_read(const uint16_t *rom, size_t word_count, uint32_t base_word,
                       uint16_t mp, uint32_t sim_address, uint32_t *word_index,
                       uint16_t *value)
{
    uint32_t absolute;
    size_t offset;

    if (mp != 1u)
        return 0;
    if (sim_address >= STUNRUN_ADSP_SOM_SIMBUF_MP_WORDS)
        return 0;
    absolute = STUNRUN_ADSP_SOM_SIMBUF_MP_WORDS + sim_address;
    if (absolute < base_word)
        return 0;
    offset = (size_t)(absolute - base_word);
    if (offset >= word_count)
        return 0;
    *word_index = absolute;
    *value = rom[offset];
    return 1;
}

int stunrun_adsp_record_schedule_run(
    const uint16_t *rom_words, size_t rom_word_count, uint32_t rom_base_word,
    uint16_t mp, uint32_t entry_dm0034,
    stunrun_adsp_record_schedule_t *out)
{
    stunrun_adsp_record_schedule_t built;
    uint16_t dm0034, dm2001, dm0036, dm0035, dm0038;
    size_t pass;

    if (rom_words == NULL || out == NULL || mp != 1u || rom_word_count == 0u)
        return 0;
    /* $0179 writes SI straight into DM($0034) and $017B hands it to /SIMLD, so
     * a value at or past the $10000-word /MP=1 bank base is not a SIM address
     * this port can serve. */
    if (entry_dm0034 >= STUNRUN_ADSP_SOM_SIMBUF_MP_WORDS)
        return 0;

    memset(&built, 0, sizeof(built));
    built.entry_dm0034 = (uint16_t)entry_dm0034;

    /* $0174-$0178 clear L3-L7; $0179 DM($0034) = SI stores the caller's SIM
     * address. SI's own origin is the $0172 SIMBUF read and is out of scope, so
     * entry_dm0034 arrives here as a measured input. */
    dm0034 = (uint16_t)entry_dm0034;
    /* DM($2001), DM($0035) and DM($0036) are all overwritten before this
     * routine reads any of them, so their pre-entry values are not inputs.
     * DM($0038) is not: $017C/$017E rewrite it on every pass. */
    dm2001 = 0u;
    dm0036 = 0u;
    dm0035 = 0u;
    dm0038 = 0u;

    for (pass = 0; pass < STUNRUN_ADSP_RECORD_SCHEDULE_MAX_PASSES; ++pass) {
        stunrun_adsp_record_schedule_pass_t *entry = &built.passes[pass];
        uint16_t ax1, si, af;
        unsigned jump_index;

        /* $017A AY1 = DM($0034); $017B DM($2001) = AY1. */
        entry->dm2001_latched = dm0034;
        dm2001 = dm0034;

        /* $017C L1 = $0000; $017D L2 = $0000; $017E DM($0038) = L1. Nothing
         * between $0174 and $017E reads DM($0038), so this store is
         * unconditional and its value is zero on every modelled path. */
        dm0038 = 0u;
        entry->dm0038 = dm0038;

        /* $017F AX1 = DM($2000) -- the pointer word. */
        if (!simbuf_read(rom_words, rom_word_count, rom_base_word, mp, dm2001,
                         &entry->start_word_index, &ax1))
            return 0;

        /* $0180 AY0 = $0187; $0181 L0 = $0000. */
        /* $0182 SI = DM($2000) -- the field word at the next address. */
        if (!simbuf_read(rom_words, rom_word_count, rom_base_word, mp,
                         (uint32_t)dm2001 + 1u,
                         &entry->command_word_index, &si))
            return 0;
        entry->start = ax1;
        entry->command = si;

        /* $0183 SR = LSHIFT SI BY -12 (LO) is a logical RIGHT shift by 12:
         * shift_op_imm() takes the count as (int8_t)op and shifts right for a
         * negative count, and SR0 is the low half of the 32-bit SR
         * (2100ops.hxx:2180-2186; adsp2100.h:311-319). $0184 AR = SR0 + AY0,
         * $0185 I4 = AR, $0186 JUMP (I4) -- index 0 reaches the RTS at $0187,
         * index 1 the JUMP at $0188, 2..14 seven NOPs, 15 the JUMP at $0196. */
        jump_index = (unsigned)(si >> 12);
        entry->jump_index = (uint8_t)jump_index;
        if (jump_index == STUNRUN_ADSP_RECORD_SCHEDULE_EXIT_INDEX) {
            /* $0187 RTS. The walk ends here, so $0199 onward never runs: DM
             * ($0034) keeps the pointer $017B just latched, DM($0036) and
             * DM($0035) keep the last record's values, and no record start is
             * latched. The caller re-establishes the port; the port itself is
             * left two words ahead of DM($0034). */
            entry->produced_record = 0;
            built.pass_count = pass + 1u;
            built.record_count = pass;
            built.dm2001_final = dm2001;
            built.dm0034_final = dm0034;
            built.dm0036_final = dm0036;
            built.dm0035_final = dm0035;
            built.dm0038_final = dm0038;
            built.next_simbuf_word_index = STUNRUN_ADSP_SOM_SIMBUF_MP_WORDS +
                                           (uint32_t)dm2001 + 2u;
            *out = built;
            return 1;
        }
        if (jump_index != STUNRUN_ADSP_RECORD_SCHEDULE_CONTINUE_INDEX)
            return 0; /* $0188 / $0189-$0195: never executed by the capture. */

        /* $0199 AX0 = $0002; $019A AR = AX0 + AY1; $019B DM($0034) = AR. */
        dm0034 = (uint16_t)(entry->dm2001_latched + 2u);
        entry->dm0034_after = dm0034;

        /* $019C AY0 = $003F; $019D AR = SI; $019E AR = AR AND AY0;
         * $019F IF EQ JUMP $01E4. A field word with no bits below $0040 leaves
         * through $01E4, whose SIMBUF drain is outside this slice and which the
         * capture never reached; reject rather than follow it. */
        if ((si & 0x003fu) == 0u)
            return 0;

        /* $01A0 DM($0036) = AR. */
        dm0036 = (uint16_t)(si & 0x003fu);
        entry->dm0036 = dm0036;

        /* $01A1 AY0 = $003F; $01A2 SR = LSHIFT SI BY -6 (LO); $01A3 AF = SR0
         * AND AY0; $01A4 IF EQ AF = AF + 1; $01A5 AR = AF; $01A6 DM($0035) = AR. */
        af = (uint16_t)((si >> 6) & 0x003fu);
        if (af == 0u)
            af = 1u;
        dm0035 = af;
        entry->dm0035 = dm0035;

        /* $01A7 DM($2001) = AX1 -- the record start. */
        dm2001 = ax1;
        entry->record_start = dm2001;
        entry->produced_record = 1;

        /* $01A8 CALL $01E8 and $01A9 IF GE JUMP $01E4 are NOT modelled: the E8
         * flags $01A9 tests are produced by a routine outside this slice, and
         * the capture never took the branch. The caller's loop back-edge is
         * $01E1 IF EQ JUMP $017A, so each pass is followed by exactly one more
         * -- which is what the capture shows and no more. */
    }

    return 0; /* walk exceeded STUNRUN_ADSP_RECORD_SCHEDULE_MAX_PASSES */
}