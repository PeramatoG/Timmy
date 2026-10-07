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

#include "timmy/command/command_lexicon.h"

namespace timmy::core {

    // Creates the shared backend-neutral hints for lighting command recognition.
    asr::RecognitionHints commandRecognitionHints()
    {
        return {
            command::spokenCommandVocabulary(),
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
                "Fixture 1 a tope",
                "Dame los canales del 1 al 10",
                "Coge el canal 4", "Toma el grupo 2", "Marca el fixture 3",
                "Dame los canales del 1 al 10 y me quitas el 6",
                "Canales 1 al 10 excepto 6",
                "Canales 1 al 10 menos el 6",
                "Canales 1 al 10 salvo el 6",
                "Channels 1 through 10 except 6",
                "Channels 1 through 10 but not 6",
                "Dame canales 1 al 10 y añade el 15",
                "Channels 1 through 10 plus 15",
                "Añade canal 4", "Anade canal 4", "Agrega fixture 7",
                "Mete el canal 4", "Incluye grupo 2", "Add channel 4",
                "Include fixture 7", "Quita fixture 7", "Resta grupo 2",
                "Excluye canal 4", "Subtract fixture 7", "Exclude group 2",
                // Elided examples require a preceding successful explicit target.
                "Súmale el 4", "Sumale el 4", "Añade el 4", "Add 4",
                "Quita el 10", "Saca el 10", "Menos el 10", "Remove 10",
                "Suma del 20 al 25", "Quita del 5 al 8",
                "Limpia selección", "Limpia la selección", "Borra selección",
                "Quita la selección", "Clear selection",
                "Limpia todo", "Limpia el editor", "Borra todo", "Borra el editor",
                "Quita todo", "Vacía el editor", "Vacia el editor",
                "Clear all", "Cler all",
                "Libera todo", "Suelta todo", "Libera el editor",
                "Release all", "Release editor"
            },
            asr::RecognitionConstraintIntent::commandPhrase
        };
    }

} // namespace timmy::core
