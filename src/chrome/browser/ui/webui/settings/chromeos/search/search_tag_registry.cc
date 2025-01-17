// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/chromeos/search/search_tag_registry.h"

#include <sstream>

#include "base/feature_list.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/chromeos/local_search_service/local_search_service.h"
#include "chrome/browser/ui/webui/settings/chromeos/search/search_concept.h"
#include "chromeos/constants/chromeos_features.h"
#include "ui/base/l10n/l10n_util.h"

namespace chromeos {
namespace settings {
namespace {

std::vector<int> GetMessageIds(const SearchConcept& concept) {
  // Start with only the canonical ID.
  std::vector<int> alt_tag_message_ids{concept.canonical_message_id};

  // Add alternate IDs, if they exist.
  for (size_t i = 0; i < SearchConcept::kMaxAltTagsPerConcept; ++i) {
    int curr_alt_tag_message_id = concept.alt_tag_ids[i];
    if (curr_alt_tag_message_id == SearchConcept::kAltTagEnd)
      break;
    alt_tag_message_ids.push_back(curr_alt_tag_message_id);
  }

  return alt_tag_message_ids;
}

}  // namespace

SearchTagRegistry::SearchTagRegistry(
    local_search_service::LocalSearchService* local_search_service)
    : index_(local_search_service->GetIndex(
          local_search_service::IndexId::kCrosSettings)) {}

SearchTagRegistry::~SearchTagRegistry() = default;

void SearchTagRegistry::AddSearchTags(
    const std::vector<SearchConcept>& search_tags) {
  if (!base::FeatureList::IsEnabled(features::kNewOsSettingsSearch))
    return;

  index_->AddOrUpdate(ConceptVectorToDataVector(search_tags));

  // Add each concept to the map. Note that it is safe to take the address of
  // each concept because all concepts are allocated via static
  // base::NoDestructor objects in the Get*SearchConcepts() helper functions.
  for (const auto& concept : search_tags) {
    result_id_to_metadata_list_map_[ToResultId(concept)] = &concept;
  }
}

void SearchTagRegistry::RemoveSearchTags(
    const std::vector<SearchConcept>& search_tags) {
  if (!base::FeatureList::IsEnabled(features::kNewOsSettingsSearch))
    return;

  std::vector<std::string> data_ids;
  for (const auto& concept : search_tags) {
    std::string result_id = ToResultId(concept);
    result_id_to_metadata_list_map_.erase(result_id);
    data_ids.push_back(std::move(result_id));
  }

  index_->Delete(data_ids);
}

const SearchConcept* SearchTagRegistry::GetTagMetadata(
    const std::string& result_id) const {
  const auto it = result_id_to_metadata_list_map_.find(result_id);
  if (it == result_id_to_metadata_list_map_.end())
    return nullptr;
  return it->second;
}

// static
std::string SearchTagRegistry::ToResultId(const SearchConcept& concept) {
  std::stringstream ss;
  switch (concept.type) {
    case mojom::SearchResultType::kSection:
      ss << concept.id.section;
      break;
    case mojom::SearchResultType::kSubpage:
      ss << concept.id.subpage;
      break;
    case mojom::SearchResultType::kSetting:
      ss << concept.id.setting;
      break;
  }
  ss << "," << concept.canonical_message_id;
  return ss.str();
}

std::vector<local_search_service::Data>
SearchTagRegistry::ConceptVectorToDataVector(
    const std::vector<SearchConcept>& search_tags) {
  std::vector<local_search_service::Data> data_list;

  for (const auto& concept : search_tags) {
    // Create a list of Content objects, which use the stringified version of
    // message IDs as identifiers.
    std::vector<local_search_service::Content> content_list;
    for (int message_id : GetMessageIds(concept)) {
      content_list.emplace_back(
          /*id=*/base::NumberToString(message_id),
          /*content=*/l10n_util::GetStringUTF16(message_id));
    }

    // Compute an identifier for this result; the same ID format it used in
    // GetTagMetadata().
    data_list.emplace_back(ToResultId(concept), std::move(content_list));
  }

  return data_list;
}

}  // namespace settings
}  // namespace chromeos
