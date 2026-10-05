/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "gui_theme.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/colour.h>
#include <wx/textctrl.h>
#include <wx/window.h>

namespace timmy::gui {

    namespace {

        struct Palette
        {
            wxColour window;
            wxColour control;
            wxColour text;
        };

        // Applies one palette recursively while preserving native widget drawing.
        void applyPalette(wxWindow& window, const Palette& palette)
        {
            window.SetBackgroundColour(palette.window);
            window.SetForegroundColour(palette.text);

            if (dynamic_cast<wxTextCtrl*>(&window) != nullptr
                || dynamic_cast<wxChoice*>(&window) != nullptr
                || dynamic_cast<wxButton*>(&window) != nullptr)
            {
                window.SetBackgroundColour(palette.control);
            }

            for (auto* child : window.GetChildren())
            {
                applyPalette(*child, palette);
            }
        }

    } // namespace

    // Applies a centralized best-effort appearance to a wxWidgets window tree.
    void applyAppearance(wxWindow& window, Appearance appearance)
    {
        if (appearance == Appearance::system)
        {
            return;
        }

        const Palette palette = appearance == Appearance::dark
            ? Palette{
                wxColour(30, 32, 36),
                wxColour(48, 51, 57),
                wxColour(232, 234, 237)
            }
            : Palette{
                wxColour(245, 245, 245),
                wxColour(255, 255, 255),
                wxColour(25, 25, 25)
            };

        applyPalette(window, palette);
        window.Refresh();
    }

} // namespace timmy::gui
