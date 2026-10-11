// SimpleBus compartido por todos los tests.

#pragma once
#include "z80/z80.h"
#include <cstdio>

// Bus de pruebas: 64 KB de RAM plana, E/S que devuelve 0x5A y no hace nada.
class SimpleBus : public z80::IBus {
public:
    std::uint8_t read(std::uint16_t addr) override { return ram[addr]; }
    void write(std::uint16_t addr, std::uint8_t v) override { ram[addr] = v; }
    std::uint8_t in(std::uint16_t) override { return 0x5A; }
    void out(std::uint16_t, std::uint8_t) override {}
    std::uint8_t ram[0x10000] = {};
};

// Volcado compacto con flags decodificados.
inline void dump(const z80::Z80& cpu, int step) {
    const auto& r = cpu.regs();
    std::printf(
        "step %2d: PC=%04X AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X SP=%04X [%c%c%c%c%c%c]\n",
        step, r.pc, r.a, r.f, r.b, r.c, r.d, r.e, r.h, r.l, r.sp,
        (r.f & z80::SF) ? 'S' : '-',
        (r.f & z80::ZF) ? 'Z' : '-',
        (r.f & z80::HF) ? 'H' : '-',
        (r.f & z80::PF) ? 'V' : '-',
        (r.f & z80::NF) ? 'N' : '-',
        (r.f & z80::CF) ? 'C' : '-');
}

// Carga un programa en memoria a partir de la dirección 0.
template <std::size_t N>
inline void load(SimpleBus& bus, const std::uint8_t (&prog)[N], std::uint16_t at = 0) {
    for (std::size_t i = 0; i < N; ++i) bus.ram[at + i] = prog[i];
}

// Ejecuta hasta HALT o hasta un número máximo de instrucciones.
inline void run(z80::Z80& cpu, int max_steps = 40) {
    int i = 0;
    while (i < max_steps && !cpu.regs().halted) {
        cpu.step();
        dump(cpu, i++);
    }
}