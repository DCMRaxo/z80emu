// Pruebas del Paso 6b

#include "z80/z80.h"
#include <cstdio>
#include "test_bus.h"

int main() {
    SimpleBus bus;
    z80::Z80 cpu(bus);

    // Datos a copiar en 0x9000..0x9004: 11 22 33 44 55
    bus.ram[0x9000] = 0x11;
    bus.ram[0x9001] = 0x22;
    bus.ram[0x9002] = 0x33;
    bus.ram[0x9003] = 0x44;
    bus.ram[0x9004] = 0x55;

    // Programa:
    //   0x0000: 21 00 90    LD HL, 0x9000   (origen)
    //   0x0003: 11 00 A0    LD DE, 0xA000   (destino)
    //   0x0006: 01 05 00    LD BC, 0x0005   (5 bytes)
    //   0x0009: ED B0       LDIR            copia 5 bytes con un solo step
    //   0x000B: 3E 33       LD A, 0x33
    //   0x000D: 21 00 A0    LD HL, 0xA000
    //   0x0010: 01 05 00    LD BC, 0x0005
    //   0x0013: ED B1       CPIR            busca A=0x33 en 5 bytes → lo encuentra en el 3º
    //   0x0015: 76          HALT
    std::uint8_t prog[] = {
        0x21, 0x00, 0x90,
        0x11, 0x00, 0xA0,
        0x01, 0x05, 0x00,
        0xED, 0xB0,
        0x3E, 0x33,
        0x21, 0x00, 0xA0,
        0x01, 0x05, 0x00,
        0xED, 0xB1,
        0x76,
    };
    for (std::size_t i = 0; i < sizeof(prog); ++i)
        bus.ram[i] = prog[i];

    int i = 0;
    while (i < 40 && !cpu.regs().halted) {
        cpu.step();
        dump(cpu, i);
        i++;
    }

    // Verificación: los 5 bytes deben estar copiados en 0xA000..0xA004
    std::printf("  mem[0xA000..A004] = %02X %02X %02X %02X %02X (esperado 11 22 33 44 55)\n",
                bus.ram[0xA000], bus.ram[0xA001], bus.ram[0xA002],
                bus.ram[0xA003], bus.ram[0xA004]);

    return 0;
}