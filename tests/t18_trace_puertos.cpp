#include "amstrad/amstrad_cpc.h"
#include <cstdio>

// Subclase que loguea IN/OUT y algunos opcodes interesantes.
class TracingCPC : public amstrad::AmstradCPC {
public:
    bool logging_ports = false;
    bool logging_calls = false;
    int step_num = 0;

    std::uint8_t in(std::uint16_t port) override {
        std::uint8_t v = amstrad::AmstradCPC::in(port);
        if (logging_ports) {
            std::printf("[step %5d] IN  0x%04X -> 0x%02X\n", step_num, port, v);
        }
        return v;
    }

    void out(std::uint16_t port, std::uint8_t value) override {
        amstrad::AmstradCPC::out(port, value);
        if (logging_ports) {
            std::printf("[step %5d] OUT 0x%04X <- 0x%02X\n", step_num, port, value);
        }
    }
};

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    TracingCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    const int STEPS = 5000;

    cpc.logging_ports = true;

    for (int i = 0; i < STEPS; ++i) {
        cpc.step_num = i;
        std::uint16_t pc = cpc.cpu().regs().pc;
        std::uint8_t op = cpc.read(pc);

        int tstates = cpc.cpu().step();
        cpc.tick(tstates);
    }

    return 0;
}