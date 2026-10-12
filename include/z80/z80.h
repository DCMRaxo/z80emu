#pragma once

#include "registers.h"
#include <cstdint>

namespace z80 {

// ---------------------------------------------------------------------------
// IBus: interfaz abstracta del bus del sistema.
// (Sin cambios respecto a versiones anteriores)
// ---------------------------------------------------------------------------
class IBus {
public:
    virtual ~IBus() = default;

    virtual std::uint8_t read (std::uint16_t addr) = 0;
    virtual void         write(std::uint16_t addr, std::uint8_t value) = 0;

    virtual std::uint8_t in (std::uint16_t port) = 0;
    virtual void         out(std::uint16_t port, std::uint8_t value) = 0;
};

// ---------------------------------------------------------------------------
// [8a] IndexMode: indica qué par de registros usar como "HL base".
//
// El Z80 tiene tres pares de 16 bits que se comportan de forma parecida:
//   - HL  (siempre disponible)
//   - IX  (solo cuando la instrucción viene precedida por el prefijo DD)
//   - IY  (solo cuando la instrucción viene precedida por el prefijo FD)
//
// Muchas instrucciones se escriben una sola vez en el código, pero eligen
// cuál de los tres pares usar según este "modo". Esto evita triplicar el
// decodificador entero para las versiones con prefijo.
//
// De momento (Paso 8a) esta distinción no se usa todavía en el comportamiento
// real: el step() público siempre pasa HL. Se usará a partir del Paso 8b.
// ---------------------------------------------------------------------------
enum class IndexMode {
    HL,   // sin prefijo
    IX,   // con prefijo DD
    IY    // con prefijo FD
};

// ---------------------------------------------------------------------------
// Z80: el procesador.
// ---------------------------------------------------------------------------
class Z80 {
public:
    explicit Z80(IBus& bus);

    void reset();

    // Ejecuta UNA instrucción completa. Devuelve los T-states consumidos.
    // Públicamente siempre arranca en modo HL; los prefijos DD/FD cambiarán
    // el modo internamente en el Paso 8b.
    int step();

    // Acceso a los registros (para tu panel didáctico y para los tests).
    Registers&       regs()       { return regs_; }
    const Registers& regs() const { return regs_; }

    // Total de T-states acumulados desde el reset.
    std::uint64_t cycles() const { return cycles_; }

private:
    // --- Referencias y estado ---
    IBus&         bus_;
    Registers     regs_;
    std::uint64_t cycles_ = 0;

    // =========================================================================
    // Helpers de fetch
    // =========================================================================

    // Lee el siguiente byte de memoria en PC y avanza PC.
    std::uint8_t  fetch_byte();

    // Lee dos bytes (little-endian) desde PC y avanza PC dos posiciones.
    std::uint16_t fetch_word();

    // =========================================================================
    // [8a] Helpers de selección de registros según IndexMode
    //
    // Estos son los que permiten que una misma instrucción trabaje con HL,
    // IX o IY indistintamente. Todos son "puros": reciben el modo y hacen
    // lo correcto.
    // =========================================================================

    // Devuelve el valor del par base (HL, IX o IY) según el modo.
    std::uint16_t get_hl (IndexMode m) const;

    // Escribe el par base. Para HL usa set_hl del bloque de registros.
    void          set_hl (IndexMode m, std::uint16_t v);

    // Devuelve el byte "alto" del par base: H, IXH o IYH.
    std::uint8_t  get_h  (IndexMode m) const;

    // Devuelve el byte "bajo" del par base: L, IXL o IYL.
    std::uint8_t  get_l  (IndexMode m) const;

    // Escribe el byte alto del par base.
    void          set_h  (IndexMode m, std::uint8_t v);

    // Escribe el byte bajo del par base.
    void          set_l  (IndexMode m, std::uint8_t v);

    // Devuelve la dirección "efectiva" de (HL)/(IX+d)/(IY+d).
    //
    // El parámetro d es el desplazamiento CON SIGNO que aparece tras el
    // opcode en las instrucciones con (IX+d) o (IY+d). Para HL se ignora
    // (siempre 0).
    //
    // En el Paso 8a esta función todavía no se usa en instrucciones reales
    // (porque aún no implementamos (IX+d)); se usará a partir del 8c.
    std::uint16_t get_hl_addr(IndexMode m, std::int8_t d) const;

    // =========================================================================
    // Helpers de lectura/escritura de registros de 8 bits por índice (0..7)
    //
    // La codificación Z80 para registros de 8 bits es:
    //   0=B  1=C  2=D  3=E  4=H  5=L  6=(HL)  7=A
    //
    // [8a] Ahora reciben un IndexMode.
    //   - Para idx = 6 (el caso (HL)), en modo IX/IY se convierte en
    //     (IX+d) o (IY+d). En el Paso 8a todavía lo tratamos como (HL)
    //     también en modo IX/IY, porque aún no leemos el byte d. En el 8c
    //     lo arreglaremos.
    //   - Para idx = 4 (H) y 5 (L), en modo IX/IY se convierten en IXH/IXL
    //     o IYH/IYL, respectivamente. En el 8a ya respetamos esto porque
    //     es gratis (los helpers get_h/get_l ya existen).
    // =========================================================================
    std::uint8_t read_reg8 (IndexMode m, int idx);
    void         write_reg8(IndexMode m, int idx, std::uint8_t value);

    // =========================================================================
    // Helpers de lectura/escritura de pares de 16 bits por índice
    //
    // Convención: 0=BC 1=DE 2=HL 3=SP
    //
    // [8a] Añadimos una variante "extendida" que interpreta el índice 2
    //      (HL) como el par base del IndexMode. Los índices 0 (BC), 1 (DE)
    //      y 3 (SP) no cambian nunca (en el Z80 real no existe IX/BC ni nada
    //      por el estilo: solo HL se sustituye por IX/IY).
    // =========================================================================
    std::uint16_t read_reg16 (int idx);                                  // sin cambios
    void          write_reg16(int idx, std::uint16_t value);             // sin cambios
    std::uint16_t read_reg16_idx (IndexMode m, int idx);                 // [8a] nuevo
    void          write_reg16_idx(IndexMode m, int idx, std::uint16_t v); // [8a] nuevo

    // =========================================================================
    // Pila
    // =========================================================================
    void          push16(std::uint16_t value);
    std::uint16_t pop16();

    // =========================================================================
    // Cálculo de flags
    // =========================================================================
    void flags_add (std::uint8_t a, std::uint8_t b, std::uint8_t res, bool carry_in);
    void flags_sub (std::uint8_t a, std::uint8_t b, std::uint8_t res, bool carry_in);
    void flags_logical(std::uint8_t res);
    void flags_inc (std::uint8_t before, std::uint8_t after);
    void flags_dec (std::uint8_t before, std::uint8_t after);
    void flags_add16(std::uint16_t a, std::uint16_t b, std::uint16_t res);
    void flags_add16_carry(std::uint16_t a, std::uint16_t b,
                           std::uint16_t res, bool carry_in);
    void flags_sub16_carry(std::uint16_t a, std::uint16_t b,
                           std::uint16_t res, bool carry_in);

    static bool parity_even(std::uint8_t v);

    // Comprueba condición "cc" (0..7): 0=NZ 1=Z 2=NC 3=C 4=PO 5=PE 6=P 7=M
    bool condition(int cc) const;

    // =========================================================================
    // Prefijos
    // =========================================================================

    // [8a] Firma modificada: ahora recibe el IndexMode.
    // En el Paso 8a el modo siempre es HL; a partir del 8b podrá ser IX/IY.
    int execute_cb(IndexMode mode);

    // [8a] Firma modificada: ahora recibe el IndexMode.
    // Nota: en el Z80 real, el prefijo ED "ignora" a DD/FD (no existen
    // instrucciones ED que se vean afectadas por DD/FD). Pero el prefijo DD
    // SÍ se consume y el ED se ejecuta normalmente. Por eso el modo se pasa
    // aunque en la práctica no se use dentro de execute_ed.
    int execute_ed(IndexMode mode);

    // =========================================================================
    // Helpers del prefijo CB (rotaciones y BIT/RES/SET)
    // =========================================================================
    std::uint8_t do_rotate(int op, std::uint8_t value);
    void         do_bit   (int bit, std::uint8_t value);

    // =========================================================================
    // [8a] Nuevo: ejecución interna con IndexMode
    //
    // step() público llama a esta función con IndexMode::HL tras leer el
    // primer byte. Cuando en el 8b implementemos DD/FD, el step() público
    // detectará el prefijo, leerá el siguiente byte y llamará a esta misma
    // función con IndexMode::IX o IndexMode::IY.
    //
    // Contiene exactamente el mismo switch que antes tenía step(), pero
    // usando los helpers get_hl/set_hl/read_reg8(m, ...) etc.
    // =========================================================================
    int execute_with_mode(IndexMode mode, std::uint8_t op);

    // [8c] Desplazamiento de la instrucción en curso (solo para IX/IY).
    // Se lee UNA VEZ por instrucción, justo después del opcode, y los
    // helpers read_reg8/write_reg8 lo usan para calcular (IX+d) o (IY+d).
    // En modo HL no se usa (get_hl_addr ignora d).
    std::int8_t ea_offset_ = 0;

    // [8c] Devuelve true si el opcode usa (HL) y por tanto, en modo IX/IY,
    // requiere leer el byte d justo después del opcode.
    bool opcode_uses_hl_indirect(std::uint8_t op) const;
};

} // namespace z80