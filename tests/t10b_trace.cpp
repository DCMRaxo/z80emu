#include "zexdoc_bus.h"
#include <cstdio>

// ===========================================================================
// Trace de ZEXDOC: ejecuta unas miles de instrucciones y a partir de un
// punto concreto imprime cada step con su opcode, PC y estado del CPU.
//
// Uso: sirve para ver qué instrucciones ejecuta ZEXDOC en el bucle inicial
// y detectar cuál puede estar dando un resultado incorrecto.
// ===========================================================================

int main() {
    ZexdocBus bus;
    z80::Z80 cpu(bus);
    bus.cpu = &cpu;

    cpu.regs().pc = 0x0100;
    cpu.regs().sp = 0xFF00;

    // Ejecutamos hasta un punto donde ya esté en el bucle de tests.
    // Sabemos que sobre el step ~67 ya estaba en 0x1C60, así que
    // arrancamos el trace un poco antes.
    const std::uint64_t TRACE_START = 60;
    const int TRACE_COUNT = 250;

    std::printf("Ejecutando hasta step %llu, luego trace de %d instrucciones...\n\n",
                static_cast<unsigned long long>(TRACE_START), TRACE_COUNT);

    // Ejecutamos sin trazar hasta TRACE_START.
    for (std::uint64_t i = 0; i < TRACE_START; ++i) {
        cpu.step();
    }

    // A partir de aquí, trazamos cada step.
    for (int i = 0; i < TRACE_COUNT; ++i) {
        const auto r_before = cpu.regs();
        std::uint16_t pc_before = r_before.pc;
        std::uint8_t op = bus.ram[pc_before];

        cpu.step();

        const auto r_after = cpu.regs();
        std::printf(
            "PC=%04X op=%02X | AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X "
            "SP=%04X -> PC=%04X AF=%02X%02X\n",
            pc_before, op,
            r_before.a, r_before.f,
            r_before.b, r_before.c,
            r_before.d, r_before.e,
            r_before.h, r_before.l,
            r_before.sp,
            r_after.pc,
            r_after.a, r_after.f);
    }

    return 0;
}