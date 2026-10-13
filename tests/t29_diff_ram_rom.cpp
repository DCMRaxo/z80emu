#include "amstrad/amstrad_cpc.h"
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    amstrad::AmstradCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    const std::uint64_t STEPS = 402'000ULL;
    for (std::uint64_t i = 0; i < STEPS; ++i) {
        int tstates = cpc.cpu().step();
        cpc.tick(tstates);
    }

    // Buscar la secuencia de 32 bytes que empieza en 0xB900 en la ROM.
    // (Sabemos que empieza en 0x0391).
    // Vamos a buscar la secuencia "C3 5E BA C3 68 BA C3 4A BA C3 54 BA" en toda la ROM.
    const std::uint8_t pattern[] = {0xC3, 0x5E, 0xBA, 0xC3, 0x68, 0xBA, 0xC3, 0x4A, 0xBA, 0xC3, 0x54, 0xBA};
    int rom_offset = -1;
    for (int addr = 0; addr < 0x10000 - 12; ++addr) {
        if (std::memcmp(&cpc.ram()[0], &pattern, 0) == 0) {} // dummy
    }
    // Buscamos leyendo directamente del bus (soporta ROM y RAM).
    for (int addr = 0; addr < 0x10000 - 12; ++addr) {
        bool match = true;
        for (int j = 0; j < 12; ++j) {
            if (cpc.read(static_cast<std::uint16_t>(addr + j)) != pattern[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            std::printf("Patrón encontrado en 0x%04X\n", addr);
            rom_offset = addr;
            break;
        }
    }

    if (rom_offset < 0) {
        std::printf("No se encontró el patrón en memoria.\n");
        return 3;
    }

    // Comparar toda la zona 0xB900-0xC000 con la ROM desde rom_offset.
    std::printf("\nComparación RAM[0xB900-0xC000] con ROM[0x%04X-...]:\n", rom_offset);
    int differences = 0;
    for (int i = 0; i < 0x0700; ++i) {
        std::uint8_t ram_val = cpc.read(static_cast<std::uint16_t>(0xB900 + i));
        std::uint8_t rom_val = cpc.read(static_cast<std::uint16_t>(rom_offset + i));
        if (ram_val != rom_val) {
            std::printf("  DIFERENCIA offset %04X: RAM[%04X]=%02X, ROM[%04X]=%02X\n",
                        i, 0xB900 + i, ram_val, rom_offset + i, rom_val);
            differences++;
            if (differences > 30) {
                std::printf("  ... (más diferencias)\n");
                break;
            }
        }
    }
    if (differences == 0) {
        std::printf("  Sin diferencias en 0x0700 bytes.\n");
    }

    // Volcado de las zonas críticas.
    std::printf("\nRAM 0xBDF0-0xC000 (zona donde el firmware se pierde):\n");
    for (int base = 0xBDF0; base < 0xC000; base += 16) {
        std::printf("  RAM %04X: ", base);
        for (int j = 0; j < 16; ++j) std::printf("%02X ", cpc.read(base + j));
        std::printf("\n");
        std::printf("  ROM %04X: ", rom_offset + (base - 0xB900));
        for (int j = 0; j < 16; ++j) {
            std::printf("%02X ", cpc.read(static_cast<std::uint16_t>(rom_offset + (base - 0xB900) + j)));
        }
        std::printf("\n");
    }

    return 0;
}