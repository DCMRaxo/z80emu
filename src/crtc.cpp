#include "amstrad/crtc.h"

namespace amstrad {

// ===========================================================================
// reset
// ===========================================================================
void CRTC::reset() {
    registers_.fill(0);
    selected_reg_ = 0;
    h_counter_ = 0;
    v_counter_ = 0;
    raster_counter_ = 0;
    tstates_accum_ = 0;
    hsync_active_ = false;
    vsync_active_ = false;
    frame_count_ = 0;
}

// ===========================================================================
// screen_address
//
// Devuelve la dirección de memoria de pantalla que el CRTC está
// direccionando en este momento. Es la dirección base (R12/R13) más el
// offset dentro del frame.
//
// En el CPC, la memoria de pantalla se organiza de forma intercalada,
// pero para la emulación básica, con devolver una dirección razonable
// es suficiente. La dirección real la calcula el chip de vídeo cuando
// dibuja, y no la vamos a usar todavía.
// ===========================================================================
std::uint16_t CRTC::screen_address() const {
    std::uint16_t base = (static_cast<std::uint16_t>(reg(12)) << 8) | reg(13);
    // Añadimos un offset simple basado en la posición actual.
    // (Cuando implementemos el vídeo de verdad, esto se calculará de
    // forma más precisa.)
    std::uint16_t offset = static_cast<std::uint16_t>(v_counter_ * 80 + h_counter_);
    return static_cast<std::uint16_t>(base + offset);
}

// ===========================================================================
// tick
//
// Avanza el CRTC en función de los T-states que ha consumido el Z80.
// El CRTC avanza un "carácter de tiempo" cada 4 T-states.
// ===========================================================================
void CRTC::tick(int tstates) {
    tstates_accum_ += tstates;

    // Mientras tengamos 4 o más T-states acumulados, avanzamos un carácter.
    while (tstates_accum_ >= 4) {
        tstates_accum_ -= 4;

        // ---------------------------------------------------------------
        // Avanzar el contador horizontal.
        // ---------------------------------------------------------------
        h_counter_++;

        // ¿HSYNC? El CRTC genera HSYNC cuando el contador horizontal
        // llega a R2. El pulso dura un número de caracteres definido
        // por el nibble bajo de R3. Para simplificar, hacemos que dure
        // 4 caracteres.
        if (h_counter_ == reg(2)) {
            hsync_active_ = true;
        }
        if (h_counter_ == static_cast<std::uint16_t>(reg(2) + 4)) {
            hsync_active_ = false;
        }

        // ---------------------------------------------------------------
        // ¿Fin de línea? El contador horizontal ha llegado a R0.
        // ---------------------------------------------------------------
        std::uint16_t h_total = reg(0);
        if (h_total == 0) h_total = 1;   // evitar bucles infinitos

        if (h_counter_ > h_total) {
            h_counter_ = 0;

            // Avanzar el contador de raster (líneas dentro de una fila
            // de caracteres). Cuando llega a R9, se reinicia y avanza
            // el contador vertical.
            std::uint16_t r9 = reg(9) & 0x1F;
            if (r9 == 0) r9 = 1;

            raster_counter_++;
            if (raster_counter_ > r9) {
                raster_counter_ = 0;
                v_counter_++;

                // ¿VSYNC? El CRTC genera VSYNC cuando el contador
                // vertical llega a R7. El pulso dura unos cuantos
                // caracteres.
                if (v_counter_ == reg(7)) {
                    vsync_active_ = true;
                }
                if (v_counter_ == static_cast<std::uint16_t>(reg(7) + 4)) {
                    vsync_active_ = false;
                }

                // ¿Fin de frame? El contador vertical ha llegado a R4.
                std::uint16_t v_total = reg(4) & 0x7F;
                if (v_total == 0) v_total = 1;

                if (v_counter_ > v_total) {
                    v_counter_ = 0;
                    frame_count_++;
                }
            }
        }
    }
}

} // namespace amstrad