# Copyright 2020 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

luci.console_view(
    name = 'android.packager',
    header = '//chromium-header.textpb',
    repo = 'https://chromium.googlesource.com/chromium/src',
    entries = [
        luci.console_view_entry(
            builder = 'ci/android-avd-packager',
            short_name = 'avd',
        ),
        luci.console_view_entry(
            builder = 'ci/android-sdk-packager',
            short_name = 'sdk',
        ),
    ],
)
