// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_ASSISTANT_MODEL_ASSISTANT_SUGGESTIONS_MODEL_H_
#define ASH_ASSISTANT_MODEL_ASSISTANT_SUGGESTIONS_MODEL_H_

#include <memory>
#include <vector>

#include "base/component_export.h"
#include "base/macros.h"
#include "base/observer_list.h"
#include "chromeos/services/assistant/public/mojom/assistant.mojom.h"

namespace base {
class UnguessableToken;
}  // namespace base

namespace ash {

class AssistantSuggestionsModelObserver;

class COMPONENT_EXPORT(ASSISTANT_MODEL) AssistantSuggestionsModel {
 public:
  using AssistantSuggestion = chromeos::assistant::mojom::AssistantSuggestion;
  using AssistantSuggestionPtr =
      chromeos::assistant::mojom::AssistantSuggestionPtr;

  AssistantSuggestionsModel();
  ~AssistantSuggestionsModel();

  // Adds/removes the specified suggestions model |observer|.
  void AddObserver(AssistantSuggestionsModelObserver* observer) const;
  void RemoveObserver(AssistantSuggestionsModelObserver* observer) const;

  // Sets the cache of conversation starters.
  void SetConversationStarters(
      std::vector<AssistantSuggestionPtr> conversation_starters);

  // Returns the conversation starter uniquely identified by |id|.
  const AssistantSuggestion* GetConversationStarterById(
      const base::UnguessableToken& id) const;

  // Returns all cached conversation starters.
  std::vector<const AssistantSuggestion*> GetConversationStarters() const;

  // Sets the cache of onboarding suggestions.
  void SetOnboardingSuggestions(
      std::vector<AssistantSuggestionPtr> onboarding_suggestions);

  // Returns all cached onboarding suggestions.
  std::vector<const AssistantSuggestion*> GetOnboardingSuggestions() const;

 private:
  void NotifyConversationStartersChanged();
  void NotifyOnboardingSuggestionsChanged();

  std::vector<AssistantSuggestionPtr> conversation_starters_;
  std::vector<AssistantSuggestionPtr> onboarding_suggestions_;

  mutable base::ObserverList<AssistantSuggestionsModelObserver> observers_;

  DISALLOW_COPY_AND_ASSIGN(AssistantSuggestionsModel);
};

}  // namespace ash

#endif  // ASH_ASSISTANT_MODEL_ASSISTANT_SUGGESTIONS_MODEL_H_
