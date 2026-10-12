#include "amstrad/amstrad_cpc.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    amstrad::AmstradCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    // Volcado de los bytes de la ROM baja en la zona del bucle de espera.
    std::printf("ROM baja 0x0F20-0x0F60:\n");
    for (int row = 0; row < 4; ++row) {
        std::uint16_t a = static_cast<std::uint16_t>(0x0F20 + row * 16);
        std::printf("  %04X: ", a);
        for (int j = 0; j < 16; ++j) {
            std::printf("%02X ", cpc.read(a + j));
        }
        std::printf("\n");
    }
    std::printf("\n");

    // Volcado también de la zona anterior por si la rutina empieza antes.
    std::printf("ROM baja 0x0F00-0x0F20:\n");
    for (int row = 0; row < 2; ++row) {
        std::uint16_t a = static_cast<std::uint16_t>(0x0F00 + row * 16);
        std::printf("  %04X: ", a);
        for (int j = 0; j < 16; ++j) {
            std::printf("%02X ", cpc.read(a + j));
        }
        std::printf("\n");
    }

    return 0;
}