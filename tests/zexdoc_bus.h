#pragma once
#include "z80/z80.h"
#include <cstdio>
#include <cstdint>
#include <cstring>

// ===========================================================================
// ZexdocBus: bus especial que simula el mínimo entorno CP/M necesario para
// ejecutar zexdoc.com.
//
// Cómo funciona:
//   - Al construirse, carga el binario "zexdoc.com" en 0x0100.
//   - En 0x0000 pone un OUT (0), A → usado como "fin de programa".
//   - En 0x0005 pone un IN A, (0) → usado como "llamada al BDOS".
//   - Al recibir OUT al puerto 0, si el PC está en 0x0000, marca fin.
//   - Al recibir IN del puerto 0, mira el registro C del Z80 para
//     decidir qué llamada al BDOS se está haciendo:
//       C == 2  → imprime el carácter en E.
//       C == 9  → imprime la cadena apuntada por DE hasta '$'.
//       (otras funciones → se ignoran)
// ===========================================================================
class ZexdocBus : public z80::IBus {
public:
    // Puntero al CPU: se asigna después de construirlo (dependencia circular).
    z80::Z80* cpu = nullptr;

    // Señal de "programa terminado".
    bool finished = false;

    // Contador de caracteres impresos (útil para saber si salió algo).
    std::size_t output_bytes = 0;

    std::uint8_t ram[0x10000] = {};

    // -----------------------------------------------------------------------
    // Constructor: carga zexdoc.com en 0x0100 y prepara los "stubs" de CP/M.
    // -----------------------------------------------------------------------
    ZexdocBus() {
        // "Stub" en 0x0000: OUT (0), A → usado como fin de programa.
        // Opcode: D3 00
        ram[0x0000] = 0xD3;
        ram[0x0001] = 0x00;

        // "Stub" en 0x0005: IN A, (0) + RET.
        // Opcodes: DB 00 C9
        ram[0x0005] = 0xDB;
        ram[0x0006] = 0x00;
        ram[0x0007] = 0xC9;

        // Cargar el binario en 0x0100.
        FILE* f = std::fopen("zexdoc.com", "rb");
        if (!f) {
            std::fprintf(stderr, "ERROR: no se puede abrir zexdoc.com\n");
            return;
        }
        std::size_t n = std::fread(&ram[0x0100], 1, 0x10000 - 0x0100, f);
        std::fclose(f);
        std::printf("Cargados %zu bytes de zexdoc.com en 0x0100\n", n);
    }

    // -----------------------------------------------------------------------
    // Memoria: acceso directo al array.
    // -----------------------------------------------------------------------
    std::uint8_t read(std::uint16_t addr) override {
        return ram[addr];
    }

    void write(std::uint16_t addr, std::uint8_t value) override {
        ram[addr] = value;
    }

    // -----------------------------------------------------------------------
    // IN: simula las llamadas al BDOS.
    //
    // Se llama cuando el Z80 ejecuta IN desde 0x0005 (el stub que hemos
    // puesto). Miramos el registro C para decidir qué hacer.
    // -----------------------------------------------------------------------
    std::uint8_t in(std::uint16_t port) override {
        if (!cpu) return 0xFF;

        if (port == 0) {
            const auto& r = cpu->regs();
            std::uint8_t func = r.c;

            if (func == 2) {
                // Imprimir el carácter en E.
                std::putchar(r.e);
                ++output_bytes;
            } else if (func == 9) {
                // Imprimir la cadena apuntada por DE hasta '$'.
                std::uint16_t addr = static_cast<std::uint16_t>((r.d << 8) | r.e);
                while (ram[addr] != '$') {
                    std::putchar(ram[addr]);
                    ++output_bytes;
                    ++addr;
                }
            }
            // Otras funciones se ignoran.
        }
        return 0xFF;
    }

    // -----------------------------------------------------------------------
    // OUT: detecta el fin de programa.
    //
    // El stub en 0x0000 ejecuta OUT (0), A. Cuando lo detectamos,
    // marcamos finished = true.
    // -----------------------------------------------------------------------
    void out(std::uint16_t port, std::uint8_t /*value*/) override {
        if (port == 0) {
            // Cualquier OUT al puerto 0 desde el PC en 0x0000 se considera
            // "fin de programa". El stub en 0x0000 siempre es OUT (0), A.
            finished = true;
        }
    }
};