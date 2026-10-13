#include "amstrad/amstrad_cpc.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    amstrad::AmstradCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    // Ejecutamos hasta justo antes del desborde del SP.
    const std::uint64_t STEPS = 402'000ULL;
    for (std::uint64_t i = 0; i < STEPS; ++i) {
        int tstates = cpc.cpu().step();
        cpc.tick(tstates);
    }

    const auto& r = cpc.cpu().regs();
    std::printf("Estado tras %llu steps:\n", static_cast<unsigned long long>(STEPS));
    std::printf("PC=%04X SP=%04X AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X\n\n",
                r.pc, r.sp, r.a, r.f, r.b, r.c, r.d, r.e, r.h, r.l);

    // Volcado de 0xB900 a 0xC000 (1.5 KB).
    std::printf("RAM 0xB900-0xC000 (tabla de saltos):\n");
    for (int base = 0xB900; base < 0xC000; base += 16) {
        std::printf("  %04X: ", base);
        for (int j = 0; j < 16; ++j) {
            std::printf("%02X ", cpc.read(base + j));
        }
        std::printf("\n");
    }
    std::printf("\n");

    // ¿De dónde se copió esto? Comparemos con la ROM baja.
    // Buscamos la secuencia "C3 5E BA" en la ROM.
    std::printf("Buscando 'C3 5E BA' en la ROM baja...\n");
    for (int addr = 0; addr < 0x4000 - 3; ++addr) {
        if (cpc.read(addr) == 0xC3 && cpc.read(addr + 1) == 0x5E && cpc.read(addr + 2) == 0xBA) {
            std::printf("  Encontrado en ROM baja: 0x%04X\n", addr);
            std::printf("  Contexto (16 bytes antes): ");
            for (int j = 0; j < 16; ++j) {
                std::printf("%02X ", cpc.read(addr - 16 + j));
            }
            std::printf("\n");
            std::printf("  Contexto (32 bytes después): ");
            for (int j = 0; j < 32; ++j) {
                std::printf("%02X ", cpc.read(addr + j));
            }
            std::printf("\n");
        }
    }
    std::printf("\n");

    // Buscar también en la ROM alta.
    std::printf("Buscando 'C3 5E BA' en la ROM alta...\n");
    for (int addr = 0x4000; addr < 0x8000 - 3; ++addr) {
        if (cpc.read(addr) == 0xC3 && cpc.read(addr + 1) == 0x5E && cpc.read(addr + 2) == 0xBA) {
            std::printf("  Encontrado en ROM alta: 0x%04X\n", addr);
        }
    }

    return 0;
}