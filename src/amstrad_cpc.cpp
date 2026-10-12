#include "amstrad/amstrad_cpc.h"
#include <cstdio>

namespace amstrad {

// ===========================================================================
// Constructor
// ===========================================================================
AmstradCPC::AmstradCPC() : cpu_(*this) {
    cpu_.reset();
    cpu_.regs().pc = 0x0000;
    cpu_.regs().sp = 0xC000;

    // [Paso 2] Inicialización mínima de los chips a valores razonables.
    // El CRTC arranca con todos sus registros a 0; el AY también.
    // El PPI, sin embargo, arranca con sus puertos en modo "entrada" y
    // los valores de entrada a 0xFF (líneas flotantes pull-up).
    ppi_port_a_ = 0xFF;
    ppi_port_b_ = 0xFF;
    ppi_port_c_ = 0xFF;
}

// ---------------------------------------------------------------------------
// load_roms
// ---------------------------------------------------------------------------
bool AmstradCPC::load_roms(const std::string& lower_path,
                           const std::string& upper_path) {
    auto load_one = [](const std::string& path, std::uint8_t* dst) -> bool {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) {
            std::fprintf(stderr, "No se puede abrir %s\n", path.c_str());
            return false;
        }
        std::size_t n = std::fread(dst, 1, 0x4000, f);
        std::fclose(f);
        if (n != 0x4000) {
            std::fprintf(stderr, "%s tiene %zu bytes, se esperaban %d\n",
                         path.c_str(), n, 0x4000);
            return false;
        }
        return true;
    };

    return load_one(lower_path, rom_lower_.data())
        && load_one(upper_path, rom_upper_.data());
}

// ---------------------------------------------------------------------------
// load_bytes
// ---------------------------------------------------------------------------
void AmstradCPC::load_bytes(std::uint16_t addr,
                            const std::uint8_t* data, std::size_t len) {
    for (std::size_t i = 0; i < len; ++i) {
        std::uint16_t a = static_cast<std::uint16_t>(addr + i);
        if (a < 0x4000) {
            rom_lower_[a] = data[i];
        } else if (a < 0x8000) {
            rom_upper_[a - 0x4000] = data[i];
        } else {
            ram_[a - 0x8000] = data[i];
        }
    }
}

// ---------------------------------------------------------------------------
// run
// ---------------------------------------------------------------------------
std::uint64_t AmstradCPC::run(std::uint64_t n_steps) {
    for (std::uint64_t i = 0; i < n_steps; ++i) {
        cpu_.step();
    }
    return cpu_.cycles();
}

// ===========================================================================
// IBus: memoria (sin cambios respecto al paso 1)
// ===========================================================================
std::uint8_t AmstradCPC::read(std::uint16_t addr) {
    if (addr < 0x4000) {
        return rom_lower_enabled_ ? rom_lower_[addr] : ram_[addr];
    } else if (addr < 0x8000) {
        return rom_upper_enabled_ ? rom_upper_[addr - 0x4000] : ram_[addr - 0x4000];
    } else {
        return ram_[addr - 0x8000];
    }
}

void AmstradCPC::write(std::uint16_t addr, std::uint8_t value) {
    if (addr < 0x8000) {
        if (addr < 0x4000) {
            if (!rom_lower_enabled_) ram_[addr] = value;
        } else {
            if (!rom_upper_enabled_) ram_[addr - 0x4000] = value;
        }
    } else {
        ram_[addr - 0x8000] = value;
    }
}

// ===========================================================================
// [Paso 2] IBus: E/S
//
// Convención: miramos los 8 bits ALTOS del puerto (port & 0xFF00) para
// decidir qué chip está siendo direccionado. Los bits bajos se ignoran
// porque en el CPC los periféricos no los usan para distinguirse.
// ===========================================================================

std::uint8_t AmstradCPC::in(std::uint16_t port) {
    std::uint8_t hi = static_cast<std::uint8_t>(port >> 8);

    switch (hi) {
        // CRTC (0xBCxx selección, 0xBDxx datos)
        case 0xBC:
            // Puerto de selección: en el CPC, la lectura devuelve 0xFF
            // (no es de lectura). Algunos periféricos lo usan, pero el
            // CRTC no responde.
            return 0xFF;

        case 0xBD:
            // Puerto de datos: devolvemos el registro seleccionado.
            return crtc_regs_[crtc_selected_reg_ & 0x1F];

        // AY-3-8912 (0xF4xx datos, 0xF5xx selección)
        case 0xF4:
            // Puerto de datos del AY: devolvemos el registro seleccionado.
            return ay_regs_[ay_selected_reg_ & 0x0F];

        case 0xF5:
            // Puerto de selección: no es de lectura, devolvemos 0xFF.
            return 0xFF;

        // PPI 8255 (0xF6xx puerto A, 0xF7xx puerto B, 0xF8xx puerto C, 0xF9xx control)
        case 0xF6:
            // Puerto A: en el CPC se usa principalmente para el teclado
            // (selección de línea) y lectura de datos de disco.
            // Devolvemos el valor que hayamos guardado (por defecto 0xFF).
            return ppi_port_a_;

        case 0xF7:
            // Puerto B: en el CPC se usa para el sonido, el cassette, etc.
            // Devolvemos el valor guardado.
            return ppi_port_b_;

        case 0xF8:
            // Puerto C: aquí llegan las señales de VSYNC del CRTC, el bit
            // de cassette, etc. Por ahora devolvemos el valor guardado.
            // MÁS ADELANTE: este puerto cambiará dinámicamente según el
            // estado del CRTC.
            return ppi_port_c_;

        case 0xF9:
            // Puerto de control del PPI: no es de lectura, devolvemos 0xFF.
            return 0xFF;

        // FDC (controlador de disco, 0xFFxx): no emulado todavía.
        case 0xFF:
            return 0xFF;

        // Cualquier otro puerto: devolvemos 0xFF (bus flotante).
        default:
            return 0xFF;
    }
}

void AmstradCPC::out(std::uint16_t port, std::uint8_t value) {
    std::uint8_t hi = static_cast<std::uint8_t>(port >> 8);

    switch (hi) {
        // Gate Array (0x7Fxx)
        case 0x7F:
            // Guardamos el último valor escrito. En el CPC real, este valor
            // selecciona qué hacer: cambiar un color, cambiar el modo de
            // vídeo, mapear memoria, escribir al AY, etc.
            // Lo dejamos guardado y lo usaremos cuando implementemos el GA.
            ga_last_value_ = value;
            break;

        // CRTC (0xBCxx selección, 0xBDxx datos)
        case 0xBC:
            // Escribir en el puerto de selección: guardamos el número de
            // registro (solo se usan los 5 bits bajos).
            crtc_selected_reg_ = value & 0x1F;
            break;

        case 0xBD:
            // Escribir en el puerto de datos: guardamos el valor en el
            // registro seleccionado.
            crtc_regs_[crtc_selected_reg_ & 0x1F] = value;
            break;

        // AY-3-8912 (0xF4xx datos, 0xF5xx selección)
        case 0xF4:
            // Escribir en el puerto de datos del AY.
            ay_regs_[ay_selected_reg_ & 0x0F] = value;
            break;

        case 0xF5:
            // Escribir en el puerto de selección: guardamos el número de
            // registro del AY (solo se usan los 4 bits bajos).
            ay_selected_reg_ = value & 0x0F;
            break;

        // PPI 8255 (0xF6xx-0xF9xx)
        case 0xF6:
            ppi_port_a_ = value;
            break;
        case 0xF7:
            ppi_port_b_ = value;
            break;
        case 0xF8:
            ppi_port_c_ = value;
            break;
        case 0xF9:
            ppi_control_ = value;
            break;

        // Otros puertos: ignoramos.
        default:
            break;
    }
}

} // namespace amstrad