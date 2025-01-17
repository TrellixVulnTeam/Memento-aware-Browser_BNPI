// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "base/bind.h"
#include "base/callback.h"
#include "base/notreached.h"
#include "base/optional.h"
#include "base/run_loop.h"
#include "base/test/bind_test_util.h"
#include "base/test/mock_callback.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/signin/reauth_result.h"
#include "chrome/browser/signin/signin_features.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/signin_reauth_view_controller.h"
#include "chrome/browser/ui/signin_view_controller.h"
#include "chrome/browser/ui/webui/signin/login_ui_test_utils.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/signin/public/base/signin_metrics.h"
#include "components/signin/public/identity_manager/consent_level.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/signin/public/identity_manager/identity_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/test_utils.h"
#include "google_apis/gaia/core_account_id.h"
#include "google_apis/gaia/gaia_switches.h"
#include "net/base/escape.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/controllable_http_response.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/test/embedded_test_server/request_handler_util.h"

namespace {

const base::TimeDelta kReauthDialogTimeout = base::TimeDelta::FromSeconds(30);
const char kReauthDonePath[] = "/embedded/xreauth/chrome?done";
const char kReauthPath[] = "/embedded/xreauth/chrome";
const char kChallengePath[] = "/challenge";

std::unique_ptr<net::test_server::BasicHttpResponse> CreateRedirectResponse(
    const GURL& redirect_url) {
  auto http_response = std::make_unique<net::test_server::BasicHttpResponse>();
  http_response->set_code(net::HTTP_TEMPORARY_REDIRECT);
  http_response->AddCustomHeader("Location", redirect_url.spec());
  http_response->AddCustomHeader("Access-Control-Allow-Origin", "*");
  return http_response;
}

std::unique_ptr<net::test_server::HttpResponse> HandleReauthURL(
    const GURL& base_url,
    const net::test_server::HttpRequest& request) {
  if (!net::test_server::ShouldHandle(request, kReauthPath)) {
    return nullptr;
  }

  GURL request_url = request.GetURL();
  std::string parameter =
      net::UnescapeBinaryURLComponent(request_url.query_piece());

  if (parameter.empty()) {
    // Parameterless request redirects to the fake challenge page.
    return CreateRedirectResponse(base_url.Resolve(kChallengePath));
  }

  if (parameter == "done") {
    // On success, the reauth returns HTTP_NO_CONTENT response.
    auto http_response =
        std::make_unique<net::test_server::BasicHttpResponse>();
    http_response->set_code(net::HTTP_NO_CONTENT);
    return http_response;
  }

  NOTREACHED();
  return nullptr;
}

class ReauthTestObserver : SigninReauthViewController::Observer {
 public:
  explicit ReauthTestObserver(SigninReauthViewController* controller) {
    controller->SetObserverForTesting(this);
  }

  void WaitUntilGaiaReauthPageIsShown() { run_loop_.Run(); }

  void OnGaiaReauthPageShown() override { run_loop_.Quit(); }

 private:
  base::RunLoop run_loop_;
};

}  // namespace

// Browser tests for SigninReauthViewController.
class SigninReauthViewControllerBrowserTest : public InProcessBrowserTest {
 public:
  SigninReauthViewControllerBrowserTest()
      : https_server_(net::EmbeddedTestServer::TYPE_HTTPS) {
    scoped_feature_list_.InitAndEnableFeature(kSigninReauthPrompt);
  }

  void SetUp() override {
    ASSERT_TRUE(https_server()->InitializeAndListen());
    InProcessBrowserTest::SetUp();
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    // HTTPS server only serves a valid cert for localhost, so this is needed to
    // load pages from other hosts without an interstitial.
    command_line->AppendSwitch("ignore-certificate-errors");
    command_line->AppendSwitchASCII(switches::kGaiaUrl, base_url().spec());
  }

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");

    https_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    https_server()->RegisterRequestHandler(
        base::BindRepeating(&HandleReauthURL, base_url()));
    reauth_challenge_response_ =
        std::make_unique<net::test_server::ControllableHttpResponse>(
            https_server(), kChallengePath);
    https_server()->StartAcceptingConnections();

    account_id_ = signin::SetUnconsentedPrimaryAccount(identity_manager(),
                                                       "alice@gmail.com")
                      .account_id;

    reauth_result_loop_ = std::make_unique<base::RunLoop>();
    InProcessBrowserTest::SetUpOnMainThread();
  }

  void ShowReauthPrompt() {
    abort_handle_ = browser()->signin_view_controller()->ShowReauthPrompt(
        account_id_, signin_metrics::ReauthAccessPoint::kAutofillDropdown,
        base::BindOnce(&SigninReauthViewControllerBrowserTest::OnReauthResult,
                       base::Unretained(this)));
  }

  // This method must be called only after the reauth dialog has been opened.
  void RedirectGaiaChallengeTo(const GURL& redirect_url) {
    reauth_challenge_response_->WaitForRequest();
    auto redirect_response = CreateRedirectResponse(redirect_url);
    reauth_challenge_response_->Send(redirect_response->ToResponseString());
    reauth_challenge_response_->Done();
  }

  void OnReauthResult(signin::ReauthResult reauth_result) {
    reauth_result_ = reauth_result;
    reauth_result_loop_->Quit();
  }

  base::Optional<signin::ReauthResult> WaitForReauthResult() {
    reauth_result_loop_->Run();
    return reauth_result_;
  }

  void ResetAbortHandle() { abort_handle_.reset(); }

  net::EmbeddedTestServer* https_server() { return &https_server_; }

  GURL base_url() { return https_server()->base_url(); }

  signin::IdentityManager* identity_manager() {
    return IdentityManagerFactory::GetForProfile(browser()->profile());
  }

  SigninReauthViewController* signin_reauth_view_controller() {
    SigninViewController* signin_view_controller =
        browser()->signin_view_controller();
    DCHECK(signin_view_controller->ShowsModalDialog());
    return static_cast<SigninReauthViewController*>(
        signin_view_controller->GetModalDialogDelegateForTesting());
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
  net::EmbeddedTestServer https_server_;
  std::unique_ptr<net::test_server::ControllableHttpResponse>
      reauth_challenge_response_;
  CoreAccountId account_id_;
  std::unique_ptr<SigninViewController::ReauthAbortHandle> abort_handle_;

  std::unique_ptr<base::RunLoop> reauth_result_loop_;
  base::Optional<signin::ReauthResult> reauth_result_;
};

// Tests that the abort handle cancels an ongoing reauth flow.
IN_PROC_BROWSER_TEST_F(SigninReauthViewControllerBrowserTest,
                       AbortReauthDialog_AbortHandle) {
  ShowReauthPrompt();
  ResetAbortHandle();
  EXPECT_EQ(WaitForReauthResult(), signin::ReauthResult::kCancelled);
}

// Tests canceling the reauth dialog through CloseModalSignin().
IN_PROC_BROWSER_TEST_F(SigninReauthViewControllerBrowserTest,
                       AbortReauthDialog_CloseModalSignin) {
  ShowReauthPrompt();
  browser()->signin_view_controller()->CloseModalSignin();
  EXPECT_EQ(WaitForReauthResult(), signin::ReauthResult::kCancelled);
}

// Tests closing the reauth dialog through by clicking on the close button (the
// X).
IN_PROC_BROWSER_TEST_F(SigninReauthViewControllerBrowserTest,
                       CloseReauthDialog) {
  ShowReauthPrompt();
  // The test cannot depend on Views implementation so it simulates clicking on
  // the close button through calling the close event.
  signin_reauth_view_controller()->OnModalSigninClosed();
  EXPECT_EQ(WaitForReauthResult(), signin::ReauthResult::kDismissedByUser);
}

// Tests clicking on the cancel button in the reauth dialog.
IN_PROC_BROWSER_TEST_F(SigninReauthViewControllerBrowserTest,
                       CancelReauthDialog) {
  ShowReauthPrompt();
  ASSERT_TRUE(login_ui_test_utils::CancelReauthConfirmationDialog(
      browser(), kReauthDialogTimeout));
  EXPECT_EQ(WaitForReauthResult(), signin::ReauthResult::kDismissedByUser);
}

// Tests the reauth result in case Gaia page failed to load.
IN_PROC_BROWSER_TEST_F(SigninReauthViewControllerBrowserTest,
                       GaiaChallengeLoadFailed) {
  ShowReauthPrompt();
  ASSERT_TRUE(login_ui_test_utils::ConfirmReauthConfirmationDialog(
      browser(), kReauthDialogTimeout));
  RedirectGaiaChallengeTo(https_server()->GetURL("/close-socket"));
  EXPECT_EQ(WaitForReauthResult(), signin::ReauthResult::kLoadFailed);
}

// Tests clicking on the confirm button in the reauth dialog. Reauth completes
// before the confirmation.
IN_PROC_BROWSER_TEST_F(SigninReauthViewControllerBrowserTest,
                       ConfirmReauthDialog_AfterReauthSuccess) {
  ShowReauthPrompt();
  RedirectGaiaChallengeTo(https_server()->GetURL(kReauthDonePath));
  ASSERT_TRUE(login_ui_test_utils::ConfirmReauthConfirmationDialog(
      browser(), kReauthDialogTimeout));
  EXPECT_EQ(WaitForReauthResult(), signin::ReauthResult::kSuccess);
}

// Tests clicking on the confirm button in the reauth dialog. Reauth completes
// after the confirmation.
IN_PROC_BROWSER_TEST_F(SigninReauthViewControllerBrowserTest,
                       ConfirmReauthDialog_BeforeReauthSuccess) {
  ShowReauthPrompt();
  ASSERT_TRUE(login_ui_test_utils::ConfirmReauthConfirmationDialog(
      browser(), kReauthDialogTimeout));
  RedirectGaiaChallengeTo(https_server()->GetURL(kReauthDonePath));
  EXPECT_EQ(WaitForReauthResult(), signin::ReauthResult::kSuccess);
}

// Tests that links from the Gaia page are opened in a new tab.
IN_PROC_BROWSER_TEST_F(SigninReauthViewControllerBrowserTest,
                       OpenLinksInNewTab) {
  content::WebContents* original_contents =
      browser()->tab_strip_model()->GetActiveWebContents();

  const GURL target_url = https_server()->GetURL("/link_with_target.html");
  content::TestNavigationObserver target_content_observer(target_url);
  target_content_observer.StartWatchingNewWebContents();
  ShowReauthPrompt();
  RedirectGaiaChallengeTo(target_url);

  ReauthTestObserver reauth_observer(signin_reauth_view_controller());
  ASSERT_TRUE(login_ui_test_utils::ConfirmReauthConfirmationDialog(
      browser(), kReauthDialogTimeout));
  reauth_observer.WaitUntilGaiaReauthPageIsShown();
  target_content_observer.Wait();

  content::WebContents* dialog_contents =
      signin_reauth_view_controller()->GetWebContents();
  content::TestNavigationObserver new_tab_observer(nullptr);
  new_tab_observer.StartWatchingNewWebContents();
  ASSERT_TRUE(content::ExecuteScript(
      dialog_contents, "document.getElementsByTagName('a')[0].click();"));
  new_tab_observer.Wait();

  content::WebContents* new_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_NE(new_contents, original_contents);
  EXPECT_NE(new_contents, dialog_contents);
  EXPECT_EQ(new_contents->GetURL(), https_server()->GetURL("/title1.html"));
}

// Tests that the authentication flow that goes outside of the reauth host is
// shown in a new tab.
IN_PROC_BROWSER_TEST_F(SigninReauthViewControllerBrowserTest,
                       CompleteSAMLInNewTab) {
  content::WebContents* original_contents =
      browser()->tab_strip_model()->GetActiveWebContents();

  // The URL contains a link that navigates to the reauth success URL.
  const std::string target_path = net::test_server::GetFilePathWithReplacements(
      "/signin/link_with_replacements.html",
      {{"REPLACE_WITH_URL", https_server()->GetURL(kReauthDonePath).spec()}});
  const GURL target_url =
      https_server()->GetURL("3p-identity-provider.com", target_path);

  content::TestNavigationObserver target_content_observer(target_url);
  target_content_observer.StartWatchingNewWebContents();
  ShowReauthPrompt();
  RedirectGaiaChallengeTo(target_url);

  ui_test_utils::TabAddedWaiter tab_added_waiter(browser());
  ReauthTestObserver reauth_observer(signin_reauth_view_controller());
  ASSERT_TRUE(login_ui_test_utils::ConfirmReauthConfirmationDialog(
      browser(), kReauthDialogTimeout));
  reauth_observer.WaitUntilGaiaReauthPageIsShown();
  tab_added_waiter.Wait();
  target_content_observer.Wait();

  content::WebContents* target_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_NE(target_contents, original_contents);
  EXPECT_EQ(target_contents, signin_reauth_view_controller()->GetWebContents());
  EXPECT_EQ(target_contents->GetURL(), target_url);

  ASSERT_TRUE(content::ExecuteScript(
      target_contents, "document.getElementsByTagName('a')[0].click();"));
  EXPECT_EQ(WaitForReauthResult(), signin::ReauthResult::kSuccess);
}

// Tests that closing of the SAML tab aborts the reauth flow.
IN_PROC_BROWSER_TEST_F(SigninReauthViewControllerBrowserTest, CloseSAMLTab) {
  const GURL target_url =
      https_server()->GetURL("3p-identity-provider.com", "/title1.html");
  ShowReauthPrompt();
  RedirectGaiaChallengeTo(target_url);

  ui_test_utils::TabAddedWaiter tab_added_waiter(browser());
  ASSERT_TRUE(login_ui_test_utils::ConfirmReauthConfirmationDialog(
      browser(), kReauthDialogTimeout));
  tab_added_waiter.Wait();

  auto* tab_strip_model = browser()->tab_strip_model();
  EXPECT_EQ(tab_strip_model->GetActiveWebContents()->GetURL(), target_url);
  tab_strip_model->CloseWebContentsAt(tab_strip_model->active_index(),
                                      TabStripModel::CLOSE_USER_GESTURE);
  EXPECT_EQ(WaitForReauthResult(), signin::ReauthResult::kDismissedByUser);
}
