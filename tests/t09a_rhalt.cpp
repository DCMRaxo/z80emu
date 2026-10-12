#include "test_bus.h"

// ===========================================================================
// Test del Paso 9a: registro R, HALT real, EI y DI.
//
// Verifica que:
//   - R se incrementa una vez por cada byte de opcode (no por operandos).
//   - R NO se incrementa al leer operandos (inmediatos, direcciones).
//   - R se incrementa con los prefijos DD/FD/ED/CB.
//   - HALT deja el CPU consumiendo 4 T-states por iteración sin avanzar PC.
//   - EI y DI modifican IFF1/IFF2.
// ===========================================================================

int main() {
    SimpleBus bus;
    z80::Z80 cpu(bus);

    // Programa:
    //
    //   0x0000: 3E 42          LD A, 0x42       R += 1 (solo el opcode)
    //   0x0002: 06 10          LD B, 0x10       R += 1
    //   0x0004: CB 07          RLC A            R += 2 (CB + 07)
    //   0x0006: DD 21 34 12    LD IX, 0x1234    R += 2 (DD + 21)
    //   0x000A: ED 47          LD I, A          R += 2 (ED + 47)
    //   0x000C: FB             EI               R += 1
    //   0x000D: F3             DI               R += 1
    //   0x000E: 76             HALT             R += 1
    //
    // A partir del HALT, cada step() adicional consume 4 T-states e
    // incrementa R, pero PC no avanza.
    std::uint8_t prog[] = {
        0x3E, 0x42,             // LD A, 0x42
        0x06, 0x10,             // LD B, 0x10
        0xCB, 0x07,             // RLC A
        0xDD, 0x21, 0x34, 0x12, // LD IX, 0x1234
        0xED, 0x47,             // LD I, A
        0xFB,                   // EI
        0xF3,                   // DI
        0x76,                   // HALT
    };
    load(bus, prog);

    // Ejecutamos 14 steps: los 9 primeros ejecutan el programa, y los 5
    // siguientes están halted y solo consumen T-states.
    for (int i = 0; i < 14; ++i) {
        cpu.step();
        const auto& r = cpu.regs();
        std::printf(
            "step %2d: PC=%04X A=%02X R=%02X IFF1=%d IFF2=%d halted=%d cycles=%llu\n",
            i, r.pc, r.a, r.r,
            r.iff1 ? 1 : 0, r.iff2 ? 1 : 0, r.halted ? 1 : 0,
            static_cast<unsigned long long>(cpu.cycles()));
    }

    return 0;
}