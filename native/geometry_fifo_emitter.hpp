#ifndef STUNRUN_GEOMETRY_FIFO_EMITTER_HPP
#define STUNRUN_GEOMETRY_FIFO_EMITTER_HPP

#include <cstdint>
#include <vector>

namespace stunrun {

/* Literal 68010 FIFO primitive at $2F470.  Field names are mechanism-only;
 * their geometry meaning is not established. */
std::vector<std::uint16_t> emit_geometry_primitive(std::uint16_t first,
                                                   std::uint16_t second,
                                                   std::uint16_t third,
                                                   std::uint16_t fourth,
                                                   std::uint8_t selector);

/* Literal caller at $2B7E8: four primitive records from D2/D3/D4. */
std::vector<std::uint16_t> emit_geometry_quad(std::uint32_t d1,
                                              std::uint32_t d2,
                                              std::uint32_t d3,
                                              std::uint32_t d4);

} // namespace stunrun

#endif
