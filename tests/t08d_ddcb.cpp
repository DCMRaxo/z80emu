#include "test_bus.h"

// ===========================================================================
// Test del Paso 8d: DD CB d op.
//
// Cubre:
//   - DD CB d op con op que USA (HL) → aplica el desplazamiento.
//   - DD CB d op con op que NO usa (HL) → ignora el prefijo, pero consume d.
// ===========================================================================

int main() {
    SimpleBus bus;
    z80::Z80 cpu(bus);

    // Programa:
    //
    //   0x0000: DD 21 00 90    LD IX, 0x9000
    //   0x0004: 3E 81          LD A, 0x81              A = 1000 0001
    //   0x0006: DD CB 05 06    RLC (IX+5)             rot. izq. de mem[0x9005] = 0x40 → 0x80, C=0
    //   0x000A: DD CB 05 46    BIT 0, (IX+5)          testea bit 0 de 0x80 → 0, Z=1
    //   0x000E: DD CB 05 C6    SET 0, (IX+5)          pone bit 0 → 0x81
    //   0x0012: DD CB 00 07    RLC A                  ¡ATENCIÓN! Como el op usa A (no (HL)),
    //                                                  el DD se IGNORA. Se ejecuta RLC A normal.
    //                                                  A = 0x81 << 1 = 0x02, C=1
    //   0x0016: DD CB FF 07    RLC A                  Igual: d=0xFF se consume y se descarta,
    //                                                  se ejecuta RLC A. A = 0x02 << 1 = 0x04, C=0
    //   0x001A: 76             HALT
    //
    // Estado inicial: mem[0x9005] = 0x40.
    std::uint8_t prog[] = {
        0xDD, 0x21, 0x00, 0x90,
        0x3E, 0x81,
        0xDD, 0xCB, 0x05, 0x06,
        0xDD, 0xCB, 0x05, 0x46,
        0xDD, 0xCB, 0x05, 0xC6,
        0xDD, 0xCB, 0x00, 0x07,
        0xDD, 0xCB, 0xFF, 0x07,
        0x76,
    };
    load(bus, prog);

    // Estado inicial: el byte indexado.
    bus.ram[0x9005] = 0x40;

    int i = 0;
    while (i < 40 && !cpu.regs().halted) {
        cpu.step();
        const auto& r = cpu.regs();
        std::printf(
            "step %2d: PC=%04X A=%02X F=%02X IX=%04X mem[9005]=%02X [%c%c%c%c%c%c]\n",
            i, r.pc, r.a, r.f, r.ix, bus.ram[0x9005],
            (r.f & z80::SF) ? 'S' : '-',
            (r.f & z80::ZF) ? 'Z' : '-',
            (r.f & z80::HF) ? 'H' : '-',
            (r.f & z80::PF) ? 'V' : '-',
            (r.f & z80::NF) ? 'N' : '-',
            (r.f & z80::CF) ? 'C' : '-');
        i++;
    }

    return 0;
}