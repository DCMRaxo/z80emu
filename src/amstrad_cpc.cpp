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
std::uint8_t AmstradCPC::in(std::uint16_t /*port*/) {
    return 0xFF;
}

void AmstradCPC::out(std::uint16_t /*port*/, std::uint8_t /*value*/) {
    // nada
}

} // namespace amstrad