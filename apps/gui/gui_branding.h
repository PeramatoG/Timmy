/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "timmy_gui_version.h"
#include <wx/bitmap.h>
#include <wx/dialog.h>
#include <wx/filename.h>
#include <wx/frame.h>
#include <wx/icon.h>
#include <wx/image.h>
#include <wx/imagpng.h>
#include <algorithm>
#include <wx/log.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>

namespace timmy::gui {

// Resolve only from the executable directory: branding must work independently
// of the working directory, both in the build tree and an installed package.
inline wxString brandingPath(const wxString& filename)
{
    wxFileName path(wxStandardPaths::Get().GetExecutablePath());
    path.SetFullName(filename);
    path.AppendDir("resources");
    path.AppendDir("branding");
    return path.GetFullPath();
}

inline wxBitmap brandingBitmap(const wxString& filename, wxWindow& window, const wxSize& logicalBounds)
{
    // Register just the required handler; tests and other wxApp frontends may
    // construct MainFrame without running TimmyApp::OnInit.
    if (!wxImage::FindHandler(wxBITMAP_TYPE_PNG)) wxImage::AddHandler(new wxPNGHandler);
    wxLogNull quiet; // Missing/corrupt optional artwork must never show a startup error.
    wxImage image;
    const auto path = brandingPath(filename);
    if (!wxFileExists(path) || !image.LoadFile(path, wxBITMAP_TYPE_PNG)) return {};
    const auto bounds = window.FromDIP(logicalBounds);
    const double ratio = std::min(static_cast<double>(bounds.x) / image.GetWidth(),
        static_cast<double>(bounds.y) / image.GetHeight());
    image.Rescale(std::max(1, static_cast<int>(image.GetWidth() * ratio)),
        std::max(1, static_cast<int>(image.GetHeight() * ratio)), wxIMAGE_QUALITY_HIGH);
    return wxBitmap(image);
}

inline wxBitmap brandingBitmap(const wxString& filename, wxWindow& window, int logicalSize)
{
    return brandingBitmap(filename, window, wxSize(logicalSize, logicalSize));
}

inline wxSizer* createBrandingHeader(wxWindow* parent,
    const wxString& filename = "timmy_wordmark.png")
{
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    const auto wordmark = brandingBitmap(filename, *parent, wxSize(300, 100));
    if (wordmark.IsOk())
    {
        auto* artwork = new wxStaticBitmap(parent, wxID_ANY, wordmark);
        artwork->SetName("branding.wordmark");
        artwork->SetToolTip("TIMMY — The Imaginary Man, Minding You");
        row->Add(artwork, 0, wxALIGN_CENTER_VERTICAL);
        return row;
    }
    // Native text remains a useful fallback when optional artwork is absent.
    auto* text = new wxBoxSizer(wxVERTICAL);
    auto* title = new wxStaticText(parent, wxID_ANY, "TIMMY");
    title->SetFont(title->GetFont().Scaled(1.65).Bold());
    title->SetName("branding.title");
    text->Add(title);
    auto* tagline = new wxStaticText(parent, wxID_ANY, "The Imaginary Man, Minding You");
    tagline->SetFont(tagline->GetFont().Scaled(0.9));
    text->Add(tagline, 0, wxTOP, parent->FromDIP(2));
    row->Add(text, 0, wxALIGN_CENTER_VERTICAL);
    return row;
}

inline void setApplicationIcon(wxFrame& frame)
{
#ifdef __WXMSW__
    // Same named resource as the executable icon, including all supplied sizes.
    wxIcon icon("TIMMY_ICON", wxBITMAP_TYPE_ICO_RESOURCE);
#else
    wxIcon icon;
    const auto bitmap = brandingBitmap("timmy_mark.png", frame, 48);
    if (bitmap.IsOk()) icon.CopyFromBitmap(bitmap);
#endif
    if (icon.IsOk()) frame.SetIcon(icon);
}

inline void showAbout(wxWindow* parent)
{
    wxDialog dialog(parent, wxID_ABOUT, "About TIMMY", wxDefaultPosition,
        wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    dialog.SetName("help.about");
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    const auto logo = brandingBitmap("timmy_logo.png", dialog, 144);
    if (logo.IsOk())
    {
        auto* artwork = new wxStaticBitmap(&dialog, wxID_ANY, logo);
        artwork->SetName("branding.logo");
        sizer->Add(artwork, 0, wxALIGN_CENTER | wxTOP, dialog.FromDIP(16));
    }
    auto* title = new wxStaticText(&dialog, wxID_ANY, "TIMMY");
    title->SetFont(title->GetFont().Scaled(1.4).Bold());
    sizer->Add(title, 0, wxALIGN_CENTER | wxTOP, dialog.FromDIP(10));
    sizer->Add(new wxStaticText(&dialog, wxID_ANY, "The Imaginary Man, Minding You"),
        0, wxALIGN_CENTER | wxALL, dialog.FromDIP(10));
    auto* description = new wxStaticText(&dialog, wxID_ANY,
        "Offline-first voice command assistant for lighting consoles and OSC-controlled systems.");
    description->Wrap(dialog.FromDIP(400));
    sizer->Add(description, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(16));
    auto* version = new wxStaticText(&dialog, wxID_ANY, "Version " TIMMY_GUI_VERSION);
    version->SetName("help.version");
    sizer->Add(version, 0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(16));
    sizer->Add(new wxStaticText(&dialog, wxID_ANY,
        "Copyright (C) 2026 Luisma Peramato\nGNU GPL version 3 or later."),
        0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(16));
    sizer->Add(dialog.CreateStdDialogButtonSizer(wxOK), 0, wxEXPAND | wxALL, dialog.FromDIP(12));
    dialog.SetSizerAndFit(sizer);
    dialog.CentreOnParent();
    dialog.ShowModal();
}

// Keep practical help in one editable text block. These examples are accepted
// by the existing command parser; no speculative natural-language promises.
inline constexpr auto quickHelpText = R"help(Basic workflow
1. Select the microphone and input channel.
2. Select the recognition engine and model.
3. Start listening.
4. Optionally enable OSC command output and select the output profile.
5. Speak a supported lighting command.

Reliable command examples
Canal 5 al 50
Enciende canal 5
Apaga canales 1 a 10
Fixture 5 a full
Selecciona fixtures 1, 3 y 5
Canales 1 a 10 excepto 5 al 50
Lleva fixture 5 a home
Limpia editor
Libera canal 5

Command targets and language
Channel, Fixture or Group can be explicit. When omitted, the configured Default command target is used. Spanish and common English lighting terms can be mixed where supported. Check the transcript and execution result when a command is rejected or unsupported by the selected output profile.

Compute and advanced settings
Automatic compute prefers Vulkan when usable and falls back to CPU. The compute status shows availability and the active device. Engine settings and VAD / Endpoint contain the advanced tuning controls.

Diagnostics
Transcript details shows normalization, assembly and semantic command diagnostics. Open benchmark... launches the guided recognition testing tool.
)help";

inline void showQuickHelp(wxWindow* parent)
{
    wxDialog dialog(parent, wxID_HELP, "TIMMY Quick Help", wxDefaultPosition,
        wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    dialog.SetName("help.quick");
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* text = new wxTextCtrl(&dialog, wxID_ANY, wxString::FromUTF8(quickHelpText),
        wxDefaultPosition, dialog.FromDIP(wxSize(540, 460)), wxTE_MULTILINE | wxTE_READONLY);
    sizer->Add(text, 1, wxEXPAND | wxALL, dialog.FromDIP(12));
    sizer->Add(dialog.CreateStdDialogButtonSizer(wxOK), 0, wxEXPAND | wxALL, dialog.FromDIP(12));
    dialog.SetSizerAndFit(sizer);
    dialog.SetMinSize(dialog.FromDIP(wxSize(360, 300)));
    dialog.CentreOnParent();
    dialog.ShowModal();
}

} // namespace timmy::gui
