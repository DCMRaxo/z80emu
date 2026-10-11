// Pruebas sueltas
#include "test_bus.h"

int main() {
    SimpleBus bus;
    z80::Z80 cpu(bus);

    // Programa de prueba para DAA:
    //
    //   Prueba 1: 15 + 27 = 42 en BCD (sin acarreo)
    //     0x0000: 3E 15       LD A, 0x15
    //     0x0002: C6 27       ADD A, 0x27      A = 0x3C (binario), H=0 (0x5+0x7=0xC no desborda), C=0
    //     0x0004: 27          DAA              A = 0x42 (BCD correcto), H=0
    //
    //   Prueba 2: 99 + 01 = 100 en BCD (con acarreo)
    //     0x0005: 3E 99       LD A, 0x99
    //     0x0007: C6 01       ADD A, 0x01      A = 0x9A, H=1 (0x9+0x1=0xA desborda), C=0
    //     0x0009: 27          DAA              A = 0x00, C=1 (99+1=100 en BCD)
    //
    //   Prueba 3: 0x45 - 0x19 = 0x26 en BCD (resta)
    //     0x000A: 3E 45       LD A, 0x45
    //     0x000C: D6 19       SUB 0x19         A = 0x2C, H=1 (0x5-0x9 pide borrow), N=1, C=0
    //     0x000E: 27          DAA              A = 0x26 (BCD correcto)
    //
    //     0x000F: 76          HALT
    std::uint8_t prog[] = {
        0x3E, 0x15,
        0xC6, 0x27,
        0x27,
        0x3E, 0x99,
        0xC6, 0x01,
        0x27,
        0x3E, 0x45,
        0xD6, 0x19,
        0x27,
        0x76,
    };
    load(bus, prog);

    run(cpu, 40);
    return 0;
}