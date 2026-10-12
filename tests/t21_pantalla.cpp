#include "amstrad/amstrad_cpc.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    amstrad::AmstradCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    // Ejecutar 50 M steps para que el firmware arranque completamente.
    for (std::uint64_t i = 0; i < 50'000'000ULL; ++i) {
        int tstates = cpc.cpu().step();
        cpc.tick(tstates);
    }

    std::printf("Estado tras 50 M steps:\n");
    const auto& r = cpc.cpu().regs();
    std::printf("PC=%04X SP=%04X AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X IX=%04X IY=%04X\n\n",
                r.pc, r.sp, r.a, r.f, r.b, r.c, r.d, r.e, r.h, r.l, r.ix, r.iy);

    // Volcado de la pantalla (0xC000-0xC040).
    std::printf("Pantalla 0xC000-0xC040:\n");
    for (int row = 0; row < 4; ++row) {
        std::uint16_t a = static_cast<std::uint16_t>(0xC000 + row * 16);
        std::printf("  %04X: ", a);
        for (int j = 0; j < 16; ++j) {
            std::printf("%02X ", cpc.read(a + j));
        }
        std::printf("\n");
    }
    std::printf("\n");

    // Volcado de la zona de variables del BASIC.
    // En el CPC, la zona 0xB000-0xBFFF contiene variables del sistema.
    std::printf("Variables del sistema 0xB100-0xB140:\n");
    for (int row = 0; row < 4; ++row) {
        std::uint16_t a = static_cast<std::uint16_t>(0xB100 + row * 16);
        std::printf("  %04X: ", a);
        for (int j = 0; j < 16; ++j) {
            std::printf("%02X ", cpc.read(a + j));
        }
        std::printf("\n");
    }
    std::printf("\n");

    // Volcado de los colores (Gate Array) y del CRTC.
    std::printf("Último valor del Gate Array: 0x%02X\n", cpc.ga_last_value());
    std::printf("Último registro CRTC seleccionado: %d\n", cpc.crtc_selected_reg());
    for (int i = 0; i < 16; ++i) {
        std::printf("  CRTC R%d = 0x%02X\n", i, cpc.crtc_reg(i));
    }

    return 0;
}