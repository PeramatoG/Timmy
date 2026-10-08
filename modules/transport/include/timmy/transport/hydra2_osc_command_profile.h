/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "timmy/transport/osc_command_profile.h"
#include <map>

namespace timmy::transport {

    struct Hydra2OscOptions
    {
        std::map<std::string, std::string> keyAddresses{
            {"CH", "/keyCH"}, {"FXT", "/keyFXT"}, {"GRP", "/keyGRP"},
            {"0", "/key0"}, {"1", "/key1"}, {"2", "/key2"}, {"3", "/key3"},
            {"4", "/key4"}, {"5", "/key5"}, {"6", "/key6"}, {"7", "/key7"},
            {"8", "/key8"}, {"9", "/key9"}, {"AT", "/key@"}, {"DOT", "/key."},
            {"C", "/keyC"}, {"RST", "/keyRST"}, {"REL", "/keyREL"},
            {"THRU", "/keyTHRU"}, {"EXCP", "/keyEXCP"}, {"CALL", "/keyCALL"},
            {"HOME", "/keyHOME"}, {"OK", "/keyOK"}, {"TEST", "/keyTEST"},
            {"NEXT", "/keyNEXT"}, {"PREV", "/keyPREV"}};
        std::chrono::milliseconds hold{30};
        std::chrono::milliseconds gap{30};
        bool preClear = false;
    };

    class Hydra2OscCommandProfile final : public IOscCommandProfile
    {
    public:
        explicit Hydra2OscCommandProfile(Hydra2OscOptions options = {});
        [[nodiscard]] OscMappingResult map(
            const command::SemanticCommand& command) const override;
        [[nodiscard]] OscMappingResult mapRendered(const RenderedCommand& rendered) const override;
    private:
        [[nodiscard]] OscMappingResult mapKeys(const std::vector<std::string>& keys) const;
        Hydra2OscOptions options_;
    };
}
