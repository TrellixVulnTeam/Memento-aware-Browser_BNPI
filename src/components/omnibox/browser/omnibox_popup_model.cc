// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/omnibox/browser/omnibox_popup_model.h"

#include <algorithm>

#include "base/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_match_type.h"
#include "components/omnibox/browser/omnibox_client.h"
#include "components/omnibox/browser/omnibox_edit_controller.h"
#include "components/omnibox/browser/omnibox_field_trial.h"
#include "components/omnibox/browser/omnibox_popup_view.h"
#include "components/omnibox/browser/omnibox_prefs.h"
#include "components/strings/grit/components_strings.h"
#include "third_party/icu/source/common/unicode/ubidi.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/geometry/rect.h"

#if !defined(OS_ANDROID) && !defined(OS_IOS)
#include "components/omnibox/browser/vector_icons.h"  // nogncheck
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/vector_icon_types.h"
#endif

///////////////////////////////////////////////////////////////////////////////
// OmniboxPopupModel::Selection

bool OmniboxPopupModel::Selection::operator==(const Selection& b) const {
  return line == b.line && state == b.state;
}

bool OmniboxPopupModel::Selection::operator!=(const Selection& b) const {
  return !operator==(b);
}

bool OmniboxPopupModel::Selection::operator<(const Selection& b) const {
  if (line == b.line)
    return state < b.state;

  return line < b.line;
}

bool OmniboxPopupModel::Selection::IsChangeToKeyword(Selection from) const {
  return state == KEYWORD && from.state != KEYWORD;
}

bool OmniboxPopupModel::Selection::IsButtonFocused() const {
  return state != NORMAL && state != KEYWORD;
}

///////////////////////////////////////////////////////////////////////////////
// OmniboxPopupModel

const size_t OmniboxPopupModel::kNoMatch = static_cast<size_t>(-1);

OmniboxPopupModel::OmniboxPopupModel(OmniboxPopupView* popup_view,
                                     OmniboxEditModel* edit_model,
                                     PrefService* pref_service)
    : view_(popup_view),
      edit_model_(edit_model),
      pref_service_(pref_service),
      selection_(kNoMatch, NORMAL) {
  edit_model->set_popup_model(this);
}

OmniboxPopupModel::~OmniboxPopupModel() = default;

// static
void OmniboxPopupModel::ComputeMatchMaxWidths(int contents_width,
                                              int separator_width,
                                              int description_width,
                                              int available_width,
                                              bool description_on_separate_line,
                                              bool allow_shrinking_contents,
                                              int* contents_max_width,
                                              int* description_max_width) {
  available_width = std::max(available_width, 0);
  *contents_max_width = std::min(contents_width, available_width);
  *description_max_width = std::min(description_width, available_width);

  // If the description is empty, or the contents and description are on
  // separate lines, each can get the full available width.
  if (!description_width || description_on_separate_line)
    return;

  // If we want to display the description, we need to reserve enough space for
  // the separator.
  available_width -= separator_width;
  if (available_width < 0) {
    *description_max_width = 0;
    return;
  }

  if (contents_width + description_width > available_width) {
    if (allow_shrinking_contents) {
      // Try to split the available space fairly between contents and
      // description (if one wants less than half, give it all it wants and
      // give the other the remaining space; otherwise, give each half).
      // However, if this makes the contents too narrow to show a significant
      // amount of information, give the contents more space.
      *contents_max_width = std::max(
          (available_width + 1) / 2, available_width - description_width);

      const int kMinimumContentsWidth = 300;
      *contents_max_width = std::min(
          std::min(std::max(*contents_max_width, kMinimumContentsWidth),
                   contents_width),
          available_width);
    }

    // Give the description the remaining space, unless this makes it too small
    // to display anything meaningful, in which case just hide the description
    // and let the contents take up the whole width.
    *description_max_width =
        std::min(description_width, available_width - *contents_max_width);
    const int kMinimumDescriptionWidth = 75;
    if (*description_max_width <
        std::min(description_width, kMinimumDescriptionWidth)) {
      *description_max_width = 0;
      // Since we're not going to display the description, the contents can have
      // the space we reserved for the separator.
      available_width += separator_width;
      *contents_max_width = std::min(contents_width, available_width);
    }
  }
}

bool OmniboxPopupModel::IsOpen() const {
  return view_->IsOpen();
}

void OmniboxPopupModel::SetSelection(Selection new_selection,
                                     bool reset_to_default,
                                     bool force_update_ui) {
  if (result().empty())
    return;

  // Cancel the query so the matches don't change on the user.
  autocomplete_controller()->Stop(false);

  if (new_selection == selection_ && !force_update_ui)
    return;  // Nothing else to do.

  // We need to update selection before notifying any views, as they will query
  // selection_ to update themselves.
  const Selection old_selection = selection_;
  selection_ = new_selection;
  view_->OnSelectionChanged(old_selection, selection_);

  if (selection_.line == kNoMatch)
    return;

  const AutocompleteMatch& match = result().match_at(selection_.line);
  DCHECK((selection_.state != KEYWORD) || match.associated_keyword.get());
  if (selection_.IsButtonFocused()) {
    old_focused_url_ = match.destination_url;
    edit_model_->SetAccessibilityLabel(match);
    // TODO(tommycli): Fold the focus hint into view_->OnSelectionChanged().
    // Caveat: We must update the accessibility label before notifying the View.
    view_->ProvideButtonFocusHint(selected_line());
  }

  base::string16 keyword;
  bool is_keyword_hint;
  TemplateURLService* service = edit_model_->client()->GetTemplateURLService();
  match.GetKeywordUIState(service, &keyword, &is_keyword_hint);

  if (selection_.state == HEADER_BUTTON_FOCUSED) {
    // If the new selection is a Header, the temporary text is an empty string.
    edit_model_->OnPopupDataChanged(base::string16(),
                                    /*is_temporary_text=*/true, keyword,
                                    is_keyword_hint);
  } else if (old_selection.line != selection_.line ||
             old_selection.state == HEADER_BUTTON_FOCUSED) {
    // Otherwise, only update the edit model for line number changes, or
    // when the old selection was a Header. Updating the edit model for every
    // state change breaks keyword mode.
    if (reset_to_default) {
      edit_model_->OnPopupDataChanged(match.inline_autocompletion,
                                      /*is_temporary_text=*/false, keyword,
                                      is_keyword_hint);
    } else {
      edit_model_->OnPopupDataChanged(match.fill_into_edit,
                                      /*is_temporary_text=*/true, keyword,
                                      is_keyword_hint);
    }
  }
}

void OmniboxPopupModel::ResetToInitialState() {
  size_t new_line = result().default_match() ? 0 : kNoMatch;
  SetSelection(Selection(new_line, NORMAL), /*reset_to_default=*/true);
  view_->OnDragCanceled();
}

void OmniboxPopupModel::TryDeletingLine(size_t line) {
  // When called with line == selected_line(), we could use
  // GetInfoForCurrentText() here, but it seems better to try and delete the
  // actual selection, rather than any "in progress, not yet visible" one.
  if (line == kNoMatch)
    return;

  // Cancel the query so the matches don't change on the user.
  autocomplete_controller()->Stop(false);

  const AutocompleteMatch& match = result().match_at(line);
  if (match.SupportsDeletion()) {
    // Try to preserve the selection even after match deletion.
    size_t old_selected_line = selected_line();

    // This will synchronously notify both the edit and us that the results
    // have changed, causing both to revert to the default match.
    autocomplete_controller()->DeleteMatch(match);

    // Clamp the old selection to the new size of result(), since there may be
    // fewer results now.
    if (old_selected_line != kNoMatch)
      old_selected_line = std::min(line, result().size() - 1);

    // Move the selection to the next choice after the deleted one.
    // SetSelectedLine() will clamp to take care of the case where we deleted
    // the last item.
    // TODO(pkasting): Eventually the controller should take care of this
    // before notifying us, reducing flicker.  At that point the check for
    // deletability can move there too.
    SetSelection(Selection(old_selected_line, NORMAL), false, true);
  }
}

bool OmniboxPopupModel::IsStarredMatch(const AutocompleteMatch& match) const {
  auto* bookmark_model = edit_model_->client()->GetBookmarkModel();
  return bookmark_model && bookmark_model->IsBookmarked(match.destination_url);
}

bool OmniboxPopupModel::SelectionOnInitialLine() const {
  size_t initial_line = result().default_match() ? 0 : kNoMatch;
  return selected_line() == initial_line;
}

void OmniboxPopupModel::OnResultChanged() {
  rich_suggestion_bitmaps_.clear();
  const AutocompleteResult& result = this->result();
  size_t old_selected_line = selected_line();

  if (result.default_match()) {
    Selection selection(0, selected_line_state());

    // If selected line state was |BUTTON_FOCUSED| and nothing has changed,
    // leave it.
    const bool has_focused_match =
        selection.state == BUTTON_FOCUSED &&
        result.match_at(selection.line).has_tab_match;
    const bool has_changed =
        selection.line != old_selected_line ||
        result.match_at(selection.line).destination_url != old_focused_url_;

    if (!has_focused_match || has_changed) {
      selection.state = NORMAL;
    }
    selection_ = selection;
  } else {
    selection_ = Selection(kNoMatch, NORMAL);
  }

  bool popup_was_open = view_->IsOpen();
  view_->UpdatePopupAppearance();
  if (view_->IsOpen() != popup_was_open)
    edit_model_->controller()->OnPopupVisibilityChanged();
}

const SkBitmap* OmniboxPopupModel::RichSuggestionBitmapAt(
    int result_index) const {
  const auto iter = rich_suggestion_bitmaps_.find(result_index);
  if (iter == rich_suggestion_bitmaps_.end()) {
    return nullptr;
  }
  return &iter->second;
}

void OmniboxPopupModel::SetRichSuggestionBitmap(int result_index,
                                                const SkBitmap& bitmap) {
  rich_suggestion_bitmaps_[result_index] = bitmap;
  view_->UpdatePopupAppearance();
}

// Android and iOS have their own platform-specific icon logic.
#if !defined(OS_ANDROID) && !defined(OS_IOS)
gfx::Image OmniboxPopupModel::GetMatchIcon(const AutocompleteMatch& match,
                                           SkColor vector_icon_color) {
  gfx::Image extension_icon =
      edit_model_->client()->GetIconIfExtensionMatch(match);
  // Extension icons are the correct size for non-touch UI but need to be
  // adjusted to be the correct size for touch mode.
  if (!extension_icon.IsEmpty())
    return edit_model_->client()->GetSizedIcon(extension_icon);

  // Get the favicon for navigational suggestions.
  if (!AutocompleteMatch::IsSearchType(match.type) &&
      match.type != AutocompleteMatchType::DOCUMENT_SUGGESTION) {
    // Because the Views UI code calls GetMatchIcon in both the layout and
    // painting code, we may generate multiple OnFaviconFetched callbacks,
    // all run one after another. This seems to be harmless as the callback
    // just flips a flag to schedule a repaint. However, if it turns out to be
    // costly, we can optimize away the redundant extra callbacks.
    gfx::Image favicon = edit_model_->client()->GetFaviconForPageUrl(
        match.destination_url,
        base::BindOnce(&OmniboxPopupModel::OnFaviconFetched,
                       weak_factory_.GetWeakPtr(), match.destination_url));

    // Extension icons are the correct size for non-touch UI but need to be
    // adjusted to be the correct size for touch mode.
    if (!favicon.IsEmpty())
      return edit_model_->client()->GetSizedIcon(favicon);
  }

  const auto& vector_icon_type = match.GetVectorIcon(IsStarredMatch(match));

  return edit_model_->client()->GetSizedIcon(vector_icon_type,
                                             vector_icon_color);
}
#endif  // !defined(OS_ANDROID) && !defined(OS_IOS)

bool OmniboxPopupModel::SelectedLineIsTabSwitchSuggestion() {
  return selected_line() != kNoMatch &&
         result().match_at(selected_line()).IsTabSwitchSuggestion();
}

std::vector<OmniboxPopupModel::Selection>
OmniboxPopupModel::GetAllAvailableSelectionsSorted(Direction direction,
                                                   Step step) const {
  // First enumerate all the accessible states based on |direction| and |step|,
  // as well as enabled feature flags. This doesn't mean each match will have
  // all of these states - just that it's possible to get there, if available.
  std::vector<LineState> all_states;
  if (step == kWholeLine || step == kAllLines) {
    // In the case of whole-line stepping, only the NORMAL state is accessible.
    all_states.push_back(NORMAL);
  } else {
    // Arrow keys should never reach the header controls.
    if (step == kStateOrLine)
      all_states.push_back(HEADER_BUTTON_FOCUSED);

    all_states.push_back(NORMAL);

    if (OmniboxFieldTrial::IsSuggestionButtonRowEnabled()) {
      // The button row experiment makes things simple. We no longer access
      // keyword mode by arrow or tab button in this case.
      all_states.push_back(FOCUSED_BUTTON_KEYWORD);
      all_states.push_back(FOCUSED_BUTTON_TAB_SWITCH);
      all_states.push_back(FOCUSED_BUTTON_PEDAL);
    } else {
      // Keyword mode is only accessible by Tabbing forward. If experimental
      // keyword mode is enabled, Right arrow also works.
      if (direction == kForward) {
        if (step == kStateOrLine ||
            (step == kStateOrNothing &&
             OmniboxFieldTrial::IsExperimentalKeywordModeEnabled())) {
          all_states.push_back(KEYWORD);
        }
      }

      all_states.push_back(BUTTON_FOCUSED);
    }
  }
  DCHECK(std::is_sorted(all_states.begin(), all_states.end()))
      << "This algorithm depends on a sorted list of line states.";

  // Now, for each accessible line, add all the available line states to a list.
  std::vector<Selection> available_selections;
  {
    auto add_available_line_states_for_line = [&](size_t line) {
      for (LineState state : all_states) {
        Selection selection(line, state);
        if (IsControlPresentOnMatch(selection))
          available_selections.push_back(selection);
      }
    };

    if (step == kStateOrNothing) {
      // Confine kStateOrNothing (right / left arrow) to the current line.
      add_available_line_states_for_line(selection_.line);
    } else {
      // Allow other steps to go to any line.
      for (size_t line = 0; line < result().size(); ++line) {
        add_available_line_states_for_line(line);
      }
    }
  }
  DCHECK(
      std::is_sorted(available_selections.begin(), available_selections.end()))
      << "This algorithm depends on a sorted list of available selections.";
  return available_selections;
}

OmniboxPopupModel::Selection OmniboxPopupModel::GetNextSelection(
    Direction direction,
    Step step) const {
  if (result().empty()) {
    return selection_;
  }

  // Implementing this was like a Google Interview Problem. It was always a
  // tough problem to handle all the cases, but has gotten much harder since
  // we can now hide whole rows from view by collapsing sections.
  //
  // The only sane thing to do is to first enumerate all available selections.
  // Other approaches I've tried all end up being a jungle of branching code.
  // It's not necessarily optimal to generate this list for each keypress, but
  // in practice it's only something like ~10 elements long, and makes the code
  // easy to reason about.
  std::vector<Selection> all_available_selections =
      GetAllAvailableSelectionsSorted(direction, step);

  if (all_available_selections.empty())
    return selection_;

  // Handle the simple case of just getting the first or last element.
  if (step == kAllLines) {
    return direction == kForward ? all_available_selections.back()
                                 : all_available_selections.front();
  }

  // We don't allow wrapping for kStateOrNothing, it's just a UI choice.
  bool wrap_allowed = step != kStateOrNothing;
  if (direction == kForward) {
    // To go forward, we want to change to the first selection that's larger
    // than the current |selection_|, and std::upper_bound() does just that.
    const auto next =
        std::upper_bound(all_available_selections.begin(),
                         all_available_selections.end(), selection_);

    // If we can't find any selections larger than the current |selection_|,
    // wrap if allowed, otherwise return the current selection.
    if (next == all_available_selections.end())
      return wrap_allowed ? all_available_selections.front() : selection_;

    // Normal case where we found the next selection.
    return *next;
  } else if (direction == kBackward) {
    // To go backwards, decrement one from std::lower_bound(), which finds the
    // current selection. I didn't use std::find() here, because
    // std::lower_bound() can gracefully handle the case where |selection_| is
    // no longer within the list of available selections.
    const auto current =
        std::lower_bound(all_available_selections.begin(),
                         all_available_selections.end(), selection_);

    // If the current selection is the first one, wrap if allowed.
    if (current == all_available_selections.begin())
      return wrap_allowed ? all_available_selections.back() : selection_;

    // Decrement one from the current selection.
    return *(current - 1);
  }

  NOTREACHED();
  return selection_;
}

OmniboxPopupModel::Selection OmniboxPopupModel::StepSelection(
    Direction direction,
    Step step) {
  // This block steps the popup model, with special consideration
  // for existing keyword logic in the edit model, where AcceptKeyword and
  // ClearKeyword must be called before changing the selected line.
  const auto old_selection = selection();
  const auto new_selection = GetNextSelection(direction, step);
  if (new_selection.IsChangeToKeyword(old_selection)) {
    edit_model()->AcceptKeyword(metrics::OmniboxEventProto::TAB);
  } else if (old_selection.IsChangeToKeyword(new_selection)) {
    edit_model()->ClearKeyword();
  }
  SetSelection(new_selection);
  return selection_;
}

bool OmniboxPopupModel::IsControlPresentOnMatch(Selection selection) const {
  if (selection.line >= result().size()) {
    return false;
  }
  const auto& match = result().match_at(selection.line);
  // Skip rows that are hidden because their header is collapsed, unless the
  // user is trying to focus the header itself (which is still shown).
  if (selection.state != HEADER_BUTTON_FOCUSED &&
      match.suggestion_group_id.has_value() && pref_service_ &&
      omnibox::IsSuggestionGroupIdHidden(pref_service_,
                                         match.suggestion_group_id.value())) {
    return false;
  }

  switch (selection.state) {
    case HEADER_BUTTON_FOCUSED: {
      // For the first match, if it a suggestion_group_id, then it has a header.
      if (selection.line == 0)
        return match.suggestion_group_id.has_value();

      // Otherwise, we only show headers that are distinct from the previous
      // match's header.
      const auto& previous_match = result().match_at(selection.line - 1);
      return match.suggestion_group_id.has_value() &&
             match.suggestion_group_id != previous_match.suggestion_group_id;
    }
    case NORMAL:
      return true;
    case KEYWORD:
      return match.associated_keyword != nullptr;
    case BUTTON_FOCUSED: {
      // TODO(orinj): Here is an opportunity to clean up the presentational
      //  logic that pkasting wanted to take out of AutocompleteMatch. The view
      //  should be driven by the model, so this is really the place to decide.
      //  In other words, this duplicates logic within OmniboxResultView.
      //  This is the proper place. OmniboxResultView should refer to here.

      // Buttons are suppressed for matches with an associated keyword.
      if (match.associated_keyword != nullptr)
        return false;
      if (match.ShouldShowTabMatchButton())
        return true;
      if (match.SupportsDeletion())
        return true;

      return false;
    }
    case FOCUSED_BUTTON_KEYWORD:
      return match.associated_keyword != nullptr;
    case FOCUSED_BUTTON_TAB_SWITCH:
      return match.has_tab_match;
    case FOCUSED_BUTTON_PEDAL:
      return match.pedal != nullptr;
    default:
      break;
  }
  NOTREACHED();
  return false;
}

bool OmniboxPopupModel::TriggerSelectionAction(Selection selection,
                                               base::TimeTicks timestamp) {
  // Early exit for the kNoMatch case. Also exits if the calling UI passes in
  // an invalid |selection|.
  if (selection.line >= result().size())
    return false;

  auto& match = result().match_at(selection.line);
  switch (selection.state) {
    case HEADER_BUTTON_FOCUSED:
      DCHECK(match.suggestion_group_id.has_value());
      omnibox::ToggleSuggestionGroupIdVisibility(
          pref_service_, match.suggestion_group_id.value());
      break;

    case FOCUSED_BUTTON_KEYWORD:
      // TODO(yoangela): Merge logic with mouse/gesture events in
      // OmniboxSuggestionButtonRowView::ButtonPressed - This case currently
      // is only reached by the call in OmniboxViewViews::HandleKeyEvent.
      if (edit_model()->is_keyword_hint()) {
        // TODO(yoangela): Rename once tab to keyword search is deprecated
        // Accept/ClearKeyword() has special conditions to handle searches
        // initiated by pressing Tab. Since tab+enter on this button behaves
        // more similar to a Tab than a Keyboard shortcut, it's easier
        // for now to treat it as a Tab entry method, otherwise the
        // autocomplete results will reset, leaving us in an unknown state.
        edit_model()->AcceptKeyword(metrics::OmniboxEventProto::TAB);
      }
      break;

    case FOCUSED_BUTTON_TAB_SWITCH:
      DCHECK(timestamp != base::TimeTicks());
      edit_model()->AcceptInput(WindowOpenDisposition::SWITCH_TO_TAB,
                                timestamp);
      break;

    case FOCUSED_BUTTON_PEDAL:
      DCHECK(timestamp != base::TimeTicks());
      DCHECK(match.pedal);
      edit_model()->ExecutePedal(match, timestamp);
      break;

    default:
      // Behavior is not yet supported, return false.
      return false;
  }

  return true;
}

base::string16 OmniboxPopupModel::GetAccessibilityLabelForCurrentSelection(
    const base::string16& match_text,
    int* label_prefix_length) {
  size_t line = selection_.line;
  DCHECK_NE(line, kNoMatch)
      << "GetAccessibilityLabelForCurrentSelection should never be called if "
         "the current selection is kNoMatch.";

  const AutocompleteMatch& match = result().match_at(line);

  int additional_message_id = 0;
  switch (selection_.state) {
    case HEADER_BUTTON_FOCUSED: {
      bool group_hidden = omnibox::IsSuggestionGroupIdHidden(
          pref_service_, match.suggestion_group_id.value());
      int message_id = group_hidden ? IDS_ACC_HEADER_SHOW_SUGGESTIONS_BUTTON
                                    : IDS_ACC_HEADER_HIDE_SUGGESTIONS_BUTTON;
      return l10n_util::GetStringFUTF16(
          message_id,
          result().GetHeaderForGroupId(match.suggestion_group_id.value()));
    }
    case NORMAL:
      if (IsControlPresentOnMatch(Selection(line, FOCUSED_BUTTON_TAB_SWITCH))) {
        additional_message_id = IDS_ACC_TAB_SWITCH_SUFFIX;
      }
      // Don't add an additional message for removable suggestions without
      // button focus, since they are relatively common.
      break;
    case KEYWORD:
      // TODO(tommycli): Investigate whether the accessibility messaging for
      // Keyword mode belongs here.
      break;
    case BUTTON_FOCUSED:
      if (IsControlPresentOnMatch(Selection(line, FOCUSED_BUTTON_TAB_SWITCH))) {
        additional_message_id = IDS_ACC_TAB_SWITCH_BUTTON_FOCUSED_PREFIX;
      } else if (match.SupportsDeletion()) {
        additional_message_id = IDS_ACC_REMOVE_SUGGESTION_FOCUSED_PREFIX;
      }
      break;
    case FOCUSED_BUTTON_KEYWORD:
      // TODO(yoangela): Add an accessibility message for the Keyword button
      // in the button-row UI configuration.
      break;
    case FOCUSED_BUTTON_TAB_SWITCH:
      additional_message_id = IDS_ACC_TAB_SWITCH_SUFFIX;
      break;
    case FOCUSED_BUTTON_PEDAL:
      // TODO(orinj): Add an accessibility message for the Pedal button
      // in the button-row UI configuration.
      break;
    default:
      break;
  }

  // If there's a button focused, we don't want the "n of m" message announced.
  size_t total_matches = selection_.IsButtonFocused() ? 0 : result().size();
  return AutocompleteMatchType::ToAccessibilityLabel(
      match, match_text, selection_.line, total_matches, additional_message_id,
      label_prefix_length);
}

void OmniboxPopupModel::OnFaviconFetched(const GURL& page_url,
                                         const gfx::Image& icon) {
  if (icon.IsEmpty() || !view_->IsOpen())
    return;

  // Notify all affected matches.
  for (size_t i = 0; i < result().size(); ++i) {
    auto& match = result().match_at(i);
    if (!AutocompleteMatch::IsSearchType(match.type) &&
        match.destination_url == page_url) {
      view_->OnMatchIconUpdated(i);
    }
  }
}
