#include "amstrad/amstrad_cpc.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    amstrad::AmstradCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    const std::uint64_t MAX_STEPS = 500'000'000ULL;

    std::uint16_t prev_sp = 0xFFFF;
    int detections = 0;

    for (std::uint64_t i = 0; i < MAX_STEPS && detections < 10; ++i) {
        std::uint16_t pc = cpc.cpu().regs().pc;
        std::uint16_t sp_before = cpc.cpu().regs().sp;

        int tstates = cpc.cpu().step();
        cpc.tick(tstates);

        std::uint16_t sp_after = cpc.cpu().regs().sp;

        // Detectar cuando el SP pasa a estar en ROM (<= 0x7FFF)
        // (excluyendo el caso de reset, donde empieza en 0xC000).
        bool prev_ok = (sp_before > 0x7FFF);
        bool now_bad = (sp_after <= 0x7FFF);

        if (prev_ok && now_bad) {
            const auto& r = cpc.cpu().regs();
            std::printf("[step %llu] SP CAMBIA A ROM: SP antes=%04X, SP después=%04X, PC antes=%04X, opcode=%02X\n",
                        static_cast<unsigned long long>(i),
                        sp_before, sp_after, pc, cpc.read(pc));
            std::printf("           AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X\n",
                        r.a, r.f, r.b, r.c, r.d, r.e, r.h, r.l);
            detections++;
        }

        prev_sp = sp_after;
    }

    if (detections == 0) {
        std::printf("No se detectó cambio del SP a ROM en %llu steps.\n",
                    static_cast<unsigned long long>(MAX_STEPS));
    }

    return 0;
}