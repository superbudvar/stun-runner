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

    /* Live keyboard controls use the raw MAME active-low port bits. */
    CHECK(stunrun_port_write(&ports, STUNRUN_PORT_MAIN_A800, 0xFFFF));
    CHECK(stunrun_port_set_active_low(&ports, STUNRUN_PORT_MAIN_A800,
                                      0x0004u, 1));
    CHECK(stunrun_port_read(&ports, STUNRUN_PORT_MAIN_A800, &value));
    CHECK((uint16_t)value == 0xFFFBu); /* Start pressed */
    CHECK(stunrun_port_set_active_low(&ports, STUNRUN_PORT_MAIN_A800,
                                      0x0001u, 1));
    CHECK(stunrun_port_read(&ports, STUNRUN_PORT_MAIN_A800, &value));
    CHECK((uint16_t)value == 0xFFFAu); /* Button 1 pressed, Start held */
    CHECK(stunrun_port_set_active_low(&ports, STUNRUN_PORT_MAIN_A800,
                                      0x0004u, 0));
    CHECK(stunrun_port_read(&ports, STUNRUN_PORT_MAIN_A800, &value));
    CHECK((uint16_t)value == 0xFFFEu); /* Start released, Button 1 held */

    CHECK(stunrun_port_write(&ports, STUNRUN_PORT_MAIN_IN0, 0x00C7));
    CHECK(stunrun_port_set_active_low(&ports, STUNRUN_PORT_MAIN_IN0,
                                      0x0080u, 1));
    CHECK(stunrun_port_read(&ports, STUNRUN_PORT_MAIN_IN0, &value));
    CHECK((uint16_t)value == 0x0047u); /* Coin 1; service DIP preserved */
    CHECK(stunrun_port_set_active_low(&ports, STUNRUN_PORT_MAIN_IN0,
                                      0x0080u, 0));
    CHECK(stunrun_port_read(&ports, STUNRUN_PORT_MAIN_IN0, &value));
    CHECK((uint16_t)value == 0x00C7u);
    CHECK(!stunrun_port_set_active_low(&ports, STUNRUN_PORT_MAIN_SW1,
                                       0x0001u, 1));
    CHECK(!stunrun_port_set_active_low(&ports, STUNRUN_PORT_MAIN_IN0,
                                       0x0100u, 1));

    printf("fake-ports: %d checks passed\n", checks);
    return 0;
}
