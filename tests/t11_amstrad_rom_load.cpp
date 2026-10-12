#include "amstrad/amstrad_cpc.h"
#include <cstdio>

// ===========================================================================
// Test de carga de las ROMs del Amstrad CPC.
//
// Verifica que:
//   - Las ROMs se cargan correctamente desde fichero.
//   - Los primeros bytes son los esperados (varía según la versión).
//   - El mapa de memoria del CPC funciona: leer 0x0000 devuelve la ROM baja,
//     leer 0x4000 devuelve la ROM alta, leer 0x8000 devuelve RAM.
//   - El Z80 ejecuta los primeros steps de la ROM sin fallar.
// ===========================================================================

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("Uso: %s <rom_baja> <rom_alta>\n", argv[0]);
        std::printf("Ejemplo: %s OS_464.ROM BASIC.ROM\n", argv[0]);
        return 1;
    }

    amstrad::AmstradCPC cpc;

    if (!cpc.load_roms(argv[1], argv[2])) {
        std::printf("Error cargando las ROMs.\n");
        return 2;
    }

    std::printf("ROMs cargadas correctamente.\n\n");

    // Volcado de los primeros bytes de cada ROM, para verificar que no son 0.
    std::printf("Primeros 16 bytes de la ROM baja (0x0000):\n  ");
    for (int i = 0; i < 16; ++i) std::printf("%02X ", cpc.rom_lower()[i]);
    std::printf("\n\n");

    std::printf("Primeros 16 bytes de la ROM alta (0x4000):\n  ");
    for (int i = 0; i < 16; ++i) std::printf("%02X ", cpc.rom_upper()[i]);
    std::printf("\n\n");

    // Comprobar el mapa de memoria a través del bus.
    std::printf("Verificación del mapa de memoria:\n");
    std::printf("  read(0x0000) = %02X  (debe coincidir con ROM baja[0])\n",
                cpc.read(0x0000));
    std::printf("  read(0x4000) = %02X  (debe coincidir con ROM alta[0])\n",
                cpc.read(0x4000));
    std::printf("  read(0x8000) = %02X  (RAM, inicialmente 0x00)\n",
                cpc.read(0x8000));

    // Ejecutar unos cuantos steps del Z80 y ver dónde está.
    std::printf("\nEjecutando 20 steps desde 0x0000...\n");

    for (int i = 0; i < 20; ++i) {
        auto& r = cpc.cpu().regs();
        std::uint16_t pc_before = r.pc;
        std::uint8_t op = cpc.read(pc_before);

        cpc.cpu().step();

        std::printf("  step %2d: PC=%04X op=%02X -> PC=%04X AF=%02X%02X BC=%02X%02X\n",
                    i, pc_before, op, r.pc,
                    r.a, r.f, r.b, r.c);
    }

    return 0;
}