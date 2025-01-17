// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/privacy_budget/scoped_privacy_budget_config.h"

#include "chrome/browser/privacy_budget/field_trial_param_conversions.h"
#include "chrome/browser/privacy_budget/privacy_budget_features.h"
#include "third_party/blink/public/common/privacy_budget/identifiable_surface.h"

namespace test {

ScopedPrivacyBudgetConfig::Parameters::Parameters() = default;
ScopedPrivacyBudgetConfig::Parameters::Parameters(const Parameters&) = default;
ScopedPrivacyBudgetConfig::Parameters::Parameters(Parameters&&) = default;
ScopedPrivacyBudgetConfig::Parameters::~Parameters() = default;
ScopedPrivacyBudgetConfig::~ScopedPrivacyBudgetConfig() = default;
ScopedPrivacyBudgetConfig::ScopedPrivacyBudgetConfig() = default;

ScopedPrivacyBudgetConfig::ScopedPrivacyBudgetConfig(
    const Parameters& parameters) {
  Apply(parameters);
}

void ScopedPrivacyBudgetConfig::Apply(const Parameters& parameters) {
  if (!parameters.enabled) {
    scoped_feature_list_.InitAndDisableFeature(features::kIdentifiabilityStudy);
    return;
  }

  Parameters defaults;

  base::FieldTrialParams ftp;

  ftp.insert({features::kIdentifiabilityStudyGeneration.name,
              base::NumberToString(parameters.generation)});

  if (!parameters.blocked_surfaces.empty()) {
    ftp.insert(
        {features::kIdentifiabilityStudyBlockedMetrics.name,
         EncodeIdentifiabilityFieldTrialParam(parameters.blocked_surfaces)});
  }
  if (!parameters.blocked_types.empty()) {
    ftp.insert(
        {features::kIdentifiabilityStudyBlockedTypes.name,
         EncodeIdentifiabilityFieldTrialParam(parameters.blocked_types)});
  }

  ftp.insert({features::kIdentifiabilityStudySurfaceSelectionRate.name,
              base::NumberToString(parameters.surface_selection_rate)});

  if (parameters.max_surfaces != defaults.max_surfaces) {
    ftp.insert({features::kIdentifiabilityStudyMaxSurfaces.name,
                base::NumberToString(parameters.max_surfaces)});
  }
  if (!parameters.per_surface_sampling_rate.empty()) {
    ftp.insert({features::kIdentifiabilityStudyPerSurfaceSettings.name,
                EncodeIdentifiabilityFieldTrialParam(
                    parameters.per_surface_sampling_rate)});
  }
  if (!parameters.per_type_sampling_rate.empty()) {
    ftp.insert({features::kIdentifiabilityStudyPerTypeSettings.name,
                EncodeIdentifiabilityFieldTrialParam(
                    parameters.per_type_sampling_rate)});
  }

  scoped_feature_list_.InitAndEnableFeatureWithParameters(
      features::kIdentifiabilityStudy, ftp);
}

}  // namespace test
