#include "amstrad/amstrad_cpc.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    amstrad::AmstradCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    const std::uint64_t MAX_STEPS = 500'000'000ULL;   // 500 M steps
    const std::uint64_t CHECK_INTERVAL = 5'000'000ULL; // 5 M steps

    std::uint64_t steps = 0;
    std::uint16_t last_pc = 0xFFFF;
    int no_progress_count = 0;

    while (steps < MAX_STEPS && !cpc.cpu().regs().halted) {
        int tstates = cpc.cpu().step();
        cpc.tick(tstates);
        ++steps;

        if ((steps % CHECK_INTERVAL) == 0) {
            std::uint16_t pc = cpc.cpu().regs().pc;
            const auto& r = cpc.cpu().regs();
            std::printf("[%6llu M steps] PC=%04X SP=%04X AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X INT=%llu\n",
                        static_cast<unsigned long long>(steps / 1'000'000ULL),
                        pc, r.sp, r.a, r.f, r.b, r.c, r.d, r.e, r.h, r.l,
                        static_cast<unsigned long long>(cpc.interrupt_count()));
            std::fflush(stdout);

            if (pc == last_pc) {
                ++no_progress_count;
                if (no_progress_count >= 20) {
                    std::printf("\n=== PC ESTANCADO en 0x%04X ===\n", pc);
                    std::printf("Memoria alrededor del PC:\n");
                    std::uint16_t base = static_cast<std::uint16_t>(pc & 0xFFF0);
                    for (int row = 0; row < 4; ++row) {
                        std::uint16_t a = static_cast<std::uint16_t>(base + row * 16);
                        std::printf("  %04X: ", a);
                        for (int j = 0; j < 16; ++j) {
                            std::printf("%02X ", cpc.read(a + j));
                        }
                        std::printf("\n");
                    }
                    break;
                }
            } else {
                no_progress_count = 0;
                last_pc = pc;
            }
        }
    }

    std::printf("\n");
    if (cpc.cpu().regs().halted) {
        std::printf("CPU en HALT tras %llu steps. PC=0x%04X\n",
                    static_cast<unsigned long long>(steps), cpc.cpu().regs().pc);
    } else {
        std::printf("Ejecución detenida tras %llu steps. PC final=0x%04X\n",
                    static_cast<unsigned long long>(steps), cpc.cpu().regs().pc);
    }
    std::printf("Interrupciones: %llu\n",
                static_cast<unsigned long long>(cpc.interrupt_count()));

    return 0;
}