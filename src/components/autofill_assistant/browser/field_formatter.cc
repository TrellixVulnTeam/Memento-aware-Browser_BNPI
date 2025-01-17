// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill_assistant/browser/field_formatter.h"

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "components/autofill/core/browser/autofill_data_util.h"
#include "components/autofill/core/browser/autofill_type.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill_assistant/browser/generic_ui.pb.h"
#include "third_party/re2/src/re2/re2.h"
#include "third_party/re2/src/re2/stringpiece.h"

namespace {
// Regex to find placeholders of the form ${key}, where key is an arbitrary
// string that does not contain curly braces.
const char kPlaceholderExtractor[] = R"re(\$\{([^{}]+)\})re";

base::Optional<std::string> GetFieldValue(
    const std::map<std::string, std::string>& mappings,
    const std::string& key) {
  auto it = mappings.find(key);
  if (it == mappings.end()) {
    return base::nullopt;
  }
  return it->second;
}

std::map<std::string, std::string> CreateFormGroupMappings(
    const autofill::FormGroup& form_group,
    const std::string& locale) {
  std::map<std::string, std::string> mappings;
  autofill::ServerFieldTypeSet available_fields;
  form_group.GetNonEmptyTypes(locale, &available_fields);
  for (const auto& field : available_fields) {
    mappings.emplace(base::NumberToString(static_cast<int>(field)),
                     base::UTF16ToUTF8(form_group.GetInfo(
                         autofill::AutofillType(field), locale)));
  }
  return mappings;
}

}  // namespace

namespace autofill_assistant {
namespace field_formatter {

base::Optional<std::string> FormatString(
    const std::string& pattern,
    const std::map<std::string, std::string>& mappings) {
  if (pattern.empty()) {
    return std::string();
  }

  std::string key;
  std::string out = pattern;
  re2::StringPiece input(pattern);
  while (re2::RE2::FindAndConsume(&input, kPlaceholderExtractor, &key)) {
    auto rewrite_value = GetFieldValue(mappings, key);
    if (!rewrite_value.has_value()) {
      VLOG(2) << "No value for " << key << " in " << pattern;
      return base::nullopt;
    }

    re2::RE2::Replace(&out, kPlaceholderExtractor,
                      re2::StringPiece(rewrite_value.value()));
  }

  return out;
}

template <>
std::map<std::string, std::string>
CreateAutofillMappings<autofill::AutofillProfile>(
    const autofill::AutofillProfile& profile,
    const std::string& locale) {
  return CreateFormGroupMappings(profile, locale);
}

template <>
std::map<std::string, std::string> CreateAutofillMappings<autofill::CreditCard>(
    const autofill::CreditCard& credit_card,
    const std::string& locale) {
  auto mappings = CreateFormGroupMappings(credit_card, locale);

  auto network = std::string(
      autofill::data_util::GetPaymentRequestData(credit_card.network())
          .basic_card_issuer_network);
  if (!network.empty()) {
    mappings[base::NumberToString(
        static_cast<int>(AutofillFormatProto::CREDIT_CARD_NETWORK))] = network;
  }
  auto network_for_display = base::UTF16ToUTF8(credit_card.NetworkForDisplay());
  if (!network_for_display.empty()) {
    mappings[base::NumberToString(static_cast<int>(
        AutofillFormatProto::CREDIT_CARD_NETWORK_FOR_DISPLAY))] =
        network_for_display;
  }
  auto last_four_digits = base::UTF16ToUTF8(credit_card.LastFourDigits());
  if (!last_four_digits.empty()) {
    mappings[base::NumberToString(static_cast<int>(
        AutofillFormatProto::CREDIT_CARD_NUMBER_LAST_FOUR_DIGITS))] =
        last_four_digits;
  }

  return mappings;
}

}  // namespace field_formatter
}  // namespace autofill_assistant
