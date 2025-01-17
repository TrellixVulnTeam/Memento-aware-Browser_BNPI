// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.share.screenshot;

import android.support.test.annotation.UiThreadTest;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;

import androidx.test.filters.MediumTest;

import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.Callback;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.share.R;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.util.browser.Features;
import org.chromium.components.browser_ui.widget.selectable_list.SelectionDelegate;
import org.chromium.content_public.browser.test.util.TestThreadUtils;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.modelutil.PropertyModelChangeProcessor;
import org.chromium.ui.test.util.DummyUiActivityTestCase;

import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Tests for the {@link ScreenshotShareSheetView}.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@Features.EnableFeatures(ChromeFeatureList.CHROME_SHARE_SCREENSHOT)
public class ScreenshotShareSheetViewTest extends DummyUiActivityTestCase {
    private ScreenshotShareSheetView mScreenshotView;
    private PropertyModel mScreenshotModel;
    private PropertyModelChangeProcessor mScreenshotMCP;

    private AtomicBoolean mCloseClicked = new AtomicBoolean();
    private AtomicBoolean mShareClicked = new AtomicBoolean();
    private AtomicBoolean mSaveClicked = new AtomicBoolean();

    private Callback<Integer> mMockNoArgListener = new Callback<Integer>() {
        @Override
        public void onResult(@ScreenshotShareSheetViewProperties.NoArgOperation Integer operation) {
            if (ScreenshotShareSheetViewProperties.NoArgOperation.SHARE == operation) {
                mShareClicked.set(true);
            } else if (ScreenshotShareSheetViewProperties.NoArgOperation.SAVE == operation) {
                mSaveClicked.set(true);
            } else if (ScreenshotShareSheetViewProperties.NoArgOperation.DELETE == operation) {
                mCloseClicked.set(true);
            }
        }
    };

    @Override
    public void setUpTest() throws Exception {
        super.setUpTest();
        ViewGroup view = new LinearLayout(getActivity());
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);

        TestThreadUtils.runOnUiThreadBlocking(() -> {
            getActivity().setContentView(view, params);

            mScreenshotView = (ScreenshotShareSheetView) getActivity().getLayoutInflater().inflate(
                    R.layout.screenshot_share_sheet, null);

            view.addView(mScreenshotView);
        });

        SelectionDelegate<Integer> mSelectionDelegate = new SelectionDelegate<>();

        mScreenshotModel =
                new PropertyModel.Builder(ScreenshotShareSheetViewProperties.ALL_KEYS)
                        .with(ScreenshotShareSheetViewProperties.NO_ARG_OPERATION_LISTENER,
                                mMockNoArgListener)
                        .build();
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            mScreenshotMCP = PropertyModelChangeProcessor.create(
                    mScreenshotModel, mScreenshotView, ScreenshotShareSheetViewBinder::bind);
        });
    }

    @Test
    @MediumTest
    @UiThreadTest
    public void testClickToClose() {
        ImageView closeButton = mScreenshotView.findViewById(R.id.close_button);

        Assert.assertFalse(mCloseClicked.get());
        closeButton.performClick();
        Assert.assertTrue(mCloseClicked.get());
        mCloseClicked.set(false);
    }

    @Override
    public void tearDownTest() throws Exception {
        mScreenshotMCP.destroy();
        super.tearDownTest();
    }
}
