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

    // PPI: puertos de entrada con líneas pull-up (0xFF).
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

// ===========================================================================
// [Paso 3] tick: avanza el contador de T-states interno y dispara
// VSYNC / interrupción si toca.
// ===========================================================================
void AmstradCPC::tick(int tstates) {
    tstates_counter_ += static_cast<std::uint64_t>(tstates);

    // Si estamos en medio del pulso de VSYNC, ver si hay que terminarlo.
    if (vsync_active_) {
        vsync_tstates_ += static_cast<std::uint64_t>(tstates);
        if (vsync_tstates_ >= VSYNC_DURATION) {
            vsync_active_ = false;
            vsync_tstates_ = 0;
        }
    }

    // ¿Ha terminado el cuadro? Entonces generamos VSYNC + interrupción.
    if (tstates_counter_ >= TSTATES_PER_FRAME) {
        tstates_counter_ -= TSTATES_PER_FRAME;
        vsync_active_ = true;
        vsync_tstates_ = 0;
        interrupt_count_++;

        // Avisar al Z80: interrupción enmascarable (INT), datos = 0xFF.
        cpu_.interrupt(0x00);
    }
}

// ===========================================================================
// run: ejecuta N steps del Z80, avanzando el estado del CPC tras cada uno.
// ===========================================================================
std::uint64_t AmstradCPC::run(std::uint64_t n_steps) {
    for (std::uint64_t i = 0; i < n_steps; ++i) {
        int tstates = cpu_.step();
        tick(tstates);
    }
    return cpu_.cycles();
}

// ===========================================================================
// IBus: memoria
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
// IBus: E/S
// ===========================================================================
std::uint8_t AmstradCPC::in(std::uint16_t port) {
    std::uint8_t hi = static_cast<std::uint8_t>(port >> 8);

    switch (hi) {
        case 0xBC:
            return 0xFF;
        case 0xBD:
            return crtc_regs_[crtc_selected_reg_ & 0x1F];
        case 0xF4:
            return ay_regs_[ay_selected_reg_ & 0x0F];
        case 0xF5:
            // Puerto de selección del AY: en un CPC real, al leer, devuelve
            // el valor del registro seleccionado (no es un puerto de solo
            // escritura).
            return ay_regs_[ay_selected_reg_ & 0x0F];
        case 0xF6:
            return ppi_port_a_;
        case 0xF7:
            return ppi_port_b_;
        case 0xF8: {
            // [Paso 3] Puerto C del PPI: aquí llega el bit de VSYNC.
            // El bit 0 refleja el estado de VSYNC (1 si está activo).
            // Los demás bits los dejamos como estén configurados.
            std::uint8_t val = ppi_port_c_ & 0xFE;
            if (vsync_active_) val |= 0x01;
            return val;
        }
        case 0xF9:
            return 0xFF;
        case 0xFF:
            return 0xFF;
        default:
            return 0xFF;
    }
}

void AmstradCPC::out(std::uint16_t port, std::uint8_t value) {
    std::uint8_t hi = static_cast<std::uint8_t>(port >> 8);

    switch (hi) {
        case 0x7F:
            ga_last_value_ = value;
            break;
        case 0xBC:
            crtc_selected_reg_ = value & 0x1F;
            break;
        case 0xBD:
            crtc_regs_[crtc_selected_reg_ & 0x1F] = value;
            break;
        case 0xF4:
            ay_regs_[ay_selected_reg_ & 0x0F] = value;
            break;
        case 0xF5:
            ay_selected_reg_ = value & 0x0F;
            break;
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
        default:
            break;
    }
}

} // namespace amstrad