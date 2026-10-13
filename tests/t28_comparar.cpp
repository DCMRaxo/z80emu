#include "amstrad/amstrad_cpc.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    amstrad::AmstradCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    // Volcado de la ROM baja desde 0x0391 (origen de la tabla).
    std::printf("ROM baja 0x0391-0x0500:\n");
    for (int base = 0x0391; base < 0x0500; base += 16) {
        std::printf("  %04X: ", base);
        for (int j = 0; j < 16; ++j) {
            std::printf("%02X ", cpc.read(base + j));
        }
        std::printf("\n");
    }
    std::printf("\n");

    // Volcado de la ROM baja desde 0x0500 (por si la tabla continúa).
    std::printf("ROM baja 0x0500-0x0600:\n");
    for (int base = 0x0500; base < 0x0600; base += 16) {
        std::printf("  %04X: ", base);
        for (int j = 0; j < 16; ++j) {
            std::printf("%02X ", cpc.read(base + j));
        }
        std::printf("\n");
    }
    std::printf("\n");

    // Ejecutar firmware hasta antes del desborde del SP.
    const std::uint64_t STEPS = 402'000ULL;
    for (std::uint64_t i = 0; i < STEPS; ++i) {
        int tstates = cpc.cpu().step();
        cpc.tick(tstates);
    }

    // Volcado de la RAM 0xB927-0xBA00 (continuación de la tabla).
    std::printf("RAM 0xB920-0xBA00:\n");
    for (int base = 0xB920; base < 0xBA00; base += 16) {
        std::printf("  %04X: ", base);
        for (int j = 0; j < 16; ++j) {
            std::printf("%02X ", cpc.read(base + j));
        }
        std::printf("\n");
    }
    std::printf("\n");

    // Comparación: ¿coincide la RAM con la ROM en 0xB900-0xBA00?
    std::printf("Comparación RAM 0xB900-0xBA00 con ROM 0x0391-0x0491:\n");
    bool mismatch_found = false;
    for (int i = 0; i < 0x100; ++i) {
        std::uint8_t ram_val = cpc.read(0xB900 + i);
        std::uint8_t rom_val = cpc.read(0x0391 + i);
        if (ram_val != rom_val) {
            std::printf("  DIFERENCIA en offset %03X: RAM[%04X]=%02X, ROM[%04X]=%02X\n",
                        i, 0xB900 + i, ram_val, 0x0391 + i, rom_val);
            mismatch_found = true;
            if (i > 20) break;  // Suficiente para ver el patrón
        }
    }
    if (!mismatch_found) {
        std::printf("  Coinciden perfectamente en los primeros 0x100 bytes.\n");
    }

    return 0;
}