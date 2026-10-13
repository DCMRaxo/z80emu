#include "amstrad/amstrad_cpc.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    amstrad::AmstradCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    const std::uint64_t MAX_STEPS = 200'000'000ULL;

    std::uint16_t last_pc = 0xFFFF;
    int entries = 0;

    for (std::uint64_t i = 0; i < MAX_STEPS && entries < 30; ++i) {
        std::uint16_t pc = cpc.cpu().regs().pc;
        int tstates = cpc.cpu().step();
        cpc.tick(tstates);

        std::uint16_t pc_after = cpc.cpu().regs().pc;

        // Detectar entrada a la rutina: el PC pasa a estar en 0x0F2C-0x0F49
        // y antes no lo estaba.
        bool in_routine_before = (last_pc >= 0x0F2C && last_pc <= 0x0F49);
        bool in_routine_after  = (pc_after >= 0x0F2C && pc_after <= 0x0F49);

        if (!in_routine_before && in_routine_after && entries < 30) {
            const auto& r = cpc.cpu().regs();
            std::printf("[step %llu] ENTRADA A RUTINA: PC antes=%04X, PC después=%04X, SP=%04X, AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X\n",
                        static_cast<unsigned long long>(i),
                        last_pc, pc_after, r.sp,
                        r.a, r.f, r.b, r.c, r.d, r.e, r.h, r.l);
            entries++;
        }

        last_pc = pc_after;
    }

    return 0;
}