#pragma once
#include "z80/z80.h"
#include "crtc.h"
#include <array>
#include <cstdint>
#include <string>

namespace amstrad {

// ===========================================================================
// AmstradCPC
//
// Esqueleto de un Amstrad CPC 464 Plus.
//
// Estado actual:
//   - Mapa de memoria ROM/RAM funcional.
//   - Puertos de E/S reconocidos.
//   - CRTC real (clase CRTC aparte), que genera HSYNC/VSYNC y cuenta frames.
//   - Interrupción al Z80 al final de cada frame (vía CRTC).
//   - Pendiente: Gate Array real, PPI real con teclado, AY real.
// ===========================================================================
class AmstradCPC : public z80::IBus {
public:
    AmstradCPC();

    // Carga las ROMs del sistema (16 KB cada una: baja y alta).
    bool load_roms(const std::string& lower_path, const std::string& upper_path);

    // Carga bytes en memoria a partir de una dirección (para pruebas).
    void load_bytes(std::uint16_t addr, const std::uint8_t* data, std::size_t len);

    // Ejecuta N steps del Z80, avanzando el estado del CPC tras cada uno.
    std::uint64_t run(std::uint64_t n_steps);

    // Avanza el estado del CPC en "tstates" T-states (llamado desde run()
    // después de cada step del Z80).
    void tick(int tstates);

    // Acceso al Z80 interno.
    z80::Z80&       cpu()       { return cpu_; }
    const z80::Z80& cpu() const { return cpu_; }

    // Acceso a las ROMs y la RAM (para depuración y tests).
    const std::array<std::uint8_t, 0x4000>& rom_lower() const { return rom_lower_; }
    const std::array<std::uint8_t, 0x4000>& rom_upper() const { return rom_upper_; }
    const std::array<std::uint8_t, 0x8000>& ram()       const { return ram_; }

    // Acceso al CRTC (para tests y depuración).
    CRTC&       crtc()       { return crtc_; }
    const CRTC& crtc() const { return crtc_; }

    // Estado del VSYNC (delegado en el CRTC).
    bool vsync_active() const { return crtc_.vsync_active(); }

    // Número total de interrupciones generadas desde el reset.
    std::uint64_t interrupt_count() const { return interrupt_count_; }

    // =========================================================================
    // Implementación de IBus
    // =========================================================================
    std::uint8_t read (std::uint16_t addr) override;
    void         write(std::uint16_t addr, std::uint8_t value) override;
    std::uint8_t in (std::uint16_t port) override;
    void         out(std::uint16_t port, std::uint8_t value) override;

    // =========================================================================
    // Acceso a otros chips (para tests)
    // =========================================================================

    // Gate Array: último valor escrito.
    std::uint8_t ga_last_value() const { return ga_last_value_; }

    // AY-3-8912: registro seleccionado y valor de un registro.
    std::uint8_t ay_selected_reg() const { return ay_selected_reg_; }
    std::uint8_t ay_reg(std::uint8_t n) const { return ay_regs_[n & 0x0F]; }

private:
    z80::Z80 cpu_;

    // -------------------------------------------------------------------------
    // Memoria del sistema
    // -------------------------------------------------------------------------
    std::array<std::uint8_t, 0x4000> rom_lower_{};   // ROM baja (BASIC)
    std::array<std::uint8_t, 0x4000> rom_upper_{};   // ROM alta (OS/firmware)
    std::array<std::uint8_t, 0x8000> ram_{};         // 64 KB de RAM (dos bancos)

    // Mapa de memoria (controlado por el Gate Array; de momento siempre
    // apunta a ROM en los rangos bajos).
    bool rom_lower_enabled_ = true;
    bool rom_upper_enabled_ = true;

    // -------------------------------------------------------------------------
    // Chips conectados a los puertos
    // -------------------------------------------------------------------------

    // Gate Array: solo guardamos el último valor escrito (por ahora).
    std::uint8_t ga_last_value_ = 0;

    // CRTC: chip de vídeo (clase aparte).
    CRTC crtc_;

    // AY-3-8912: 16 registros internos, uno seleccionado mediante 0xF5xx.
    std::uint8_t ay_selected_reg_ = 0;
    std::array<std::uint8_t, 16> ay_regs_{};

    // PPI 8255: puertos A, B, C y control.
    std::uint8_t ppi_port_a_ = 0xFF;
    std::uint8_t ppi_port_b_ = 0xFF;
    std::uint8_t ppi_port_c_ = 0xFF;
    std::uint8_t ppi_control_ = 0;

    // -------------------------------------------------------------------------
    // Interrupción de 50 Hz (disparada por el CRTC al terminar cada frame)
    // -------------------------------------------------------------------------

    // Contador de frames ya procesados. Se compara con crtc_.frame_count()
    // para detectar cuándo ha empezado un frame nuevo.
    std::uint64_t last_frame_ = 0;

    // Pulso de interrupción: el bit 5 del puerto C del PPI se pone a 1
    // durante un tiempo tras cada interrupción, para que el firmware sepa
    // que la interrupción viene del CRTC.
    bool int_pulse_active_ = false;
    std::uint64_t int_pulse_tstates_ = 0;

    // Contador total de interrupciones generadas.
    std::uint64_t interrupt_count_ = 0;

    // Duración del pulso de interrupción (en T-states).
    static constexpr std::uint64_t INT_PULSE_DURATION = 1000;
};

} // namespace amstrad