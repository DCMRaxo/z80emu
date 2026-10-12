#include "amstrad/amstrad_cpc.h"
#include <cstdio>

// ===========================================================================
// Trace del firmware cuando entra en la zona de RAM del sistema (0xB800+).
// Objetivo: ver qué hace el firmware justo antes de perderse.
// ===========================================================================

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    amstrad::AmstradCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    const int MAX_STEPS = 200'000;
    int steps_printed = 0;

    for (int i = 0; i < MAX_STEPS; ++i) {
        std::uint16_t pc = cpc.cpu().regs().pc;

        // Solo imprimimos cuando el PC entra en la zona de RAM donde se pierde.
        bool interesting = (pc >= 0xB800 && pc < 0xC200);

        if (interesting && steps_printed < 2000) {
            const auto& r = cpc.cpu().regs();
            std::uint8_t op = cpc.read(pc);
            std::printf("[%6d] PC=%04X op=%02X SP=%04X AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X\n",
                        i, pc, op, r.sp, r.a, r.f, r.b, r.c, r.d, r.e, r.h, r.l);
            steps_printed++;
        }

        int tstates = cpc.cpu().step();
        cpc.tick(tstates);
    }

    return 0;
}