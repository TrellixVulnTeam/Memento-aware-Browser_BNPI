// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/test/bind_test_util.h"
#include "components/ukm/ukm_test_helper.h"
#include "weblayer/browser/android/metrics/weblayer_metrics_service_client.h"
#include "weblayer/browser/profile_impl.h"
#include "weblayer/public/navigation_controller.h"
#include "weblayer/public/profile.h"
#include "weblayer/public/tab.h"
#include "weblayer/shell/android/browsertests_apk/metrics_test_helper.h"
#include "weblayer/shell/browser/shell.h"
#include "weblayer/test/weblayer_browser_test.h"

namespace weblayer {

class UkmBrowserTest : public WebLayerBrowserTest {
 public:
  void SetUp() override {
    InstallTestGmsBridge(user_consent_);

    WebLayerBrowserTest::SetUp();
  }

  void TearDown() override {
    RemoveTestGmsBridge();
    WebLayerBrowserTest::TearDown();
  }

  ukm::UkmService* GetUkmService() {
    return WebLayerMetricsServiceClient::GetInstance()->GetUkmService();
  }

  void disable_user_consent() { user_consent_ = false; }

 private:
  bool user_consent_ = true;
};

// Even if there's user consent for UMA, need to explicitly enable UKM.
IN_PROC_BROWSER_TEST_F(UkmBrowserTest, UserConsentButNotEnabled) {
  ukm::UkmTestHelper ukm_test_helper(GetUkmService());

  EXPECT_FALSE(ukm_test_helper.IsRecordingEnabled());
}

// UKMs are only enabled when there's user consent and it's explicitly enabled.
IN_PROC_BROWSER_TEST_F(UkmBrowserTest, Enabled) {
  ukm::UkmTestHelper ukm_test_helper(GetUkmService());

  GetProfile()->SetBooleanSetting(SettingType::UKM_ENABLED, true);
  EXPECT_TRUE(ukm_test_helper.IsRecordingEnabled());
}

// If UKMs are disabled it's reflected accordingly.
IN_PROC_BROWSER_TEST_F(UkmBrowserTest, EnabledThenDisable) {
  ukm::UkmTestHelper ukm_test_helper(GetUkmService());

  GetProfile()->SetBooleanSetting(SettingType::UKM_ENABLED, true);
  EXPECT_TRUE(ukm_test_helper.IsRecordingEnabled());

  GetProfile()->SetBooleanSetting(SettingType::UKM_ENABLED, false);
  EXPECT_FALSE(ukm_test_helper.IsRecordingEnabled());
}

// Make sure that UKM is disabled while an incognito profile is alive.
IN_PROC_BROWSER_TEST_F(UkmBrowserTest, RegularPlusIncognitoCheck) {
  ukm::UkmTestHelper ukm_test_helper(GetUkmService());

  GetProfile()->SetBooleanSetting(SettingType::UKM_ENABLED, true);
  EXPECT_TRUE(ukm_test_helper.IsRecordingEnabled());
  uint64_t original_client_id = ukm_test_helper.GetClientId();
  EXPECT_NE(0U, original_client_id);

  // Incognito profiles have an empty name.
  CreateProfile(std::string());
  EXPECT_FALSE(ukm_test_helper.IsRecordingEnabled());

  // Creating another regular profile mustn't enable UKM.
  CreateProfile("foo");
  EXPECT_FALSE(ukm_test_helper.IsRecordingEnabled());

  // Note WebLayer can only have one incognito profile so we can't test creating
  // and destroying another one here.

  DestroyProfile("foo");
  EXPECT_FALSE(ukm_test_helper.IsRecordingEnabled());

  DestroyProfile(std::string());
  EXPECT_TRUE(ukm_test_helper.IsRecordingEnabled());
  // Client ID should not have been reset.
  EXPECT_EQ(original_client_id, ukm_test_helper.GetClientId());
}

// Make sure creating a real profile after Incognito doesn't enable UKM.
IN_PROC_BROWSER_TEST_F(UkmBrowserTest, IncognitoPlusRegularCheck) {
  ukm::UkmTestHelper ukm_test_helper(GetUkmService());

  GetProfile()->SetBooleanSetting(SettingType::UKM_ENABLED, true);

  // Incognito profiles have an empty name.
  CreateProfile(std::string());
  EXPECT_FALSE(ukm_test_helper.IsRecordingEnabled());

  CreateProfile("foo");
  EXPECT_FALSE(ukm_test_helper.IsRecordingEnabled());

  DestroyProfile(std::string());
  EXPECT_TRUE(ukm_test_helper.IsRecordingEnabled());
}

class UkmDisabledBrowserTest : public UkmBrowserTest {
 public:
  UkmDisabledBrowserTest() { disable_user_consent(); }
};

// If there's no user consent UKMs are disabled.
IN_PROC_BROWSER_TEST_F(UkmDisabledBrowserTest, Disabled) {
  ukm::UkmTestHelper ukm_test_helper(GetUkmService());

  EXPECT_FALSE(ukm_test_helper.IsRecordingEnabled());

  // Ensure enabling UKMs still doesn't enable it.
  GetProfile()->SetBooleanSetting(SettingType::UKM_ENABLED, true);
  EXPECT_FALSE(ukm_test_helper.IsRecordingEnabled());
}

class UkmIncognitoBrowserTest : public UkmBrowserTest {
 public:
  UkmIncognitoBrowserTest() { SetShellStartsInIncognitoMode(); }
};

// Starting with one incognito window should disable UKM.
IN_PROC_BROWSER_TEST_F(UkmIncognitoBrowserTest, Disabled) {
  ukm::UkmTestHelper ukm_test_helper(GetUkmService());

  // Enabling UKMs doesn't enable it because of the incognito window.
  GetProfile()->SetBooleanSetting(SettingType::UKM_ENABLED, true);

  EXPECT_FALSE(ukm_test_helper.IsRecordingEnabled());
}

}  // namespace weblayer
