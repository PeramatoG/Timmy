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
                "channel", "channels", "canal", "canales", "fixture", "fixtures",
                "group", "groups", "grupo", "grupos", "at", "al", "full", "tope",
                "thru", "through", "hasta", "excepto", "except", "and", "y",
                "select", "selecciona", "set", "pon", "sube", "baja",
                "libera", "release", "suelta", "test", "prueba", "comprueba",
                "siguiente", "next", "anterior", "previous", "prev",
                "editor", "limpia", "borra", "clear", "home", "lleva"
            },
            {
                "Canal 4 al 100%",
                "Grupo 5 al 70%",
                "Pon el grupo 5 al 70%",
                "Selecciona el canal 25",
                "Selecciona los canales del 20 al 30",
                "Canales 1, 2 y 3 al 40%",
                "Sube el canal 5 al 50%",
                "Baja el grupo 2 al 20%",
                "Channel 4 at 50%",
                "Set group 5 at 70%",
                "Select channels 20 through 30",
                "Canales 1 a 10 al 50",
                "Canales del 1 al 10 al 50",
                "Canal 1 thru 10 al 50",
                "Canales 1 hasta 15 excepto 5 a 8 al 50",
                "Set channels 1, 3 and 5 at 70",
                "Libera canales 30 a 40",
                "Suelta canal 5",
                "Release channels 1 through 10",
                "Test canal 21",
                "Prueba fixtures 1 a 6",
                "Comprueba canal 1",
                "Test grupo 3",
                "Test", "Siguiente", "Next test",
                "Siguiente canal", "Canal anterior", "Next fixture", "Previous group",
                "Fixtures 1 a 6 a home", "Lleva fixture 5 a home",
                "Limpia editor", "Borra todo el editor", "Clear editor",
                "Fixture 1 a tope"
            },
            asr::RecognitionConstraintIntent::commandPhrase
        };
    }

} // namespace timmy::core
