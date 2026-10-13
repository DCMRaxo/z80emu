#include "amstrad/amstrad_cpc.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    amstrad::AmstradCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    const std::uint64_t START = 390'000ULL;
    const std::uint64_t END   = 403'000ULL;
    const std::uint64_t STEP  = 100ULL;

    for (std::uint64_t i = 0; i < START; ++i) {
        int tstates = cpc.cpu().step();
        cpc.tick(tstates);
    }

    for (std::uint64_t i = START; i < END; i += STEP) {
        // Ejecutar STEP steps
        for (std::uint64_t j = 0; j < STEP; ++j) {
            int tstates = cpc.cpu().step();
            cpc.tick(tstates);
        }
        const auto& r = cpc.cpu().regs();
        std::printf("[step %llu] PC=%04X SP=%04X AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X\n",
                    static_cast<unsigned long long>(i + STEP),
                    r.pc, r.sp, r.a, r.f, r.b, r.c, r.d, r.e, r.h, r.l);
    }

    return 0;
}