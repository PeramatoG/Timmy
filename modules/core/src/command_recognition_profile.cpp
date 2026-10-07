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
    asr::RecognitionHints commandRecognitionHints(const command::CommandProfile& profile)
    {
        return {profile.recognition.terms, profile.recognition.examples,
            asr::RecognitionConstraintIntent::commandPhrase};
    }
}
