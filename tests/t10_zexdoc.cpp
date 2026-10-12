#include "zexdoc_bus.h"
#include <cstdio>
#include <cstdint>

// ===========================================================================
// Test del Paso 10: ejecución de ZEXDOC.
//
// ZEXDOC es el test de referencia para emuladores de Z80. Ejecuta miles de
// combinaciones de operandos sobre cada instrucción y compara los flags
// contra valores conocidos obtenidos de un Z80 real.
//
// Cómo se ejecuta:
//   - El binario zexdoc.com se carga en 0x0100 (dirección típica de CP/M).
//   - Los stubs de CP/M en 0x0000 (fin de programa) y 0x0005 (BDOS) se
//     ponen en su sitio.
//   - Arrancamos con PC = 0x0100.
//   - Ejecutamos hasta que el bus detecte el OUT (0), A de fin de programa.
//   - La salida (banner + OK/ERROR por grupo) se imprime por stdout.
//
// Este fichero incluye un diagnóstico de bucle infinito: si pasados unos
// millones de steps el PC no cambia y la salida tampoco, volcamos el estado
// del CPU y la memoria alrededor del PC, y paramos.
// ===========================================================================

int main() {
    ZexdocBus bus;
    z80::Z80 cpu(bus);

    // Dependencia circular: el bus necesita al CPU para leer registros,
    // y el CPU necesita al bus para acceder a memoria.
    bus.cpu = &cpu;

    // Arrancamos como haría CP/M: PC = 0x0100, SP en la parte alta.
    cpu.regs().pc = 0x0100;
    cpu.regs().sp = 0xFF00;

    std::printf("Arrancando ZEXDOC... (esto puede tardar varios minutos)\n\n");

    // Límite absoluto de seguridad. Con un emulador en C++ en un PC moderno,
    // ZEXDOC puede tardar entre 1 y 10 minutos (cientos de millones de steps).
    // Ponemos un valor alto por si acaso.
    const std::uint64_t MAX_STEPS = 50'000'000'000ULL;
    std::uint64_t steps = 0;

    // Variables para detección de bucle infinito.
    // Cada CHECK_INTERVAL steps comprobamos si el PC y la salida han cambiado.
    const std::uint64_t CHECK_INTERVAL = 500'000'000ULL;   // 10 M steps
    const int  MAX_NO_PROGRESS = 3;                       // 3 ciclos sin progreso = bucle

    std::uint16_t last_pc = 0;
    std::size_t   last_output_bytes = 0;
    int no_progress_count = 0;

    while (!bus.finished && !cpu.regs().halted && steps < MAX_STEPS) {
        cpu.step();
        ++steps;

        // Cada CHECK_INTERVAL steps, comprobar progreso.
        if ((steps % CHECK_INTERVAL) == 0) {
            std::uint16_t pc = cpu.regs().pc;

            bool no_progress = (pc == last_pc) &&
                               (bus.output_bytes == last_output_bytes);

            if (no_progress) {
                ++no_progress_count;
                if (no_progress_count >= MAX_NO_PROGRESS) {
                    // Demasiado tiempo sin progreso: volcar estado y salir.
                    std::printf("\n\n=== SIN PROGRESO DETECTADO ===\n");
                    std::printf("PC=0x%04X (sin cambios durante %d M steps)\n",
                                pc, MAX_NO_PROGRESS * 10);

                    const auto& r = cpu.regs();
                    std::printf("AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X\n",
                                r.a, r.f, r.b, r.c, r.d, r.e, r.h, r.l);
                    std::printf("IX=%04X IY=%04X SP=%04X\n",
                                r.ix, r.iy, r.sp);
                    std::printf("IFF1=%d IFF2=%d IM=%d halted=%d R=%02X\n",
                                r.iff1 ? 1 : 0, r.iff2 ? 1 : 0,
                                r.im, r.halted ? 1 : 0, r.r);

                    // Volcado de los 64 bytes alrededor del PC.
                    std::printf("\nMemoria alrededor del PC (0x%04X):\n", pc);
                    std::uint16_t base = static_cast<std::uint16_t>(pc & 0xFFC0);
                    for (int row = 0; row < 4; ++row) {
                        std::uint16_t a = static_cast<std::uint16_t>(base + row * 16);
                        std::printf("  %04X: ", a);
                        for (int j = 0; j < 16; ++j) {
                            std::printf("%02X ", bus.ram[a + j]);
                        }
                        std::printf("\n");
                    }

                    // Volcado de la pila (SP y siguientes 16 bytes).
                    std::printf("\nPila (SP=0x%04X):\n", r.sp);
                    for (int row = 0; row < 2; ++row) {
                        std::uint16_t a = static_cast<std::uint16_t>(r.sp + row * 8);
                        std::printf("  %04X: ", a);
                        for (int j = 0; j < 8; ++j) {
                            std::printf("%02X ", bus.ram[a + j]);
                        }
                        std::printf("\n");
                    }
                    break;
                }
            } else {
                no_progress_count = 0;
            }

            last_pc = pc;
            last_output_bytes = bus.output_bytes;

            std::printf("[%llu M steps, %zu bytes, PC=%04X]\n",
                        static_cast<unsigned long long>(steps / 1'000'000ULL),
                        bus.output_bytes, pc);
            std::fflush(stdout);
        }
    }

    std::printf("\n\n");
    if (bus.finished) {
        std::printf("ZEXDOC terminado. Steps totales: %llu\n",
                    static_cast<unsigned long long>(steps));
    } else if (cpu.regs().halted) {
        std::printf("ZEXDOC detenido: CPU en HALT tras %llu steps.\n",
                    static_cast<unsigned long long>(steps));
        std::printf("Último PC: 0x%04X\n", cpu.regs().pc);
    } else {
        std::printf("ZEXDOC NO ha terminado tras %llu steps.\n",
                    static_cast<unsigned long long>(steps));
        std::printf("Último PC: 0x%04X\n", cpu.regs().pc);
    }

    return 0;
}