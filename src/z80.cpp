#include "z80/z80.h"
#include <cstdio>
#include <utility>

namespace z80 {

// ---------------------------------------------------------------------------
// Constructor: guarda la referencia al bus y hace un reset inicial.
// ---------------------------------------------------------------------------
Z80::Z80(IBus& bus) : bus_(bus) {
    reset();
}

// ---------------------------------------------------------------------------
// reset(): deja el CPU en un estado conocido, equivalente al power-on.
//
// En el Z80 real, tras un reset:
//   - PC = 0x0000
//   - SP = 0xFFFF (o algo parecido; no está estrictamente garantizado)
//   - I = 0, R = 0, IM = 0
//   - IFF1 = IFF2 = 0  (interrupciones deshabilitadas)
//   - AF, BC, DE, HL y sus alternativos quedan indefinidos, pero por
//     comodidad los ponemos a 0.
// ---------------------------------------------------------------------------
void Z80::reset() {
    regs_ = Registers{};          // todos los campos a su valor por defecto (0/false)
    regs_.sp = 0xFFFF;
    regs_.pc = 0x0000;
    regs_.im = 0;
    regs_.iff1 = false;
    regs_.iff2 = false;
    regs_.halted = false;
    cycles_ = 0;
}

// ---------------------------------------------------------------------------
// fetch_byte(): lee el byte en PC y avanza PC en 1.
// ---------------------------------------------------------------------------
std::uint8_t Z80::fetch_byte() {
    return bus_.read(regs_.pc++);
}

// ---------------------------------------------------------------------------
// fetch_word(): lee dos bytes desde PC. El Z80 es little-endian,
// así que el primer byte es el BAJO y el segundo el ALTO.
//
// Ejemplo: si en memoria hay [34, 12] y PC apunta ahí, devolvemos 0x1234.
// ---------------------------------------------------------------------------
std::uint16_t Z80::fetch_word() {
    std::uint8_t lo = fetch_byte();       // primer byte = bajo
    std::uint8_t hi = fetch_byte();       // segundo byte = alto
    return static_cast<std::uint16_t>(lo | (hi << 8));
}

// ---------------------------------------------------------------------------
// read_reg8(idx): devuelve el valor del registro 8 bits "idx".
//
// La codificación del Z80 para registros de 8 bits es:
//   0=B  1=C  2=D  3=E  4=H  5=L  6=(HL)  7=A
//
// El índice 6 NO es un registro: es un acceso a memoria en la dirección HL.
// Es la forma que tiene el Z80 de decir "usa el byte apuntado por HL".
// ---------------------------------------------------------------------------
std::uint8_t Z80::read_reg8(int idx) {
    switch (idx) {
        case 0: return regs_.b;
        case 1: return regs_.c;
        case 2: return regs_.d;
        case 3: return regs_.e;
        case 4: return regs_.h;
        case 5: return regs_.l;
        case 6: return bus_.read(regs_.hl().word);  // (HL)
        case 7: return regs_.a;
    }
    return 0;  // nunca debería llegar aquí si idx está en 0..7
}

// ---------------------------------------------------------------------------
// write_reg8(idx, v): escribe v en el registro 8 bits "idx".
// Misma codificación que read_reg8.
// ---------------------------------------------------------------------------
void Z80::write_reg8(int idx, std::uint8_t value) {
    switch (idx) {
        case 0: regs_.b = value; return;
        case 1: regs_.c = value; return;
        case 2: regs_.d = value; return;
        case 3: regs_.e = value; return;
        case 4: regs_.h = value; return;
        case 5: regs_.l = value; return;
        case 6: bus_.write(regs_.hl().word, value); return;  // (HL)
        case 7: regs_.a = value; return;
    }
}

// ---------------------------------------------------------------------------
// parity_even(v): devuelve true si v tiene un número PAR de bits a 1.
// Es la "paridad par" que usa el Z80 para el flag P/V en operaciones lógicas.
//
// Truco clásico: XOR de todos los bits, agrupando.
// ---------------------------------------------------------------------------
bool Z80::parity_even(std::uint8_t v) {
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (v & 1) == 0;   // par si el bit 0 resultante es 0
}

// ---------------------------------------------------------------------------
// flags_add: recalcula S, Z, H, V, N, C, XF, YF tras una suma (ADD o ADC).
//
//   a        = operando izquierdo (A antes de la operación)
//   b        = operando derecho (registro, inmediato o (HL))
//   res      = resultado de 8 bits (a + b + carry_in)
//   carry_in = carry de entrada (0 para ADD, CF anterior para ADC)
//
// Fórmulas (documentadas en el manual del Z80):
//   S  = bit 7 de res
//   Z  = (res == 0)
//   H  = acarreo de bit 3: ((a & 0x0F) + (b & 0x0F) + carry_in) > 0x0F
//   V  = overflow con signo: ((a ^ res) & (b ^ res) & 0x80) != 0
//   N  = 0 (fue suma)
//   C  = acarreo de bit 7: (a + b + carry_in) > 0xFF
//   XF = bit 3 de res, YF = bit 5 de res
// ---------------------------------------------------------------------------
void Z80::flags_add(std::uint8_t a, std::uint8_t b,
                    std::uint8_t res, bool carry_in) {
    std::uint8_t f = 0;

    if (res & 0x80)          f |= SF;
    if (res == 0)            f |= ZF;
    if (((a & 0x0F) + (b & 0x0F) + (carry_in ? 1 : 0)) > 0x0F) f |= HF;
    if (((a ^ res) & (b ^ res) & 0x80) != 0) f |= PF;   // overflow con signo
    // N queda a 0
    if ((static_cast<int>(a) + b + (carry_in ? 1 : 0)) > 0xFF) f |= CF;
    f |= res & (XF | YF);   // copiar bits 3 y 5 del resultado

    regs_.f = f;
}

// ---------------------------------------------------------------------------
// flags_sub: recalcula los flags tras una resta (SUB, SBC o CP).
//
//   a, b, res, carry_in  →  igual que en flags_add, pero aquí b y carry_in
//                            se RESTAN.
//
// Fórmulas:
//   S  = bit 7 de res
//   Z  = (res == 0)
//   H  = borrow del bit 4: (a & 0x0F) < ((b & 0x0F) + carry_in)
//   V  = overflow con signo: ((a ^ b) & (a ^ res) & 0x80) != 0
//   N  = 1 (fue resta)
//   C  = borrow del bit 7: a < (b + carry_in)
//   XF = bit 3 de res, YF = bit 5 de res
// ---------------------------------------------------------------------------
void Z80::flags_sub(std::uint8_t a, std::uint8_t b,
                    std::uint8_t res, bool carry_in) {
    std::uint8_t f = 0;

    if (res & 0x80)          f |= SF;
    if (res == 0)            f |= ZF;
    if ((a & 0x0F) < ((b & 0x0F) + (carry_in ? 1 : 0))) f |= HF;
    if (((a ^ b) & (a ^ res) & 0x80) != 0) f |= PF;   // overflow con signo
    f |= NF;
    if (a < static_cast<int>(b) + (carry_in ? 1 : 0)) f |= CF;
    f |= res & (XF | YF);

    regs_.f = f;
}

// ---------------------------------------------------------------------------
// flags_logical: flags para AND / OR / XOR.
//
// Reglas:
//   S  = bit 7 de res
//   Z  = (res == 0)
//   H  = 0   (a diferencia de las aritméticas)
//   V  = paridad par de res
//   N  = 0
//   C  = 0   (SIEMPRE se limpia, sin importar lo que hubiera)
//   XF = bit 3 de res, YF = bit 5 de res
// ---------------------------------------------------------------------------
void Z80::flags_logical(std::uint8_t res) {
    std::uint8_t f = 0;

    if (res & 0x80)     f |= SF;
    if (res == 0)       f |= ZF;
    if (parity_even(res)) f |= PF;
    // H, N, C quedan a 0
    f |= res & (XF | YF);

    regs_.f = f;
}

// ---------------------------------------------------------------------------
// flags_inc: flags para INC r (NO toca el carry).
//
// INC se comporta como una suma con 1, salvo que:
//   - C se PRESERVA tal cual estaba.
//   - El flag H sí se recalcula.
// ---------------------------------------------------------------------------
void Z80::flags_inc(std::uint8_t before, std::uint8_t after) {
    std::uint8_t f = regs_.f & CF;   // preservar carry

    if (after & 0x80)         f |= SF;
    if (after == 0)           f |= ZF;
    if ((before & 0x0F) == 0x0F) f |= HF;      // 0x_ F + 1 desborda el nibble bajo
    if (before == 0x7F)       f |= PF;         // overflow: 127 + 1 = -128 (signed)
    // N = 0
    f |= after & (XF | YF);

    regs_.f = f;
}

// ---------------------------------------------------------------------------
// flags_dec: flags para DEC r (NO toca el carry).
//
// DEC se comporta como una resta con 1, salvo que:
//   - C se PRESERVA.
//   - El flag H sí se recalcula.
// ---------------------------------------------------------------------------
void Z80::flags_dec(std::uint8_t before, std::uint8_t after) {
    std::uint8_t f = regs_.f & CF;   // preservar carry

    if (after & 0x80)         f |= SF;
    if (after == 0)           f |= ZF;
    if ((before & 0x0F) == 0x00) f |= HF;      // borrow del nibble bajo
    if (before == 0x80)       f |= PF;         // overflow: -128 - 1 = 127
    f |= NF;
    f |= after & (XF | YF);

    regs_.f = f;
}

// ---------------------------------------------------------------------------
// step(): ejecuta UNA instrucción completa y devuelve los T-states usados.
//
// Estructura típica de un emulador:
//   1. Fetch: leer el opcode (y operandos si hace falta).
//   2. Decode: identificar la operación.
//   3. Execute: modificar el estado del CPU.
//   4. Contabilizar T-states y sumarlos al contador global.
//
// De momento implementamos un subconjunto de instrucciones:
//   - NOP                (0x00)
//   - HALT               (0x76)
//   - JP nn              (0xC3)
//   - JR e               (0x18)
//   - LD r,n             (0x06, 0x0E, 0x16, 0x1E, 0x26, 0x2E, 0x3E)
//   - LD r,r'            (0x40..0x7F, excepto 0x76 que ya es HALT)
//
// Los opcodes desconocidos imprimen un aviso y detienen el CPU, para no
// corromper el estado silenciosamente mientras desarrollamos.
// ---------------------------------------------------------------------------
int Z80::step() {
    std::uint8_t op = fetch_byte();
    int tstates = 4;

    switch (op) {

        // =================================================================
        // GRUPO 1: instrucciones de 16 bits y control de flujo "directo"
        // =================================================================

        // ---- NOP ----------------------------------------------------
        case 0x00: break;

        // ---- HALT ---------------------------------------------------
        case 0x76: regs_.halted = true; break;

        // ---- LD rr, nn ----------------------------------------------
        // Opcodes: 0x01=BC 0x11=DE 0x21=HL 0x31=SP
        case 0x01: case 0x11: case 0x21: case 0x31: {
            int rr = (op >> 4) & 0x03;      // 0=BC, 1=DE, 2=HL, 3=SP
            std::uint16_t nn = fetch_word();
            write_reg16(rr, nn);
            tstates = 10;
            break;
        }

        // ---- EX AF, AF' ---------------------------------------------
        // Intercambia el par AF con el par alternativo AF'.
        case 0x08: {
            std::swap(regs_.a, regs_.a_);
            std::swap(regs_.f, regs_.f_);
            tstates = 4;
            break;
        }

        // ---- EXX ----------------------------------------------------
        // Intercambia BC, DE, HL con sus alternativos.
        case 0xD9: {
            std::swap(regs_.b, regs_.b_);
            std::swap(regs_.c, regs_.c_);
            std::swap(regs_.d, regs_.d_);
            std::swap(regs_.e, regs_.e_);
            std::swap(regs_.h, regs_.h_);
            std::swap(regs_.l, regs_.l_);
            tstates = 4;
            break;
        }

        // ---- EX DE, HL ----------------------------------------------
        // Intercambia los pares DE y HL. No toca flags.
        case 0xEB: {
            std::swap(regs_.d, regs_.h);
            std::swap(regs_.e, regs_.l);
            tstates = 4;
            break;
        }

        // ---- EX (SP), HL --------------------------------------------
        // Intercambia HL con el valor apilado en (SP).
        // En memoria: (SP) = byte bajo, (SP+1) = byte alto.
        case 0xE3: {
            std::uint8_t lo = bus_.read(regs_.sp);
            std::uint8_t hi = bus_.read(static_cast<std::uint16_t>(regs_.sp + 1));
            bus_.write(regs_.sp, regs_.l);
            bus_.write(static_cast<std::uint16_t>(regs_.sp + 1), regs_.h);
            regs_.h = hi;
            regs_.l = lo;
            tstates = 19;
            break;
        }

        // ---- CPL ----------------------------------------------------
        // A = ~A. Pone H y N a 1. No toca S, Z, P/V ni C.
        case 0x2F: {
            regs_.a = static_cast<std::uint8_t>(~regs_.a);
            std::uint8_t f = regs_.f & (SF | ZF | PF | CF);  // preservar S, Z, P/V, C
            f |= HF | NF;
            f |= regs_.a & (XF | YF);   // XF/YF del nuevo A
            regs_.f = f;
            tstates = 4;
            break;
        }

        // ---- SCF ----------------------------------------------------
        // C = 1, H = 0, N = 0. No toca S, Z, P/V.
        case 0x37: {
            std::uint8_t f = regs_.f & (SF | ZF | PF);  // preservar S, Z, P/V
            f |= CF;
            f |= regs_.a & (XF | YF);   // XF/YF de A
            regs_.f = f;
            tstates = 4;
            break;
        }

        // ---- CCF ----------------------------------------------------
        // C = !C_anterior, H = C_anterior, N = 0. No toca S, Z, P/V.
        case 0x3F: {
            bool old_c = regs_.flag(CF);
            std::uint8_t f = regs_.f & (SF | ZF | PF);  // preservar S, Z, P/V
            if (old_c) f |= HF;      // nuevo H = carry viejo
            if (!old_c) f |= CF;     // nuevo C = complemento del viejo
            f |= regs_.a & (XF | YF);   // XF/YF de A
            regs_.f = f;
            tstates = 4;
            break;
        }

        // ---- DAA ----------------------------------------------------
        // Ajusta A para que sea un BCD válido tras ADD/ADC o SUB/SBC.
        // Mira los flags H y N para saber cómo ajustar, y usa C para el
        // nibble alto. Reglas del Z80 real.
        case 0x27: {
            std::uint8_t a = regs_.a;
            bool n = regs_.flag(NF);
            bool h = regs_.flag(HF);
            bool c = regs_.flag(CF);

            std::uint8_t diff = 0;
            if ((a & 0x0F) > 9 || h) diff |= 0x06;
            if (a > 0x99 || c)       diff |= 0x60;

            // Nota: el chequeo "a > 0x99" es un atajo: el nibble alto es > 9
            // si el byte es > 0x99. El Z80 real usa exactamente esto.

            std::uint8_t res;
            bool new_c;
            if (n) {
                res = static_cast<std::uint8_t>(a - diff);
                new_c = c && (a >= 0x60);   // el carry se mantiene si venía a 1 y restamos 0x60
            } else {
                res = static_cast<std::uint8_t>(a + diff);
                new_c = c || (a > 0x99);    // el carry se activa si venía a 1 o si sumamos 0x60
            }

            regs_.a = res;

            std::uint8_t f = 0;
            if (res & 0x80)            f |= SF;
            if (res == 0)              f |= ZF;
            if (parity_even(res))      f |= PF;
            if (new_c)                 f |= CF;
            if (n)                     f |= NF;   // N se preserva
            // H se limpia siempre
            f |= res & (XF | YF);

            regs_.f = f;
            tstates = 4;
            break;
        }

        // ---- LD (nn), A ---------------------------------------------
        // Formato: 0x32, nn_low, nn_high
        // Escribe A en la dirección nn.
        case 0x32: {
            std::uint16_t addr = fetch_word();
            bus_.write(addr, regs_.a);
            tstates = 13;
            break;
        }

        // ---- LD A, (nn) ---------------------------------------------
        case 0x3A: {
            std::uint16_t addr = fetch_word();
            regs_.a = bus_.read(addr);
            tstates = 13;
            break;
        }

        // ---- LD (nn), HL  y  LD HL, (nn) ----------------------------
        // Opcodes "de lujo" (no ED): 0x22 = LD (nn),HL;  0x2A = LD HL,(nn)
        case 0x22: {
            std::uint16_t addr = fetch_word();
            std::uint16_t hl = regs_.hl().word;
            bus_.write(addr,       static_cast<std::uint8_t>(hl & 0xFF));
            bus_.write(addr + 1,   static_cast<std::uint8_t>(hl >> 8));
            tstates = 16;
            break;
        }
        case 0x2A: {
            std::uint16_t addr = fetch_word();
            std::uint8_t lo = bus_.read(addr);
            std::uint8_t hi = bus_.read(addr + 1);
            regs_.set_hl(static_cast<std::uint16_t>(lo | (hi << 8)));
            tstates = 16;
            break;
        }

        // ---- INC rr -------------------------------------------------
        // Opcodes: 0x03=BC 0x13=DE 0x23=HL 0x33=SP
        // ¡NO tocan NINGÚN flag!
        case 0x03: case 0x13: case 0x23: case 0x33: {
            int rr = (op >> 4) & 0x03;
            write_reg16(rr, static_cast<std::uint16_t>(read_reg16(rr) + 1));
            tstates = 6;
            break;
        }

        // ---- DEC rr -------------------------------------------------
        // Opcodes: 0x0B=BC 0x1B=DE 0x2B=HL 0x3B=SP
        // ¡NO tocan NINGÚN flag!
        case 0x0B: case 0x1B: case 0x2B: case 0x3B: {
            int rr = (op >> 4) & 0x03;
            write_reg16(rr, static_cast<std::uint16_t>(read_reg16(rr) - 1));
            tstates = 6;
            break;
        }

        // ---- ADD HL, rr ---------------------------------------------
        // Opcodes: 0x09=BC 0x19=DE 0x29=HL 0x39=SP
        case 0x09: case 0x19: case 0x29: case 0x39: {
            int rr = (op >> 4) & 0x03;
            std::uint16_t a = regs_.hl().word;
            std::uint16_t b = read_reg16(rr);
            std::uint16_t r = static_cast<std::uint16_t>(a + b);
            flags_add16(a, b, r);
            regs_.set_hl(r);
            tstates = 11;
            break;
        }

        // ---- PUSH rr / POP rr ---------------------------------------
        // PUSH: 0xC5=BC 0xD5=DE 0xE5=HL 0xF5=AF
        // POP:  0xC1=BC 0xD1=DE 0xE1=HL 0xF1=AF
        // Nota: los índices de la codificación son 0=BC 1=DE 2=HL 3=AF.
        // Para leer/escribir AF usamos regs_.af()/set_af, no read_reg16.
        case 0xC5: case 0xD5: case 0xE5: case 0xF5: {
            int idx = (op >> 4) & 0x03;
            std::uint16_t v = (idx == 3) ? regs_.af().word : read_reg16(idx);
            push16(v);
            tstates = 11;
            break;
        }
        case 0xC1: case 0xD1: case 0xE1: case 0xF1: {
            int idx = (op >> 4) & 0x03;
            std::uint16_t v = pop16();
            if (idx == 3) regs_.set_af(v);
            else          write_reg16(idx, v);
            tstates = 10;
            break;
        }

        // ---- JP nn --------------------------------------------------
        case 0xC3: {
            regs_.pc = fetch_word();
            tstates = 10;
            break;
        }

        // ---- JP cc, nn ----------------------------------------------
        // Opcodes: 0xC2=NZ 0xCA=Z 0xD2=NC 0xDA=C 0xE2=PO 0xEA=PE 0xF2=P 0xFA=M
        // Patrón: 11 ccc 010
        case 0xC2: case 0xCA: case 0xD2: case 0xDA:
        case 0xE2: case 0xEA: case 0xF2: case 0xFA: {
            int cc = (op >> 3) & 0x07;
            std::uint16_t addr = fetch_word();
            if (condition(cc)) {
                regs_.pc = addr;
                tstates = 10;
            } else {
                tstates = 10;
            }
            break;
        }

        // ---- JR e ---------------------------------------------------
        case 0x18: {
            std::int8_t offset = static_cast<std::int8_t>(fetch_byte());
            regs_.pc = static_cast<std::uint16_t>(regs_.pc + offset);
            tstates = 12;
            break;
        }

        // ---- JR cc, e -----------------------------------------------
        // Opcodes: 0x20=NZ 0x28=Z 0x30=NC 0x38=C
        // (solo estos cuatro; el resto no existen en el Z80)
        case 0x20: case 0x28: case 0x30: case 0x38: {
            // En la codificación de JR cc, cc = 0..3 (NZ, Z, NC, C)
            int cc = (op >> 3) & 0x03;
            std::int8_t offset = static_cast<std::int8_t>(fetch_byte());
            if (condition(cc)) {
                regs_.pc = static_cast<std::uint16_t>(regs_.pc + offset);
                tstates = 12;
            } else {
                tstates = 7;
            }
            break;
        }

        // ---- DJNZ e -------------------------------------------------
        case 0x10: {
            std::int8_t offset = static_cast<std::int8_t>(fetch_byte());
            regs_.b--;
            if (regs_.b != 0) {
                regs_.pc = static_cast<std::uint16_t>(regs_.pc + offset);
                tstates = 13;
            } else {
                tstates = 8;
            }
            break;
        }

        // ---- JP (HL) ------------------------------------------------
        case 0xE9: {
            regs_.pc = regs_.hl().word;
            tstates = 4;
            break;
        }

        // ---- CALL nn ------------------------------------------------
        case 0xCD: {
            std::uint16_t addr = fetch_word();
            push16(regs_.pc);   // apilar dirección de retorno (PC ya avanzó)
            regs_.pc = addr;
            tstates = 17;
            break;
        }

        // ---- CALL cc, nn --------------------------------------------
        // Opcodes: 0xC4=NZ 0xCC=Z 0xD4=NC 0xDC=C 0xE4=PO 0xEC=PE 0xF4=P 0xFC=M
        // Patrón: 11 ccc 100
        case 0xC4: case 0xCC: case 0xD4: case 0xDC:
        case 0xE4: case 0xEC: case 0xF4: case 0xFC: {
            int cc = (op >> 3) & 0x07;
            std::uint16_t addr = fetch_word();
            if (condition(cc)) {
                push16(regs_.pc);
                regs_.pc = addr;
                tstates = 17;
            } else {
                tstates = 10;
            }
            break;
        }

        // ---- RET ----------------------------------------------------
        case 0xC9: {
            regs_.pc = pop16();
            tstates = 10;
            break;
        }

        // ---- RET cc -------------------------------------------------
        // Opcodes: 0xC0=NZ 0xC8=Z 0xD0=NC 0xD8=C 0xE0=PO 0xE8=PE 0xF0=P 0xF8=M
        // Patrón: 11 ccc 000
        case 0xC0: case 0xC8: case 0xD0: case 0xD8:
        case 0xE0: case 0xE8: case 0xF0: case 0xF8: {
            int cc = (op >> 3) & 0x07;
            if (condition(cc)) {
                regs_.pc = pop16();
                tstates = 11;
            } else {
                tstates = 5;
            }
            break;
        }

        // ---- RST n --------------------------------------------------
        // Opcodes: 0xC7, 0xCF, 0xD7, 0xDF, 0xE7, 0xEF, 0xF7, 0xFF
        // Patrón: 11 ttt 111  donde ttt * 8 = dirección de destino (0x00..0x38)
        case 0xC7: case 0xCF: case 0xD7: case 0xDF:
        case 0xE7: case 0xEF: case 0xF7: case 0xFF: {
            std::uint16_t target = (op & 0x38);
            push16(regs_.pc);
            regs_.pc = target;
            tstates = 11;
            break;
        }

        // ---- Prefijo CB ---------------------------------------------
        // 0xCB es el "prefijo de extensión": significa que el opcode real
        // está en el byte siguiente. Delegamos en execute_cb().
        case 0xCB: {
            int used = execute_cb();
            cycles_ += used;
            return used;
        }

        // ---- Prefijo ED ---------------------------------------------
        case 0xED: {
            int used = execute_ed();
            cycles_ += used;
            return used;
        }

        // =================================================================
        // GRUPO 2: ALU de 8 bits (del Paso 3, sin cambios)
        // =================================================================
        default: {

            // ---- LD r, n ------------------------------------------------
            if ((op & 0xC7) == 0x06) {
                int dst = (op >> 3) & 0x07;
                std::uint8_t n = fetch_byte();
                write_reg8(dst, n);
                tstates = (dst == 6) ? 10 : 7;
                break;
            }

            // ---- LD r, r' -----------------------------------------------
            if ((op & 0xC0) == 0x40) {
                int dst = (op >> 3) & 0x07;
                int src =  op       & 0x07;
                std::uint8_t v = read_reg8(src);
                write_reg8(dst, v);
                tstates = (dst == 6 || src == 6) ? 7 : 4;
                break;
            }

            // ---- ALU con registro ---------------------------------------
            if ((op & 0xC0) == 0x80) {
                int alu_op = (op >> 3) & 0x07;
                int src    =  op       & 0x07;
                std::uint8_t v = read_reg8(src);

                switch (alu_op) {
                    case 0: { std::uint8_t a=regs_.a, r=a+v; flags_add(a,v,r,false); regs_.a=r; break; }
                    case 1: { std::uint8_t a=regs_.a; bool c=regs_.flag(CF);
                              std::uint8_t r=a+v+(c?1:0); flags_add(a,v,r,c); regs_.a=r; break; }
                    case 2: { std::uint8_t a=regs_.a, r=a-v; flags_sub(a,v,r,false); regs_.a=r; break; }
                    case 3: { std::uint8_t a=regs_.a; bool c=regs_.flag(CF);
                              std::uint8_t r=a-v-(c?1:0); flags_sub(a,v,r,c); regs_.a=r; break; }
                    case 4: { std::uint8_t r=regs_.a & v; flags_logical(r); regs_.a=r; break; }
                    case 5: { std::uint8_t r=regs_.a ^ v; flags_logical(r); regs_.a=r; break; }
                    case 6: { std::uint8_t r=regs_.a | v; flags_logical(r); regs_.a=r; break; }
                    case 7: { std::uint8_t a=regs_.a, r=a-v; flags_sub(a,v,r,false); break; }
                }
                tstates = (src == 6) ? 7 : 4;
                break;
            }

            // ---- ALU con inmediato --------------------------------------
            if ((op & 0xC7) == 0xC6) {
                int alu_op = (op >> 3) & 0x07;
                std::uint8_t n = fetch_byte();

                switch (alu_op) {
                    case 0: { std::uint8_t a=regs_.a, r=a+n; flags_add(a,n,r,false); regs_.a=r; break; }
                    case 1: { std::uint8_t a=regs_.a; bool c=regs_.flag(CF);
                              std::uint8_t r=a+n+(c?1:0); flags_add(a,n,r,c); regs_.a=r; break; }
                    case 2: { std::uint8_t a=regs_.a, r=a-n; flags_sub(a,n,r,false); regs_.a=r; break; }
                    case 3: { std::uint8_t a=regs_.a; bool c=regs_.flag(CF);
                              std::uint8_t r=a-n-(c?1:0); flags_sub(a,n,r,c); regs_.a=r; break; }
                    case 4: { std::uint8_t r=regs_.a & n; flags_logical(r); regs_.a=r; break; }
                    case 5: { std::uint8_t r=regs_.a ^ n; flags_logical(r); regs_.a=r; break; }
                    case 6: { std::uint8_t r=regs_.a | n; flags_logical(r); regs_.a=r; break; }
                    case 7: { std::uint8_t a=regs_.a, r=a-n; flags_sub(a,n,r,false); break; }
                }
                tstates = 7;
                break;
            }

            // ---- INC r --------------------------------------------------
            if ((op & 0xC7) == 0x04) {
                int dst = (op >> 3) & 0x07;
                std::uint8_t before = read_reg8(dst);
                std::uint8_t after  = static_cast<std::uint8_t>(before + 1);
                write_reg8(dst, after);
                flags_inc(before, after);
                tstates = (dst == 6) ? 11 : 4;
                break;
            }

            // ---- DEC r --------------------------------------------------
            if ((op & 0xC7) == 0x05) {
                int dst = (op >> 3) & 0x07;
                std::uint8_t before = read_reg8(dst);
                std::uint8_t after  = static_cast<std::uint8_t>(before - 1);
                write_reg8(dst, after);
                flags_dec(before, after);
                tstates = (dst == 6) ? 11 : 4;
                break;
            }

            // ---- Opcode no implementado ---------------------------------
            std::fprintf(stderr,
                         "Opcode no implementado: %02X en PC=%04X\n",
                         op, static_cast<unsigned>(regs_.pc - 1));
            regs_.halted = true;
            tstates = 4;
            break;
        }
    }

    cycles_ += tstates;
    return tstates;
}

// ---------------------------------------------------------------------------
// read_reg16(idx): devuelve el par de 16 bits indicado.
// Convención: 0=BC  1=DE  2=HL  3=SP
// ---------------------------------------------------------------------------
std::uint16_t Z80::read_reg16(int idx) {
    switch (idx) {
        case 0: return regs_.bc().word;
        case 1: return regs_.de().word;
        case 2: return regs_.hl().word;
        case 3: return regs_.sp;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// write_reg16(idx, v): escribe el par de 16 bits indicado.
// ---------------------------------------------------------------------------
void Z80::write_reg16(int idx, std::uint16_t value) {
    switch (idx) {
        case 0: regs_.set_bc(value); return;
        case 1: regs_.set_de(value); return;
        case 2: regs_.set_hl(value); return;
        case 3: regs_.sp = value;    return;
    }
}

// ---------------------------------------------------------------------------
// push16: apila un valor de 16 bits.
//
// La pila del Z80 crece HACIA ABAJO. Para apilar un valor:
//   1. SP -= 2
//   2. Memoria[SP]   = byte BAJO del valor
//   3. Memoria[SP+1] = byte ALTO del valor
//
// Truco: primero decrementamos SP, luego escribimos little-endian.
// ---------------------------------------------------------------------------
void Z80::push16(std::uint16_t value) {
    regs_.sp--;
    bus_.write(regs_.sp, static_cast<std::uint8_t>(value >> 8));  // byte alto en SP
    regs_.sp--;
    bus_.write(regs_.sp, static_cast<std::uint8_t>(value & 0xFF)); // byte bajo en SP-1
}

// ---------------------------------------------------------------------------
// pop16: desapila un valor de 16 bits (inverso de push16).
//   1. Leer byte BAJO de Memoria[SP]
//   2. Leer byte ALTO de Memoria[SP+1]
//   3. SP += 2
// ---------------------------------------------------------------------------
std::uint16_t Z80::pop16() {
    std::uint8_t lo = bus_.read(regs_.sp);
    regs_.sp++;
    std::uint8_t hi = bus_.read(regs_.sp);
    regs_.sp++;
    return static_cast<std::uint16_t>(lo | (hi << 8));
}

// ---------------------------------------------------------------------------
// flags_add16: flags para ADD HL, rr (suma de 16 bits).
//
// Reglas especiales:
//   - S, Z y P/V NO se tocan (se preservan tal cual estaban).
//   - H, N, C sí se recalculan, sobre 16 bits.
//   - XF e YF se copian de los bits 11 y 13 del resultado (¡raro, pero real!).
// ---------------------------------------------------------------------------
void Z80::flags_add16(std::uint16_t a, std::uint16_t b, std::uint16_t res) {
    std::uint8_t f = regs_.f & (SF | ZF | PF);   // preservar S, Z, P/V

    if (((a & 0x0FFF) + (b & 0x0FFF)) > 0x0FFF) f |= HF;
    // N = 0 (fue suma)
    if ((static_cast<std::uint32_t>(a) + b) > 0xFFFF) f |= CF;
    // XF e YF: copiar bits 11 y 13 del resultado a bits 3 y 5 de F
    if (res & 0x0800) f |= XF;
    if (res & 0x2000) f |= YF;

    regs_.f = f;
}

// ---------------------------------------------------------------------------
// condition(cc): devuelve true si la condición "cc" se cumple.
//
// Codificación Z80 (3 bits):
//   0=NZ (Z=0)  1=Z (Z=1)
//   2=NC (C=0)  3=C (C=1)
//   4=PO (P/V=0)  5=PE (P/V=1)
//   6=P (S=0)     7=M (S=1)
// ---------------------------------------------------------------------------
bool Z80::condition(int cc) const {
    switch (cc) {
        case 0: return !regs_.flag(ZF);
        case 1: return  regs_.flag(ZF);
        case 2: return !regs_.flag(CF);
        case 3: return  regs_.flag(CF);
        case 4: return !regs_.flag(PF);
        case 5: return  regs_.flag(PF);
        case 6: return !regs_.flag(SF);
        case 7: return  regs_.flag(SF);
    }
    return false;
}

// ---------------------------------------------------------------------------
// do_rotate: aplica una rotación o shift al byte dado.
//
// op:  0=RLC  1=RRC  2=RL  3=RR  4=SLA  5=SRA  6=SLL  7=SRL
//
// Devuelve el resultado. Actualiza S, Z, H, P/V, N, C y XF/YF.
// El carry previo solo se usa en RL (op 2) y RR (op 3).
// ---------------------------------------------------------------------------
std::uint8_t Z80::do_rotate(int op, std::uint8_t v) {
    bool old_c = regs_.flag(CF);
    std::uint8_t res = 0;
    bool new_c = false;

    switch (op) {
        case 0: // RLC: bit 7 → C, bit 7 → bit 0
            new_c = (v & 0x80) != 0;
            res = static_cast<std::uint8_t>((v << 1) | (new_c ? 1 : 0));
            break;
        case 1: // RRC: bit 0 → C, bit 0 → bit 7
            new_c = (v & 0x01) != 0;
            res = static_cast<std::uint8_t>((v >> 1) | (new_c ? 0x80 : 0));
            break;
        case 2: // RL: rota a través del carry (bit 7 → C, C → bit 0)
            new_c = (v & 0x80) != 0;
            res = static_cast<std::uint8_t>((v << 1) | (old_c ? 1 : 0));
            break;
        case 3: // RR: rota a través del carry (bit 0 → C, C → bit 7)
            new_c = (v & 0x01) != 0;
            res = static_cast<std::uint8_t>((v >> 1) | (old_c ? 0x80 : 0));
            break;
        case 4: // SLA: shift left, bit 0 = 0
            new_c = (v & 0x80) != 0;
            res = static_cast<std::uint8_t>(v << 1);
            break;
        case 5: // SRA: shift right aritmético, conserva bit 7
            new_c = (v & 0x01) != 0;
            res = static_cast<std::uint8_t>((v >> 1) | (v & 0x80));
            break;
        case 6: // SLL: shift left lógico pero mete un 1 por la derecha (no documentado)
            new_c = (v & 0x80) != 0;
            res = static_cast<std::uint8_t>((v << 1) | 1);
            break;
        case 7: // SRL: shift right lógico, bit 7 = 0
            new_c = (v & 0x01) != 0;
            res = static_cast<std::uint8_t>(v >> 1);
            break;
    }

    // Flags: S, Z, H=0, V=paridad, N=0, C=new_c, XF/YF del resultado
    std::uint8_t f = 0;
    if (res & 0x80)         f |= SF;
    if (res == 0)           f |= ZF;
    if (parity_even(res))   f |= PF;
    if (new_c)              f |= CF;
    f |= res & (XF | YF);
    // H y N quedan a 0
    regs_.f = f;

    return res;
}

// ---------------------------------------------------------------------------
// do_bit: implementa BIT b, v. NO modifica v; solo recalcula flags.
//
// Reglas:
//   Z   = 1 si el bit b de v es 0
//   N   = 0
//   H   = 1
//   S   = 1 si b==7 y bit 7 de v es 1 (solo importa si b==7)
//   P/V = Z
//   C   = no cambia
//   XF/YF = bits 3 y 5 del VALOR ORIGINAL v (no del bit testeado)
// ---------------------------------------------------------------------------
void Z80::do_bit(int bit, std::uint8_t v) {
    bool set = (v & (1 << bit)) != 0;

    std::uint8_t f = regs_.f & CF;      // preservar carry
    f |= HF;                             // H siempre 1
    if (!set) {
        f |= ZF;
        f |= PF;                         // P/V = Z
    }
    if (bit == 7 && set) f |= SF;        // S solo si testeamos el bit 7 y vale 1
    f |= v & (XF | YF);                  // XF/YF del valor original

    regs_.f = f;
}

// ---------------------------------------------------------------------------
// execute_cb: implementa las 256 instrucciones del prefijo CB.
// El byte 0xCB ya fue consumido por step(); aquí leemos el segundo byte.
//
// Estructura:
//   - 0x00..0x3F: rotaciones/shifts. Los 3 bits altos (>>3) dan la operación,
//                 los 3 bits bajos dan el registro (0..7, 6=(HL)).
//   - 0x40..0x7F: BIT b, r. Los 3 bits altos (>>3)&7 dan b, los bajos el reg.
//   - 0x80..0xBF: RES b, r.
//   - 0xC0..0xFF: SET b, r.
// ---------------------------------------------------------------------------
int Z80::execute_cb() {
    std::uint8_t op = fetch_byte();
    int reg = op & 0x07;   // registro destino/fuente (0..7, 6=(HL))

    // --- Rotaciones/shifts: 0x00..0x3F -----------------------------------
    if (op < 0x40) {
        int rot = (op >> 3) & 0x07;
        std::uint8_t v = read_reg8(reg);
        std::uint8_t r = do_rotate(rot, v);
        write_reg8(reg, r);
        return (reg == 6) ? 15 : 8;
    }

    // --- BIT b, r: 0x40..0x7F --------------------------------------------
    if (op < 0x80) {
        int bit = (op >> 3) & 0x07;
        std::uint8_t v = read_reg8(reg);
        do_bit(bit, v);
        return (reg == 6) ? 12 : 8;
    }

    // --- RES b, r: 0x80..0xBF --------------------------------------------
    if (op < 0xC0) {
        int bit = (op >> 3) & 0x07;
        std::uint8_t v = read_reg8(reg);
        v &= static_cast<std::uint8_t>(~(1 << bit));
        write_reg8(reg, v);
        return (reg == 6) ? 15 : 8;
    }

    // --- SET b, r: 0xC0..0xFF --------------------------------------------
    int bit = (op >> 3) & 0x07;
    std::uint8_t v = read_reg8(reg);
    v |= static_cast<std::uint8_t>(1 << bit);
    write_reg8(reg, v);
    return (reg == 6) ? 15 : 8;
}

// ---------------------------------------------------------------------------
// flags_add16_carry: flags para ADC HL, rr (16 bits con carry de entrada).
// Afecta a TODOS los flags, con reglas análogas a las de 8 bits:
//   S  = bit 15 del resultado
//   Z  = (res == 0)
//   H  = acarreo del bit 11 al 12 (usando nibble alto de cada mitad)
//   V  = overflow con signo de 16 bits
//   N  = 0
//   C  = acarreo del bit 15
//   XF = bit 11 del resultado, YF = bit 13 del resultado
// ---------------------------------------------------------------------------
void Z80::flags_add16_carry(std::uint16_t a, std::uint16_t b,
                            std::uint16_t res, bool carry_in) {
    std::uint8_t f = 0;

    if (res & 0x8000) f |= SF;
    if (res == 0)     f |= ZF;
    if (((a & 0x0FFF) + (b & 0x0FFF) + (carry_in ? 1 : 0)) > 0x0FFF) f |= HF;
    if (((a ^ res) & (b ^ res) & 0x8000) != 0) f |= PF;
    // N = 0
    if ((static_cast<std::uint32_t>(a) + b + (carry_in ? 1 : 0)) > 0xFFFF) f |= CF;
    if (res & 0x0800) f |= XF;
    if (res & 0x2000) f |= YF;

    regs_.f = f;
}

// ---------------------------------------------------------------------------
// flags_sub16_carry: flags para SBC HL, rr.
// ---------------------------------------------------------------------------
void Z80::flags_sub16_carry(std::uint16_t a, std::uint16_t b,
                            std::uint16_t res, bool carry_in) {
    std::uint8_t f = 0;

    if (res & 0x8000) f |= SF;
    if (res == 0)     f |= ZF;
    if ((a & 0x0FFF) < ((b & 0x0FFF) + (carry_in ? 1 : 0))) f |= HF;
    if (((a ^ b) & (a ^ res) & 0x8000) != 0) f |= PF;
    f |= NF;
    if (a < static_cast<std::uint32_t>(b) + (carry_in ? 1 : 0)) f |= CF;
    if (res & 0x0800) f |= XF;
    if (res & 0x2000) f |= YF;

    regs_.f = f;
}

// ---------------------------------------------------------------------------
// execute_ed: instrucciones con prefijo ED.
//
// El byte 0xED ya fue consumido; aquí leemos el segundo byte.
// ---------------------------------------------------------------------------
int Z80::execute_ed() {
    std::uint8_t op = fetch_byte();

    // ---- ADC HL, rr  (0x4A BC, 0x5A DE, 0x6A HL, 0x7A SP) --------------
    if (op == 0x4A || op == 0x5A || op == 0x6A || op == 0x7A) {
        int rr = (op >> 4) & 0x03;
        bool c = regs_.flag(CF);
        std::uint16_t a = regs_.hl().word;
        std::uint16_t b = read_reg16(rr);
        std::uint16_t r = static_cast<std::uint16_t>(a + b + (c ? 1 : 0));
        flags_add16_carry(a, b, r, c);
        regs_.set_hl(r);
        return 15;
    }

    // ---- SBC HL, rr  (0x42 BC, 0x52 DE, 0x62 HL, 0x72 SP) --------------
    if (op == 0x42 || op == 0x52 || op == 0x62 || op == 0x72) {
        int rr = (op >> 4) & 0x03;
        bool c = regs_.flag(CF);
        std::uint16_t a = regs_.hl().word;
        std::uint16_t b = read_reg16(rr);
        std::uint16_t r = static_cast<std::uint16_t>(a - b - (c ? 1 : 0));
        flags_sub16_carry(a, b, r, c);
        regs_.set_hl(r);
        return 15;
    }

    // ---- LD (nn), rr  (0x43 BC, 0x53 DE, 0x63 HL, 0x73 SP) -------------
    if (op == 0x43 || op == 0x53 || op == 0x63 || op == 0x73) {
        int rr = (op >> 4) & 0x03;
        std::uint16_t addr = fetch_word();
        std::uint16_t v = read_reg16(rr);
        bus_.write(addr,     static_cast<std::uint8_t>(v & 0xFF));
        bus_.write(addr + 1, static_cast<std::uint8_t>(v >> 8));
        return 20;
    }

    // ---- LD rr, (nn)  (0x4B BC, 0x5B DE, 0x6B HL, 0x7B SP) -------------
    if (op == 0x4B || op == 0x5B || op == 0x6B || op == 0x7B) {
        int rr = (op >> 4) & 0x03;
        std::uint16_t addr = fetch_word();
        std::uint8_t lo = bus_.read(addr);
        std::uint8_t hi = bus_.read(addr + 1);
        write_reg16(rr, static_cast<std::uint16_t>(lo | (hi << 8)));
        return 20;
    }

    // ---- NEG (0x44, 0x4C, 0x54, 0x5C, 0x64, 0x6C, 0x74, 0x7C) ---------
    // El Z80 real acepta los 8 opcodes como el mismo NEG (los no
    // documentados son equivalentes).
    switch (op) {
        case 0x44: case 0x4C: case 0x54: case 0x5C:
        case 0x64: case 0x6C: case 0x74: case 0x7C: {
            std::uint8_t a = regs_.a;
            std::uint8_t r = static_cast<std::uint8_t>(0 - a);
            flags_sub(0, a, r, false);
            regs_.a = r;
            return 8;
        }
    }

    // ---- RETN (0x45, 0x55, 0x65, 0x75) y RETI (0x4D, 0x5D, 0x6D, 0x7D) --
    // Ambos copian IFF2 a IFF1 antes de retornar.
    switch (op) {
        case 0x45: case 0x55: case 0x65: case 0x75:   // RETN
        case 0x4D: case 0x5D: case 0x6D: case 0x7D: { // RETI
            regs_.pc = pop16();
            regs_.iff1 = regs_.iff2;
            return 14;
        }
    }

    // ---- IM 0/1/2 --------------------------------------------------------
    switch (op) {
        case 0x46: case 0x4E: case 0x66: case 0x6E: regs_.im = 0; return 8;
        case 0x56: case 0x76:                       regs_.im = 1; return 8;
        case 0x5E: case 0x7E:                       regs_.im = 2; return 8;
    }

    // ---- LD I, A / LD R, A / LD A, I / LD A, R ---------------------------
    switch (op) {
        case 0x47: regs_.i = regs_.a; return 9;   // LD I, A
        case 0x4F: regs_.r = regs_.a; return 9;   // LD R, A
        case 0x57: {                              // LD A, I
            regs_.a = regs_.i;
            // Flags: S, Z según A; H=0, N=0, C preservado; P/V = IFF2
            std::uint8_t f = regs_.f & CF;
            if (regs_.a & 0x80) f |= SF;
            if (regs_.a == 0)   f |= ZF;
            if (regs_.iff2)     f |= PF;
            regs_.f = f;
            return 9;
        }
        case 0x5F: {                              // LD A, R
            regs_.a = regs_.r;
            std::uint8_t f = regs_.f & CF;
            if (regs_.a & 0x80) f |= SF;
            if (regs_.a == 0)   f |= ZF;
            if (regs_.iff2)     f |= PF;
            regs_.f = f;
            return 9;
        }
    }

    // ---- RRD (0x67) y RLD (0x6F) ----------------------------------------
    if (op == 0x67) {  // RRD: rotar nibble bajo de A con (HL)
        std::uint16_t addr = regs_.hl().word;
        std::uint8_t v = bus_.read(addr);
        std::uint8_t new_a = (regs_.a & 0xF0) | (v & 0x0F);
        std::uint8_t new_m = ((v >> 4) & 0x0F) | ((regs_.a & 0x0F) << 4);
        bus_.write(addr, new_m);
        regs_.a = new_a;
        // Flags: S, Z del nuevo A; H=0, N=0; P/V = paridad de A; C preservado
        std::uint8_t f = regs_.f & CF;
        if (regs_.a & 0x80) f |= SF;
        if (regs_.a == 0)   f |= ZF;
        if (parity_even(regs_.a)) f |= PF;
        regs_.f = f;
        return 18;
    }
    if (op == 0x6F) {  // RLD: rotar nibble alto de A con (HL)
        std::uint16_t addr = regs_.hl().word;
        std::uint8_t v = bus_.read(addr);
        std::uint8_t new_a = (regs_.a & 0xF0) | ((v >> 4) & 0x0F);
        std::uint8_t new_m = ((v << 4) & 0xF0) | (regs_.a & 0x0F);
        bus_.write(addr, new_m);
        regs_.a = new_a;
        std::uint8_t f = regs_.f & CF;
        if (regs_.a & 0x80) f |= SF;
        if (regs_.a == 0)   f |= ZF;
        if (parity_even(regs_.a)) f |= PF;
        regs_.f = f;
        return 18;
    }

    // ---- IN r,(C)  (0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x78) -----------
    // Nota: 0x70 (IN (C)) NO escribe en registro, solo en flags.
    switch (op) {
        case 0x40: case 0x48: case 0x50: case 0x58:
        case 0x60: case 0x68: case 0x78: {
            int reg = (op >> 3) & 0x07;
            std::uint8_t v = bus_.in(regs_.bc().word);
            write_reg8(reg, v);
            std::uint8_t f = regs_.f & CF;
            if (v & 0x80) f |= SF;
            if (v == 0)   f |= ZF;
            if (parity_even(v)) f |= PF;
            regs_.f = f;
            return 12;
        }
        case 0x70: {  // IN (C) — solo flags, sin escritura
            std::uint8_t v = bus_.in(regs_.bc().word);
            std::uint8_t f = regs_.f & CF;
            if (v & 0x80) f |= SF;
            if (v == 0)   f |= ZF;
            if (parity_even(v)) f |= PF;
            regs_.f = f;
            return 12;
        }
    }

    // ---- OUT (C), r  (0x41, 0x49, 0x51, 0x59, 0x61, 0x69, 0x79) ---------
    switch (op) {
        case 0x41: case 0x49: case 0x51: case 0x59:
        case 0x61: case 0x69: case 0x79: {
            int reg = (op >> 3) & 0x07;
            bus_.out(regs_.bc().word, read_reg8(reg));
            return 12;
        }
    }

    // =====================================================================
    // INSTRUCCIONES DE BLOQUE (LDI/LDIR, CPI/CPIR, INI/INIR, OUTI/OTIR)
    // =====================================================================

    // ---- LDI (0xA0) / LDD (0xA8) / LDIR (0xB0) / LDDR (0xB8) -----------
    // Copia (HL) → (DE), incrementa o decrementa HL y DE, decrementa BC.
    // P/V = 1 si BC == 0 tras decrementar, 0 si queda algo.
    switch (op) {
        case 0xA0: case 0xA8: case 0xB0: case 0xB8: {
            bool decrementar = (op == 0xA8 || op == 0xB8);
            bool repetir    = (op == 0xB0 || op == 0xB8);
            int tstates = 0;

            do {
                std::uint8_t v = bus_.read(regs_.hl().word);
                bus_.write(regs_.de().word, v);

                if (decrementar) {
                    regs_.set_hl(static_cast<std::uint16_t>(regs_.hl().word - 1));
                    regs_.set_de(static_cast<std::uint16_t>(regs_.de().word - 1));
                } else {
                    regs_.set_hl(static_cast<std::uint16_t>(regs_.hl().word + 1));
                    regs_.set_de(static_cast<std::uint16_t>(regs_.de().word + 1));
                }
                regs_.set_bc(static_cast<std::uint16_t>(regs_.bc().word - 1));

                // Flags:
                //   H = 0, N = 0, C y S y Z se preservan
                //   P/V = 1 si BC == 0, 0 si BC != 0
                //   XF/YF = bits 3 y 5 del byte movido, sumado al nibble alto
                //           de A (esto es lo que hacen los Z80 reales, pero
                //           muchos emuladores simplifican a solo el byte).
                std::uint8_t f = regs_.f & (SF | ZF | CF);  // preservar S, Z, C
                if (regs_.bc().word == 0) f |= PF;
                f |= v & (XF | YF);
                regs_.f = f;

                tstates += 21;   // por iteración
            } while (repetir && regs_.bc().word != 0);

            // La última iteración de LDIR (cuando BC llega a 0) consume 16
            // en lugar de 21, así que restamos 5 si repetíamos.
            if (repetir && regs_.bc().word == 0) tstates -= 5;

            return tstates;
        }
    }

    // ---- CPI (0xA1) / CPD (0xA9) / CPIR (0xB1) / CPDR (0xB9) ----------
    // Compara A con (HL), incrementa o decrementa HL, decrementa BC.
    // Z = 1 si A == (HL). P/V = 1 si BC == 0 tras decrementar.
    switch (op) {
        case 0xA1: case 0xA9: case 0xB1: case 0xB9: {
            bool decrementar = (op == 0xA9 || op == 0xB9);
            bool repetir    = (op == 0xB1 || op == 0xB9);
            int tstates = 0;

            do {
                std::uint8_t v = bus_.read(regs_.hl().word);
                std::uint8_t a = regs_.a;
                std::uint8_t r = static_cast<std::uint8_t>(a - v);

                // Guardamos los flags de la comparación (S, Z, H, N, XF, YF)
                // antes de decrementar BC, porque flags_sub los sobreescribe.
                flags_sub(a, v, r, false);
                std::uint8_t f = regs_.f & (SF | ZF | HF | NF | XF | YF);

                if (decrementar)
                    regs_.set_hl(static_cast<std::uint16_t>(regs_.hl().word - 1));
                else
                    regs_.set_hl(static_cast<std::uint16_t>(regs_.hl().word + 1));

                regs_.set_bc(static_cast<std::uint16_t>(regs_.bc().word - 1));

                // C siempre se preserva (ya lo está porque no lo tocamos).
                // P/V = 1 si BC == 0.
                if (regs_.bc().word == 0) f |= PF;
                regs_.f = f;

                // CPIR se detiene cuando BC == 0 O cuando Z == 1 (A == (HL)).
                if (!repetir) break;
                if (regs_.bc().word == 0) break;
                if (regs_.f & ZF) break;

                tstates += 21;
            } while (true);

            tstates += 16;   // la última iteración siempre cuesta 16
            return tstates;
        }
    }

    // ---- INI (0xA2) / IND (0xAA) / INIR (0xB2) / INDR (0xBA) ----------
    // Lee un byte del puerto (C) y lo escribe en (HL). Decrementa B.
    // P/V = 1 si B == 0 tras decrementar.
    switch (op) {
        case 0xA2: case 0xAA: case 0xB2: case 0xBA: {
            bool decrementar = (op == 0xAA || op == 0xBA);
            bool repetir    = (op == 0xB2 || op == 0xBA);
            int tstates = 0;

            do {
                std::uint8_t v = bus_.in(regs_.bc().word);
                bus_.write(regs_.hl().word, v);

                if (decrementar)
                    regs_.set_hl(static_cast<std::uint16_t>(regs_.hl().word - 1));
                else
                    regs_.set_hl(static_cast<std::uint16_t>(regs_.hl().word + 1));

                regs_.b--;

                // Flags:
                //   N = bit 7 del byte transferido
                //   P/V = paridad del byte transferido
                //   S, Z, H: indefinidos; los dejamos como estaban (no los tocamos)
                //   C: preservado
                std::uint8_t f = regs_.f & (SF | ZF | HF | CF);
                if (v & 0x80) f |= NF;
                if (parity_even(v)) f |= PF;
                f |= v & (XF | YF);
                regs_.f = f;

                tstates += 21;
            } while (repetir && regs_.b != 0);

            if (repetir && regs_.b == 0) tstates -= 5;

            return tstates;
        }
    }

    // ---- OUTI (0xA3) / OUTD (0xAB) / OTIR (0xB3) / OTDR (0xBB) --------
    // Lee un byte de (HL) y lo escribe en el puerto (C). Decrementa B.
    switch (op) {
        case 0xA3: case 0xAB: case 0xB3: case 0xBB: {
            bool decrementar = (op == 0xAB || op == 0xBB);
            bool repetir    = (op == 0xB3 || op == 0xBB);
            int tstates = 0;

            do {
                std::uint8_t v = bus_.read(regs_.hl().word);
                bus_.out(regs_.bc().word, v);

                if (decrementar)
                    regs_.set_hl(static_cast<std::uint16_t>(regs_.hl().word - 1));
                else
                    regs_.set_hl(static_cast<std::uint16_t>(regs_.hl().word + 1));

                regs_.b--;

                std::uint8_t f = regs_.f & (SF | ZF | HF | CF);
                if (v & 0x80) f |= NF;
                if (parity_even(v)) f |= PF;
                f |= v & (XF | YF);
                regs_.f = f;

                tstates += 21;
            } while (repetir && regs_.b != 0);

            if (repetir && regs_.b == 0) tstates -= 5;

            return tstates;
        }
    }

    // ---- Opcodes ED no implementados o huecos -----------------------------
    // El Z80 real trata los ED sin significado como NOP de 8 T-states.
    // Muchos de estos huecos son instrucciones no documentadas; de momento
    // los tratamos como NOP (salvo los de bloque, que implementaremos
    // en el Paso 6b).
    std::fprintf(stderr,
                 "ED no implementado: ED %02X en PC=%04X\n",
                 op, static_cast<unsigned>(regs_.pc - 2));
    return 8;
}
} // namespace z80