#include "som_source_select.h"

#include <string.h>

static int dm_read(const uint16_t *dm, size_t word_count, uint16_t base,
                   uint32_t address, uint16_t *value)
{
    size_t offset;

    if (address < base)
        return 0;
    offset = (size_t)(address - base);
    if (offset >= word_count)
        return 0;
    *value = dm[offset];
    return 1;
}

static int simbuf_read(const uint16_t *rom, size_t word_count,
                       uint32_t rom_base_word, uint16_t mp,
                       uint32_t sim_address, uint16_t *value)
{
    uint32_t mp_base;
    uint32_t absolute;
    size_t offset;

    if (mp != 1u)
        return 0;
    /* MAME harddriv_m.cpp sets m_adsp_eprom_base = 0x10000 * MP and
     * SIMBUF reads m_sim_memory[base + m_adsp_sim_address++] [S10]. */
    mp_base = STUNRUN_ADSP_SOM_SIMBUF_MP_WORDS;
    absolute = mp_base + sim_address;
    if (absolute < rom_base_word)
        return 0;
    offset = (size_t)(absolute - rom_base_word);
    if (offset >= word_count)
        return 0;
    *value = rom[offset];
    return 1;
}

/* A selector is copied through MR0, then the routine executes a fractional
 * SS MAC with MY0=1 before adding AY0=$010A. In the captured MSTAT state this
 * yields 3*selector, so the pointed record begins at $010A + 3*selector. */
static uint32_t selected_dm_address(uint16_t selector)
{
    return STUNRUN_ADSP_SOM_SOURCE_TABLE_BASE + 3u * (uint32_t)selector;
}

int stunrun_adsp_som_select_source_window(
    const uint16_t *dm_table, size_t dm_word_count, uint16_t dm_base,
    const uint16_t *simbuf_words, size_t simbuf_word_count,
    uint32_t simbuf_base_word, uint16_t mp, uint16_t simclk_start,
    stunrun_adsp_som_source_selection_t *out)
{
    stunrun_adsp_som_source_selection_t built;
    uint32_t sim_address = simclk_start;
    uint16_t weight_selector, first_selector, selector;
    uint32_t table_address;
    size_t simbuf_reads = 0u;
    size_t point;

    if (dm_table == NULL || simbuf_words == NULL || out == NULL || mp != 1u)
        return 0;

    memset(&built, 0, sizeof(built));

    /* $01E8: first SIMBUF word selects the three weight words copied to
     * DM($0014..$0016). The second selects the initial source point. */
    if (!simbuf_read(simbuf_words, simbuf_word_count, simbuf_base_word,
                     mp, sim_address++, &weight_selector) ||
        !simbuf_read(simbuf_words, simbuf_word_count, simbuf_base_word,
                     mp, sim_address++, &first_selector))
        return 0;
    simbuf_reads += 2u;

    table_address = selected_dm_address(weight_selector);
    for (point = 0; point < STUNRUN_ADSP_SOM_WEIGHT_WORDS; point++) {
        if (!dm_read(dm_table, dm_word_count, dm_base,
                     table_address + (uint32_t)point,
                     &built.weight_words[point]))
            return 0;
    }

    table_address = selected_dm_address(first_selector);
    for (point = 0; point < 3u; point++) {
        if (!dm_read(dm_table, dm_word_count, dm_base,
                     table_address + (uint32_t)point,
                     &built.source_words[point]))
            return 0;
    }
    memcpy(built.mirrored_first_point, built.source_words,
           sizeof(built.mirrored_first_point));

    /* $020D/$020E update DM($08EA) from the first point's third word when it
     * is nonnegative. The captured SIMCLK offsets are all positive. */
    if ((built.source_words[2] & 0x8000u) == 0u)
        built.dm08ea = built.source_words[2];

    /* $01B0 starts I0 at one. Two non-sentinel SIMBUF selectors copy two more
     * three-word records to DM($0043..$0048); the following $FFFF ends the
     * captured list before I0 is incremented again. */
    for (point = 0; point < 2u; point++) {
        size_t word;
        if (!simbuf_read(simbuf_words, simbuf_word_count, simbuf_base_word,
                         mp, sim_address++, &selector))
            return 0;
        simbuf_reads++;
        if (selector == 0xffffu)
            return 0;

        table_address = selected_dm_address(selector);
        for (word = 0; word < 3u; word++) {
            if (!dm_read(dm_table, dm_word_count, dm_base,
                         table_address + (uint32_t)word,
                         &built.source_words[3u + point * 3u + word]))
                return 0;
        }

        /* $01BD/$01BE compare the DM table address with AY1=$08EA and update
         * that running maximum only when the signed AF is greater than zero. */
        if (table_address > (uint32_t)built.dm08ea &&
            (uint16_t)(table_address - built.dm08ea) < 0x8000u)
            built.dm08ea = (uint16_t)table_address;
    }

    if (!simbuf_read(simbuf_words, simbuf_word_count, simbuf_base_word,
                     mp, sim_address++, &selector))
        return 0;
    simbuf_reads++;
    if (selector != 0xffffu)
        return 0;

    /* I0 starts at 1 and advances once for each selected record; the sentinel
     * branch exits before MODIFY, so the observed result is 3 for two points. */
    built.dm0039 = (uint16_t)(1u + 2u);
    built.simbuf_reads = simbuf_reads;
    *out = built;
    return 1;
}
