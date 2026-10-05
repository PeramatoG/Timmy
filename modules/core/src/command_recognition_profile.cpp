/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/core/command_recognition_profile.h"

namespace timmy::core {

    // Creates the shared backend-neutral hints for lighting command recognition.
    asr::RecognitionHints commandRecognitionHints()
    {
        return {
            {
                "channel", "canal", "fixture", "group", "grupo",
                "at", "al", "full", "select", "selecciona", "set",
                "pon", "up", "sube", "down", "baja"
            },
            {
                "Canal 4 al 100%",
                "Grupo 5 al 70%",
                "Pon el grupo 5 al 70%",
                "Selecciona el canal 25",
                "Selecciona los canales del 20 al 30",
                "Canal 1, Canal 2 y Canal 3 al 40%",
                "Sube el canal 5 al 50%",
                "Baja el grupo 2 al 20%",
                "Channel 4 at 50%",
                "Set group 5 at 70%",
                "Select channels 20 through 30"
            },
            asr::RecognitionConstraintIntent::commandPhrase
        };
    }

} // namespace timmy::core
