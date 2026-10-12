#include "amstrad/amstrad_cpc.h"
#include <cstdio>

// ===========================================================================
// Test del Paso 2: puertos de E/S del Amstrad CPC.
//
// Verifica que:
//   - Los puertos del Gate Array (0x7Fxx) guardan el último valor escrito.
//   - Los puertos del CRTC (0xBCxx / 0xBDxx) guardan registros.
//   - Los puertos del AY (0xF4xx / 0xF5xx) guardan registros.
//   - Los puertos del PPI (0xF6xx-0xF9xx) guardan valores.
//   - Los puertos desconocidos devuelven 0xFF.
// ===========================================================================

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("Uso: %s <rom_baja> <rom_alta>\n", argv[0]);
        return 1;
    }

    amstrad::AmstradCPC cpc;
    if (!cpc.load_roms(argv[1], argv[2])) {
        return 2;
    }

    std::printf("=== Pruebas de puertos ===\n\n");

    // ---- Gate Array ----
    std::printf("Gate Array (0x7Fxx):\n");
    cpc.out(0x7F00, 0x42);
    std::printf("  out(0x7F00, 0x42) -> ga_last_value() = %02X (esperado 42)\n",
                cpc.ga_last_value());
    cpc.out(0x7F10, 0x99);
    std::printf("  out(0x7F10, 0x99) -> ga_last_value() = %02X (esperado 99)\n",
                cpc.ga_last_value());
    std::printf("\n");

    // ---- CRTC ----
    std::printf("CRTC (0xBCxx / 0xBDxx):\n");
    cpc.out(0xBC00, 0x05);          // seleccionar registro 5
    cpc.out(0xBD00, 0xAB);          // escribir 0xAB en registro 5
    std::printf("  crtc_selected_reg() = %02X (esperado 05)\n",
                cpc.crtc_selected_reg());
    std::printf("  crtc_reg(5)         = %02X (esperado AB)\n",
                cpc.crtc_reg(5));
    std::printf("  in(0xBD00)          = %02X (esperado AB)\n",
                cpc.in(0xBD00));
    std::printf("\n");

    // ---- AY-3-8912 ----
    std::printf("AY-3-8912 (0xF4xx / 0xF5xx):\n");
    cpc.out(0xF500, 0x07);          // seleccionar registro 7 del AY
    cpc.out(0xF400, 0x3C);          // escribir 0x3C en registro 7
    std::printf("  ay_selected_reg() = %02X (esperado 07)\n",
                cpc.ay_selected_reg());
    std::printf("  ay_reg(7)         = %02X (esperado 3C)\n",
                cpc.ay_reg(7));
    std::printf("  in(0xF400)        = %02X (esperado 3C)\n",
                cpc.in(0xF400));
    std::printf("\n");

    // ---- PPI 8255 ----
    std::printf("PPI 8255 (0xF6xx-0xF9xx):\n");
    cpc.out(0xF600, 0x11);
    cpc.out(0xF700, 0x22);
    cpc.out(0xF800, 0x33);
    std::printf("  in(0xF600) = %02X (esperado 11)\n", cpc.in(0xF600));
    std::printf("  in(0xF700) = %02X (esperado 22)\n", cpc.in(0xF700));
    std::printf("  in(0xF800) = %02X (esperado 33)\n", cpc.in(0xF800));
    std::printf("\n");

    // ---- Puertos desconocidos ----
    std::printf("Puertos desconocidos:\n");
    std::printf("  in(0x1234) = %02X (esperado FF)\n", cpc.in(0x1234));
    std::printf("  in(0xAB00) = %02X (esperado FF)\n", cpc.in(0xAB00));

    return 0;
}