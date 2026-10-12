#pragma once
#include "z80/z80.h"
#include <array>
#include <cstdint>
#include <string>

namespace amstrad {

// ===========================================================================
// AmstradCPC: esqueleto de un Amstrad CPC (464 Plus en concreto).
//
// Estado actual:
//   - Mapa de memoria ROM/RAM funcional.
//   - Puertos de E/S reconocidos (aunque la mayoría aún no hacen nada útil).
//   - Pendiente: Gate Array real (vídeo, colores, mapeo de RAM),
//     CRTC real (generación de vídeo y VSYNC),
//     PPI real (teclado, cassette),
//     AY real (sonido), interrupciones de 50 Hz.
// ===========================================================================
class AmstradCPC : public z80::IBus {
public:
    AmstradCPC();

    bool load_roms(const std::string& lower_path, const std::string& upper_path);
    void load_bytes(std::uint16_t addr, const std::uint8_t* data, std::size_t len);
    std::uint64_t run(std::uint64_t n_steps);

    z80::Z80&       cpu()       { return cpu_; }
    const z80::Z80& cpu() const { return cpu_; }

    const std::array<std::uint8_t, 0x4000>& rom_lower() const { return rom_lower_; }
    const std::array<std::uint8_t, 0x4000>& rom_upper() const { return rom_upper_; }
    const std::array<std::uint8_t, 0x8000>& ram()       const { return ram_; }

    // =========================================================================
    // Implementación de IBus
    // =========================================================================
    std::uint8_t read (std::uint16_t addr) override;
    void         write(std::uint16_t addr, std::uint8_t value) override;
    std::uint8_t in (std::uint16_t port) override;
    void         out(std::uint16_t port, std::uint8_t value) override;

    // [Paso 2] Métodos para inspeccionar el estado de los chips (para tests).
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

    // RAM del sistema: 64 KB.
    std::array<std::uint8_t, 0x8000> ram_{};

    // Estado del mapa de memoria.
    bool rom_lower_enabled_ = true;
    bool rom_upper_enabled_ = true;

    // =========================================================================
    // [Paso 2] Estado de los chips conectados a los puertos
    // =========================================================================

    // Gate Array: registra el último valor escrito.
    // (Más adelante lo usaremos para colores, modo de vídeo, mapeo de RAM...)
    std::uint8_t ga_last_value_ = 0;

    // CRTC 6845: tiene 18 registros internos (R0..R17) y un registro
    // "seleccionado" que se controla escribiendo en el puerto 0xBCxx.
    // Al escribir en 0xBDxx, se escribe el registro seleccionado.
    // Al leer de 0xBDxx, se lee el registro seleccionado.
    std::uint8_t crtc_selected_reg_ = 0;
    std::array<std::uint8_t, 32> crtc_regs_{};   // 32 por comodidad, solo se usan 18.

    // AY-3-8912: 16 registros internos, uno seleccionado mediante 0xF5xx.
    // El puerto de datos (0xF4xx) lee/escribe el registro seleccionado.
    std::uint8_t ay_selected_reg_ = 0;
    std::array<std::uint8_t, 16> ay_regs_{};

    // PPI 8255: por ahora solo guardamos los bytes escritos.
    // En el CPC, el PPI gestiona el teclado, el cassette, el sonido y las
    // señales de VSYNC/HSYNC del CRTC.
    std::uint8_t ppi_port_a_ = 0;
    std::uint8_t ppi_port_b_ = 0;
    std::uint8_t ppi_port_c_ = 0;
    std::uint8_t ppi_control_ = 0;
};

} // namespace amstrad