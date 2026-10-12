#pragma once
#include "z80/z80.h"
#include <array>
#include <cstdint>
#include <string>

namespace amstrad {

// ===========================================================================
// AmstradCPC: esqueleto de un Amstrad CPC (464 Plus en concreto).
//
// En este primer paso implementa SOLO el mapa de memoria:
//   0x0000-0x3FFF  ROM baja  (BASIC)       — 16 KB
//   0x4000-0x7FFF  ROM alta  (OS)          — 16 KB
//   0x8000-0xBFFF  RAM banco 0             — 16 KB
//   0xC000-0xFFFF  RAM banco 1 (pantalla)  — 16 KB
//
// Todavía NO hay:
//   - Gate Array (mapeo avanzado, vídeo, colores)
//   - CRTC (generación de vídeo)
//   - PPI 8255 (teclado, cassette, sonido)
//   - AY-3-8912 (sonido)
//   - Interrupciones de 50 Hz
//
// Los puertos de E/S devuelven 0xFF y las escrituras se ignoran, por ahora.
// ===========================================================================
class AmstradCPC : public z80::IBus {
public:
    AmstradCPC();

    // Carga las dos ROMs del sistema desde fichero (16 KB cada una).
    //   lower_path: ROM baja (BASIC).
    //   upper_path: ROM alta (OS/firmware).
    // Devuelve true si ambas se cargaron correctamente.
    bool load_roms(const std::string& lower_path, const std::string& upper_path);

    // Carga un bloque de bytes en memoria a partir de una dirección.
    // Se usa para pruebas y para inyectar programas directamente.
    // Si la dirección cae en ROM, se escribe en la ROM (útil para tests).
    void load_bytes(std::uint16_t addr, const std::uint8_t* data, std::size_t len);

    // Ejecuta el Z80 durante N steps (instrucciones).
    // Devuelve el total acumulado de T-states.
    std::uint64_t run(std::uint64_t n_steps);

    // Acceso al Z80 interno.
    z80::Z80&       cpu()       { return cpu_; }
    const z80::Z80& cpu() const { return cpu_; }

    // Acceso directo a la RAM y las ROMs (para depuración y tests).
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

private:
    z80::Z80 cpu_;

    // ROMs del sistema.
    std::array<std::uint8_t, 0x4000> rom_lower_{};
    std::array<std::uint8_t, 0x4000> rom_upper_{};

    // RAM del sistema: 64 KB (2 bancos).
    std::array<std::uint8_t, 0x8000> ram_{};

    // Estado del mapa de memoria (controlado por el Gate Array en el futuro).
    bool rom_lower_enabled_ = true;
    bool rom_upper_enabled_ = true;
};

} // namespace amstrad