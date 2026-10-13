#include "amstrad/amstrad_cpc.h"
#include <cstdio>

// Subclase que traza las interrupciones.
class TraceCPC : public amstrad::AmstradCPC {
public:
    bool trace_int = false;
    int step_num = 0;

    void trace_state(const char* label) {
        const auto& r = cpu().regs();
        std::printf("[step %6d] %s PC=%04X SP=%04X AF=%02X%02X IM=%d IFF1=%d IFF2=%d I=%02X\n",
                    step_num, label, r.pc, r.sp, r.a, r.f, r.im,
                    r.iff1 ? 1 : 0, r.iff2 ? 1 : 0, r.i);
    }
};

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    TraceCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    const int STEPS = 20'000'000;

    for (int i = 0; i < STEPS; ++i) {
        cpc.step_num = i;
        std::uint16_t pc_before = cpc.cpu().regs().pc;
        int tstates = cpc.cpu().step();
        cpc.tick(tstates);

        // Si el PC ha saltado a la zona del handler de interrupción
        // (0x0038 para IM 1, o cualquier sitio si IM 2), trazar.
        std::uint16_t pc_after = cpc.cpu().regs().pc;
        if (pc_before != 0x0038 && pc_after == 0x0038) {
            std::printf("[step %6d] INTERRUPCIÓN → handler en 0x0038\n", i);
        }
    }

    // Estado final
    const auto& r = cpc.cpu().regs();
    std::printf("\nEstado final:\n");
    std::printf("PC=%04X SP=%04X AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X\n",
                r.pc, r.sp, r.a, r.f, r.b, r.c, r.d, r.e, r.h, r.l);
    std::printf("IM=%d IFF1=%d IFF2=%d I=%02X\n",
                r.im, r.iff1 ? 1 : 0, r.iff2 ? 1 : 0, r.i);

    return 0;
}