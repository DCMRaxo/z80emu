#include "amstrad/amstrad_cpc.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    amstrad::AmstradCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    const int STEPS = 5000;

    for (int i = 0; i < STEPS; ++i) {
        std::uint16_t pc = cpc.cpu().regs().pc;
        std::uint16_t sp = cpc.cpu().regs().sp;
        std::uint8_t op = cpc.read(pc);

        // Trazamos solo CALL, CALL cc, RET, RET cc, RST, JP y saltos.
        // También trazamos si el PC ya está en 0xC000-0xFFFF.
        bool is_call = (op == 0xCD) ||
                       (op >= 0xC4 && op <= 0xFC && (op & 0x07) == 0x04 && op != 0xDC);
        // ↑ CALL cc solo son los que terminan en 100 con cc válido.
        // Simplificamos: trazamos cualquier opcode que empiece por C si está en 0xC0-0xFF
        // y veremos.

        bool is_ret = (op == 0xC9) ||
                      (op >= 0xC0 && op <= 0xF8 && (op & 0x07) == 0x00);

        bool is_rst = (op & 0xC7) == 0xC7;

        bool in_screen = (pc >= 0xC000);

        if (is_call || is_ret || is_rst || in_screen) {
            const auto& r = cpc.cpu().regs();
            std::printf("[step %5d] PC=%04X SP=%04X op=%02X AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X\n",
                        i, pc, sp, op, r.a, r.f, r.b, r.c, r.d, r.e, r.h, r.l);
        }

        int tstates = cpc.cpu().step();
        cpc.tick(tstates);
    }

    return 0;
}