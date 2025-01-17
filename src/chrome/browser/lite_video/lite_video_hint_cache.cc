// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/lite_video/lite_video_hint_cache.h"

#include "chrome/browser/lite_video/lite_video_features.h"
#include "chrome/browser/lite_video/lite_video_hint.h"

namespace lite_video {

LiteVideoHintCache::LiteVideoHintCache()
    : origin_hints_(features::GetLiteVideoOriginHintsFromFieldTrial()) {}

LiteVideoHintCache::~LiteVideoHintCache() = default;

base::Optional<LiteVideoHint> LiteVideoHintCache::GetHintForNavigationURL(
    const GURL& url) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!origin_hints_ || !origin_hints_->is_dict())
    return base::nullopt;

  base::Optional<int> target_downlink_bandwidth_kbps =
      origin_hints_->FindIntKey(url.host());

  if (!target_downlink_bandwidth_kbps)
    return base::nullopt;

  return LiteVideoHint(*target_downlink_bandwidth_kbps,
                       features::LiteVideoTargetDownlinkRTTLatencyMs(),
                       features::LiteVideoKilobytesToBufferBeforeThrottle());
}

}  // namespace lite_video
