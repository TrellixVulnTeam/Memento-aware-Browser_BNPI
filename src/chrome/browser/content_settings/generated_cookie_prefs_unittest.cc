// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/content_settings/generated_cookie_prefs.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/extensions/api/settings_private/generated_pref.h"
#include "chrome/common/extensions/api/settings_private.h"
#include "chrome/test/base/testing_profile.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/features.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/content_settings/core/test/content_settings_mock_provider.h"
#include "components/content_settings/core/test/content_settings_test_utils.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace settings_api = extensions::api::settings_private;

namespace content_settings {

namespace {

// Sets the value of |generated_pref| to |pref_value| and then ensures that
// the cookie content settings and preferences match |expected_content_setting|,
// |expected_block_third_party|, and |expected_cookie_controls_mode|.
// The value of the new PrefObject returned by the |generated_pref| is then
// checked against |expected_pref_value|.
void ValidatePrimarySettingPrefValue(
    HostContentSettingsMap* map,
    sync_preferences::TestingPrefServiceSyncable* prefs,
    GeneratedCookiePrimarySettingPref* generated_pref,
    CookiePrimarySetting pref_value,
    ContentSetting expected_content_setting,
    bool expected_block_third_party,
    CookieControlsMode expected_cookie_controls_mode,
    CookiePrimarySetting expected_pref_value) {
  EXPECT_EQ(
      generated_pref->SetPref(
          std::make_unique<base::Value>(static_cast<int>(pref_value)).get()),
      extensions::settings_private::SetPrefResult::SUCCESS);
  EXPECT_EQ(
      map->GetDefaultContentSetting(ContentSettingsType::COOKIES, nullptr),
      expected_content_setting);
  EXPECT_EQ(prefs->GetUserPref(prefs::kBlockThirdPartyCookies)->GetBool(),
            expected_block_third_party);
  EXPECT_EQ(static_cast<CookieControlsMode>(
                prefs->GetUserPref(prefs::kCookieControlsMode)->GetInt()),
            expected_cookie_controls_mode);
  EXPECT_EQ(static_cast<CookiePrimarySetting>(
                generated_pref->GetPrefObject()->value->GetInt()),
            expected_pref_value);
}

// All of the possible managed states for a boolean preference that can be
// both enforced and recommended.
enum class PrefSetting {
  kEnforcedOff,
  kEnforcedOn,
  kRecommendedOff,
  kRecommendedOn,
  kNotSet,
};

// Possible preference sources supported by TestingPrefService.
// TODO(crbug.com/1063281): Extend TestingPrefService to support prefs set for
//                          supervised users.
enum class PrefSource {
  kExtension,
  kDevicePolicy,
  kRecommended,
  kNone,
};

// Define additional unused values of Enforcement, ControlledBy and
// CookiePrimarySetting, to support testing not set values.
const settings_api::ControlledBy kNoControlledBy =
    static_cast<settings_api::ControlledBy>(-1);
const settings_api::Enforcement kNoEnforcement =
    static_cast<settings_api::Enforcement>(-1);
const CookiePrimarySetting kNoRecommendedValue =
    static_cast<CookiePrimarySetting>(-1);

// Represents a set of settings, preferences and the associated expected
// fields for the returned preference object.
struct PrimaryCookieSettingManagedTestCase {
  ContentSetting default_content_setting;
  content_settings::SettingSource default_content_setting_source;
  PrefSetting block_third_party;
  PrefSource block_third_party_source;
  settings_api::ControlledBy expected_controlled_by;
  settings_api::Enforcement expected_enforcement;
  CookiePrimarySetting expected_recommended_value;
  std::vector<CookiePrimarySetting> expected_user_selectable_values;
};

const std::vector<PrimaryCookieSettingManagedTestCase> managed_test_cases = {
    {CONTENT_SETTING_DEFAULT,
     content_settings::SETTING_SOURCE_NONE,
     PrefSetting::kEnforcedOff,
     PrefSource::kExtension,
     settings_api::ControlledBy::CONTROLLED_BY_EXTENSION,
     settings_api::Enforcement::ENFORCEMENT_ENFORCED,
     kNoRecommendedValue,
     {CookiePrimarySetting::ALLOW_ALL, CookiePrimarySetting::BLOCK_ALL}},
    {CONTENT_SETTING_DEFAULT,
     content_settings::SETTING_SOURCE_NONE,
     PrefSetting::kEnforcedOn,
     PrefSource::kDevicePolicy,
     settings_api::ControlledBy::CONTROLLED_BY_DEVICE_POLICY,
     settings_api::Enforcement::ENFORCEMENT_ENFORCED,
     kNoRecommendedValue,
     {CookiePrimarySetting::BLOCK_THIRD_PARTY,
      CookiePrimarySetting::BLOCK_ALL}},
    {CONTENT_SETTING_DEFAULT,
     content_settings::SETTING_SOURCE_NONE,
     PrefSetting::kRecommendedOff,
     PrefSource::kRecommended,
     kNoControlledBy,
     settings_api::Enforcement::ENFORCEMENT_RECOMMENDED,
     CookiePrimarySetting::ALLOW_ALL,
     {}},
    {CONTENT_SETTING_DEFAULT,
     content_settings::SETTING_SOURCE_NONE,
     PrefSetting::kRecommendedOn,
     PrefSource::kRecommended,
     kNoControlledBy,
     settings_api::Enforcement::ENFORCEMENT_RECOMMENDED,
     CookiePrimarySetting::BLOCK_THIRD_PARTY,
     {}},
    {CONTENT_SETTING_DEFAULT,
     content_settings::SETTING_SOURCE_NONE,
     PrefSetting::kNotSet,
     PrefSource::kNone,
     kNoControlledBy,
     kNoEnforcement,
     kNoRecommendedValue,
     {}},
    {CONTENT_SETTING_ALLOW,
     content_settings::SETTING_SOURCE_POLICY,
     PrefSetting::kEnforcedOff,
     PrefSource::kExtension,
     settings_api::ControlledBy::CONTROLLED_BY_EXTENSION,
     settings_api::Enforcement::ENFORCEMENT_ENFORCED,
     kNoRecommendedValue,
     {}},
    {CONTENT_SETTING_ALLOW,
     content_settings::SETTING_SOURCE_EXTENSION,
     PrefSetting::kEnforcedOn,
     PrefSource::kDevicePolicy,
     settings_api::ControlledBy::CONTROLLED_BY_DEVICE_POLICY,
     settings_api::Enforcement::ENFORCEMENT_ENFORCED,
     kNoRecommendedValue,
     {}},
    {CONTENT_SETTING_ALLOW,
     content_settings::SETTING_SOURCE_SUPERVISED,
     PrefSetting::kRecommendedOff,
     PrefSource::kRecommended,
     settings_api::ControlledBy::CONTROLLED_BY_CHILD_RESTRICTION,
     settings_api::Enforcement::ENFORCEMENT_ENFORCED,
     CookiePrimarySetting::ALLOW_ALL,
     {CookiePrimarySetting::ALLOW_ALL,
      CookiePrimarySetting::BLOCK_THIRD_PARTY_INCOGNITO,
      CookiePrimarySetting::BLOCK_THIRD_PARTY}},
    {CONTENT_SETTING_ALLOW,
     content_settings::SETTING_SOURCE_POLICY,
     PrefSetting::kRecommendedOn,
     PrefSource::kRecommended,
     settings_api::ControlledBy::CONTROLLED_BY_DEVICE_POLICY,
     settings_api::Enforcement::ENFORCEMENT_ENFORCED,
     CookiePrimarySetting::BLOCK_THIRD_PARTY,
     {CookiePrimarySetting::ALLOW_ALL,
      CookiePrimarySetting::BLOCK_THIRD_PARTY_INCOGNITO,
      CookiePrimarySetting::BLOCK_THIRD_PARTY}},
    {CONTENT_SETTING_ALLOW,
     content_settings::SETTING_SOURCE_EXTENSION,
     PrefSetting::kNotSet,
     PrefSource::kNone,
     settings_api::ControlledBy::CONTROLLED_BY_EXTENSION,
     settings_api::Enforcement::ENFORCEMENT_ENFORCED,
     kNoRecommendedValue,
     {CookiePrimarySetting::ALLOW_ALL,
      CookiePrimarySetting::BLOCK_THIRD_PARTY_INCOGNITO,
      CookiePrimarySetting::BLOCK_THIRD_PARTY}},
    {CONTENT_SETTING_BLOCK,
     content_settings::SETTING_SOURCE_SUPERVISED,
     PrefSetting::kEnforcedOff,
     PrefSource::kDevicePolicy,
     settings_api::ControlledBy::CONTROLLED_BY_CHILD_RESTRICTION,
     settings_api::Enforcement::ENFORCEMENT_ENFORCED,
     kNoRecommendedValue,
     {}},
    {CONTENT_SETTING_BLOCK,
     content_settings::SETTING_SOURCE_POLICY,
     PrefSetting::kEnforcedOn,
     PrefSource::kExtension,
     settings_api::ControlledBy::CONTROLLED_BY_DEVICE_POLICY,
     settings_api::Enforcement::ENFORCEMENT_ENFORCED,
     kNoRecommendedValue,
     {}},
    {CONTENT_SETTING_BLOCK,
     content_settings::SETTING_SOURCE_EXTENSION,
     PrefSetting::kRecommendedOff,
     PrefSource::kRecommended,
     settings_api::ControlledBy::CONTROLLED_BY_EXTENSION,
     settings_api::Enforcement::ENFORCEMENT_ENFORCED,
     kNoRecommendedValue,
     {}},
    {CONTENT_SETTING_BLOCK,
     content_settings::SETTING_SOURCE_SUPERVISED,
     PrefSetting::kRecommendedOn,
     PrefSource::kRecommended,
     settings_api::ControlledBy::CONTROLLED_BY_CHILD_RESTRICTION,
     settings_api::Enforcement::ENFORCEMENT_ENFORCED,
     kNoRecommendedValue,
     {}},
    {CONTENT_SETTING_BLOCK,
     content_settings::SETTING_SOURCE_POLICY,
     PrefSetting::kNotSet,
     PrefSource::kNone,
     settings_api::ControlledBy::CONTROLLED_BY_DEVICE_POLICY,
     settings_api::Enforcement::ENFORCEMENT_ENFORCED,
     kNoRecommendedValue,
     {}},
    {CONTENT_SETTING_SESSION_ONLY,
     content_settings::SETTING_SOURCE_EXTENSION,
     PrefSetting::kEnforcedOff,
     PrefSource::kDevicePolicy,
     settings_api::ControlledBy::CONTROLLED_BY_DEVICE_POLICY,
     settings_api::Enforcement::ENFORCEMENT_ENFORCED,
     kNoRecommendedValue,
     {}},
    {CONTENT_SETTING_SESSION_ONLY,
     content_settings::SETTING_SOURCE_SUPERVISED,
     PrefSetting::kEnforcedOn,
     PrefSource::kExtension,
     settings_api::ControlledBy::CONTROLLED_BY_EXTENSION,
     settings_api::Enforcement::ENFORCEMENT_ENFORCED,
     kNoRecommendedValue,
     {}},
    {CONTENT_SETTING_SESSION_ONLY,
     content_settings::SETTING_SOURCE_POLICY,
     PrefSetting::kRecommendedOff,
     PrefSource::kRecommended,
     settings_api::ControlledBy::CONTROLLED_BY_DEVICE_POLICY,
     settings_api::Enforcement::ENFORCEMENT_ENFORCED,
     CookiePrimarySetting::ALLOW_ALL,
     {CookiePrimarySetting::ALLOW_ALL,
      CookiePrimarySetting::BLOCK_THIRD_PARTY_INCOGNITO,
      CookiePrimarySetting::BLOCK_THIRD_PARTY}},
    {CONTENT_SETTING_SESSION_ONLY,
     content_settings::SETTING_SOURCE_EXTENSION,
     PrefSetting::kRecommendedOn,
     PrefSource::kRecommended,
     settings_api::ControlledBy::CONTROLLED_BY_EXTENSION,
     settings_api::Enforcement::ENFORCEMENT_ENFORCED,
     CookiePrimarySetting::BLOCK_THIRD_PARTY,
     {CookiePrimarySetting::ALLOW_ALL,
      CookiePrimarySetting::BLOCK_THIRD_PARTY_INCOGNITO,
      CookiePrimarySetting::BLOCK_THIRD_PARTY}},
    {CONTENT_SETTING_SESSION_ONLY,
     content_settings::SETTING_SOURCE_SUPERVISED,
     PrefSetting::kNotSet,
     PrefSource::kNone,
     settings_api::ControlledBy::CONTROLLED_BY_CHILD_RESTRICTION,
     settings_api::Enforcement::ENFORCEMENT_ENFORCED,
     kNoRecommendedValue,
     {CookiePrimarySetting::ALLOW_ALL,
      CookiePrimarySetting::BLOCK_THIRD_PARTY_INCOGNITO,
      CookiePrimarySetting::BLOCK_THIRD_PARTY}},
};

void SetupManagedTestConditions(
    HostContentSettingsMap* map,
    sync_preferences::TestingPrefServiceSyncable* prefs,
    const PrimaryCookieSettingManagedTestCase& test_case) {
  auto provider = std::make_unique<content_settings::MockProvider>();
  provider->SetWebsiteSetting(
      ContentSettingsPattern::Wildcard(), ContentSettingsPattern::Wildcard(),
      ContentSettingsType::COOKIES, std::string(),
      std::make_unique<base::Value>(test_case.default_content_setting));

  if (test_case.default_content_setting != CONTENT_SETTING_DEFAULT) {
    auto provider = std::make_unique<content_settings::MockProvider>();
    provider->SetWebsiteSetting(
        ContentSettingsPattern::Wildcard(), ContentSettingsPattern::Wildcard(),
        ContentSettingsType::COOKIES, std::string(),
        std::make_unique<base::Value>(test_case.default_content_setting));
    HostContentSettingsMap::ProviderType provider_type;
    switch (test_case.default_content_setting_source) {
      case content_settings::SETTING_SOURCE_POLICY:
        provider_type = HostContentSettingsMap::POLICY_PROVIDER;
        break;
      case content_settings::SETTING_SOURCE_EXTENSION:
        provider_type = HostContentSettingsMap::CUSTOM_EXTENSION_PROVIDER;
        break;
      case content_settings::SETTING_SOURCE_SUPERVISED:
        provider_type = HostContentSettingsMap::SUPERVISED_PROVIDER;
        break;
      case content_settings::SETTING_SOURCE_NONE:
      default:
        provider_type = HostContentSettingsMap::DEFAULT_PROVIDER;
    }
    content_settings::TestUtils::OverrideProvider(map, std::move(provider),
                                                  provider_type);
  }
  if (test_case.block_third_party != PrefSetting::kNotSet) {
    bool third_party_value =
        test_case.block_third_party == PrefSetting::kRecommendedOn ||
        test_case.block_third_party == PrefSetting::kEnforcedOn;
    if (test_case.block_third_party_source == PrefSource::kExtension) {
      prefs->SetExtensionPref(prefs::kBlockThirdPartyCookies,
                              std::make_unique<base::Value>(third_party_value));
    } else if (test_case.block_third_party_source ==
               PrefSource::kDevicePolicy) {
      prefs->SetManagedPref(prefs::kBlockThirdPartyCookies,
                            std::make_unique<base::Value>(third_party_value));
    } else if (test_case.block_third_party_source == PrefSource::kRecommended) {
      prefs->SetRecommendedPref(
          prefs::kBlockThirdPartyCookies,
          std::make_unique<base::Value>(third_party_value));
    }
  }
}

void ValidateManagedPreference(
    settings_api::PrefObject* pref,
    const PrimaryCookieSettingManagedTestCase& test_case) {
  if (test_case.expected_controlled_by != kNoControlledBy)
    EXPECT_EQ(pref->controlled_by, test_case.expected_controlled_by);

  if (test_case.expected_enforcement != kNoEnforcement)
    EXPECT_EQ(pref->enforcement, test_case.expected_enforcement);

  if (test_case.expected_recommended_value != kNoRecommendedValue)
    EXPECT_EQ(
        static_cast<CookiePrimarySetting>(pref->recommended_value->GetInt()),
        test_case.expected_recommended_value);

  // Ensure user selectable values are as expected. Ordering is enforced here
  // despite not being required by the SettingsPrivate API.
  // First convert std::vector<std::unique_ptr<base::value(T)>> to
  // std::vector<T> for easier comparison.
  std::vector<CookiePrimarySetting> pref_user_selectable_values;
  if (pref->user_selectable_values) {
    for (const auto& value : *pref->user_selectable_values) {
      pref_user_selectable_values.push_back(
          static_cast<CookiePrimarySetting>(value->GetInt()));
    }
  }
  EXPECT_EQ(pref_user_selectable_values.size(),
            test_case.expected_user_selectable_values.size());

  // Avoid crashing the test if the previous check fails.
  if (pref_user_selectable_values.size() ==
      test_case.expected_user_selectable_values.size())
    EXPECT_TRUE(std::equal(pref_user_selectable_values.begin(),
                           pref_user_selectable_values.end(),
                           test_case.expected_user_selectable_values.begin()));
}

}  // namespace

class GeneratedCookiePrefsTest : public testing::Test {
 protected:
  TestingProfile* profile() { return &profile_; }

 private:
  content::BrowserTaskEnvironment task_environment_;
  TestingProfile profile_;
};

class TestGeneratedPrefObserver
    : public extensions::settings_private::GeneratedPref::Observer {
 public:
  void OnGeneratedPrefChanged(const std::string& pref_name) override {
    updated_pref_name_ = pref_name;
  }
  void Reset() { updated_pref_name_ = ""; }
  std::string GetUpdatedPrefName() { return updated_pref_name_; }

 protected:
  std::string updated_pref_name_;
};

TEST_F(GeneratedCookiePrefsTest, PrimarySettingPref) {
  auto pref =
      std::make_unique<content_settings::GeneratedCookiePrimarySettingPref>(
          profile());
  HostContentSettingsMap* map =
      HostContentSettingsMapFactory::GetForProfile(profile());
  sync_preferences::TestingPrefServiceSyncable* prefs =
      profile()->GetTestingPrefService();

  // Setup a baseline content setting and preference state.
  map->SetDefaultContentSetting(ContentSettingsType::COOKIES,
                                ContentSetting::CONTENT_SETTING_ALLOW);
  prefs->SetDefaultPrefValue(prefs::kBlockThirdPartyCookies,
                             base::Value(false));
  prefs->SetDefaultPrefValue(
      prefs::kCookieControlsMode,
      base::Value(static_cast<int>(CookieControlsMode::kOff)));

  // Check that each of the four possible preference values sets the correct
  // state and is correctly reflected in a newly returned PrefObject.
  // First test this without the improved cookie controls enabled.
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitWithFeatures({}, {kImprovedCookieControls});
  ValidatePrimarySettingPrefValue(
      map, prefs, pref.get(), CookiePrimarySetting::BLOCK_ALL,
      ContentSetting::CONTENT_SETTING_BLOCK, /* block 3P */ true,
      CookieControlsMode::kBlockThirdParty, CookiePrimarySetting::BLOCK_ALL);
  ValidatePrimarySettingPrefValue(
      map, prefs, pref.get(), CookiePrimarySetting::BLOCK_THIRD_PARTY,
      ContentSetting::CONTENT_SETTING_ALLOW, /* block 3P */ true,
      CookieControlsMode::kBlockThirdParty,
      CookiePrimarySetting::BLOCK_THIRD_PARTY);
  ValidatePrimarySettingPrefValue(
      map, prefs, pref.get(), CookiePrimarySetting::BLOCK_THIRD_PARTY_INCOGNITO,
      ContentSetting::CONTENT_SETTING_ALLOW, /* block 3P */ false,
      CookieControlsMode::kIncognitoOnly, CookiePrimarySetting::ALLOW_ALL);
  ValidatePrimarySettingPrefValue(
      map, prefs, pref.get(), CookiePrimarySetting::ALLOW_ALL,
      ContentSetting::CONTENT_SETTING_ALLOW, /* block 3P */ false,
      CookieControlsMode::kOff, CookiePrimarySetting::ALLOW_ALL);

  // Enable improved cookie controls feature and check the pref is capable of
  // returning the incognito setting.
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures({kImprovedCookieControls}, {});
  ValidatePrimarySettingPrefValue(
      map, prefs, pref.get(), CookiePrimarySetting::BLOCK_THIRD_PARTY_INCOGNITO,
      ContentSetting::CONTENT_SETTING_ALLOW, /* block 3P */ false,
      CookieControlsMode::kIncognitoOnly,
      CookiePrimarySetting::BLOCK_THIRD_PARTY_INCOGNITO);

  // Confirm that a type mismatch is reported as such.
  EXPECT_EQ(pref->SetPref(std::make_unique<base::Value>(true).get()),
            extensions::settings_private::SetPrefResult::PREF_TYPE_MISMATCH);
  // Check a numerical value outside of the acceptable range.
  EXPECT_EQ(
      pref->SetPref(std::make_unique<base::Value>(
                        static_cast<int>(CookiePrimarySetting::BLOCK_ALL) + 1)
                        .get()),
      extensions::settings_private::SetPrefResult::PREF_TYPE_MISMATCH);

  // Confirm that when content settings are managed, un-managed preferences are
  // still set.
  auto provider = std::make_unique<content_settings::MockProvider>();
  provider->SetWebsiteSetting(
      ContentSettingsPattern::Wildcard(), ContentSettingsPattern::Wildcard(),
      ContentSettingsType::COOKIES, std::string(),
      std::make_unique<base::Value>(ContentSetting::CONTENT_SETTING_ALLOW));
  content_settings::TestUtils::OverrideProvider(
      map, std::move(provider), HostContentSettingsMap::POLICY_PROVIDER);
  ValidatePrimarySettingPrefValue(
      map, prefs, pref.get(), CookiePrimarySetting::BLOCK_THIRD_PARTY,
      ContentSetting::CONTENT_SETTING_ALLOW, /* block 3P */ true,
      CookieControlsMode::kBlockThirdParty,
      CookiePrimarySetting::BLOCK_THIRD_PARTY);

  // Update source preferences and ensure that an observer is fired.
  TestGeneratedPrefObserver test_observer;
  pref->AddObserver(&test_observer);
  prefs->SetUserPref(prefs::kBlockThirdPartyCookies,
                     std::make_unique<base::Value>(false));
  EXPECT_EQ(test_observer.GetUpdatedPrefName(), kCookiePrimarySetting);

  test_observer.Reset();
  prefs->SetUserPref(prefs::kCookieControlsMode,
                     std::make_unique<base::Value>(
                         static_cast<int>(CookieControlsMode::kOff)));
  EXPECT_EQ(test_observer.GetUpdatedPrefName(), kCookiePrimarySetting);
}

TEST_F(GeneratedCookiePrefsTest, PrimarySettingPrefManagedState) {
  for (const auto& test_case : managed_test_cases) {
    TestingProfile profile;
    HostContentSettingsMap* map =
        HostContentSettingsMapFactory::GetForProfile(&profile);
    sync_preferences::TestingPrefServiceSyncable* prefs =
        profile.GetTestingPrefService();
    testing::Message scope_message;
    scope_message << "Content Setting:" << test_case.default_content_setting
                  << " Block Third Party:"
                  << static_cast<int>(test_case.block_third_party);
    SCOPED_TRACE(scope_message);
    SetupManagedTestConditions(map, prefs, test_case);
    auto pref =
        std::make_unique<content_settings::GeneratedCookiePrimarySettingPref>(
            &profile);
    auto pref_object = pref->GetPrefObject();
    ValidateManagedPreference(pref_object.get(), test_case);
  }
}

TEST_F(GeneratedCookiePrefsTest, SessionOnlyPref) {
  auto pref =
      std::make_unique<content_settings::GeneratedCookieSessionOnlyPref>(
          profile());
  HostContentSettingsMap* map =
      HostContentSettingsMapFactory::GetForProfile(profile());

  // Ensure an allow content setting sets the preference to false and enabled.
  map->SetDefaultContentSetting(ContentSettingsType::COOKIES,
                                ContentSetting::CONTENT_SETTING_ALLOW);
  auto pref_object = pref->GetPrefObject();
  EXPECT_FALSE(pref_object->value->GetBool());
  EXPECT_FALSE(*pref_object->user_control_disabled);

  // Ensure setting the preference correctly updates content settings and the
  // preference state.
  EXPECT_EQ(pref->SetPref(std::make_unique<base::Value>(true).get()),
            extensions::settings_private::SetPrefResult::SUCCESS);
  EXPECT_EQ(
      map->GetDefaultContentSetting(ContentSettingsType::COOKIES, nullptr),
      ContentSetting::CONTENT_SETTING_SESSION_ONLY);
  pref_object = pref->GetPrefObject();
  EXPECT_TRUE(pref_object->value->GetBool());
  EXPECT_FALSE(*pref_object->user_control_disabled);

  EXPECT_EQ(pref->SetPref(std::make_unique<base::Value>(false).get()),
            extensions::settings_private::SetPrefResult::SUCCESS);
  EXPECT_EQ(
      map->GetDefaultContentSetting(ContentSettingsType::COOKIES, nullptr),
      ContentSetting::CONTENT_SETTING_ALLOW);
  pref_object = pref->GetPrefObject();
  EXPECT_FALSE(pref_object->value->GetBool());
  EXPECT_FALSE(*pref_object->user_control_disabled);

  // Ensure a block content setting results in a disabled and false pref.
  map->SetDefaultContentSetting(ContentSettingsType::COOKIES,
                                ContentSetting::CONTENT_SETTING_BLOCK);
  pref_object = pref->GetPrefObject();
  EXPECT_FALSE(pref_object->value->GetBool());
  EXPECT_TRUE(*pref_object->user_control_disabled);

  // Confirm that the pref cannot be changed while the content setting is block.
  EXPECT_EQ(pref->SetPref(std::make_unique<base::Value>(true).get()),
            extensions::settings_private::SetPrefResult::PREF_NOT_MODIFIABLE);

  // Confirm that a type mismatch is reported as such.
  EXPECT_EQ(pref->SetPref(std::make_unique<base::Value>(2).get()),
            extensions::settings_private::SetPrefResult::PREF_TYPE_MISMATCH);

  // Ensure management state is correctly reported for all possible content
  // setting management sources.
  auto provider = std::make_unique<content_settings::MockProvider>();
  provider->SetWebsiteSetting(
      ContentSettingsPattern::Wildcard(), ContentSettingsPattern::Wildcard(),
      ContentSettingsType::COOKIES, std::string(),
      std::make_unique<base::Value>(ContentSetting::CONTENT_SETTING_ALLOW));
  content_settings::TestUtils::OverrideProvider(
      map, std::move(provider),
      HostContentSettingsMap::CUSTOM_EXTENSION_PROVIDER);
  pref_object = pref->GetPrefObject();
  EXPECT_EQ(pref_object->controlled_by,
            settings_api::ControlledBy::CONTROLLED_BY_EXTENSION);
  EXPECT_EQ(pref_object->enforcement,
            settings_api::Enforcement::ENFORCEMENT_ENFORCED);

  provider = std::make_unique<content_settings::MockProvider>();
  provider->SetWebsiteSetting(
      ContentSettingsPattern::Wildcard(), ContentSettingsPattern::Wildcard(),
      ContentSettingsType::COOKIES, std::string(),
      std::make_unique<base::Value>(ContentSetting::CONTENT_SETTING_ALLOW));
  content_settings::TestUtils::OverrideProvider(
      map, std::move(provider), HostContentSettingsMap::SUPERVISED_PROVIDER);
  pref_object = pref->GetPrefObject();
  EXPECT_EQ(pref_object->controlled_by,
            settings_api::ControlledBy::CONTROLLED_BY_CHILD_RESTRICTION);
  EXPECT_EQ(pref_object->enforcement,
            settings_api::Enforcement::ENFORCEMENT_ENFORCED);

  provider = std::make_unique<content_settings::MockProvider>();
  provider->SetWebsiteSetting(
      ContentSettingsPattern::Wildcard(), ContentSettingsPattern::Wildcard(),
      ContentSettingsType::COOKIES, std::string(),
      std::make_unique<base::Value>(ContentSetting::CONTENT_SETTING_ALLOW));
  content_settings::TestUtils::OverrideProvider(
      map, std::move(provider), HostContentSettingsMap::POLICY_PROVIDER);
  pref_object = pref->GetPrefObject();
  EXPECT_EQ(pref_object->controlled_by,
            settings_api::ControlledBy::CONTROLLED_BY_DEVICE_POLICY);
  EXPECT_EQ(pref_object->enforcement,
            settings_api::Enforcement::ENFORCEMENT_ENFORCED);

  // Ensure the preference cannot be changed when it is enforced.
  EXPECT_EQ(pref->SetPref(std::make_unique<base::Value>(true).get()),
            extensions::settings_private::SetPrefResult::PREF_NOT_MODIFIABLE);
  EXPECT_EQ(
      map->GetDefaultContentSetting(ContentSettingsType::COOKIES, nullptr),
      ContentSetting::CONTENT_SETTING_ALLOW);
}

}  // namespace content_settings
