#include "geometry_fifo_emitter.hpp"

namespace stunrun {

std::vector<std::uint16_t> emit_geometry_primitive(std::uint16_t first,
                                                   std::uint16_t second,
                                                   std::uint16_t third,
                                                   std::uint16_t fourth,
                                                   std::uint8_t selector)
{
    return {static_cast<std::uint16_t>(0x2000u + selector), first, second,
            third, fourth};
}

std::vector<std::uint16_t> emit_geometry_quad(std::uint32_t d1,
                                              std::uint32_t d2,
                                              std::uint32_t d3,
                                              std::uint32_t d4)
{
    std::vector<std::uint16_t> result;
    const auto append = [&result](std::uint16_t a, std::uint16_t b,
                                  std::uint16_t c, std::uint16_t d,
                                  std::uint8_t selector) {
        const auto record = emit_geometry_primitive(a, b, c, d, selector);
        result.insert(result.end(), record.begin(), record.end());
    };

    const auto x = static_cast<std::uint16_t>(d2);
    const auto y = static_cast<std::uint16_t>(d3);
    const auto z = static_cast<std::uint16_t>(d4);
    const auto x_plus_80 = static_cast<std::uint16_t>(d2 + 0x80u);
    const auto y_plus_38 = static_cast<std::uint16_t>(d3 + 0x38u);
    /* The 68010 pushes arguments right-to-left; $2F470 consumes the
     * resulting frame slots as (arg1,arg2,arg3,arg4,selector). */
    append(x, y, x_plus_80, y, z);
    append(y_plus_38, x_plus_80, y, x_plus_80, z);
    append(y_plus_38, x, y_plus_38, x_plus_80, z);
    append(y, x, y_plus_38, x, z);
    return result;
}

} // namespace stunrun
