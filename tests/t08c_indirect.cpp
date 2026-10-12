#include "test_bus.h"

// ===========================================================================
// Test del Paso 8c: instrucciones con (IX+d) e (IY+d).
//
// Verifica que:
//   - LD (IX+d), n     (inmediato a memoria indexada).
//   - LD r, (IX+d)     (memoria indexada a registro).
//   - INC (IX+d)       (modifica memoria in-place).
//   - DEC (IX-d)       (desplazamiento negativo).
//   - ALU (IX-d)       (operación aritmética con memoria indexada).
//   - LD (IX+d), r     (registro a memoria indexada).
// ===========================================================================

int main() {
    SimpleBus bus;
    z80::Z80 cpu(bus);

    // Programa:
    //
    //   0x0000: DD 21 00 90    LD IX, 0x9000
    //   0x0004: DD 36 05 42    LD (IX+5), 0x42     escribe 0x42 en 0x9005
    //   0x0008: DD 7E 05       LD A, (IX+5)        A = 0x42
    //   0x000B: DD 34 05       INC (IX+5)          mem[0x9005] = 0x43
    //   0x000E: DD 35 FE       DEC (IX-2)          mem[0x8FFE]--
    //   0x0011: DD 86 FE       ADD A, (IX-2)       A = 0x42 + mem[0x8FFE]
    //   0x0014: DD 77 05       LD (IX+5), A        mem[0x9005] = A
    //   0x0017: 76             HALT
    //
    // Estado inicial: mem[0x8FFE] = 0x10.
    //
    // Resultado esperado:
    //   Tras INC (IX+5):         mem[0x9005] = 0x43
    //   Tras DEC (IX-2):         mem[0x8FFE] = 0x0F
    //   Tras ADD A, (IX-2):      A = 0x42 + 0x0F = 0x51
    //   Tras LD (IX+5), A:       mem[0x9005] = 0x51
    std::uint8_t prog[] = {
        0xDD, 0x21, 0x00, 0x90,   // LD IX, 0x9000
        0xDD, 0x36, 0x05, 0x42,   // LD (IX+5), 0x42
        0xDD, 0x7E, 0x05,         // LD A, (IX+5)
        0xDD, 0x34, 0x05,         // INC (IX+5)
        0xDD, 0x35, 0xFE,         // DEC (IX-2)
        0xDD, 0x86, 0xFE,         // ADD A, (IX-2)
        0xDD, 0x77, 0x05,         // LD (IX+5), A
        0x76,                     // HALT
    };
    load(bus, prog);

    // Preparar memoria previa
    bus.ram[0x8FFE] = 0x10;

    int i = 0;
    while (i < 40 && !cpu.regs().halted) {
        cpu.step();
        const auto& r = cpu.regs();
        std::printf(
            "step %2d: PC=%04X A=%02X F=%02X IX=%04X [%c%c%c%c%c%c]\n",
            i, r.pc, r.a, r.f, r.ix,
            (r.f & z80::SF) ? 'S' : '-',
            (r.f & z80::ZF) ? 'Z' : '-',
            (r.f & z80::HF) ? 'H' : '-',
            (r.f & z80::PF) ? 'V' : '-',
            (r.f & z80::NF) ? 'N' : '-',
            (r.f & z80::CF) ? 'C' : '-');
        i++;
    }

    std::printf("  mem[0x9005] = %02X (esperado 51)\n", bus.ram[0x9005]);
    std::printf("  mem[0x8FFE] = %02X (esperado 0F)\n", bus.ram[0x8FFE]);

    return 0;
}