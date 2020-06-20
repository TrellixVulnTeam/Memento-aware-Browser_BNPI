// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_INPUT_METHOD_UI_SUGGESTION_WINDOW_VIEW_H_
#define CHROME_BROWSER_CHROMEOS_INPUT_METHOD_UI_SUGGESTION_WINDOW_VIEW_H_

#include <memory>

#include "base/macros.h"
#include "ui/chromeos/ui_chromeos_export.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"

namespace ui {
namespace ime {

class AssistiveDelegate;
class SettingLinkView;
class SuggestionView;

// SuggestionWindowView is the main container of the suggestion window UI.
class UI_CHROMEOS_EXPORT SuggestionWindowView
    : public views::BubbleDialogDelegateView {
 public:
  SuggestionWindowView(gfx::NativeView parent, AssistiveDelegate* delegate);
  ~SuggestionWindowView() override;
  views::Widget* InitWidget();

  // Hides.
  void Hide();

  // Shows suggestion text.
  void Show(const base::string16& text,
            const size_t confirmed_length,
            const bool show_tab);

  void ShowMultipleCandidates(const std::vector<base::string16>& candidates);

  void HighlightCandidate(int index);

  void SetBounds(const gfx::Rect& cursor_bounds);

 private:
  friend class SuggestionWindowViewTest;

  void MaybeInitializeSuggestionViews(size_t candidates_size);

  void MakeVisible();

  // views::BubbleDialogDelegateView:
  const char* GetClassName() const override;

  // The delegate to handle events from this class.
  AssistiveDelegate* delegate_;

  // The view containing all the suggestions.
  views::View* candidate_area_;

  // The view for rendering setting link, positioned below candidate_area_.
  SettingLinkView* setting_link_view_;

  // The items in view_
  std::vector<std::unique_ptr<SuggestionView>> candidate_views_;

  int selected_index_ = -1;

  DISALLOW_COPY_AND_ASSIGN(SuggestionWindowView);
};

}  // namespace ime
}  // namespace ui

#endif  // CHROME_BROWSER_CHROMEOS_INPUT_METHOD_UI_SUGGESTION_WINDOW_VIEW_H_
