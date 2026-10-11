#include "test_bus.h"

int main() {
    SimpleBus bus;
    z80::Z80 cpu(bus);

    // Programa:
    //   0x0000: 3E 0F       LD A, 0x0F
    //   0x0002: 37          SCF            C=1, H=0, N=0
    //   0x0003: 3F          CCF            C=0, H=1, N=0
    //   0x0004: 2F          CPL            A=0xF0, H=1, N=1
    //   0x0005: 08          EX AF, AF'     A y F actuales van a A' y F'
    //   0x0006: 3E 99       LD A, 0x99
    //   0x0007: 08          EX AF, AF'     recupera A=0xF0 y F=0x36 (H+N)
    //   0x0008: 21 34 12    LD HL, 0x1234
    //   0x000B: 11 78 56    LD DE, 0x5678
    //   0x000E: EB          EX DE, HL      DE=0x1234, HL=0x5678
    //   0x000F: D9          EXX            BC/DE/HL ↔ alternativos (están a 0)
    //   0x0010: D9          EXX            vuelve a los originales
    //   0x0011: 76          HALT
    std::uint8_t prog[] = {
        0x3E, 0x0F,
        0x37,
        0x3F,
        0x2F,
        0x08,
        0x3E, 0x99,
        0x08,
        0x21, 0x34, 0x12,
        0x11, 0x78, 0x56,
        0xEB,
        0xD9,
        0xD9,
        0x76,
    };
    load(bus, prog);

    run(cpu, 40);
    return 0;
}