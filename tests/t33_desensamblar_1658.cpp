#include "amstrad/amstrad_cpc.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    amstrad::AmstradCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    std::printf("ROM baja 0x1600-0x1700:\n");
    for (int base = 0x1600; base < 0x1700; base += 16) {
        std::printf("  %04X: ", base);
        for (int j = 0; j < 16; ++j) std::printf("%02X ", cpc.read(base + j));
        std::printf("\n");
    }

    return 0;
}