#pragma once
// ^ "pragma once" evita que este fichero se incluya más de una vez por unidad
//   de traducción (equivalente moderno a los clásicos #ifndef/#define).

#include <cstdint>
// ^ Necesario para std::uint8_t, std::uint16_t, etc.

namespace z80 {
// ^ Todo el emulador vive en el namespace "z80" para no contaminar el global.

// ---------------------------------------------------------------------------
// Banderas (flags) del registro F.
//
// El Z80 guarda 6 flags "reales" (más 2 bits no documentados) en un único
// byte llamado F. Cada bit tiene un significado distinto según la operación.
// Los definimos como constantes para poder escribir cosas como:
//     regs.f |= CF;              // activar carry
//     regs.f &= ~ZF;             // desactivar zero
// ---------------------------------------------------------------------------
enum Flag : std::uint8_t {
    CF = 0x01,  // bit 0: Carry
    NF = 0x02,  // bit 1: Add/Subtract (1 = la última op fue resta)
    PF = 0x04,  // bit 2: Parity / Overflow (doble uso según instrucción)
    XF = 0x08,  // bit 3: no documentado (copia del bit 3 del resultado)
    HF = 0x10,  // bit 4: Half Carry (acarreo del bit 3 al 4)
    YF = 0x20,  // bit 5: no documentado (copia del bit 5 del resultado)
    ZF = 0x40,  // bit 6: Zero (1 = el resultado fue 0)
    SF = 0x80,  // bit 7: Sign (copia del bit 7 del resultado)
};

// ---------------------------------------------------------------------------
// Par de registros de 16 bits con acceso simultáneo a los dos bytes.
//
// Un "union" en C++ permite que varios campos compartan la MISMA memoria.
// Aquí:
//   - "word" ocupa 2 bytes (16 bits).
//   - "bytes.low" y "bytes.high" ocupan 1 byte cada uno y se solapan con word.
//
// Gracias a esto, escribir en "word" cambia también low y high, y viceversa.
// Es la forma limpia y sin coste de modelar AF, BC, DE, HL.
//
// ATENCIÓN: el layout real depende del endianness de la máquina. En x86 y
// la mayoría de plataformas modernas (little-endian), bytes.low se solapa
// con el byte 0 de word y bytes.high con el byte 1. Eso es lo que queremos.
// El Z80 en memoria es little-endian también, así que todo encaja.
// ---------------------------------------------------------------------------
union RegisterPair {
    std::uint16_t word;   // vista como 16 bits
    struct {
        std::uint8_t low;   // byte bajo (F, C, E, L, ...)
        std::uint8_t high;  // byte alto  (A, B, D, H, ...)
    } bytes;              // vista como dos bytes
};

// ---------------------------------------------------------------------------
// Estado completo de registros del Z80.
//
// Agrupamos todo aquí para que la clase Z80 solo tenga que gestionar "un
// bloque de estado" en lugar de decenas de variables sueltas. Además, así
// es trivial hacer copias de estado (útil para depuración / rewind).
// ---------------------------------------------------------------------------
struct Registers {
    // --- Registros principales de 8 bits ---
    std::uint8_t a = 0;   // Acumulador
    std::uint8_t f = 0;   // Flags
    std::uint8_t b = 0;
    std::uint8_t c = 0;
    std::uint8_t d = 0;
    std::uint8_t e = 0;
    std::uint8_t h = 0;
    std::uint8_t l = 0;

    // --- Registros alternativos (los "primos" del Z80) ---
    // Se intercambian con los principales mediante EX AF,AF' y EXX.
    // El Z80 no tiene registros alternativos para IX, IY, SP ni PC.
    std::uint8_t a_ = 0;
    std::uint8_t f_ = 0;
    std::uint8_t b_ = 0;
    std::uint8_t c_ = 0;
    std::uint8_t d_ = 0;
    std::uint8_t e_ = 0;
    std::uint8_t h_ = 0;
    std::uint8_t l_ = 0;

    // --- Registros índice de 16 bits ---
    // Se usan con los prefijos DD y FD. Por ejemplo: LD A,(IX+5).
    std::uint16_t ix = 0;
    std::uint16_t iy = 0;

    // --- Puntero de pila y contador de programa ---
    std::uint16_t sp = 0;   // Stack Pointer (pila, crece hacia abajo)
    std::uint16_t pc = 0;   // Program Counter (siguiente instrucción)

    // --- Registros especiales ---
    std::uint8_t i = 0;     // Vector base para interrupciones modo 2
    std::uint8_t r = 0;     // Refresh (solo 7 bits útiles, el bit 7 se preserva)

    // --- Estado auxiliar del CPU ---
    bool         iff1  = false;  // Interrupt Flip-Flop 1: ¿interrupciones habilitadas?
    bool         iff2  = false;  // Interrupt Flip-Flop 2 (copia para NMI/RETN)
    std::uint8_t im    = 0;      // Interrupt Mode: 0, 1 o 2
    bool         halted = false; // ¿El CPU está detenido por HALT?

    // -----------------------------------------------------------------------
    // Helpers para leer/escribir pares de registros como 16 bits.
    //
    // En el Z80, el byte ALTO es el primer registro del nombre:
    //   AF  ->  A es el byte alto, F el bajo
    //   BC  ->  B alto, C bajo    (etc.)
    // -----------------------------------------------------------------------
    RegisterPair af() const { return { static_cast<std::uint16_t>((a << 8) | f) }; }
    RegisterPair bc() const { return { static_cast<std::uint16_t>((b << 8) | c) }; }
    RegisterPair de() const { return { static_cast<std::uint16_t>((d << 8) | e) }; }
    RegisterPair hl() const { return { static_cast<std::uint16_t>((h << 8) | l) }; }

    void set_af(std::uint16_t v) { a = static_cast<std::uint8_t>(v >> 8); f = static_cast<std::uint8_t>(v & 0xFF); }
    void set_bc(std::uint16_t v) { b = static_cast<std::uint8_t>(v >> 8); c = static_cast<std::uint8_t>(v & 0xFF); }
    void set_de(std::uint16_t v) { d = static_cast<std::uint8_t>(v >> 8); e = static_cast<std::uint8_t>(v & 0xFF); }
    void set_hl(std::uint16_t v) { h = static_cast<std::uint8_t>(v >> 8); l = static_cast<std::uint8_t>(v & 0xFF); }

    // -----------------------------------------------------------------------
    // Helpers para manipular flags de F de forma legible.
    // -----------------------------------------------------------------------
    bool flag(std::uint8_t bit) const { return (f & bit) != 0; }

    void set_flag(std::uint8_t bit, bool value) {
        if (value) f |=  bit;
        else       f &= static_cast<std::uint8_t>(~bit);
    }
};

} // namespace z80