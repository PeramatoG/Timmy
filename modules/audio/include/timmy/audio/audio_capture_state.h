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

namespace timmy::audio {

    // Describes the externally observable lifecycle of an audio capture session.
    enum class AudioCaptureState
    {
        stopped,
        starting,
        running,
        stopping,
        failed
    };

} // namespace timmy::audio
