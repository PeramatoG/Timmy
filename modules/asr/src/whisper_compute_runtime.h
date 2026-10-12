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
#include "timmy/asr/compute_selection.h"

namespace timmy::asr::detail {
    [[nodiscard]] ComputeDiscovery discoverWhisperCompute(bool loadVulkan = true);
}
