#pragma once
// ^ Igual que en registers.h: evita doble inclusión.

#include "registers.h"
// ^ Incluimos la definición de Registers, Flag, RegisterPair...
//   La ruta es relativa al directorio "include" (que se pasa con -Iinclude).

#include <cstdint>

namespace z80 {

// ---------------------------------------------------------------------------
// IBus: interfaz abstracta del bus del sistema.
//
// El Z80 real no tiene memoria ni puertos propios: solo tiene un bus de
// direcciones, un bus de datos y unas señales de control. Modelamos eso
// con esta interfaz.
//
// Cualquier "máquina" (Amstrad CPC, Spectrum, un test...) implementará
// IBus y decidirá qué devuelve cada dirección y cada puerto.
//
// Al ser una interfaz pura (todos los métodos virtuales puros), el CPU
// no depende de ninguna implementación concreta.
// ---------------------------------------------------------------------------
class IBus {
public:
    virtual ~IBus() = default;

    // Memoria: dirección de 16 bits, dato de 8 bits.
    virtual std::uint8_t read (std::uint16_t addr) = 0;
    virtual void         write(std::uint16_t addr, std::uint8_t value) = 0;

    // Puertos de E/S (instrucciones IN / OUT).
    // En el Z80, el puerto se direcciona con 16 bits (A en el byte alto,
    // el operando en el bajo), pero muchos periféricos solo miran los 8 bajos.
    virtual std::uint8_t in (std::uint16_t port) = 0;
    virtual void         out(std::uint16_t port, std::uint8_t value) = 0;
};

// ---------------------------------------------------------------------------
// Z80: el procesador.
//
// La clase contiene:
//   - Una referencia al bus (IBus&), que le da acceso al exterior.
//   - El bloque de registros (Registers).
//   - Un contador acumulado de T-states (ciclos de reloj), útil para
//     sincronizar con el resto del sistema (vídeo, sonido, etc.).
// ---------------------------------------------------------------------------
class Z80 {
public:
    // Construimos el CPU conectándolo a un bus concreto. El bus debe
    // sobrevivir al Z80 (por eso guardamos una referencia, no una copia).
    explicit Z80(IBus& bus);

    // Reinicio del CPU como si se acabara de encender (o se pulsara RESET).
    void reset();

    // Ejecuta UNA instrucción completa.
    // Devuelve el número de T-states consumidos por esa instrucción.
    int step();

    // Acceso a los registros (para depuración, tests, panel didáctico...).
    Registers&       regs()       { return regs_; }
    const Registers& regs() const { return regs_; }

    // Total de T-states acumulados desde el último reset.
    std::uint64_t cycles() const { return cycles_; }

private:
    // --- Referencias y estado ---
    IBus&          bus_;    // conexión con el exterior (memoria + E/S)
    Registers      regs_;   // todos los registros del Z80
    std::uint64_t  cycles_ = 0;  // contador acumulado de T-states

    // --- Helpers internos del ciclo fetch/decode/execute ---

    // Lee el siguiente byte de memoria en PC y avanza PC.
    std::uint8_t  fetch_byte();

    // Lee dos bytes (little-endian) desde PC y avanza PC dos posiciones.
    // El primer byte leído es el BAJO, el segundo el ALTO.
    std::uint16_t fetch_word();

    // Lee el registro de 8 bits indicado por índice 0..7.
    // El índice 6 significa "(HL)": acceso a memoria en la dirección HL.
    std::uint8_t read_reg8(int idx);

    // Escribe el registro de 8 bits indicado por índice 0..7.
    // Igual que read_reg8: el índice 6 escribe en (HL).
    void write_reg8(int idx, std::uint8_t value);

    // --- Cálculo de flags ---
    // Recalculan todos los flags según el resultado y los operandos.
    void flags_add (std::uint8_t a, std::uint8_t b, std::uint8_t res, bool carry_in);
    void flags_sub (std::uint8_t a, std::uint8_t b, std::uint8_t res, bool carry_in);
    void flags_logical(std::uint8_t res);   // AND / OR / XOR

    // Versiones para INC/DEC, que NO tocan el flag C.
    void flags_inc (std::uint8_t before, std::uint8_t after);
    void flags_dec (std::uint8_t before, std::uint8_t after);

    // Paridad par: ¿hay un número par de bits a 1?
    static bool parity_even(std::uint8_t v);

    // Lee un registro de 16 bits por índice.
    // Convención Z80 para pares "principales" (grupo LD rr,nn, ADD HL,rr, PUSH/POP):
    //   0=BC  1=DE  2=HL  3=SP
    // Para PUSH/POP AF hay un índice aparte (4) porque A y F no forman
    // un par "normal" en la codificación de LD rr,nn.
    std::uint16_t read_reg16(int idx);
    void          write_reg16(int idx, std::uint16_t value);

    // Pila (SP crece hacia abajo).
    void push16(std::uint16_t value);
    std::uint16_t pop16();

    // Flags específicos de operaciones de 16 bits.
    void flags_add16(std::uint16_t a, std::uint16_t b, std::uint16_t res);

    // Comprueba condición "cc" (0..7) según la codificación Z80:
    //   0=NZ 1=Z 2=NC 3=C 4=PO 5=PE 6=P 7=M
    bool condition(int cc) const;

    // Ejecuta una instrucción CB (el opcode CB ya fue leído, aquí viene el segundo byte).
    int  execute_cb();

    // Aplica una operación de rotación/shift al valor dado.
    // op: 0=RLC 1=RRC 2=RL 3=RR 4=SLA 5=SRA 6=SLL 7=SRL
    // Devuelve el resultado y actualiza los flags. El carry de entrada solo
    // se usa en RL y RR (para el "a través del carry").
    std::uint8_t do_rotate(int op, std::uint8_t value);

    // Aplica BIT b, value. Devuelve solo actualiza flags, no modifica value.
    void do_bit(int bit, std::uint8_t value);

    // Ejecuta una instrucción ED (el 0xED ya fue leído; aquí viene el segundo byte).
    int execute_ed();

    // Flags para ADC HL, rr / SBC HL, rr (16 bits, con carry).
    void flags_add16_carry(std::uint16_t a, std::uint16_t b, std::uint16_t res, bool carry_in);
    void flags_sub16_carry(std::uint16_t a, std::uint16_t b, std::uint16_t res, bool carry_in);
};

} // namespace z80