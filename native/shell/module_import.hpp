#pragma once
#include "scripts.hpp"
#include <exception>
#include <optional>
#include <string>

namespace sc64::shell {
// What follows a game module the user picked: it is installed (`install` throws when it is refused), then a ROM
// that is already kept starts the game, and otherwise the screen asks for the ROM. The steps are callables so that
// the sequence is tested without a session.
template <class Install, class StoredRom, class LoadRom>
ImportEvent module_import_event(Install&& install, StoredRom&& stored_rom, LoadRom&& load_rom) {
    using Kind = ImportEvent::Kind;
    try {
        install();
    } catch (const std::exception& error) { return {Kind::failed, error.what()}; }
    if (const std::optional<std::string> rom = stored_rom()) {
        if (const std::optional<std::string> error = load_rom(*rom)) return {Kind::failed, *error};
        return {Kind::started, {}};
    }
    return {Kind::notice, "The game module is installed. Now choose your ROM."};
}
}
