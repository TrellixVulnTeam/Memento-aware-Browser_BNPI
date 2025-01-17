// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/input_method/assistive_suggester.h"

#include "ash/public/cpp/window_properties.h"
#include "base/feature_list.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/string_util.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "chromeos/constants/chromeos_features.h"
#include "chromeos/constants/chromeos_pref_names.h"
#include "components/exo/wm_helper.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "url/gurl.h"

namespace chromeos {

namespace {

const char kMaxTextBeforeCursorLength = 50;
const char kKeydown[] = "keydown";

const char* kAllowedDomainsForPersonalInfoSuggester[] = {
    "amazon.com", "facebook.com", "instagram.com", "netflix.com",
    "twitch.tv",  "twitter.com",  "youtube.com",
};

const char* kAllowedDomainsForEmojiSuggester[] = {
    "amazon.com", "facebook.com", "instagram.com", "netflix.com",
    "twitch.tv",  "twitter.com",  "youtube.com",
};

const char* kTestUrls[] = {
    "e14s-test",
    "simple_textarea.html",
};

const char* kAllowedAppsForPersonalInfoSuggester[] = {
    "pondihnpfglkihppmldjekdcjgbkpnen",  // Facebook messenger
    "cnbgggchhmkkdmeppjobngjoejnihlei",  // Google Play Store
    "mmfbcljfglbokpmkimbfghdkjmjhdgbg",  // System text
};

const char* kAllowedAppsForEmojiSuggester[] = {
    "pondihnpfglkihppmldjekdcjgbkpnen",  // Facebook messenger
    "cnbgggchhmkkdmeppjobngjoejnihlei",  // Google Play Store
    "mmfbcljfglbokpmkimbfghdkjmjhdgbg",  // System text
};

void RecordAssistiveMatch(AssistiveType type) {
  base::UmaHistogramEnumeration("InputMethod.Assistive.Match", type);
}

void RecordAssistiveDisabled(AssistiveType type) {
  base::UmaHistogramEnumeration("InputMethod.Assistive.Disabled", type);
}

void RecordAssistiveCoverage(AssistiveType type) {
  base::UmaHistogramEnumeration("InputMethod.Assistive.Coverage", type);
}

void RecordAssistiveSuccess(AssistiveType type) {
  base::UmaHistogramEnumeration("InputMethod.Assistive.Success", type);
}

bool IsTestUrl(GURL url) {
  std::string filename = url.ExtractFileName();
  for (const char* test_url : kTestUrls) {
    if (base::CompareCaseInsensitiveASCII(filename, test_url) == 0) {
      return true;
    }
  }
  return false;
}

template <size_t N>
bool IsAllowedUrl(const char* (&allowedDomains)[N]) {
  Browser* browser = chrome::FindLastActive();
  if (browser && browser->window()->IsActive()) {
    GURL url = browser->tab_strip_model()
                   ->GetActiveWebContents()
                   ->GetLastCommittedURL();
    if (IsTestUrl(url))
      return true;
    for (size_t i = 0; i < N; i++) {
      if (url.DomainIs(allowedDomains[i])) {
        return true;
      }
    }
  }
  return false;
}

template <size_t N>
bool IsAllowedApp(const char* (&allowedApps)[N]) {
  // WMHelper is not available in Chrome on Linux.
  auto* wm_helper = exo::WMHelper::GetInstance();
  auto* window = wm_helper ? wm_helper->GetActiveWindow() : nullptr;
  if (!window)
    return false;

  // TODO(crbug/1094113): improve to cover more scenarios such as chat heads.
  const std::string* app_id = window->GetProperty(ash::kAppIDKey);
  if (app_id) {
    for (size_t i = 0; i < N; i++) {
      if (strcmp(app_id->c_str(), allowedApps[i]) == 0) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

AssistiveSuggester::AssistiveSuggester(InputMethodEngine* engine,
                                       Profile* profile)
    : profile_(profile),
      personal_info_suggester_(engine, profile),
      emoji_suggester_(engine) {}

bool AssistiveSuggester::IsAssistiveFeatureEnabled() {
  return IsAssistPersonalInfoEnabled() || IsEmojiSuggestAdditionEnabled();
}

bool AssistiveSuggester::IsAssistPersonalInfoEnabled() {
  return base::FeatureList::IsEnabled(
             chromeos::features::kAssistPersonalInfo) &&
         profile_->GetPrefs()->GetBoolean(prefs::kAssistPersonalInfoEnabled);
}

bool AssistiveSuggester::IsEmojiSuggestAdditionEnabled() {
  return base::FeatureList::IsEnabled(
             chromeos::features::kEmojiSuggestAddition) &&
         profile_->GetPrefs()->GetBoolean(prefs::kEmojiSuggestionEnabled);
}

bool AssistiveSuggester::IsActionEnabled(AssistiveType action) {
  switch (action) {
    case AssistiveType::kPersonalEmail:
    case AssistiveType::kPersonalAddress:
    case AssistiveType::kPersonalPhoneNumber:
    case AssistiveType::kPersonalName:
    case AssistiveType::kPersonalNumber:
    case AssistiveType::kPersonalFirstName:
    case AssistiveType::kPersonalLastName:
      // TODO: Use value from settings when crbug/1068457 is done.
      return IsAssistPersonalInfoEnabled();
      break;
    case AssistiveType::kEmoji:
      return IsEmojiSuggestAdditionEnabled();
    default:
      break;
  }
  return false;
}

void AssistiveSuggester::OnFocus(int context_id) {
  context_id_ = context_id;
  personal_info_suggester_.OnFocus(context_id_);
  emoji_suggester_.OnFocus(context_id_);
}

void AssistiveSuggester::OnBlur() {
  context_id_ = -1;
  personal_info_suggester_.OnBlur();
  emoji_suggester_.OnBlur();
}

bool AssistiveSuggester::OnKeyEvent(
    const InputMethodEngineBase::KeyboardEvent& event) {
  if (context_id_ == -1)
    return false;

  // We only track keydown event because the suggesting action is triggered by
  // surrounding text change, which is triggered by a keydown event. As a
  // result, the next key event after suggesting would be a keyup event of the
  // same key, and that event is meaningless to us.
  if (IsSuggestionShown() && event.type == kKeydown) {
    SuggestionStatus status = current_suggester_->HandleKeyEvent(event);
    switch (status) {
      case SuggestionStatus::kAccept:
        RecordAssistiveSuccess(current_suggester_->GetProposeActionType());
        current_suggester_ = nullptr;
        return true;
      case SuggestionStatus::kDismiss:
        current_suggester_ = nullptr;
        return true;
      case SuggestionStatus::kBrowsing:
        return true;
      default:
        break;
    }
  }
  return false;
}

void AssistiveSuggester::RecordAssistiveMatchMetrics(const base::string16& text,
                                                     int cursor_pos,
                                                     int anchor_pos) {
  int len = static_cast<int>(text.length());
  if (cursor_pos > 0 && cursor_pos <= len && cursor_pos == anchor_pos &&
      (cursor_pos == len || base::IsAsciiWhitespace(text[cursor_pos]))) {
    int start_pos = std::max(0, cursor_pos - kMaxTextBeforeCursorLength);
    base::string16 text_before_cursor =
        text.substr(start_pos, cursor_pos - start_pos);
    AssistiveType action = ProposeAssistiveAction(text_before_cursor);
    if (action != AssistiveType::kGenericAction) {
      RecordAssistiveMatch(action);
      if (!IsActionEnabled(action))
        RecordAssistiveDisabled(action);
    }
  }
}

bool AssistiveSuggester::OnSurroundingTextChanged(const base::string16& text,
                                                  int cursor_pos,
                                                  int anchor_pos) {
  if (context_id_ == -1)
    return false;

  if (!Suggest(text, cursor_pos, anchor_pos)) {
    DismissSuggestion();
  }
  return IsSuggestionShown();
}

bool AssistiveSuggester::Suggest(const base::string16& text,
                                 int cursor_pos,
                                 int anchor_pos) {
  int len = static_cast<int>(text.length());
  if (cursor_pos > 0 && cursor_pos <= len && cursor_pos == anchor_pos &&
      (cursor_pos == len || base::IsAsciiWhitespace(text[cursor_pos])) &&
      (base::IsAsciiWhitespace(text[cursor_pos - 1]) || IsSuggestionShown())) {
    // |text| could be very long, we get at most |kMaxTextBeforeCursorLength|
    // characters before cursor.
    int start_pos = std::max(0, cursor_pos - kMaxTextBeforeCursorLength);
    base::string16 text_before_cursor =
        text.substr(start_pos, cursor_pos - start_pos);

    if (IsSuggestionShown()) {
      return current_suggester_->Suggest(text_before_cursor);
    }
    if (IsAssistPersonalInfoEnabled() &&
        (IsAllowedUrl(kAllowedDomainsForPersonalInfoSuggester) ||
         IsAllowedApp(kAllowedAppsForPersonalInfoSuggester)) &&
        personal_info_suggester_.Suggest(text_before_cursor)) {
      current_suggester_ = &personal_info_suggester_;
      if (personal_info_suggester_.IsFirstShown()) {
        RecordAssistiveCoverage(current_suggester_->GetProposeActionType());
      }
      return true;
    } else if (IsEmojiSuggestAdditionEnabled() &&
               (IsAllowedUrl(kAllowedDomainsForEmojiSuggester) ||
                IsAllowedApp(kAllowedAppsForEmojiSuggester)) &&
               emoji_suggester_.Suggest(text_before_cursor)) {
      current_suggester_ = &emoji_suggester_;
      RecordAssistiveCoverage(current_suggester_->GetProposeActionType());
      return true;
    }
  }
  return false;
}

void AssistiveSuggester::DismissSuggestion() {
  if (current_suggester_)
    current_suggester_->DismissSuggestion();
  current_suggester_ = nullptr;
}

bool AssistiveSuggester::IsSuggestionShown() {
  return current_suggester_ != nullptr;
}

}  // namespace chromeos
