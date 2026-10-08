/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "timmy/core/recognition_benchmark.h"

#include <wx/dialog.h>
#include <functional>

class wxButton;
class wxStaticText;
class wxTextCtrl;

namespace timmy::gui {

// Presentation only: the owning frontend coordinates resources and Core sessions.
class BenchmarkDialog final : public wxDialog
{
public:
    struct Actions
    {
        std::function<void()> start, stop, next, retry, noResult, close;
        std::function<void(const wxString&)> pathChanged;
    };

    BenchmarkDialog(wxWindow* parent, Actions actions);
    wxString resultPath() const;
    void beginSession(const core::RecognitionBenchmarkSnapshot& snapshot);
    void setCase(std::size_t index);
    void showAttempt(const core::RecognitionBenchmarkAttempt& attempt);
    void showDetails(const core::RecognitionTranscript& transcript);
    void showError(const wxString& message);
    void stopped(const wxString& diagnostic);
    void updateControls(bool canStart, bool active, bool listening,
        bool stopping, bool awaiting, bool lastCase);

private:
    void setState(const wxString& state, const wxString& instruction, bool accent = false);
    void wrapLabels();
    Actions actions_;
    wxTextCtrl* path_ = nullptr;
    wxButton *start_, *stop_, *next_, *retry_, *noResult_, *browse_;
    wxStaticText *summary_, *case_, *phrase_, *state_, *instruction_, *result_, *counts_;
    wxTextCtrl* details_ = nullptr;
    wxString summaryText_, caseText_, phraseText_, instructionText_;
    wxString stateText_{"Ready to start"}, resultText_{"Last result: -"};
    unsigned passes_ = 0, failures_ = 0, noResults_ = 0;
    bool finished_ = false;
};
} // namespace timmy::gui
