#include "amstrad/amstrad_cpc.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    amstrad::AmstradCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    // Ejecutamos hasta 100 M steps, o hasta que el PC se estabilice.
    const std::uint64_t MAX_STEPS = 100'000'000ULL;
    const std::uint64_t CHECK = 1'000'000ULL;

    std::uint64_t steps = 0;
    std::uint16_t last_pc = 0xFFFF;
    int stable_count = 0;

    while (steps < MAX_STEPS) {
        cpc.run(CHECK);
        steps += CHECK;

        std::uint16_t pc = cpc.cpu().regs().pc;
        if (pc == last_pc) {
            stable_count++;
            if (stable_count >= 10) break;
        } else {
            stable_count = 0;
            last_pc = pc;
        }
    }

    const auto& r = cpc.cpu().regs();
    std::printf("Tras %llu steps:\n", static_cast<unsigned long long>(steps));
    std::printf("PC=%04X SP=%04X AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X IX=%04X IY=%04X\n\n",
                r.pc, r.sp, r.a, r.f, r.b, r.c, r.d, r.e, r.h, r.l, r.ix, r.iy);

    std::printf("CRTC: frames=%llu h=%d v=%d raster=%d hsync=%d vsync=%d\n\n",
                static_cast<unsigned long long>(cpc.crtc().frame_count()),
                cpc.crtc().horizontal_counter(),
                cpc.crtc().vertical_counter(),
                cpc.crtc().raster_counter(),
                cpc.crtc().hsync_active() ? 1 : 0,
                cpc.crtc().vsync_active() ? 1 : 0);

    std::printf("Pantalla 0xC000-0xC100:\n");
    for (int base = 0xC000; base < 0xC100; base += 16) {
        std::printf("  %04X: ", base);
        for (int j = 0; j < 16; ++j) std::printf("%02X ", cpc.read(base + j));
        std::printf("\n");
    }
    std::printf("\n");

    std::printf("ROM baja 0x0000-0x0100:\n");
    for (int base = 0x0000; base < 0x0100; base += 16) {
        std::printf("  %04X: ", base);
        for (int j = 0; j < 16; ++j) std::printf("%02X ", cpc.read(base + j));
        std::printf("\n");
    }
    std::printf("\n");

    std::printf("ROM baja 0x0030-0x0050 (handler de interrupción):\n");
    for (int base = 0x0030; base < 0x0050; base += 16) {
        std::printf("  %04X: ", base);
        for (int j = 0; j < 16; ++j) std::printf("%02X ", cpc.read(base + j));
        std::printf("\n");
    }

    return 0;
}