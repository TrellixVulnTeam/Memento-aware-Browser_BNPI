// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/privacy_budget/privacy_budget_features.h"

#include "chrome/browser/privacy_budget/identifiability_study_state.h"

namespace features {

const base::Feature kIdentifiabilityStudy = {"IdentifiabilityStudy",
                                             base::FEATURE_DISABLED_BY_DEFAULT};

const base::FeatureParam<int> kIdentifiabilityStudyGeneration = {
    &kIdentifiabilityStudy, "Gen", 0};

const base::FeatureParam<std::string> kIdentifiabilityStudyBlockedMetrics = {
    &kIdentifiabilityStudy, "BlockedHashes", ""};

const base::FeatureParam<std::string> kIdentifiabilityStudyBlockedTypes = {
    &kIdentifiabilityStudy, "BlockedTypes", ""};

const base::FeatureParam<int> kIdentifiabilityStudySurfaceSelectionRate = {
    &kIdentifiabilityStudy, "Rho", 0};

const base::FeatureParam<int> kIdentifiabilityStudyMaxSurfaces = {
    &kIdentifiabilityStudy, "Max",
    IdentifiabilityStudyState::kMaxSampledIdentifiableSurfaces};

const base::FeatureParam<std::string> kIdentifiabilityStudyPerSurfaceSettings =
    {&kIdentifiabilityStudy, "HashRate", ""};

const base::FeatureParam<std::string> kIdentifiabilityStudyPerTypeSettings = {
    &kIdentifiabilityStudy, "TypeRate", ""};

}  // namespace features
