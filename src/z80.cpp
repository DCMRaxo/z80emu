#include "z80/z80.h"
#include <cstdio>
#include <utility>   // para std::swap

namespace z80 {

// ===========================================================================
// Constructor y reset
// ===========================================================================

// ---------------------------------------------------------------------------
// Constructor: guarda la referencia al bus y hace un reset inicial.
// ---------------------------------------------------------------------------
Z80::Z80(IBus& bus) : bus_(bus) {
    reset();
}

// ---------------------------------------------------------------------------
// reset(): deja el CPU en un estado conocido, equivalente al power-on.
//
// Valores reales tras un reset en el Z80:
//   PC  = 0x0000
//   SP  = 0xFFFF (aproximado; el manual no lo garantiza pero todo el mundo lo asume)
//   I   = 0, R = 0, IM = 0
//   IFF1 = IFF2 = false
//   AF, BC, DE, HL y alternativos quedan indefinidos; por comodidad los ponemos a 0.
// ---------------------------------------------------------------------------
void Z80::reset() {
    regs_ = Registers{};
    regs_.sp = 0xFFFF;
    regs_.pc = 0x0000;
    regs_.im = 0;
    regs_.iff1 = false;
    regs_.iff2 = false;
    regs_.halted = false;
    cycles_ = 0;
}

// ===========================================================================
// Fetch
// ===========================================================================

// ---------------------------------------------------------------------------
// fetch_byte(): lee el byte en PC y avanza PC en 1.
// ---------------------------------------------------------------------------
std::uint8_t Z80::fetch_byte() {
    return bus_.read(regs_.pc++);
}

// ---------------------------------------------------------------------------
// fetch_word(): lee dos bytes desde PC. El Z80 es little-endian:
// el primer byte leído es el BAJO y el segundo el ALTO.
// ---------------------------------------------------------------------------
std::uint16_t Z80::fetch_word() {
    std::uint8_t lo = fetch_byte();
    std::uint8_t hi = fetch_byte();
    return static_cast<std::uint16_t>(lo | (hi << 8));
}

// ---------------------------------------------------------------------------
// [9a] refresh: incrementa los 7 bits bajos del registro R.
// El bit 7 se preserva (solo se modifica con LD R, A).
// ---------------------------------------------------------------------------
void Z80::refresh() {
    regs_.r = static_cast<std::uint8_t>((regs_.r & 0x80) | ((regs_.r + 1) & 0x7F));
}

// ---------------------------------------------------------------------------
// [9a] fetch_opcode: lee un byte como opcode (o prefijo) e incrementa R.
// ---------------------------------------------------------------------------
std::uint8_t Z80::fetch_opcode() {
    std::uint8_t b = bus_.read(regs_.pc++);
    refresh();
    return b;
}

// ===========================================================================
// [8a] Helpers de selección de registros según IndexMode
//
// Estos helpers son el corazón del soporte para IX/IY. Cada vez que una
// instrucción necesita "el par HL" (o "el registro H", o "el L"), en lugar
// de tocar regs_.h / regs_.l directamente, llama a estos métodos pasando
// el modo activo. Así, la misma instrucción sirve para HL, IX o IY.
//
// En el Paso 8a el modo siempre llega como IndexMode::HL (el step() público
// aún no detecta DD/FD). A partir del 8b podrá ser IX o IY.
// ===========================================================================

// ---------------------------------------------------------------------------
// get_hl / set_hl: leen/escriben el par base (HL, IX o IY).
// ---------------------------------------------------------------------------
std::uint16_t Z80::get_hl(IndexMode m) const {
    switch (m) {
        case IndexMode::HL: return regs_.hl().word;
        case IndexMode::IX: return regs_.ix;
        case IndexMode::IY: return regs_.iy;
    }
    return 0;  // nunca debería llegar aquí
}

void Z80::set_hl(IndexMode m, std::uint16_t v) {
    switch (m) {
        case IndexMode::HL: regs_.set_hl(v); break;
        case IndexMode::IX: regs_.ix = v;     break;
        case IndexMode::IY: regs_.iy = v;     break;
    }
}

// ---------------------------------------------------------------------------
// get_h / get_l: devuelven el byte ALTO o BAJO del par base.
//   Modo HL: H y L.
//   Modo IX: IXH (parte alta de IX) e IXL (parte baja).
//   Modo IY: IYH e IYL.
//
// Nota: IXH/IXL e IYH/IYL NO son registros físicos independientes en el Z80.
// Son los bytes alto y bajo de los registros IX e IY. Es decir, cuando
// escribes en IXH, estás escribiendo el byte alto de IX.
// ---------------------------------------------------------------------------
std::uint8_t Z80::get_h(IndexMode m) const {
    switch (m) {
        case IndexMode::HL: return regs_.h;
        case IndexMode::IX: return static_cast<std::uint8_t>(regs_.ix >> 8);
        case IndexMode::IY: return static_cast<std::uint8_t>(regs_.iy >> 8);
    }
    return 0;
}

std::uint8_t Z80::get_l(IndexMode m) const {
    switch (m) {
        case IndexMode::HL: return regs_.l;
        case IndexMode::IX: return static_cast<std::uint8_t>(regs_.ix & 0xFF);
        case IndexMode::IY: return static_cast<std::uint8_t>(regs_.iy & 0xFF);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// set_h / set_l: escriben el byte ALTO o BAJO del par base.
//
// En modo IX/IY, leer el valor actual, sustituir el byte correspondiente
// y volver a escribir el par completo. Esto es un pequeño coste pero
// mantiene el código simple y correcto.
// ---------------------------------------------------------------------------
void Z80::set_h(IndexMode m, std::uint8_t v) {
    switch (m) {
        case IndexMode::HL:
            regs_.h = v;
            break;
        case IndexMode::IX:
            regs_.ix = static_cast<std::uint16_t>((regs_.ix & 0x00FF) | (v << 8));
            break;
        case IndexMode::IY:
            regs_.iy = static_cast<std::uint16_t>((regs_.iy & 0x00FF) | (v << 8));
            break;
    }
}

void Z80::set_l(IndexMode m, std::uint8_t v) {
    switch (m) {
        case IndexMode::HL:
            regs_.l = v;
            break;
        case IndexMode::IX:
            regs_.ix = static_cast<std::uint16_t>((regs_.ix & 0xFF00) | v);
            break;
        case IndexMode::IY:
            regs_.iy = static_cast<std::uint16_t>((regs_.iy & 0xFF00) | v);
            break;
    }
}

// ---------------------------------------------------------------------------
// get_hl_addr: dirección efectiva del acceso de memoria para (HL) / (IX+d) / (IY+d).
//
//   Modo HL: dirección = HL (ignoramos d, que debe venir siempre a 0).
//   Modo IX: dirección = IX + d (d es un desplazamiento CON SIGNO de 8 bits).
//   Modo IY: dirección = IY + d.
//
// En el Paso 8a esta función todavía no se usa (porque no implementamos
// (IX+d) todavía), pero ya está lista para cuando lleguemos al 8c.
// ---------------------------------------------------------------------------
std::uint16_t Z80::get_hl_addr(IndexMode m, std::int8_t d) const {
    switch (m) {
        case IndexMode::HL: return regs_.hl().word;
        case IndexMode::IX: return static_cast<std::uint16_t>(regs_.ix + d);
        case IndexMode::IY: return static_cast<std::uint16_t>(regs_.iy + d);
    }
    return 0;
}

// ===========================================================================
// [8a] Lectura/escritura de registros de 8 bits por índice (0..7)
//
//   idx:  0=B  1=C  2=D  3=E  4=H  5=L  6=(HL)  7=A
//
// Cambios respecto a versiones anteriores:
//   - Ahora reciben IndexMode.
//   - Para idx = 4 (H) y 5 (L), en modo IX/IY usan IXH/IXL o IYH/IYL.
//   - Para idx = 6 ((HL)), EN EL 8a todavía leen/escriben (HL) siempre,
//     también en modo IX/IY. Esto se arreglará en el 8c, donde leeremos
//     el byte d. En el 8a, como mode siempre es HL, no hay diferencia.
// ===========================================================================
std::uint8_t Z80::read_reg8(IndexMode m, int idx) {
    switch (idx) {
        case 0: return regs_.b;
        case 1: return regs_.c;
        case 2: return regs_.d;
        case 3: return regs_.e;
        case 4: return get_h(m);                                     // [8a] H / IXH / IYH
        case 5: return get_l(m);                                     // [8a] L / IXL / IYL
        case 6: return bus_.read(get_hl_addr(m, ea_offset_));        // [8c] (HL) — en 8a sin d
        case 7: return regs_.a;
    }
    return 0;
}

void Z80::write_reg8(IndexMode m, int idx, std::uint8_t value) {
    switch (idx) {
        case 0: regs_.b = value; return;
        case 1: regs_.c = value; return;
        case 2: regs_.d = value; return;
        case 3: regs_.e = value; return;
        case 4: set_h(m, value); return;                                // [8a] H / IXH / IYH
        case 5: set_l(m, value); return;                                // [8a] L / IXL / IYL
        case 6: bus_.write(get_hl_addr(m, ea_offset_), value); return;  // [8c] (HL) — en 8a sin d
        case 7: regs_.a = value; return;
    }
}

// ===========================================================================
// Lectura/escritura de pares de 16 bits por índice (0=BC 1=DE 2=HL 3=SP)
// ===========================================================================

// ---------------------------------------------------------------------------
// read_reg16 / write_reg16: versión clásica, sin modo.
// Se sigue usando para los índices BC, DE y SP (que nunca se sustituyen).
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

void Z80::write_reg16(int idx, std::uint16_t value) {
    switch (idx) {
        case 0: regs_.set_bc(value); return;
        case 1: regs_.set_de(value); return;
        case 2: regs_.set_hl(value); return;
        case 3: regs_.sp = value;    return;
    }
}

// ---------------------------------------------------------------------------
// [8a] read_reg16_idx / write_reg16_idx: variante que interpreta el índice 2
// (HL) como el par base del modo (HL, IX o IY). Los demás índices son los
// mismos que en la versión clásica.
//
// Se usará en el 8b para instrucciones como LD rr,nn (donde rr puede ser IX),
// ADD HL,rr (que con DD se convierte en ADD IX,rr), PUSH/POP, etc.
// ---------------------------------------------------------------------------
std::uint16_t Z80::read_reg16_idx(IndexMode m, int idx) {
    if (idx == 2) return get_hl(m);
    return read_reg16(idx);
}

void Z80::write_reg16_idx(IndexMode m, int idx, std::uint16_t v) {
    if (idx == 2) { set_hl(m, v); return; }
    write_reg16(idx, v);
}

// ===========================================================================
// Pila
// ===========================================================================

// ---------------------------------------------------------------------------
// push16: apila un valor de 16 bits.
//
// La pila del Z80 crece HACIA ABAJO. Para apilar un valor:
//   1. SP -= 1; escribir el byte ALTO del valor.
//   2. SP -= 1; escribir el byte BAJO del valor.
//
// En memoria queda: [SP] = byte bajo, [SP+1] = byte alto (little-endian).
// ---------------------------------------------------------------------------
void Z80::push16(std::uint16_t value) {
    regs_.sp--;
    bus_.write(regs_.sp, static_cast<std::uint8_t>(value >> 8));
    regs_.sp--;
    bus_.write(regs_.sp, static_cast<std::uint8_t>(value & 0xFF));
}

// ---------------------------------------------------------------------------
// pop16: desapila un valor de 16 bits (inverso de push16).
// ---------------------------------------------------------------------------
std::uint16_t Z80::pop16() {
    std::uint8_t lo = bus_.read(regs_.sp);
    regs_.sp++;
    std::uint8_t hi = bus_.read(regs_.sp);
    regs_.sp++;
    return static_cast<std::uint16_t>(lo | (hi << 8));
}

// ===========================================================================
// Cálculo de flags
// ===========================================================================

// ---------------------------------------------------------------------------
// parity_even(v): true si v tiene un número PAR de bits a 1.
// ---------------------------------------------------------------------------
bool Z80::parity_even(std::uint8_t v) {
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (v & 1) == 0;
}

// ---------------------------------------------------------------------------
// flags_add: flags tras una suma de 8 bits (ADD o ADC).
//
//   a, b: operandos
//   res:  resultado de 8 bits
//   carry_in: carry de entrada (false en ADD, CF en ADC)
// ---------------------------------------------------------------------------
void Z80::flags_add(std::uint8_t a, std::uint8_t b,
                    std::uint8_t res, bool carry_in) {
    std::uint8_t f = 0;
    if (res & 0x80) f |= SF;
    if (res == 0)   f |= ZF;
    if (((a & 0x0F) + (b & 0x0F) + (carry_in ? 1 : 0)) > 0x0F) f |= HF;
    if (((a ^ res) & (b ^ res) & 0x80) != 0) f |= PF;
    if ((static_cast<int>(a) + b + (carry_in ? 1 : 0)) > 0xFF) f |= CF;
    f |= res & (XF | YF);
    regs_.f = f;
}

// ---------------------------------------------------------------------------
// flags_sub: flags tras una resta de 8 bits (SUB, SBC o CP).
// ---------------------------------------------------------------------------
void Z80::flags_sub(std::uint8_t a, std::uint8_t b,
                    std::uint8_t res, bool carry_in) {
    std::uint8_t f = 0;
    if (res & 0x80) f |= SF;
    if (res == 0)   f |= ZF;
    if ((a & 0x0F) < ((b & 0x0F) + (carry_in ? 1 : 0))) f |= HF;
    if (((a ^ b) & (a ^ res) & 0x80) != 0) f |= PF;
    f |= NF;
    if (a < static_cast<int>(b) + (carry_in ? 1 : 0)) f |= CF;
    f |= res & (XF | YF);
    regs_.f = f;
}

// ---------------------------------------------------------------------------
// flags_logical: flags para AND / OR / XOR.
// ---------------------------------------------------------------------------
void Z80::flags_logical(std::uint8_t res) {
    std::uint8_t f = 0;
    if (res & 0x80)        f |= SF;
    if (res == 0)          f |= ZF;
    if (parity_even(res))  f |= PF;
    f |= res & (XF | YF);
    regs_.f = f;
}

// ---------------------------------------------------------------------------
// flags_inc: flags para INC r (NO toca el carry).
// ---------------------------------------------------------------------------
void Z80::flags_inc(std::uint8_t before, std::uint8_t after) {
    std::uint8_t f = regs_.f & CF;
    if (after & 0x80)            f |= SF;
    if (after == 0)              f |= ZF;
    if ((before & 0x0F) == 0x0F) f |= HF;
    if (before == 0x7F)          f |= PF;
    f |= after & (XF | YF);
    regs_.f = f;
}

// ---------------------------------------------------------------------------
// flags_dec: flags para DEC r (NO toca el carry).
// ---------------------------------------------------------------------------
void Z80::flags_dec(std::uint8_t before, std::uint8_t after) {
    std::uint8_t f = regs_.f & CF;
    if (after & 0x80)            f |= SF;
    if (after == 0)              f |= ZF;
    if ((before & 0x0F) == 0x00) f |= HF;
    if (before == 0x80)          f |= PF;
    f |= NF;
    f |= after & (XF | YF);
    regs_.f = f;
}

// ---------------------------------------------------------------------------
// flags_add16: flags para ADD HL, rr.
// S, Z y P/V se preservan. H, N y C se recalculan sobre 16 bits.
// XF e YF se copian de los bits 11 y 13 del resultado (¡no de los bits 3 y 5!).
// ---------------------------------------------------------------------------
void Z80::flags_add16(std::uint16_t a, std::uint16_t b, std::uint16_t res) {
    std::uint8_t f = regs_.f & (SF | ZF | PF);
    if (((a & 0x0FFF) + (b & 0x0FFF)) > 0x0FFF) f |= HF;
    if ((static_cast<std::uint32_t>(a) + b) > 0xFFFF) f |= CF;
    if (res & 0x0800) f |= XF;
    if (res & 0x2000) f |= YF;
    regs_.f = f;
}

// ---------------------------------------------------------------------------
// flags_add16_carry: flags para ADC HL, rr (16 bits con carry).
// Afecta a TODOS los flags.
// ---------------------------------------------------------------------------
void Z80::flags_add16_carry(std::uint16_t a, std::uint16_t b,
                            std::uint16_t res, bool carry_in) {
    std::uint8_t f = 0;
    if (res & 0x8000) f |= SF;
    if (res == 0)     f |= ZF;
    if (((a & 0x0FFF) + (b & 0x0FFF) + (carry_in ? 1 : 0)) > 0x0FFF) f |= HF;
    if (((a ^ res) & (b ^ res) & 0x8000) != 0) f |= PF;
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
// condition(cc): evalúa la condición cc:
//   0=NZ 1=Z 2=NC 3=C 4=PO 5=PE 6=P 7=M
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

// ===========================================================================
// Prefijo CB
// ===========================================================================

// ---------------------------------------------------------------------------
// do_rotate: aplica rotación/shift al byte dado.
//   op: 0=RLC 1=RRC 2=RL 3=RR 4=SLA 5=SRA 6=SLL 7=SRL
// Actualiza S, Z, H=0, P/V=paridad, N=0, C=bit saliente, XF/YF del resultado.
// ---------------------------------------------------------------------------
std::uint8_t Z80::do_rotate(int op, std::uint8_t v) {
    bool old_c = regs_.flag(CF);
    std::uint8_t res = 0;
    bool new_c = false;

    switch (op) {
        case 0: new_c = (v & 0x80) != 0; res = static_cast<std::uint8_t>((v << 1) | (new_c ? 1 : 0)); break;
        case 1: new_c = (v & 0x01) != 0; res = static_cast<std::uint8_t>((v >> 1) | (new_c ? 0x80 : 0)); break;
        case 2: new_c = (v & 0x80) != 0; res = static_cast<std::uint8_t>((v << 1) | (old_c ? 1 : 0)); break;
        case 3: new_c = (v & 0x01) != 0; res = static_cast<std::uint8_t>((v >> 1) | (old_c ? 0x80 : 0)); break;
        case 4: new_c = (v & 0x80) != 0; res = static_cast<std::uint8_t>(v << 1); break;
        case 5: new_c = (v & 0x01) != 0; res = static_cast<std::uint8_t>((v >> 1) | (v & 0x80)); break;
        case 6: new_c = (v & 0x80) != 0; res = static_cast<std::uint8_t>((v << 1) | 1); break;
        case 7: new_c = (v & 0x01) != 0; res = static_cast<std::uint8_t>(v >> 1); break;
    }

    std::uint8_t f = 0;
    if (res & 0x80)       f |= SF;
    if (res == 0)         f |= ZF;
    if (parity_even(res)) f |= PF;
    if (new_c)            f |= CF;
    f |= res & (XF | YF);
    regs_.f = f;

    return res;
}

// ---------------------------------------------------------------------------
// do_bit: BIT b, v. No modifica v, solo flags.
// ---------------------------------------------------------------------------
void Z80::do_bit(int bit, std::uint8_t v) {
    bool set = (v & (1 << bit)) != 0;
    std::uint8_t f = regs_.f & CF;
    f |= HF;
    if (!set) {
        f |= ZF;
        f |= PF;
    }
    if (bit == 7 && set) f |= SF;
    f |= v & (XF | YF);
    regs_.f = f;
}

// ---------------------------------------------------------------------------
// [8a] execute_cb: ahora recibe IndexMode.
//
// En el Paso 8a, mode siempre es HL (lo garantiza el step público, que
// todavía no detecta DD/FD). En el 8b/8c añadiremos la parte que lee el
// byte d para las formas DD CB d op, y usará read_reg8(mode, reg) que
// ya es consciente del modo.
// ---------------------------------------------------------------------------
int Z80::execute_cb(IndexMode mode) {
    std::uint8_t op = fetch_opcode();
    return execute_cb_prefetched(mode, op);
}

// ===========================================================================
// [8d] execute_cb_prefetched: implementación real de las instrucciones CB.
//
// Recibe el opcode ya leído (porque en el caso DD CB d op el byte d va
// antes y tenemos que haberlo consumido para saber si el prefijo aplica).
//
// El modo indica qué par usar para (HL): HL, IX o IY.
// El desplazamiento d ya está en ea_offset_ (lo pone el que llama).
// ===========================================================================
int Z80::execute_cb_prefetched(IndexMode mode, std::uint8_t op) {
    int reg = op & 0x07;

    // Rotaciones/shifts: 0x00..0x3F
    if (op < 0x40) {
        int rot = (op >> 3) & 0x07;
        std::uint8_t v = read_reg8(mode, reg);
        std::uint8_t r = do_rotate(rot, v);
        write_reg8(mode, reg, r);
        return (reg == 6) ? 15 : 8;
    }

    // BIT b, r: 0x40..0x7F
    if (op < 0x80) {
        int bit = (op >> 3) & 0x07;
        std::uint8_t v = read_reg8(mode, reg);
        do_bit(bit, v);
        return (reg == 6) ? 12 : 8;
    }

    // RES b, r: 0x80..0xBF
    if (op < 0xC0) {
        int bit = (op >> 3) & 0x07;
        std::uint8_t v = read_reg8(mode, reg);
        v &= static_cast<std::uint8_t>(~(1 << bit));
        write_reg8(mode, reg, v);
        return (reg == 6) ? 15 : 8;
    }

    // SET b, r: 0xC0..0xFF
    int bit = (op >> 3) & 0x07;
    std::uint8_t v = read_reg8(mode, reg);
    v |= static_cast<std::uint8_t>(1 << bit);
    write_reg8(mode, reg, v);
    return (reg == 6) ? 15 : 8;
}

// ===========================================================================
// Prefijo ED
// ===========================================================================

// ---------------------------------------------------------------------------
// [8a] execute_ed: ahora recibe IndexMode.
//
// En el Z80 real, DD/FD no afectan a las instrucciones ED (no existen
// versiones con IX/IY de, por ejemplo, LDIR). Pero el prefijo DD sí se
// consume; es decir, "DD ED xx" ejecuta ED xx normalmente. Por eso
// recibimos el modo aunque no lo usemos: para dejar claro que la firma
// es coherente y para que el lector sepa que no se nos ha olvidado.
// ---------------------------------------------------------------------------
int Z80::execute_ed(IndexMode /*mode*/) {
    std::uint8_t op = fetch_opcode();

    // ---- ADC HL, rr ----
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

    // ---- SBC HL, rr ----
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

    // ---- LD (nn), rr ----
    if (op == 0x43 || op == 0x53 || op == 0x63 || op == 0x73) {
        int rr = (op >> 4) & 0x03;
        std::uint16_t addr = fetch_word();
        std::uint16_t v = read_reg16(rr);
        bus_.write(addr,     static_cast<std::uint8_t>(v & 0xFF));
        bus_.write(addr + 1, static_cast<std::uint8_t>(v >> 8));
        return 20;
    }

    // ---- LD rr, (nn) ----
    if (op == 0x4B || op == 0x5B || op == 0x6B || op == 0x7B) {
        int rr = (op >> 4) & 0x03;
        std::uint16_t addr = fetch_word();
        std::uint8_t lo = bus_.read(addr);
        std::uint8_t hi = bus_.read(addr + 1);
        write_reg16(rr, static_cast<std::uint16_t>(lo | (hi << 8)));
        return 20;
    }

    // ---- NEG (y variantes no documentadas) ----
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

    // ---- RETN / RETI ----
    switch (op) {
        case 0x45: case 0x55: case 0x65: case 0x75:
        case 0x4D: case 0x5D: case 0x6D: case 0x7D: {
            regs_.pc = pop16();
            regs_.iff1 = regs_.iff2;
            return 14;
        }
    }

    // ---- IM 0/1/2 ----
    switch (op) {
        case 0x46: case 0x4E: case 0x66: case 0x6E: regs_.im = 0; return 8;
        case 0x56: case 0x76:                       regs_.im = 1; return 8;
        case 0x5E: case 0x7E:                       regs_.im = 2; return 8;
    }

    // ---- LD I, A / LD R, A / LD A, I / LD A, R ----
    switch (op) {
        case 0x47: regs_.i = regs_.a; return 9;
        case 0x4F: regs_.r = regs_.a; return 9;
        case 0x57: {
            regs_.a = regs_.i;
            std::uint8_t f = regs_.f & CF;
            if (regs_.a & 0x80) f |= SF;
            if (regs_.a == 0)   f |= ZF;
            if (regs_.iff2)     f |= PF;
            regs_.f = f;
            return 9;
        }
        case 0x5F: {
            regs_.a = regs_.r;
            std::uint8_t f = regs_.f & CF;
            if (regs_.a & 0x80) f |= SF;
            if (regs_.a == 0)   f |= ZF;
            if (regs_.iff2)     f |= PF;
            regs_.f = f;
            return 9;
        }
    }

    // ---- RRD / RLD ----
    if (op == 0x67) {
        std::uint16_t addr = regs_.hl().word;
        std::uint8_t v = bus_.read(addr);
        std::uint8_t new_a = static_cast<std::uint8_t>((regs_.a & 0xF0) | (v & 0x0F));
        std::uint8_t new_m = static_cast<std::uint8_t>(((v >> 4) & 0x0F) | ((regs_.a & 0x0F) << 4));
        bus_.write(addr, new_m);
        regs_.a = new_a;
        std::uint8_t f = regs_.f & CF;
        if (regs_.a & 0x80) f |= SF;
        if (regs_.a == 0)   f |= ZF;
        if (parity_even(regs_.a)) f |= PF;
        regs_.f = f;
        return 18;
    }
    if (op == 0x6F) {
        std::uint16_t addr = regs_.hl().word;
        std::uint8_t v = bus_.read(addr);
        std::uint8_t new_a = static_cast<std::uint8_t>((regs_.a & 0xF0) | ((v >> 4) & 0x0F));
        std::uint8_t new_m = static_cast<std::uint8_t>(((v << 4) & 0xF0) | (regs_.a & 0x0F));
        bus_.write(addr, new_m);
        regs_.a = new_a;
        std::uint8_t f = regs_.f & CF;
        if (regs_.a & 0x80) f |= SF;
        if (regs_.a == 0)   f |= ZF;
        if (parity_even(regs_.a)) f |= PF;
        regs_.f = f;
        return 18;
    }

    // ---- IN r,(C) ----
    switch (op) {
        case 0x40: case 0x48: case 0x50: case 0x58:
        case 0x60: case 0x68: case 0x78: {
            int reg = (op >> 3) & 0x07;
            std::uint8_t v = bus_.in(regs_.bc().word);
            // En modo HL, read/write_reg8 no cambia de comportamiento.
            write_reg8(IndexMode::HL, reg, v);
            std::uint8_t f = regs_.f & CF;
            if (v & 0x80) f |= SF;
            if (v == 0)   f |= ZF;
            if (parity_even(v)) f |= PF;
            regs_.f = f;
            return 12;
        }
        case 0x70: {
            std::uint8_t v = bus_.in(regs_.bc().word);
            std::uint8_t f = regs_.f & CF;
            if (v & 0x80) f |= SF;
            if (v == 0)   f |= ZF;
            if (parity_even(v)) f |= PF;
            regs_.f = f;
            return 12;
        }
    }

    // ---- OUT (C), r ----
    switch (op) {
        case 0x41: case 0x49: case 0x51: case 0x59:
        case 0x61: case 0x69: case 0x79: {
            int reg = (op >> 3) & 0x07;
            bus_.out(regs_.bc().word, read_reg8(IndexMode::HL, reg));
            return 12;
        }
    }

    // ---- Instrucciones de bloque ----

    // LDI / LDD / LDIR / LDDR
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
                std::uint8_t f = regs_.f & (SF | ZF | CF);
                if (regs_.bc().word == 0) f |= PF;
                f |= v & (XF | YF);
                regs_.f = f;
                tstates += 21;
            } while (repetir && regs_.bc().word != 0);
            if (repetir && regs_.bc().word == 0) tstates -= 5;
            return tstates;
        }
    }

    // CPI / CPD / CPIR / CPDR
    switch (op) {
        case 0xA1: case 0xA9: case 0xB1: case 0xB9: {
            bool decrementar = (op == 0xA9 || op == 0xB9);
            bool repetir    = (op == 0xB1 || op == 0xB9);
            int tstates = 0;
            do {
                std::uint8_t v = bus_.read(regs_.hl().word);
                std::uint8_t a = regs_.a;
                std::uint8_t r = static_cast<std::uint8_t>(a - v);
                flags_sub(a, v, r, false);
                std::uint8_t f = regs_.f & (SF | ZF | HF | NF | XF | YF);
                if (decrementar)
                    regs_.set_hl(static_cast<std::uint16_t>(regs_.hl().word - 1));
                else
                    regs_.set_hl(static_cast<std::uint16_t>(regs_.hl().word + 1));
                regs_.set_bc(static_cast<std::uint16_t>(regs_.bc().word - 1));
                if (regs_.bc().word == 0) f |= PF;
                regs_.f = f;
                if (!repetir) break;
                if (regs_.bc().word == 0) break;
                if (regs_.f & ZF) break;
                tstates += 21;
            } while (true);
            tstates += 16;
            return tstates;
        }
    }

    // INI / IND / INIR / INDR
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

    // OUTI / OUTD / OTIR / OTDR
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

    // ED no implementado
    std::fprintf(stderr,
                 "ED no implementado: ED %02X en PC=%04X\n",
                 op, static_cast<unsigned>(regs_.pc - 2));
    return 8;
}

// ===========================================================================
// [8a] execute_with_mode: el corazón del decodificador.
//
// Contiene el mismo switch que antes tenía step(), pero:
//   - Recibe el opcode ya leído (no lo lee él).
//   - Recibe el IndexMode.
//   - Usa los helpers get_hl/set_hl/read_reg8(mode, ...) en lugar de
//     manipular regs_.h/regs_.l/regs_.hl() directamente.
//
// Devuelve los T-states consumidos. La suma a cycles_ la hace el que llama.
// ===========================================================================

// ===========================================================================
// [8c] opcode_uses_hl_indirect: ¿este opcode usa (HL)?
//
// Devuelve true si, tras aplicar un prefijo DD/FD, la instrucción debe leer
// el byte d y por tanto opera sobre (IX+d) o (IY+d). Se usa al principio de
// execute_with_mode para decidir si hay que hacer fetch de ese byte.
// ===========================================================================
bool Z80::opcode_uses_hl_indirect(std::uint8_t op) const {
    // LD r, (HL)  y  LD (HL), r  —  0x40..0x7F con algún índice 6,
    // excluyendo 0x76 (HALT).
    if (op >= 0x40 && op <= 0x7F && op != 0x76) {
        int dst = (op >> 3) & 0x07;
        int src =  op       & 0x07;
        if (dst == 6 || src == 6) return true;
    }

    // LD (HL), n  (0x36)
    if (op == 0x36) return true;

    // ALU con (HL): 0x80..0xBF con índice 6 en los bits bajos.
    if (op >= 0x80 && op <= 0xBF && (op & 0x07) == 6) return true;

    // INC (HL) y DEC (HL)
    if (op == 0x34 || op == 0x35) return true;

    return false;
}

int Z80::execute_with_mode(IndexMode mode, std::uint8_t op) {
    int tstates = 4;

    // [8c] Si estamos en modo IX/IY y el opcode usa (HL), leemos el
    // desplazamiento d justo después del opcode. Lo guardamos en
    // ea_offset_ para que read_reg8/write_reg8 lo usen al vuelo.
    // En modo HL siempre es 0 y da igual.
    ea_offset_ = 0;
    if (mode != IndexMode::HL && opcode_uses_hl_indirect(op)) {
        ea_offset_ = static_cast<std::int8_t>(fetch_byte());
    }

    switch (op) {

        // =====================================================================
        // Instrucciones sueltas
        // =====================================================================

        case 0x00: break;   // NOP

        case 0x76:          // HALT
            regs_.halted = true;
            break;

        case 0xF3:  // DI — deshabilita interrupciones
            regs_.iff1 = false;
            regs_.iff2 = false;
            ei_pending_ = false;   // [9b] cancelar cualquier EI pendiente
            break;

        case 0xFB:  // EI — habilita interrupciones
            //regs_.iff1 = true;
            //regs_.iff2 = true;
            ei_pending_ = true;    // [9b] se aplicará al final del próximo step()
            break;

        case 0x08:          // EX AF, AF'
            std::swap(regs_.a, regs_.a_);
            std::swap(regs_.f, regs_.f_);
            break;

        case 0xD9:          // EXX
            std::swap(regs_.b, regs_.b_);
            std::swap(regs_.c, regs_.c_);
            std::swap(regs_.d, regs_.d_);
            std::swap(regs_.e, regs_.e_);
            std::swap(regs_.h, regs_.h_);
            std::swap(regs_.l, regs_.l_);
            break;

            case 0xEB:      // EX DE, HL
            // NOTA IMPORTANTE: aunque venga con prefijo DD o FD, esta
            // instrucción intercambia SIEMPRE DE con HL, nunca con IX/IY.
            // Por eso usamos regs_.h / regs_.l directamente y NO
            // get_h(mode) / get_l(mode). Es la única excepción real del Z80
            // a la regla "DD/FD sustituyen HL por IX/IY".
            std::swap(regs_.d, regs_.h);
            std::swap(regs_.e, regs_.l);
            break;

        case 0xE3: {        // EX (SP), HL  →  con DD sería EX (SP), IX
            std::uint8_t lo = bus_.read(regs_.sp);
            std::uint8_t hi = bus_.read(static_cast<std::uint16_t>(regs_.sp + 1));
            bus_.write(regs_.sp, get_l(mode));
            bus_.write(static_cast<std::uint16_t>(regs_.sp + 1), get_h(mode));
            set_h(mode, hi);
            set_l(mode, lo);
            tstates = 19;
            break;
        }

        case 0x2F: {        // CPL
            regs_.a = static_cast<std::uint8_t>(~regs_.a);
            std::uint8_t f = regs_.f & (SF | ZF | PF | CF);
            f |= HF | NF;
            f |= regs_.a & (XF | YF);
            regs_.f = f;
            break;
        }

        case 0x37: {        // SCF
            std::uint8_t f = regs_.f & (SF | ZF | PF);
            f |= CF;
            f |= regs_.a & (XF | YF);
            regs_.f = f;
            break;
        }

        case 0x3F: {        // CCF
            bool old_c = regs_.flag(CF);
            std::uint8_t f = regs_.f & (SF | ZF | PF);
            if (old_c)  f |= HF;
            if (!old_c) f |= CF;
            f |= regs_.a & (XF | YF);
            regs_.f = f;
            break;
        }

        case 0x07: {  // RLCA: rota A a la izquierda circular, solo toca C.
            std::uint8_t a = regs_.a;
            bool c = (a & 0x80) != 0;
            regs_.a = static_cast<std::uint8_t>((a << 1) | (c ? 1 : 0));
            // Solo se modifica C, H=0, N=0. S, Z y P/V se preservan.
            std::uint8_t f = regs_.f & (SF | ZF | PF);
            if (c) f |= CF;
            f |= regs_.a & (XF | YF);
            regs_.f = f;
            break;
        }

        case 0x0F: {  // RRCA: rota A a la derecha circular, solo toca C.
            std::uint8_t a = regs_.a;
            bool c = (a & 0x01) != 0;
            regs_.a = static_cast<std::uint8_t>((a >> 1) | (c ? 0x80 : 0));
            std::uint8_t f = regs_.f & (SF | ZF | PF);
            if (c) f |= CF;
            f |= regs_.a & (XF | YF);
            regs_.f = f;
            break;
        }

        case 0x17: {  // RLA: rota A a la izquierda a través del carry.
            std::uint8_t a = regs_.a;
            bool old_c = regs_.flag(CF);
            bool new_c = (a & 0x80) != 0;
            regs_.a = static_cast<std::uint8_t>((a << 1) | (old_c ? 1 : 0));
            std::uint8_t f = regs_.f & (SF | ZF | PF);
            if (new_c) f |= CF;
            f |= regs_.a & (XF | YF);
            regs_.f = f;
            break;
        }

        case 0x1F: {  // RRA: rota A a la derecha a través del carry.
            std::uint8_t a = regs_.a;
            bool old_c = regs_.flag(CF);
            bool new_c = (a & 0x01) != 0;
            regs_.a = static_cast<std::uint8_t>((a >> 1) | (old_c ? 0x80 : 0));
            std::uint8_t f = regs_.f & (SF | ZF | PF);
            if (new_c) f |= CF;
            f |= regs_.a & (XF | YF);
            regs_.f = f;
            break;
        }

        case 0x27: {        // DAA
            std::uint8_t a = regs_.a;
            bool n = regs_.flag(NF);
            bool h = regs_.flag(HF);
            bool c = regs_.flag(CF);
            std::uint8_t diff = 0;
            if ((a & 0x0F) > 9 || h) diff |= 0x06;
            if (a > 0x99 || c)       diff |= 0x60;
            std::uint8_t res;
            bool new_c;
            if (n) {
                res = static_cast<std::uint8_t>(a - diff);
                new_c = c && (a >= 0x60);
            } else {
                res = static_cast<std::uint8_t>(a + diff);
                new_c = c || (a > 0x99);
            }
            regs_.a = res;
            std::uint8_t f = 0;
            if (res & 0x80)       f |= SF;
            if (res == 0)         f |= ZF;
            if (parity_even(res)) f |= PF;
            if (new_c)            f |= CF;
            if (n)                f |= NF;
            f |= res & (XF | YF);
            regs_.f = f;
            break;
        }

        // =====================================================================
        // 16 bits sin modo especial (BC, DE, SP)
        // =====================================================================

        case 0x01: case 0x11: case 0x21: case 0x31: {  // LD rr, nn
            // rr: 0=BC 1=DE 2=HL/IX/IY 3=SP
            int rr = (op >> 4) & 0x03;
            std::uint16_t nn = fetch_word();
            // [8a] Usamos write_reg16_idx: con mode=IX/IY, rr=2 escribe en IX/IY.
            write_reg16_idx(mode, rr, nn);
            tstates = 10;
            break;
        }

        case 0x03: case 0x13: case 0x23: case 0x33: {  // INC rr
            int rr = (op >> 4) & 0x03;
            write_reg16_idx(mode, rr,
                static_cast<std::uint16_t>(read_reg16_idx(mode, rr) + 1));
            tstates = 6;
            break;
        }

        case 0x0B: case 0x1B: case 0x2B: case 0x3B: {  // DEC rr
            int rr = (op >> 4) & 0x03;
            write_reg16_idx(mode, rr,
                static_cast<std::uint16_t>(read_reg16_idx(mode, rr) - 1));
            tstates = 6;
            break;
        }

        case 0x09: case 0x19: case 0x29: case 0x39: {  // ADD HL, rr
            // En modo IX/IY: ADD IX, rr o ADD IY, rr
            int rr = (op >> 4) & 0x03;
            std::uint16_t a = get_hl(mode);
            std::uint16_t b = read_reg16_idx(mode, rr);
            std::uint16_t r = static_cast<std::uint16_t>(a + b);
            flags_add16(a, b, r);
            set_hl(mode, r);
            tstates = 11;
            break;
        }

        // PUSH/POP: en modo IX/IY, rr=2 es IX/IY
        case 0xC5: case 0xD5: case 0xE5: case 0xF5: {  // PUSH rr
            int idx = (op >> 4) & 0x03;
            std::uint16_t v = (idx == 3) ? regs_.af().word : read_reg16_idx(mode, idx);
            push16(v);
            tstates = 11;
            break;
        }
        case 0xC1: case 0xD1: case 0xE1: case 0xF1: {  // POP rr
            int idx = (op >> 4) & 0x03;
            std::uint16_t v = pop16();
            if (idx == 3) regs_.set_af(v);
            else          write_reg16_idx(mode, idx, v);
            tstates = 10;
            break;
        }

        // LD (nn), HL  /  LD HL, (nn)  →  con DD/FD serían IX/IY
        case 0x22: {
            std::uint16_t addr = fetch_word();
            std::uint16_t v = get_hl(mode);
            bus_.write(addr,     static_cast<std::uint8_t>(v & 0xFF));
            bus_.write(addr + 1, static_cast<std::uint8_t>(v >> 8));
            tstates = 16;
            break;
        }
        case 0x2A: {
            std::uint16_t addr = fetch_word();
            std::uint8_t lo = bus_.read(addr);
            std::uint8_t hi = bus_.read(addr + 1);
            set_hl(mode, static_cast<std::uint16_t>(lo | (hi << 8)));
            tstates = 16;
            break;
        }

        case 0xF9:  // LD SP, HL  →  con DD/FD: LD SP, IX / LD SP, IY
            regs_.sp = get_hl(mode);
            tstates = 6;
            break;

        case 0x32: {  // LD (nn), A  (no usa HL; DD/FD no la afectan)
            std::uint16_t addr = fetch_word();
            bus_.write(addr, regs_.a);
            tstates = 13;
            break;
        }

        case 0xD3: {  // OUT (n), A
            std::uint8_t n = fetch_byte();
            std::uint16_t port = static_cast<std::uint16_t>((regs_.a << 8) | n);
            bus_.out(port, regs_.a);
            tstates = 11;
            break;
        }

        case 0xDB: {  // IN A, (n)
            std::uint8_t n = fetch_byte();
            std::uint16_t port = static_cast<std::uint16_t>((regs_.a << 8) | n);
            regs_.a = bus_.in(port);
            tstates = 11;
            break;
        }

        case 0x3A: {  // LD A, (nn)  (no usa HL)
            std::uint16_t addr = fetch_word();
            regs_.a = bus_.read(addr);
            tstates = 13;
            break;
        }

        case 0x0A: {  // LD A, (BC)
            std::uint16_t addr = regs_.bc().word;
            regs_.a = bus_.read(addr);
            tstates = 7;
            break;
        }

        case 0x02: {  // LD (BC), A
            std::uint16_t addr = regs_.bc().word;
            bus_.write(addr, regs_.a);
            tstates = 7;
            break;
        }

        case 0x1A: {  // LD A, (DE)
            std::uint16_t addr = regs_.de().word;
            regs_.a = bus_.read(addr);
            tstates = 7;
            break;
        }

        case 0x12: {  // LD (DE), A
            std::uint16_t addr = regs_.de().word;
            bus_.write(addr, regs_.a);
            tstates = 7;
            break;
        }

        // =====================================================================
        // Control de flujo
        // =====================================================================

        case 0xC3:  // JP nn
            regs_.pc = fetch_word();
            tstates = 10;
            break;

        case 0xC2: case 0xCA: case 0xD2: case 0xDA:
        case 0xE2: case 0xEA: case 0xF2: case 0xFA: {  // JP cc, nn
            int cc = (op >> 3) & 0x07;
            std::uint16_t addr = fetch_word();
            if (condition(cc)) regs_.pc = addr;
            tstates = 10;
            break;
        }

        case 0x18: {  // JR e
            std::int8_t offset = static_cast<std::int8_t>(fetch_byte());
            regs_.pc = static_cast<std::uint16_t>(regs_.pc + offset);
            tstates = 12;
            break;
        }

        case 0x20: case 0x28: case 0x30: case 0x38: {  // JR cc, e
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

        case 0x10: {  // DJNZ e
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

        case 0xE9:  // JP (HL)  →  con DD/FD: JP (IX) / JP (IY)
            regs_.pc = get_hl(mode);
            tstates = 4;
            break;

        case 0xCD: {  // CALL nn
            std::uint16_t addr = fetch_word();
            push16(regs_.pc);
            regs_.pc = addr;
            tstates = 17;
            break;
        }

        case 0xC4: case 0xCC: case 0xD4: case 0xDC:
        case 0xE4: case 0xEC: case 0xF4: case 0xFC: {  // CALL cc, nn
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

        case 0xC9:  // RET
            regs_.pc = pop16();
            tstates = 10;
            break;

        case 0xC0: case 0xC8: case 0xD0: case 0xD8:
        case 0xE0: case 0xE8: case 0xF0: case 0xF8: {  // RET cc
            int cc = (op >> 3) & 0x07;
            if (condition(cc)) {
                regs_.pc = pop16();
                tstates = 11;
            } else {
                tstates = 5;
            }
            break;
        }

        case 0xC7: case 0xCF: case 0xD7: case 0xDF:
        case 0xE7: case 0xEF: case 0xF7: case 0xFF: {  // RST n
            std::uint16_t target = (op & 0x38);
            push16(regs_.pc);
            regs_.pc = target;
            tstates = 11;
            break;
        }

        // =====================================================================
        // Prefijos
        // =====================================================================

        case 0xCB: {
            int used = execute_cb(mode);
            tstates = used;
            break;
        }

        case 0xED: {
            int used = execute_ed(mode);
            tstates = used;
            break;
        }

        // =====================================================================
        // ALU de 8 bits (patrones por bits)
        // =====================================================================

        default: {

            // LD r, n
            if ((op & 0xC7) == 0x06) {
                int dst = (op >> 3) & 0x07;
                std::uint8_t n = fetch_byte();
                write_reg8(mode, dst, n);
                tstates = (dst == 6) ? 10 : 7;
                break;
            }

            // LD r, r'
            if ((op & 0xC0) == 0x40) {
                int dst = (op >> 3) & 0x07;
                int src =  op       & 0x07;
                std::uint8_t v = read_reg8(mode, src);
                write_reg8(mode, dst, v);
                tstates = (dst == 6 || src == 6) ? 7 : 4;
                break;
            }

            // ALU con registro
            if ((op & 0xC0) == 0x80) {
                int alu_op = (op >> 3) & 0x07;
                int src    =  op       & 0x07;
                std::uint8_t v = read_reg8(mode, src);
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

            // ALU con inmediato
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

            // INC r
            if ((op & 0xC7) == 0x04) {
                int dst = (op >> 3) & 0x07;
                std::uint8_t before = read_reg8(mode, dst);
                std::uint8_t after  = static_cast<std::uint8_t>(before + 1);
                write_reg8(mode, dst, after);
                flags_inc(before, after);
                tstates = (dst == 6) ? 11 : 4;
                break;
            }

            // DEC r
            if ((op & 0xC7) == 0x05) {
                int dst = (op >> 3) & 0x07;
                std::uint8_t before = read_reg8(mode, dst);
                std::uint8_t after  = static_cast<std::uint8_t>(before - 1);
                write_reg8(mode, dst, after);
                flags_dec(before, after);
                tstates = (dst == 6) ? 11 : 4;
                break;
            }

            // Opcode no implementado
            std::fprintf(stderr,
                         "Opcode no implementado: %02X en PC=%04X\n",
                         op, static_cast<unsigned>(regs_.pc - 1));
            regs_.halted = true;
            tstates = 4;
            break;
        }
    }

    return tstates;
}

// ---------------------------------------------------------------------------
// [9b] non_maskable_interrupt: atiende una NMI.
//
// Comportamiento:
//   - Apila el PC actual.
//   - Guarda IFF1 en IFF2, y limpia IFF1 (deshabilita interrupciones).
//   - Salta a 0x0066.
//   - Sale del estado HALT si estaba halted.
//   - Consume 11 T-states.
// ---------------------------------------------------------------------------
void Z80::non_maskable_interrupt() {
    // Salir de HALT si procede.
    regs_.halted = false;

    // Guardar IFF1 en IFF2 y limpiar IFF1.
    regs_.iff2 = regs_.iff1;
    regs_.iff1 = false;

    // Apilar el PC actual.
    push16(regs_.pc);

    // Saltar al vector de NMI.
    regs_.pc = 0x0066;

    cycles_ += 11;
}

// ---------------------------------------------------------------------------
// [9b] interrupt: atiende una INT enmascarable.
//
// Comportamiento:
//   - Si IFF1 == false, la interrupción no se atiende (el CPU sigue igual).
//   - Si IFF1 == true:
//       - Sale de HALT si estaba halted.
//       - Limpia IFF1 e IFF2 (deshabilita interrupciones futuras).
//       - Apila el PC actual.
//       - Según IM:
//           IM 0: ejecuta el opcode que viene en "data".
//           IM 1: salta a 0x0038.
//           IM 2: lee el vector (I << 8 | data), lee la dirección del
//                 handler desde ahí y salta.
//       - Consume 13 T-states.
// ---------------------------------------------------------------------------
void Z80::interrupt(std::uint8_t data) {
    // Si las interrupciones están deshabilitadas, no se atiende.
    if (!regs_.iff1) {
        return;
    }

    // Salir de HALT.
    regs_.halted = false;

    // Limpiar solo IFF1 (deshabilita interrupciones).
    // IFF2 NO se toca: guarda el estado previo para que RETI/RETN
    // puedan restaurar IFF1 desde IFF2.
    regs_.iff1 = false;

    // Cancelar cualquier EI pendiente: si había un EI en curso, al
    // atender la interrupción ese EI se descarta.
    ei_pending_ = false;

    // Apilar el PC.
    push16(regs_.pc);

    switch (regs_.im) {
        case 0: {
            // IM 0: el dispositivo nos da un opcode.
            // Lo ejecutamos como si fuera una instrucción normal, pero
            // sin incrementar PC antes (el PC que hemos apilado es el
            // actual, y el opcode se ejecutará "sobre" ese PC).
            // La forma más limpia: pasarlo a execute_with_mode, pero
            // como el opcode puede ser un RST n, funciona bien.
            execute_with_mode(IndexMode::HL, data);
            break;
        }
        case 1: {
            // IM 1: salto fijo a 0x0038.
            regs_.pc = 0x0038;
            break;
        }
        case 2: {
            // IM 2: vector = (I << 8) | data.
            std::uint16_t vector_addr = static_cast<std::uint16_t>((regs_.i << 8) | data);
            std::uint8_t lo = bus_.read(vector_addr);
            std::uint8_t hi = bus_.read(static_cast<std::uint16_t>(vector_addr + 1));
            regs_.pc = static_cast<std::uint16_t>(lo | (hi << 8));
            break;
        }
    }

    cycles_ += 13;
}

// ===========================================================================
// [9a] step(): punto de entrada público.
//
// Cambios respecto al 8d:
//   - Si el CPU está halted, consume 4 T-states y sale sin ejecutar nada.
//   - Usa fetch_opcode() en lugar de fetch_byte() para leer los opcodes y
//     prefijos (así se incrementa R correctamente).
//   - Los operandos (inmediatos, direcciones, desplazamientos) siguen
//     leyéndose con fetch_byte().
// ===========================================================================
int Z80::step() {

    // Si está halted: consume 4 T-states y sale.
    if (regs_.halted) {
        refresh();
        cycles_ += 4;
        return 4;
    }

    std::uint8_t op = fetch_opcode();
    IndexMode mode = IndexMode::HL;
    int prefix_cycles = 0;

    while (op == 0xDD || op == 0xFD) {
        mode = (op == 0xDD) ? IndexMode::IX : IndexMode::IY;
        prefix_cycles += 4;
        op = fetch_opcode();
    }

    int tstates;

    if (op == 0xCB && mode != IndexMode::HL) {
        std::int8_t d = static_cast<std::int8_t>(fetch_byte());
        std::uint8_t cb_op = fetch_opcode();
        tstates = prefix_cycles + 4;
        int reg = cb_op & 0x07;
        IndexMode effective_mode = (reg == 6) ? mode : IndexMode::HL;
        ea_offset_ = (effective_mode == IndexMode::HL) ? 0 : d;
        tstates += execute_cb_prefetched(effective_mode, cb_op);
        ea_offset_ = 0;
    } else {
        tstates = prefix_cycles + execute_with_mode(mode, op);
    }

    cycles_ += tstates;

    // [9b] Aplicar EI pendiente: si el step anterior fue EI, ahora se
    // habilitan las interrupciones (una instrucción después).
    if (ei_pending_) {
        regs_.iff1 = true;
        regs_.iff2 = true;
        ei_pending_ = false;
    }

    return tstates;
}
} // namespace z80
