#include "amstrad/amstrad_cpc.h"
#include <cstdio>

// ===========================================================================
// Trace detallado del arranque del firmware del CPC.
// Objetivo: ver en qué momento el SP se corrompe y por qué.
// ===========================================================================

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    amstrad::AmstradCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    const int MAX_STEPS = 200'000;
    const int REPORT_EVERY = 5'000;

    std::uint8_t prev_ga = 0xFF;
    std::uint8_t prev_crtc_sel = 0xFF;
    std::uint8_t prev_ay_sel = 0xFF;

    for (int i = 0; i < MAX_STEPS; ++i) {
        std::uint16_t pc_before = cpc.cpu().regs().pc;
        std::uint8_t op = cpc.read(pc_before);

        int tstates = cpc.cpu().step();
        cpc.tick(tstates);

        // Detectar cambios en el GA.
        if (cpc.ga_last_value() != prev_ga) {
            std::printf("[step %6d] GA=0x%02X (PC=%04X op=%02X)\n",
                        i, cpc.ga_last_value(), pc_before, op);
            prev_ga = cpc.ga_last_value();
        }
        // Detectar cambios en la selección del CRTC.
        if (cpc.crtc_selected_reg() != prev_crtc_sel) {
            std::printf("[step %6d] CRTC_sel=%d (PC=%04X op=%02X)\n",
                        i, cpc.crtc_selected_reg(), pc_before, op);
            prev_crtc_sel = cpc.crtc_selected_reg();
        }
        // Detectar cambios en la selección del AY.
        if (cpc.ay_selected_reg() != prev_ay_sel) {
            std::printf("[step %6d] AY_sel=%d (PC=%04X op=%02X)\n",
                        i, cpc.ay_selected_reg(), pc_before, op);
            prev_ay_sel = cpc.ay_selected_reg();
        }

        // Resumen cada REPORT_EVERY steps.
        if ((i + 1) % REPORT_EVERY == 0) {
            const auto& r = cpc.cpu().regs();
            std::printf("[%6d] PC=%04X SP=%04X AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X\n",
                        i, r.pc, r.sp, r.a, r.f, r.b, r.c, r.d, r.e, r.h, r.l);
            std::fflush(stdout);

            // Si el SP se sale del rango razonable, avisar.
            if (r.sp < 0x8000 && r.sp > 0x4000) {
                std::printf("  ^^^ SP en zona sospechosa (ROM alta) ^^^\n");
            }
            if (r.sp < 0x4000) {
                std::printf("  ^^^ SP en ROM baja, MUY SOSPECHOSO ^^^\n");
            }
        }
    }

    return 0;
}