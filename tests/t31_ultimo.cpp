#include "amstrad/amstrad_cpc.h"
#include <cstdio>

class TraceCPC : public amstrad::AmstradCPC {
public:
    bool logging = false;
    int step_num = 0;

    std::uint8_t read(std::uint16_t addr) override {
        std::uint8_t v = amstrad::AmstradCPC::read(addr);
        if (logging) {
            std::printf("[step %5d] READ  [%04X] = %02X\n", step_num, addr, v);
        }
        return v;
    }

    void write(std::uint16_t addr, std::uint8_t value) override {
        amstrad::AmstradCPC::write(addr, value);
        if (logging) {
            // Solo loguear escrituras a 0xB900-0xC000.
            if (addr >= 0xB900) {
                std::printf("[step %5d] WRITE [%04X] = %02X\n", step_num, addr, value);
            }
        }
    }
};

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    TraceCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    // Ejecutar hasta el step 200 con logging de escrituras a 0xB900+.
    cpc.logging = true;
    for (int i = 0; i < 200; ++i) {
        cpc.step_num = i;
        int tstates = cpc.cpu().step();
        cpc.tick(tstates);
    }

    return 0;
}