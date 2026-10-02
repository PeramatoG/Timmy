/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "main_frame.h"

#include <wx/app.h>

class TimmyApp final
    : public wxApp
{
public:
    bool OnInit() override
    {
        auto* frame =
            new timmy::gui::MainFrame();

        frame->Show();

        return true;
    }
};

wxIMPLEMENT_APP(TimmyApp);