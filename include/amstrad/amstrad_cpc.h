#pragma once
#include "z80/z80.h"
#include <array>
#include <cstdint>
#include <string>

namespace amstrad {

// ===========================================================================
// AmstradCPC
//
// Estado actual:
//   - Mapa de memoria ROM/RAM funcional.
//   - Puertos de E/S reconocidos.
//   - Contador de T-states interno.
//   - VSYNC simulado cada 79872 T-states (un cuadro a 50 Hz).
//   - Interrupción al Z80 al final de cada cuadro.
//   - Pendiente: CRTC real (que respete sus registros),
//     Gate Array real, PPI real con teclado, AY real.
// ===========================================================================
class AmstradCPC : public z80::IBus {
public:
    AmstradCPC();

    bool load_roms(const std::string& lower_path, const std::string& upper_path);
    void load_bytes(std::uint16_t addr, const std::uint8_t* data, std::size_t len);

    // Ejecuta el Z80 durante N steps. Después de cada step, avanza el
    // contador de T-states interno del CPC y dispara VSYNC/interrupción
    // si toca.
    // Devuelve el total de T-states acumulados por el Z80 desde el reset.
    std::uint64_t run(std::uint64_t n_steps);

    // Avanza el estado del CPC en "tstates" T-states. Se llama desde run()
    // después de cada step del Z80. Puede usarse directamente si el test
    // controla el Z80 a mano.
    void tick(int tstates);

    z80::Z80&       cpu()       { return cpu_; }
    const z80::Z80& cpu() const { return cpu_; }

    const std::array<std::uint8_t, 0x4000>& rom_lower() const { return rom_lower_; }
    const std::array<std::uint8_t, 0x4000>& rom_upper() const { return rom_upper_; }
    const std::array<std::uint8_t, 0x8000>& ram()       const { return ram_; }

    // [Paso 3] Estado del VSYNC y del contador de cuadro.
    bool     vsync_active() const { return vsync_active_; }
    std::uint64_t scanline_counter() const { return tstates_counter_; }

    // [Paso 3] Número total de interrupciones generadas (para tests).
    std::uint64_t interrupt_count() const { return interrupt_count_; }

    // =========================================================================
    // Implementación de IBus
    // =========================================================================
    std::uint8_t read (std::uint16_t addr) override;
    void         write(std::uint16_t addr, std::uint8_t value) override;
    std::uint8_t in (std::uint16_t port) override;
    void         out(std::uint16_t port, std::uint8_t value) override;

    // Métodos para inspeccionar el estado de los chips (para tests).
    std::uint8_t ga_last_value() const { return ga_last_value_; }
    std::uint8_t crtc_selected_reg() const { return crtc_selected_reg_; }
    std::uint8_t crtc_reg(std::uint8_t n) const { return crtc_regs_[n & 0x1F]; }
    std::uint8_t ay_selected_reg() const { return ay_selected_reg_; }
    std::uint8_t ay_reg(std::uint8_t n) const { return ay_regs_[n & 0x0F]; }

private:
    z80::Z80 cpu_;

    // ROMs del sistema.
    std::array<std::uint8_t, 0x4000> rom_lower_{};
    std::array<std::uint8_t, 0x4000> rom_upper_{};

    // RAM del sistema.
    std::array<std::uint8_t, 0x8000> ram_{};

    // Estado del mapa de memoria.
    bool rom_lower_enabled_ = true;
    bool rom_upper_enabled_ = true;

    // Estado de los chips en puertos.
    std::uint8_t ga_last_value_ = 0;
    std::uint8_t crtc_selected_reg_ = 0;
    std::array<std::uint8_t, 32> crtc_regs_{};
    std::uint8_t ay_selected_reg_ = 0;
    std::array<std::uint8_t, 16> ay_regs_{};
    std::uint8_t ppi_port_a_ = 0;
    std::uint8_t ppi_port_b_ = 0;
    std::uint8_t ppi_port_c_ = 0;
    std::uint8_t ppi_control_ = 0;

    // =========================================================================
    // [Paso 3] Contador de T-states y VSYNC
    // =========================================================================

    // Contador de T-states desde el inicio del cuadro actual.
    std::uint64_t tstates_counter_ = 0;

    // Estado actual de la señal VSYNC.
    bool vsync_active_ = false;

    // Bit 5 del puerto C del PPI: pulso de interrupción del CRTC.
    // Se pone a 1 durante unos ciclos tras una interrupción.
    bool int_pulse_active_ = false;
    std::uint64_t int_pulse_tstates_ = 0;

    // Contador de T-states desde el inicio del pulso de VSYNC.
    std::uint64_t vsync_tstates_ = 0;

    // Contador total de interrupciones generadas.
    std::uint64_t interrupt_count_ = 0;

    // Constantes de temporización (valores aproximados del CPC).
    static constexpr std::uint64_t TSTATES_PER_FRAME = 79872;  // 50 Hz @ 4 MHz
    static constexpr std::uint64_t VSYNC_DURATION    = 1024;   // ~1 ms
};

} // namespace amstrad