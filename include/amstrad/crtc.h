#pragma once
#include <array>
#include <cstdint>

namespace amstrad {

// ===========================================================================
// CRTC: emulación del chip Motorola 6845 (o compatible) del Amstrad CPC.
//
// El CRTC es el chip que genera las señales de vídeo (HSYNC, VSYNC) y
// direcciona la memoria de pantalla. El firmware del CPC lo configura al
// arrancar y lo lee para sincronizarse.
//
// Cómo funciona internamente:
//   - Tiene 18 registros (R0-R17). El Z80 los escribe/lee a través de
//     los puertos 0xBCxx (selección) y 0xBDxx (datos).
//   - Tiene dos contadores: uno horizontal y otro vertical.
//   - Avanza un "carácter de tiempo" cada 4 T-states del Z80.
//   - Cuando el contador horizontal llega a R0, se reinicia y avanza el
//     contador vertical.
//   - Cuando el contador horizontal llega a R2, se genera HSYNC.
//   - Cuando el contador vertical llega a R7, se genera VSYNC.
//   - Cuando el contador vertical llega a R4, se reinicia.
// ===========================================================================
class CRTC {
public:
    CRTC() = default;

    // -----------------------------------------------------------------------
    // Selección de registro (puerto 0xBCxx)
    // -----------------------------------------------------------------------
    void select_register(std::uint8_t reg) { selected_reg_ = reg & 0x1F; }
    std::uint8_t selected_register() const { return selected_reg_; }

    // -----------------------------------------------------------------------
    // Lectura/escritura de datos (puerto 0xBDxx)
    //
    // En el CPC, los registros R12-R17 son accesibles en lectura y escritura
    // (con matices: R14-R17 son de solo lectura en el CRTC real, pero el
    // firmware los lee para consultar el estado del cursor).
    // -----------------------------------------------------------------------
    std::uint8_t read_data() const {
        return registers_[selected_reg_ & 0x1F];
    }

    void write_data(std::uint8_t value) {
        registers_[selected_reg_ & 0x1F] = value;
    }

    // -----------------------------------------------------------------------
    // Acceso a los registros desde fuera (para los tests)
    // -----------------------------------------------------------------------
    std::uint8_t reg(std::uint8_t n) const { return registers_[n & 0x1F]; }

    // -----------------------------------------------------------------------
    // Avance del estado del CRTC.
    //
    // Se llama con cada instrucción del Z80. Recibe los T-states que ha
    // consumido el Z80, y avanza el contador interno del CRTC, que va a
    // 1 carácter de tiempo cada 4 T-states.
    // -----------------------------------------------------------------------
    void tick(int tstates);

    // -----------------------------------------------------------------------
    // Estado del CRTC en cualquier momento
    // -----------------------------------------------------------------------
    bool     hsync_active()  const { return hsync_active_; }
    bool     vsync_active()  const { return vsync_active_; }

    // Fila (0-...) y columna (0-...) de caracteres actual.
    std::uint16_t horizontal_counter() const { return h_counter_; }
    std::uint16_t vertical_counter()   const { return v_counter_; }
    std::uint16_t raster_counter()     const { return raster_counter_; }

    // Dirección de memoria de pantalla que el CRTC está direccionando ahora.
    // Es lo que el chip de vídeo lee para dibujar el píxel actual.
    std::uint16_t screen_address() const;

    // Contador de cuadros completados desde el reset.
    std::uint64_t frame_count() const { return frame_count_; }

    // Reset: pone todos los registros a 0 y los contadores a 0.
    void reset();

private:
    // 18 registros internos.
    std::array<std::uint8_t, 32> registers_{};
    std::uint8_t selected_reg_ = 0;

    // Contadores.
    std::uint16_t h_counter_ = 0;      // 0 .. R0
    std::uint16_t v_counter_ = 0;      // 0 .. R4
    std::uint16_t raster_counter_ = 0; // 0 .. R9

    // Sub-contador de T-states: acumula hasta 4 para avanzar un carácter.
    int tstates_accum_ = 0;

    // Estado de las señales.
    bool hsync_active_ = false;
    bool vsync_active_ = false;

    // Contador de cuadros.
    std::uint64_t frame_count_ = 0;
};

} // namespace amstrad