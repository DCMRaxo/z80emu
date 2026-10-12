#include "amstrad/amstrad_cpc.h"
#include <cstdio>

// Subclase que loguea IN/OUT.
class LoggingCPC : public amstrad::AmstradCPC {
public:
    bool logging = false;
    int step_num = 0;

    std::uint8_t in(std::uint16_t port) override {
        std::uint8_t v = amstrad::AmstradCPC::in(port);
        if (logging) {
            std::uint8_t hi = static_cast<std::uint8_t>(port >> 8);
            if (hi >= 0xF4 && hi <= 0xF9) {
                std::printf("[step %6d] IN  0x%04X -> 0x%02X\n", step_num, port, v);
            }
        }
        return v;
    }

    void out(std::uint16_t port, std::uint8_t value) override {
        amstrad::AmstradCPC::out(port, value);
        if (logging) {
            std::uint8_t hi = static_cast<std::uint8_t>(port >> 8);
            if (hi >= 0xF4 && hi <= 0xF9) {
                std::printf("[step %6d] OUT 0x%04X <- 0x%02X\n", step_num, port, value);
            }
        }
    }
};

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    LoggingCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    const int START_STEP = 140000;
    const int END_STEP   = 144500;

    for (int i = 0; i < START_STEP; ++i) {
        cpc.step_num = i;
        int tstates = cpc.cpu().step();
        cpc.tick(tstates);
    }

    cpc.logging = true;
    for (int i = START_STEP; i < END_STEP; ++i) {
        cpc.step_num = i;
        int tstates = cpc.cpu().step();
        cpc.tick(tstates);

        // Parar si el firmware intenta ejecutar código en la zona vacía.
        if (cpc.cpu().regs().pc >= 0xC006 && cpc.cpu().regs().pc < 0xC100) {
            std::printf("\n[step %d] PC llegó a 0x%04X, parando.\n",
                        i, cpc.cpu().regs().pc);
            break;
        }
    }

    return 0;
}