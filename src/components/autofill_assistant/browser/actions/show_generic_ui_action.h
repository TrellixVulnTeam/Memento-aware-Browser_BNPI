// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_ACTIONS_SHOW_GENERIC_UI_ACTION_H_
#define COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_ACTIONS_SHOW_GENERIC_UI_ACTION_H_

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/autofill/core/browser/personal_data_manager_observer.h"
#include "components/autofill_assistant/browser/actions/action.h"
#include "components/autofill_assistant/browser/element_precondition.h"
#include "components/autofill_assistant/browser/website_login_manager.h"

namespace autofill_assistant {

// Action to show generic UI in the sheet.
class ShowGenericUiAction : public Action,
                            public autofill::PersonalDataManagerObserver {
 public:
  explicit ShowGenericUiAction(ActionDelegate* delegate,
                               const ActionProto& proto);
  ~ShowGenericUiAction() override;

  ShowGenericUiAction(const ShowGenericUiAction&) = delete;
  ShowGenericUiAction& operator=(const ShowGenericUiAction&) = delete;

 private:
  // Overrides Action:
  void InternalProcessAction(ProcessActionCallback callback) override;

  void RegisterChecks(
      BatchElementChecker* checker,
      base::OnceCallback<void(const ClientStatus&)> wait_for_dom_callback);
  void OnPreconditionResult(size_t choice_index,
                            const ClientStatus& status,
                            const std::vector<std::string>& ignored_payloads);
  void OnElementChecksDone(
      base::OnceCallback<void(const ClientStatus&)> wait_for_dom_callback);
  void OnDoneWaitForDom(const ClientStatus& status);
  // If there is an active WaitForDom this method ends it before calling
  // EndAction, otherwise it just calls EndAction.
  void OnEndActionInteraction(const ClientStatus& status);
  void EndAction(const ClientStatus& status);

  void OnViewInflationFinished(const ClientStatus& status);

  // From autofill::PersonalDataManagerObserver.
  void OnPersonalDataChanged() override;

  void OnGetLogins(const ShowGenericUiProto::RequestLoginOptions& proto,
                   std::vector<WebsiteLoginManager::Login> logins);

  bool has_pending_wait_for_dom_ = false;
  bool should_end_action_ = false;
  std::vector<std::unique_ptr<ElementPrecondition>> preconditions_;
  ProcessActionCallback callback_;
  base::WeakPtrFactory<ShowGenericUiAction> weak_ptr_factory_{this};
};

}  // namespace autofill_assistant
#endif  // COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_ACTIONS_SHOW_GENERIC_UI_ACTION_H_
