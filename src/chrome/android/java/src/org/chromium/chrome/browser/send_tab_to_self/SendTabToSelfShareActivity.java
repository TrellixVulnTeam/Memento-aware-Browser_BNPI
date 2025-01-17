// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.send_tab_to_self;

import android.content.Context;

import androidx.annotation.VisibleForTesting;

import org.chromium.chrome.browser.ChromeAccessorActivity;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetContent;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.content_public.browser.NavigationEntry;
import org.chromium.content_public.browser.WebContents;

/**
 * A simple activity that allows Chrome to expose send tab to self as an option in the share menu.
 */
public class SendTabToSelfShareActivity extends ChromeAccessorActivity {
    private static BottomSheetContent sBottomSheetContentForTesting;

    @Override
    protected void handleAction(ChromeActivity triggeringActivity) {
        Tab tab = triggeringActivity.getActivityTabProvider().get();
        if (tab == null) return;
        NavigationEntry entry = tab.getWebContents().getNavigationController().getVisibleEntry();
        actionHandler(triggeringActivity, entry, triggeringActivity.getBottomSheetController(),
                tab.getWebContents());
    }

    public static void actionHandler(Context context, NavigationEntry entry,
            BottomSheetController controller, WebContents webContents) {
        if (entry == null || controller == null) {
            return;
        }

        controller.requestShowContent(
                createBottomSheetContent(context, entry, controller, webContents), true);
        // TODO(crbug.com/968246): Remove the need to call this explicitly and instead have it
        // automatically show since PeekStateEnabled is set to false.
        controller.expandSheet();
    }

    static BottomSheetContent createBottomSheetContent(Context context, NavigationEntry entry,
            BottomSheetController controller, WebContents webContents) {
        if (sBottomSheetContentForTesting != null) {
            return sBottomSheetContentForTesting;
        }
        return new DevicePickerBottomSheetContent(context, entry, controller, webContents);
    }

    public static boolean featureIsAvailable(Tab currentTab) {
        return SendTabToSelfAndroidBridge.isFeatureAvailable(currentTab.getWebContents());
    }

    @VisibleForTesting
    public static void setBottomSheetContentForTesting(BottomSheetContent bottomSheetContent) {
        sBottomSheetContentForTesting = bottomSheetContent;
    }
}
