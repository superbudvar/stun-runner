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

    const auto a = static_cast<std::uint16_t>(d2);
    const auto b = static_cast<std::uint16_t>(d3);
    const auto c = static_cast<std::uint16_t>(d4);
    append(a, b, static_cast<std::uint16_t>(d1), b, c);
    append(static_cast<std::uint16_t>(d2 + 0x80u), b,
           static_cast<std::uint16_t>(d2 + 0x80u),
           static_cast<std::uint16_t>(d3 + 0x38u), c);
    append(static_cast<std::uint16_t>(d2 + 0x80u),
           static_cast<std::uint16_t>(d3 + 0x38u), a,
           static_cast<std::uint16_t>(d3 + 0x38u), c);
    append(a, static_cast<std::uint16_t>(d3 + 0x38u), a, b, c);
    return result;
}

} // namespace stunrun
