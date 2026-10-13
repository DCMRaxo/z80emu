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

    // CRTC: estado inicial a cero (todos los registros a 0).
    crtc_.reset();

    // PPI: puertos de entrada con líneas pull-up (0xFF).
    ppi_port_a_ = 0xFF;
    ppi_port_b_ = 0xFF;
    ppi_port_c_ = 0xFF;
}

// ---------------------------------------------------------------------------
// load_roms: carga las dos ROMs desde fichero (16 KB cada una).
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
// load_bytes: copia bytes a memoria a partir de una dirección.
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
// tick: avanza el estado del CPC tras un step del Z80.
//
// Orden:
//   1. Avanzar el CRTC con los T-states del Z80.
//   2. Comprobar si el CRTC ha terminado un frame nuevo. Si sí, generar
//      interrupción al Z80 y activar el pulso de interrupción del PPI.
//   3. Actualizar el pulso de interrupción (apagarlo cuando expire).
// ===========================================================================
void AmstradCPC::tick(int tstates) {
    // 1. Avanzar el CRTC.
    crtc_.tick(tstates);

    // 2. ¿Ha terminado un frame nuevo? El CRTC incrementa frame_count()
    //    al cerrar cada frame. Comparamos con el último frame que
    //    procesamos.
    if (crtc_.frame_count() != last_frame_) {
        last_frame_ = crtc_.frame_count();

        // Generar la interrupción al Z80 (INT enmascarable).
        interrupt_count_++;
        cpu_.interrupt(0xFF);

        // Activar el pulso de interrupción: el bit 5 del puerto C del
        // PPI se pone a 1 durante unos ciclos.
        int_pulse_active_ = true;
        int_pulse_tstates_ = 0;
    }

    // 3. Actualizar el pulso de interrupción: apagarlo cuando expire.
    if (int_pulse_active_) {
        int_pulse_tstates_ += static_cast<std::uint64_t>(tstates);
        if (int_pulse_tstates_ >= INT_PULSE_DURATION) {
            int_pulse_active_ = false;
            int_pulse_tstates_ = 0;
        }
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
        // CRTC: puerto de selección (no responde a lecturas).
        case 0xBC:
            return 0xFF;

        // CRTC: puerto de datos. Devuelve el registro seleccionado.
        case 0xBD:
            return crtc_.read_data();

        // AY-3-8912: puerto de datos.
        case 0xF4:
            return ay_regs_[ay_selected_reg_ & 0x0F];

        // AY-3-8912: puerto de selección (en el CPC responde devolviendo
        // el registro seleccionado).
        case 0xF5:
            return ay_regs_[ay_selected_reg_ & 0x0F];

        // PPI 8255: puerto A.
        case 0xF6:
            return ppi_port_a_;

        // PPI 8255: puerto B. Refleja VSYNC en los bits 0 y 5.
        case 0xF7: {
            std::uint8_t val = ppi_port_b_ & 0xDE;   // limpia bits 0 y 5
            if (crtc_.vsync_active()) {
                val |= 0x01;
                val |= 0x20;
            }
            return val;
        }

        // PPI 8255: puerto C. Bit 0 = VSYNC, bit 5 = pulso de interrupción.
        case 0xF8: {
            std::uint8_t val = ppi_port_c_ & 0xDC;   // limpia bits 0 y 5
            if (crtc_.vsync_active()) val |= 0x01;
            if (int_pulse_active_)    val |= 0x20;
            return val;
        }

        // PPI 8255: puerto de control (no responde a lecturas).
        case 0xF9:
            return 0xFF;

        // FDC (controlador de disco): no emulado.
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
        // Gate Array: guardamos el último valor escrito.
        case 0x7F:
            ga_last_value_ = value;
            break;

        // CRTC: puerto de selección.
        case 0xBC:
            crtc_.select_register(value);
            break;

        // CRTC: puerto de datos.
        case 0xBD:
            crtc_.write_data(value);
            break;

        // AY-3-8912: puerto de datos.
        case 0xF4:
            ay_regs_[ay_selected_reg_ & 0x0F] = value;
            break;

        // AY-3-8912: puerto de selección.
        case 0xF5:
            ay_selected_reg_ = value & 0x0F;
            break;

        // PPI 8255: puerto A.
        case 0xF6:
            ppi_port_a_ = value;
            break;

        // PPI 8255: puerto B.
        case 0xF7:
            ppi_port_b_ = value;
            break;

        // PPI 8255: puerto C.
        case 0xF8:
            ppi_port_c_ = value;
            break;

        // PPI 8255: puerto de control.
        case 0xF9:
            ppi_control_ = value;
            break;

        default:
            break;
    }
}

} // namespace amstrad