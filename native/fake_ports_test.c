#include "fake_ports.h"

#include <stdio.h>

static int checks;

#define CHECK(expr) do { \
    ++checks; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

int main(void)
{
    stunrun_fake_ports_t ports;
    int32_t value = 0;

    stunrun_ports_init(&ports);
    CHECK(stunrun_port_read(&ports, STUNRUN_PORT_MAIN_A800, &value));
    CHECK((uint16_t)value == 0xFFF8u);
    CHECK(((uint16_t)value & 0x8000u) != 0); /* sound-queue gate at $023EB0 */
    CHECK(((uint16_t)value & 0x0007u) == 0); /* preserve current boot inputs */

    printf("fake-ports: %d checks passed\n", checks);
    return 0;
}
