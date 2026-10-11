#include "test_bus.h"

int main() {
    SimpleBus bus;
    z80::Z80 cpu(bus);

    // Programa de prueba del Paso 8b: prefijos DD/FD sin desplazamiento.
    //
    //   0x0000: DD 21 34 12   LD IX, 0x1234
    //   0x0004: FD 21 78 56   LD IY, 0x5678
    //   0x0008: DD 26 AA      LD IXH, 0xAA     IX = 0xAA34
    //   0x000B: DD 2E BB      LD IXL, 0xBB     IX = 0xAABB
    //   0x000E: 21 00 90      LD HL, 0x9000    HL independiente de IX
    //   0x0011: DD E5         PUSH IX          apila IX en la pila
    //   0x0013: DD E1         POP IX           recupera IX (sin cambios)
    //   0x0015: DD E9         JP (IX)          salta a la dirección de IX
    //
    // Ponemos un HALT en 0xAABB para que JP (IX) termine el programa.
    std::uint8_t prog[] = {
        0xDD, 0x21, 0x34, 0x12,   // LD IX, 0x1234
        0xFD, 0x21, 0x78, 0x56,   // LD IY, 0x5678
        0xDD, 0x26, 0xAA,         // LD IXH, 0xAA    IX = 0xAA34
        0xDD, 0x2E, 0xBB,         // LD IXL, 0xBB    IX = 0xAABB
        0x21, 0x00, 0x90,         // LD HL, 0x9000
        0xDD, 0xE5,               // PUSH IX
        0xDD, 0xE1,               // POP IX
        0xDD, 0xE9,               // JP (IX)  →  salta a 0xAABB
    };
    load(bus, prog);

    // El JP (IX) saltará a 0xAABB, así que ponemos un HALT ahí.
    bus.ram[0xAABB] = 0x76;

    // Inicializamos SP, B y C a valores conocidos antes de arrancar.
    // (Aún no hemos implementado un programa de inicialización, así que
    //  los fijamos directamente en los registros.)
    cpu.regs().sp = 0xFF00;
    cpu.regs().b  = 0x01;
    cpu.regs().c  = 0x23;   // BC = 0x0123

    // Bucle principal con volcado extendido para ver IX e IY.
    int i = 0;
    while (i < 40 && !cpu.regs().halted) {
        cpu.step();

        const auto& r = cpu.regs();
        std::printf(
            "step %2d: PC=%04X AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X IX=%04X IY=%04X SP=%04X\n",
            i, r.pc, r.a, r.f, r.b, r.c, r.d, r.e, r.h, r.l,
            r.ix, r.iy, r.sp);

        // Verificación de memoria tras PUSH IX (que ocurre en el step 5,
        // contando desde 0: steps 0..5 son las 6 primeras instrucciones,
        // y PUSH IX es la 6ª → índice 5).
        if (i == 5) {
            std::printf("        mem[FEFE]=%02X mem[FEFF]=%02X (esperado BB AA)\n",
                        bus.ram[0xFEFE], bus.ram[0xFEFF]);
        }

        i++;
    }

    return 0;
}