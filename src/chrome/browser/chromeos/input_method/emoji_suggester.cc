// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/input_method/emoji_suggester.h"

#include "base/files/file_util.h"
#include "base/i18n/number_formatting.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/ui/ash/keyboard/chrome_keyboard_controller_client.h"
#include "chromeos/services/ime/constants.h"

namespace chromeos {

namespace {

const int kMaxCandidateSize = 5;
const char kSpaceChar = ' ';
const base::FilePath::CharType kEmojiMapFilePath[] =
    FILE_PATH_LITERAL("/emoji/emoji-map.csv");
const int kMaxSuggestionIndex = 31;
const int kMaxSuggestionSize = kMaxSuggestionIndex + 1;
const char kShowEmojiSuggestionMessage[] =
    "Emoji suggested. Press up or down to choose an emoji. Press enter to "
    "insert.";
const char kDismissEmojiSuggestionMessage[] = "Emoji suggestion dismissed.";

std::string ReadEmojiDataFromFile() {
  if (!base::DirectoryExists(base::FilePath(ime::kBundledInputMethodsDirPath)))
    return base::EmptyString();

  std::string emoji_data;
  base::FilePath::StringType path(ime::kBundledInputMethodsDirPath);
  path.append(kEmojiMapFilePath);
  if (!base::ReadFileToString(base::FilePath(path), &emoji_data))
    LOG(WARNING) << "Emoji map file missing.";
  return emoji_data;
}

std::vector<std::string> SplitString(const std::string& str,
                                     const std::string& delimiter) {
  return base::SplitString(str, delimiter, base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_NONEMPTY);
}

std::string GetLastWord(const std::string& str) {
  // We only suggest if last char is a white space so search for last word from
  // second last char.
  DCHECK_EQ(kSpaceChar, str.back());
  size_t last_pos_to_search = str.length() - 2;

  const auto space_before_last_word = str.find_last_of(" ", last_pos_to_search);

  // If not found, return the entire string up to the last position to search
  // else return the last word.
  return space_before_last_word == std::string::npos
             ? str.substr(0, last_pos_to_search + 1)
             : str.substr(space_before_last_word + 1,
                          last_pos_to_search - space_before_last_word);
}

}  // namespace

EmojiSuggester::EmojiSuggester(InputMethodEngine* engine) : engine_(engine) {
  LoadEmojiMap();
  properties_.type = ui::ime::AssistiveWindowType::kEmojiSuggestion;
}

EmojiSuggester::~EmojiSuggester() = default;

void EmojiSuggester::LoadEmojiMap() {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()}, base::BindOnce(&ReadEmojiDataFromFile),
      base::BindOnce(&EmojiSuggester::OnEmojiDataLoaded,
                     weak_factory_.GetWeakPtr()));
}

void EmojiSuggester::LoadEmojiMapForTesting(const std::string& emoji_data) {
  OnEmojiDataLoaded(emoji_data);
}

void EmojiSuggester::OnEmojiDataLoaded(const std::string& emoji_data) {
  // Split data into lines.
  for (const auto& line : SplitString(emoji_data, "\n")) {
    // Get a word and a string of emojis from the line.
    const auto comma_pos = line.find_first_of(",");
    DCHECK(comma_pos != std::string::npos);
    std::string word = line.substr(0, comma_pos);
    base::string16 emojis = base::UTF8ToUTF16(line.substr(comma_pos + 1));
    // Build emoji_map_ from splitting the string of emojis.
    emoji_map_[word] =
        base::SplitString(emojis, base::UTF8ToUTF16(";"), base::TRIM_WHITESPACE,
                          base::SPLIT_WANT_NONEMPTY);
    // TODO(crbug/1093179): Implement arrow to indicate more emojis available.
    // Only loads 5 emojis for now until arrow is implemented.
    if (emoji_map_[word].size() > kMaxCandidateSize)
      emoji_map_[word].resize(kMaxCandidateSize);
    DCHECK_LE(static_cast<int>(emoji_map_[word].size()), kMaxSuggestionSize);
  }
}

void EmojiSuggester::RecordAcceptanceIndex(int index) {
  base::UmaHistogramExactLinear(
      "InputMethod.Assistive.EmojiSuggestAddition.AcceptancePosition", index,
      kMaxSuggestionIndex);
}

void EmojiSuggester::OnFocus(int context_id) {
  context_id_ = context_id;
}

void EmojiSuggester::OnBlur() {
  context_id_ = -1;
}

SuggestionStatus EmojiSuggester::HandleKeyEvent(
    const InputMethodEngineBase::KeyboardEvent& event) {
  if (!suggestion_shown_)
    return SuggestionStatus::kNotHandled;
  SuggestionStatus status = SuggestionStatus::kNotHandled;
  std::string error;
  if (event.key == "Enter" && candidate_id_ != -1) {
    suggestion_shown_ = false;
    engine_->AcceptSuggestionCandidate(context_id_, candidates_[candidate_id_],
                                       &error);
    RecordAcceptanceIndex(candidate_id_);
    status = SuggestionStatus::kAccept;
  } else if (event.key == "Down") {
    candidate_id_ < static_cast<int>(candidates_.size()) - 1
        ? candidate_id_++
        : candidate_id_ = 0;
    engine_->HighlightSuggestionCandidate(context_id_, candidate_id_, &error);
    status = SuggestionStatus::kBrowsing;
  } else if (event.key == "Up") {
    candidate_id_ > 0
        ? candidate_id_--
        : candidate_id_ = static_cast<int>(candidates_.size()) - 1;
    engine_->HighlightSuggestionCandidate(context_id_, candidate_id_, &error);
    status = SuggestionStatus::kBrowsing;
  } else if (event.key == "Esc") {
    DismissSuggestion();
    suggestion_shown_ = false;
    status = SuggestionStatus::kDismiss;
  }
  if (!error.empty()) {
    LOG(ERROR) << "Fail to handle event. " << error;
  }
  return status;
}

bool EmojiSuggester::Suggest(const base::string16& text) {
  if (emoji_map_.empty() || text[text.length() - 1] != kSpaceChar)
    return false;
  std::string last_word =
      base::ToLowerASCII(GetLastWord(base::UTF16ToUTF8(text)));
  if (!last_word.empty() && emoji_map_.count(last_word)) {
    ShowSuggestion(last_word);
    return true;
  }
  return false;
}

void EmojiSuggester::ShowSuggestion(const std::string& text) {
  if (ChromeKeyboardControllerClient::Get()->is_keyboard_enabled())
    return;
  std::string error;
  suggestion_shown_ = true;
  candidates_.clear();
  candidate_id_ = -1;
  candidates_ = emoji_map_.at(text);
  properties_.visible = true;
  properties_.candidates = candidates_;
  properties_.announce_string = kShowEmojiSuggestionMessage;
  engine_->SetAssistiveWindowProperties(context_id_, properties_, &error);
  if (!error.empty()) {
    LOG(ERROR) << "Fail to show suggestion. " << error;
  }
}

void EmojiSuggester::DismissSuggestion() {
  std::string error;
  suggestion_shown_ = false;
  properties_.visible = false;
  properties_.announce_string = kDismissEmojiSuggestionMessage;
  engine_->SetAssistiveWindowProperties(context_id_, properties_, &error);
  if (!error.empty()) {
    LOG(ERROR) << "Failed to dismiss suggestion. " << error;
  }
}

AssistiveType EmojiSuggester::GetProposeActionType() {
  return AssistiveType::kEmoji;
}

bool EmojiSuggester::GetSuggestionShownForTesting() const {
  return suggestion_shown_;
}

}  // namespace chromeos
