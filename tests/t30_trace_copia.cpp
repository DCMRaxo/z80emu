#include "amstrad/amstrad_cpc.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    amstrad::AmstradCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) return 2;

    // Ejecutamos hasta el step 402000 (antes del desborde del SP) y luego
    // volcamos un histórico de la RAM 0xB900-0xC000 desde el principio.
    // Para ello, ejecutamos por tramos y volcamos cada 1000 steps.

    const std::uint64_t STEPS = 402'000ULL;
    const std::uint64_t INTERVAL = 1000ULL;

    for (std::uint64_t i = 0; i <= STEPS; i += INTERVAL) {
        // Ejecutar INTERVAL steps
        for (std::uint64_t j = 0; j < INTERVAL && (i + j) < STEPS; ++j) {
            int tstates = cpc.cpu().step();
            cpc.tick(tstates);
        }

        // Comprobar el estado de la RAM en 0xBAE9 y 0xBB00.
        std::uint8_t byte_BAE9 = cpc.read(0xBAE9);
        std::uint8_t byte_BB00 = cpc.read(0xBB00);

        // Solo imprimir cuando cambien (indicando que la copia ha avanzado).
        static std::uint8_t prev_BAE9 = 0xFF;
        static std::uint8_t prev_BB00 = 0xFF;
        if (byte_BAE9 != prev_BAE9 || byte_BB00 != prev_BB00) {
            const auto& r = cpc.cpu().regs();
            std::printf("[step %6llu] PC=%04X RAM[BAE9]=%02X RAM[BB00]=%02X SP=%04X HL=%04X DE=%04X BC=%04X\n",
                        static_cast<unsigned long long>(i),
                        r.pc, byte_BAE9, byte_BB00, r.sp, r.hl().word, r.de().word, r.bc().word);
            prev_BAE9 = byte_BAE9;
            prev_BB00 = byte_BB00;
        }
    }

    return 0;
}