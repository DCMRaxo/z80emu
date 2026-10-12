#include "amstrad/amstrad_cpc.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    amstrad::AmstradCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    // Test de escritura/lectura en varias zonas de RAM.
    std::printf("Test de RAM:\n\n");

    // Zona 1: 0xB900-0xB90F (RAM del sistema).
    std::printf("Zona 0xB900-0xB90F:\n");
    for (int i = 0; i < 16; ++i) {
        cpc.write(0xB900 + i, static_cast<std::uint8_t>(0x11 + i));
    }
    bool ok1 = true;
    for (int i = 0; i < 16; ++i) {
        std::uint8_t v = cpc.read(0xB900 + i);
        std::uint8_t expected = static_cast<std::uint8_t>(0x11 + i);
        if (v != expected) {
            std::printf("  ERROR en 0x%04X: esperado %02X, leído %02X\n",
                        0xB900 + i, expected, v);
            ok1 = false;
        }
    }
    if (ok1) std::printf("  OK\n\n");

    // Zona 2: 0xC000-0xC00F (RAM de pantalla).
    std::printf("Zona 0xC000-0xC00F:\n");
    for (int i = 0; i < 16; ++i) {
        cpc.write(0xC000 + i, static_cast<std::uint8_t>(0xAA + i));
    }
    bool ok2 = true;
    for (int i = 0; i < 16; ++i) {
        std::uint8_t v = cpc.read(0xC000 + i);
        std::uint8_t expected = static_cast<std::uint8_t>(0xAA + i);
        if (v != expected) {
            std::printf("  ERROR en 0x%04X: esperado %02X, leído %02X\n",
                        0xC000 + i, expected, v);
            ok2 = false;
        }
    }
    if (ok2) std::printf("  OK\n\n");

    // Zona 3: 0xFF00-0xFF0F (parte alta de la RAM de pantalla).
    std::printf("Zona 0xFF00-0xFF0F:\n");
    for (int i = 0; i < 16; ++i) {
        cpc.write(0xFF00 + i, static_cast<std::uint8_t>(0x55 + i));
    }
    bool ok3 = true;
    for (int i = 0; i < 16; ++i) {
        std::uint8_t v = cpc.read(0xFF00 + i);
        std::uint8_t expected = static_cast<std::uint8_t>(0x55 + i);
        if (v != expected) {
            std::printf("  ERROR en 0x%04X: esperado %02X, leído %02X\n",
                        0xFF00 + i, expected, v);
            ok3 = false;
        }
    }
    if (ok3) std::printf("  OK\n\n");

    // Zona 4: 0x8000-0x800F (RAM baja).
    std::printf("Zona 0x8000-0x800F:\n");
    for (int i = 0; i < 16; ++i) {
        cpc.write(0x8000 + i, static_cast<std::uint8_t>(0x00 + i));
    }
    bool ok4 = true;
    for (int i = 0; i < 16; ++i) {
        std::uint8_t v = cpc.read(0x8000 + i);
        std::uint8_t expected = static_cast<std::uint8_t>(0x00 + i);
        if (v != expected) {
            std::printf("  ERROR en 0x%04X: esperado %02X, leído %02X\n",
                        0x8000 + i, expected, v);
            ok4 = false;
        }
    }
    if (ok4) std::printf("  OK\n\n");

    return 0;
}