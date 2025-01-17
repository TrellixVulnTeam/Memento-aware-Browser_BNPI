// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/input_method/assistive_window_controller.h"

#include <string>
#include <vector>

#include "ash/public/cpp/ash_pref_names.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/shell.h"
#include "ash/wm/window_util.h"
#include "base/metrics/user_metrics.h"
#include "chrome/browser/chromeos/input_method/assistive_window_controller_delegate.h"
#include "chrome/browser/chromeos/input_method/assistive_window_properties.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/settings_window_manager_chromeos.h"
#include "chrome/browser/ui/webui/settings/chromeos/constants/routes.mojom.h"
#include "components/prefs/pref_service.h"
#include "ui/base/ime/chromeos/ime_bridge.h"
#include "ui/views/widget/widget.h"

namespace chromeos {
namespace input_method {

namespace {
gfx::NativeView GetParentView() {
  gfx::NativeView parent = nullptr;

  aura::Window* active_window = ash::window_util::GetActiveWindow();
  // Use VirtualKeyboardContainer so that it works even with a system modal
  // dialog.
  parent = ash::Shell::GetContainer(
      active_window ? active_window->GetRootWindow()
                    : ash::Shell::GetRootWindowForNewWindows(),
      ash::kShellWindowId_VirtualKeyboardContainer);
  return parent;
}

constexpr base::TimeDelta kTtsShowDelay =
    base::TimeDelta::FromMilliseconds(100);

}  // namespace

TtsHandler::TtsHandler(Profile* profile) : profile_(profile) {}
TtsHandler::~TtsHandler() = default;

void TtsHandler::Announce(const std::string& text,
                          const base::TimeDelta delay) {
  const bool chrome_vox_enabled = profile_->GetPrefs()->GetBoolean(
      ash::prefs::kAccessibilitySpokenFeedbackEnabled);
  if (!chrome_vox_enabled)
    return;

  delay_timer_ = std::make_unique<base::OneShotTimer>();
  delay_timer_->Start(
      FROM_HERE, delay,
      base::BindOnce(&TtsHandler::Speak, base::Unretained(this), text));
}

void TtsHandler::OnTtsEvent(content::TtsUtterance* utterance,
                            content::TtsEventType event_type,
                            int char_index,
                            int length,
                            const std::string& error_message) {}

void TtsHandler::Speak(const std::string& text) {
  std::unique_ptr<content::TtsUtterance> utterance =
      content::TtsUtterance::Create(profile_);
  utterance->SetText(text);
  utterance->SetEventDelegate(this);

  auto* tts_controller = content::TtsController::GetInstance();
  tts_controller->Stop();
  tts_controller->SpeakOrEnqueue(std::move(utterance));
}

AssistiveWindowController::AssistiveWindowController(
    AssistiveWindowControllerDelegate* delegate,
    Profile* profile,
    std::unique_ptr<TtsHandler> tts_handler)
    : delegate_(delegate),
      tts_handler_(tts_handler ? std::move(tts_handler)
                               : std::make_unique<TtsHandler>(profile)) {}

AssistiveWindowController::~AssistiveWindowController() {
  if (suggestion_window_view_ && suggestion_window_view_->GetWidget())
    suggestion_window_view_->GetWidget()->RemoveObserver(this);
  if (undo_window_ && undo_window_->GetWidget())
    undo_window_->GetWidget()->RemoveObserver(this);
}

void AssistiveWindowController::InitSuggestionWindow() {
  if (suggestion_window_view_)
    return;
  // suggestion_window_view_ is deleted by DialogDelegateView::DeleteDelegate.
  suggestion_window_view_ =
      new ui::ime::SuggestionWindowView(GetParentView(), this);
  views::Widget* widget = suggestion_window_view_->InitWidget();
  widget->AddObserver(this);
  widget->Show();
}

void AssistiveWindowController::InitUndoWindow() {
  if (undo_window_)
    return;
  // undo_window is deleted by DialogDelegateView::DeleteDelegate.
  undo_window_ = new ui::ime::UndoWindow(GetParentView(), this);
  views::Widget* widget = undo_window_->InitWidget();
  widget->AddObserver(this);
  widget->Show();
}

void AssistiveWindowController::OnWidgetClosing(views::Widget* widget) {
  if (suggestion_window_view_ &&
      widget == suggestion_window_view_->GetWidget()) {
    widget->RemoveObserver(this);
    suggestion_window_view_ = nullptr;
  }
  if (undo_window_ && widget == undo_window_->GetWidget()) {
    widget->RemoveObserver(this);
    undo_window_ = nullptr;
  }
}

void AssistiveWindowController::AcceptSuggestion(
    const base::string16& suggestion) {
  tts_handler_->Announce(base::StringPrintf(
      "%s inserted.", base::UTF16ToUTF8(suggestion).c_str()));
  HideSuggestion();
}

void AssistiveWindowController::HideSuggestion() {
  suggestion_text_ = base::EmptyString16();
  confirmed_length_ = 0;
  if (suggestion_window_view_)
    suggestion_window_view_->GetWidget()->Close();
}

void AssistiveWindowController::SetBounds(const gfx::Rect& cursor_bounds) {
  if (suggestion_window_view_ && confirmed_length_ == 0)
    suggestion_window_view_->SetBounds(cursor_bounds);
  if (undo_window_)
    undo_window_->SetBounds(cursor_bounds);
}

void AssistiveWindowController::FocusStateChanged() {
  if (suggestion_window_view_)
    HideSuggestion();
  if (undo_window_)
    undo_window_->Hide();
}

void AssistiveWindowController::ShowSuggestion(const base::string16& text,
                                               const size_t confirmed_length,
                                               const bool show_tab) {
  if (!suggestion_window_view_)
    InitSuggestionWindow();
  suggestion_text_ = text;
  confirmed_length_ = confirmed_length;
  suggestion_window_view_->Show(text, confirmed_length, show_tab);
}

void AssistiveWindowController::ShowMultipleSuggestions(
    const std::vector<base::string16>& suggestions) {
  if (!suggestion_window_view_)
    InitSuggestionWindow();
  suggestion_window_view_->ShowMultipleCandidates(suggestions);
}

void AssistiveWindowController::HighlightSuggestionCandidate(int index) {
  if (suggestion_window_view_)
    suggestion_window_view_->HighlightCandidate(index);
  if (index < static_cast<int>(window_.candidates.size()))
    tts_handler_->Announce(base::StringPrintf(
        "%s. %d of %zu", base::UTF16ToUTF8(window_.candidates[index]).c_str(),
        index + 1, window_.candidates.size()));
}

base::string16 AssistiveWindowController::GetSuggestionText() const {
  return suggestion_text_;
}

size_t AssistiveWindowController::GetConfirmedLength() const {
  return confirmed_length_;
}

void AssistiveWindowController::SetAssistiveWindowProperties(
    const AssistiveWindowProperties& window) {
  window_ = window;
  switch (window.type) {
    case ui::ime::AssistiveWindowType::kUndoWindow:
      if (!undo_window_)
        InitUndoWindow();
      window.visible ? undo_window_->Show() : undo_window_->Hide();
      break;
    case ui::ime::AssistiveWindowType::kEmojiSuggestion:
      if (!suggestion_window_view_)
        InitSuggestionWindow();
      if (window_.visible) {
        suggestion_window_view_->ShowMultipleCandidates(window.candidates);
      } else {
        HideSuggestion();
      }
      break;
    case ui::ime::AssistiveWindowType::kNone:
      break;
  }
  tts_handler_->Announce(window.announce_string, kTtsShowDelay);
}

void AssistiveWindowController::AssistiveWindowButtonClicked(
    ui::ime::ButtonId id,
    ui::ime::AssistiveWindowType type) const {
  if (id == ui::ime::ButtonId::kSmartInputsSettingLink) {
    base::RecordAction(base::UserMetricsAction("OpenSmartInputsSettings"));
    chrome::SettingsWindowManager::GetInstance()->ShowOSSettings(
        ProfileManager::GetActiveUserProfile(),
        chromeos::settings::mojom::kSmartInputsSubpagePath);
  } else {
    delegate_->AssistiveWindowButtonClicked(id, type);
  }
}

ui::ime::SuggestionWindowView*
AssistiveWindowController::GetSuggestionWindowViewForTesting() {
  return suggestion_window_view_;
}

ui::ime::UndoWindow* AssistiveWindowController::GetUndoWindowForTesting()
    const {
  return undo_window_;
}

}  // namespace input_method
}  // namespace chromeos
