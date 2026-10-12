#include "amstrad/amstrad_cpc.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    amstrad::AmstradCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    // Ejecutamos hasta justo después de que el firmware copie el área
    // de trabajo. Según el trace, eso ocurre alrededor del step 144090.
    // Vamos a ejecutar hasta el step 144100, y luego volcar las zonas.
    const int STEPS = 144100;

    for (int i = 0; i < STEPS; ++i) {
        int tstates = cpc.cpu().step();
        cpc.tick(tstates);
    }

    std::printf("Estado tras %d steps:\n", STEPS);
    const auto& r = cpc.cpu().regs();
    std::printf("PC=%04X SP=%04X AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X\n\n",
                r.pc, r.sp, r.a, r.f, r.b, r.c, r.d, r.e, r.h, r.l);

    // Volcado de 0xC000-0xC020 (donde el firmware intenta ejecutar).
    std::printf("RAM 0xC000-0xC020:\n  ");
    for (int i = 0; i < 32; ++i) {
        std::printf("%02X ", cpc.read(0xC000 + i));
    }
    std::printf("\n\n");

    // Volcado de 0xB900-0xB920 (donde el firmware copió).
    std::printf("RAM 0xB900-0xB920:\n  ");
    for (int i = 0; i < 32; ++i) {
        std::printf("%02X ", cpc.read(0xB900 + i));
    }
    std::printf("\n\n");

    // Volcado de 0x0090-0x00A0 (zona de la ROM baja).
    std::printf("ROM 0x0090-0x00A0:\n  ");
    for (int i = 0; i < 16; ++i) {
        std::printf("%02X ", cpc.read(0x0090 + i));
    }
    std::printf("\n\n");

    // Volcado de 0x0096-0x00B6 (donde el firmware lee).
    std::printf("ROM 0x0096-0x00B6:\n  ");
    for (int i = 0; i < 32; ++i) {
        std::printf("%02X ", cpc.read(0x0096 + i));
    }
    std::printf("\n\n");

    return 0;
}