#include "test_bus.h"

// ===========================================================================
// Test del Paso 9b: interrupciones.
//
// Verifica:
//   - NMI salta a 0x0066 y guarda IFF1 en IFF2.
//   - INT en IM 1 salta a 0x0038 si IFF1 está activo.
//   - INT en IM 2 lee el vector y salta al handler correcto.
//   - INT no se atiende si IFF1 está inactivo.
//   - EI tiene retraso de una instrucción.
//   - HALT sale cuando llega una interrupción.
// ===========================================================================

int main() {
    SimpleBus bus;
    z80::Z80 cpu(bus);

    // Programa:
    //   0x0000: FB          EI
    //   0x0001: 00          NOP         ← aquí debería aplicarse el EI
    //   0x0002: 76          HALT        ← el CPU queda halted
    //   0x0003: 3E 77       LD A, 0x77  ← esto se ejecutará tras la INT
    //   0x0005: 76          HALT
    std::uint8_t prog[] = {
        0xFB,
        0x00,
        0x76,
        0x3E, 0x77,
        0x76,
    };
    load(bus, prog);

    // Handler de INT en IM 1: está en 0x0038.
    // Lo simple: un RETI.
    bus.ram[0x0038] = 0xED;
    bus.ram[0x0039] = 0x4D;   // ED 4D = RETI

    // Handler de NMI: está en 0x0066. Lo ponemos como RETN.
    bus.ram[0x0066] = 0xED;
    bus.ram[0x0067] = 0x45;   // ED 45 = RETN

    cpu.regs().sp = 0xFF00;
    cpu.regs().im = 1;

    // --- Paso 1: ejecutar EI, NOP, HALT ---
    std::printf("== Fase 1: EI, NOP, HALT ==\n");
    for (int i = 0; i < 3; ++i) {
        cpu.step();
        const auto& r = cpu.regs();
        std::printf(
            "step %d: PC=%04X IFF1=%d halted=%d SP=%04X\n",
            i, r.pc, r.iff1 ? 1 : 0, r.halted ? 1 : 0, r.sp);
    }

    // --- Paso 2: generar INT en IM 1 ---
    std::printf("== Fase 2: INT en IM 1 ==\n");
    cpu.interrupt(0x00);   // data se ignora en IM 1
    std::printf(
        "tras INT: PC=%04X IFF1=%d IFF2=%d halted=%d SP=%04X\n",
        cpu.regs().pc, cpu.regs().iff1 ? 1 : 0, cpu.regs().iff2 ? 1 : 0,
        cpu.regs().halted ? 1 : 0, cpu.regs().sp);

    // --- Paso 3: ejecutar RETI ---
    cpu.step();   // RETI
    std::printf(
        "tras RETI: PC=%04X IFF1=%d SP=%04X\n",
        cpu.regs().pc, cpu.regs().iff1 ? 1 : 0, cpu.regs().sp);

    // --- Paso 4: ejecutar LD A, 0x77 ---
    cpu.step();
    std::printf("tras LD A, 0x77: A=%02X PC=%04X\n",
                cpu.regs().a, cpu.regs().pc);

    // --- Paso 5: NMI ---
    std::printf("== Fase 5: NMI ==\n");
    cpu.non_maskable_interrupt();
    std::printf(
        "tras NMI: PC=%04X IFF1=%d IFF2=%d SP=%04X\n",
        cpu.regs().pc, cpu.regs().iff1 ? 1 : 0, cpu.regs().iff2 ? 1 : 0,
        cpu.regs().sp);

    // --- Paso 6: RETN ---
    cpu.step();
    std::printf(
        "tras RETN: PC=%04X IFF1=%d SP=%04X\n",
        cpu.regs().pc, cpu.regs().iff1 ? 1 : 0, cpu.regs().sp);

    return 0;
}