/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "benchmark_dialog.h"

#include <wx/button.h>
#include <wx/collpane.h>
#include <wx/filedlg.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <algorithm>
#include <utility>

namespace timmy::gui {
namespace {
wxString displayPath(const std::filesystem::path& path)
{
#ifdef _WIN32
    return wxString(path.wstring());
#else
    return wxString::FromUTF8(path.string());
#endif
}
}

BenchmarkDialog::BenchmarkDialog(wxWindow* parent, Actions actions)
    : wxDialog(parent, wxID_ANY, "Recognition benchmark", wxDefaultPosition,
          wxSize(720, 650), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      actions_(std::move(actions))
{
    SetName("benchmark.window");
    SetMinSize(wxSize(520, 480));
    auto* outer = new wxBoxSizer(wxVERTICAL);
    auto* content = new wxScrolledWindow(this);
    content->SetScrollRate(0, 10);
    auto* body = new wxBoxSizer(wxVERTICAL);
    const auto label = [&](const wxString& text, const char* name) {
        auto* control = new wxStaticText(content, wxID_ANY, text,
            wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
        control->SetName(name);
        control->SetMinSize(wxSize(1, -1));
        body->Add(control, 0, wxEXPAND | wxBOTTOM, 8);
        return control;
    };
    label("Corpus: TIMMY guided live recognition / "
        + wxString::FromUTF8(core::liveBenchmarkCorpusVersion), "benchmark.corpus");
    label("JSONL result file (append):", "benchmark.pathLabel");
    auto* fileRow = new wxBoxSizer(wxHORIZONTAL);
    path_ = new wxTextCtrl(content, wxID_ANY);
    path_->SetName("benchmark.path");
    browse_ = new wxButton(content, wxID_ANY, "Browse...");
    browse_->SetName("benchmark.browse");
    fileRow->Add(path_, 1, wxEXPAND | wxRIGHT, 6);
    fileRow->Add(browse_);
    body->Add(fileRow, 0, wxEXPAND | wxBOTTOM, 8);
    path_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { actions_.pathChanged(resultPath()); });
    browse_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (!browse_->IsEnabled()) return;
        wxFileDialog picker(this, "Select benchmark result file",
            wxStandardPaths::Get().GetDocumentsDir(), "timmy-live-v1.jsonl",
            "JSONL files (*.jsonl)|*.jsonl", wxFD_SAVE);
        if (picker.ShowModal() == wxID_OK) path_->SetValue(picker.GetPath());
    });
    summaryText_ = "Engine / model / language / profile will be frozen at Start.";
    summary_ = label(summaryText_, "benchmark.summary");
    state_ = label("Ready to start", "benchmark.state");
    state_->SetFont(state_->GetFont().Bold());
    instruction_ = label("", "benchmark.instruction");
    caseText_ = "Case / progress: -";
    case_ = label(caseText_, "benchmark.case");
    phraseText_ = "Phrase to speak: -";
    phrase_ = label(phraseText_, "benchmark.phrase");
    phrase_->SetFont(phrase_->GetFont().Larger().Bold());
    result_ = label("Last result: -", "benchmark.result");
    result_->SetFont(result_->GetFont().Bold());
    counts_ = label("PASS: 0   FAIL: 0   No result: 0", "benchmark.counts");
    counts_->SetToolTip("PASS and FAIL count all recorded attempts, including retries. No result is a subset of those attempts.");
    auto* details = new wxCollapsiblePane(content, wxID_ANY, "Details",
        wxDefaultPosition, wxDefaultSize, wxCP_NO_TLW_RESIZE);
    details->SetName("benchmark.details");
    auto* detailsSizer = new wxBoxSizer(wxVERTICAL);
    details_ = new wxTextCtrl(details->GetPane(), wxID_ANY, "No attempt yet.",
        wxDefaultPosition, wxSize(-1, 150), wxTE_MULTILINE | wxTE_READONLY);
    details_->SetName("benchmark.detailsText");
    detailsSizer->Add(details_, 1, wxEXPAND);
    details->GetPane()->SetSizer(detailsSizer);
    body->Add(details, 0, wxEXPAND);
    details->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED, [content](wxCollapsiblePaneEvent&) {
        content->Layout();
        content->FitInside();
    });
    auto* padding = new wxBoxSizer(wxVERTICAL);
    padding->Add(body, 1, wxEXPAND | wxALL, 12);
    content->SetSizer(padding);
    outer->Add(content, 1, wxEXPAND);
    auto* session = new wxBoxSizer(wxHORIZONTAL);
    auto* navigation = new wxBoxSizer(wxHORIZONTAL);
    const auto button = [&](wxBoxSizer* row, const char* text, const char* name,
                            const std::function<void()>& action) {
        auto* control = new wxButton(this, wxID_ANY, text);
        control->SetName(name);
        control->Bind(wxEVT_BUTTON, [action](wxCommandEvent&) { action(); });
        row->Add(control, 0, wxRIGHT, 6);
        return control;
    };
    start_ = button(session, "Start benchmark", "benchmark.start", actions_.start);
    stop_ = button(session, "Stop benchmark", "benchmark.stop", actions_.stop);
    next_ = button(navigation, "Next", "benchmark.next", actions_.next);
    retry_ = button(navigation, "Retry", "benchmark.retry", actions_.retry);
    noResult_ = button(navigation, "No result", "benchmark.noResult", actions_.noResult);
    noResult_->SetToolTip("Record an explicit attempt when no final ASR result arrives. There is no automatic timeout.");
    navigation->AddStretchSpacer();
    button(navigation, "Close", "benchmark.close", actions_.close);
    outer->Add(session, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    outer->Add(navigation, 0, wxEXPAND | wxALL, 12);
    SetSizer(outer);
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        if (!event.CanVeto()) { event.Skip(); return; }
        event.Veto();
        actions_.close();
    });
    content->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) { wrapLabels(); event.Skip(); });
    updateControls(true, false, false, false, false, false);
    CentreOnParent();
}

wxString BenchmarkDialog::resultPath() const { return path_->GetValue(); }

void BenchmarkDialog::wrapLabels()
{
    const int width = std::max(100, phrase_->GetParent()->GetClientSize().x - 24);
    const std::pair<wxStaticText*, wxString> labels[] = {
        {summary_, summaryText_}, {case_, caseText_}, {phrase_, phraseText_},
        {instruction_, instructionText_}, {state_, stateText_}, {result_, resultText_}};
    for (const auto& [control, text] : labels)
    {
        control->SetLabel(text);
        control->Wrap(width);
        // Wrapping before the first layout can leave a tall cached minimum on
        // native ports. Recompute the height from the final wrapped lines.
        control->SetMinSize(wxSize(1,
            static_cast<int>(control->GetLabel().Freq('\n') + 1) * control->GetCharHeight() + 2));
    }
    phrase_->GetParent()->Layout();
    static_cast<wxScrolledWindow*>(phrase_->GetParent())->FitInside();
}

void BenchmarkDialog::setState(const wxString& state, const wxString& instruction, bool accent)
{
    stateText_ = state;
    state_->SetForegroundColour(accent ? wxColour(100, 165, 230) : GetForegroundColour());
    instructionText_ = instruction;
    wrapLabels();
}

void BenchmarkDialog::beginSession(const core::RecognitionBenchmarkSnapshot& snapshot)
{
    passes_ = failures_ = noResults_ = 0;
    finished_ = false;
    resultText_ = "Last result: -";
    result_->SetForegroundColour(GetForegroundColour());
    counts_->SetLabel("PASS: 0   FAIL: 0   No result: 0");
    summaryText_ = "Frozen engine: " + wxString::FromUTF8(snapshot.engineId)
        + "\nModel: " + displayPath(snapshot.configuration.modelResourcePath)
        + "\nLanguage: " + wxString::FromUTF8(snapshot.configuration.preferredLanguage.value_or("automatic"))
        + " | Profile: " + wxString::FromUTF8(snapshot.commandProfile.id);
    wrapLabels();
}

void BenchmarkDialog::setCase(std::size_t index)
{
    const auto& test = core::liveBenchmarkCases().at(index);
    caseText_ = wxString::FromUTF8("Case / progress: " + test.id + " / " + test.category
        + " (" + std::to_string(index + 1) + "/" + std::to_string(core::liveBenchmarkCases().size()) + ")");
    phraseText_ = "Phrase to speak: " + wxString::FromUTF8(test.phrase);
    details_->ChangeValue("Waiting for this attempt's result.");
    wrapLabels();
}

void BenchmarkDialog::showAttempt(const core::RecognitionBenchmarkAttempt& attempt)
{
    if (attempt.passed) ++passes_; else ++failures_;
    const bool noResult = attempt.outcome == core::BenchmarkRecognitionOutcome::noResult;
    if (noResult) { ++noResults_; details_->ChangeValue("No final ASR result. No transcript or timings available."); }
    resultText_ = wxString::FromUTF8("Last result: " + attempt.caseId + " attempt "
        + std::to_string(attempt.attempt) + (attempt.passed ? " PASS" : " FAIL")
        + (noResult ? " (No ASR result)" : ""));
    result_->SetForegroundColour(attempt.passed ? wxColour(70, 180, 100) : wxColour(230, 90, 90));
    counts_->SetLabel(wxString::Format("PASS: %u   FAIL: %u   No result: %u", passes_, failures_, noResults_));
    wrapLabels();
}

void BenchmarkDialog::showDetails(const core::RecognitionTranscript& transcript)
{
    const auto timing = [](const std::optional<double>& value) {
        return value ? wxString::Format("%.3f s", *value) : wxString("Unavailable");
    };
    details_->ChangeValue("Raw transcript: " + wxString::FromUTF8(transcript.rawTranscript)
        + "\nNormalized transcript: " + wxString::FromUTF8(transcript.normalizedTranscript)
        + "\nAssembly: " + wxString::FromUTF8(transcript.assemblyText)
        + "\nSemantic command: " + (transcript.semanticCommand
            ? wxString::FromUTF8(command::describe(*transcript.semanticCommand)) : wxString("None"))
        + "\nValidation: " + (transcript.validation.accepted ? wxString("accepted") : wxString("rejected"))
        + " " + wxString::FromUTF8(transcript.validation.diagnostic)
        + "\nRendered command: " + wxString::FromUTF8(transcript.rendering.text.value_or("None"))
        + " " + wxString::FromUTF8(transcript.rendering.diagnostic)
        + "\nDetected language: " + wxString::FromUTF8(transcript.detectedLanguage.value_or("Unavailable"))
        + "\nAudio duration: " + timing(transcript.metrics.audioDurationSeconds)
        + "\nInference duration: " + timing(transcript.metrics.inferenceDurationSeconds));
}

void BenchmarkDialog::showError(const wxString& message)
{
    resultText_ = "Last result: " + message;
    result_->SetForegroundColour(wxColour(230, 90, 90));
    wrapLabels();
}

void BenchmarkDialog::stopped(const wxString& diagnostic)
{
    if (!diagnostic.empty()) showError(diagnostic);
    setState(finished_ && diagnostic.empty() ? "Finished" : "Stopped",
        "Press Start benchmark to begin a new session, or Close.");
}

void BenchmarkDialog::updateControls(bool canStart, bool active, bool listening,
    bool stopping, bool awaiting, bool lastCase)
{
    start_->Enable(canStart && !active);
    stop_->Enable(active && listening && !stopping);
    next_->Enable(active && listening && !stopping && !awaiting && !lastCase);
    retry_->Enable(active && listening && !stopping && !awaiting);
    noResult_->Enable(active && listening && !stopping && awaiting);
    path_->Enable(canStart && !active);
    browse_->Enable(canStart && !active);
    if (active)
    {
        if (stopping) setState("Stopping recognition...", "Wait for recognition resources to stop.", true);
        else if (awaiting) {
            finished_ = false;
            setState("Listening / waiting for speech end and recognition",
                "Speak the displayed phrase once. Wait for the recognition result.\n"
                "If no final ASR result arrives, press No result.", true);
        }
        else if (lastCase) {
            finished_ = true;
            setState("Finished", "Review the last result. Choose Retry to repeat it, or Stop benchmark / Close.");
        }
        else setState("Waiting for operator decision", "Review the result, then choose Next or Retry.", true);
    }
    else if (state_->GetLabel() == "Ready to start")
        setState("Ready to start", canStart ? "Select a JSONL file, then press Start benchmark to begin."
            : "Stop main-window recognition before starting a benchmark.");
}
} // namespace timmy::gui
