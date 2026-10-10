#include "geometry_fifo_emitter.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main()
{
    const auto one = stunrun::emit_geometry_primitive(0x10, 0x20, 0x30,
                                                      0x40, 0x07);
    assert((one == std::vector<std::uint16_t>{0x2007, 0x10, 0x20, 0x30,
                                               0x40}));

    const auto quad = stunrun::emit_geometry_quad(0x003, 0x100, 0x200, 0x04);
    assert(quad.size() == 20u);
    const std::vector<std::uint16_t> expected = {
        0x2004, 0x100, 0x200, 0x180, 0x200,
        0x2004, 0x238, 0x180, 0x200, 0x180,
        0x2004, 0x238, 0x100, 0x238, 0x180,
        0x2004, 0x200, 0x100, 0x238, 0x100};
    assert(quad == expected);
    return 0;
}
