// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.toolbar;

import static junit.framework.Assert.assertEquals;

import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.doReturn;
import static org.robolectric.Shadows.shadowOf;

import android.app.Activity;
import android.content.res.ColorStateList;
import android.graphics.Bitmap;
import android.graphics.drawable.BitmapDrawable;
import android.view.LayoutInflater;
import android.view.ViewGroup;
import android.widget.LinearLayout;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.robolectric.Robolectric;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowDrawable;

import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.omaha.UpdateMenuItemHelper;

/**
 * Unit tests for MenuButton.
 */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class MenuButtonTest {
    @Mock
    private UpdateMenuItemHelper mUpdateMenuItemHelper;
    @Mock
    private ColorStateList mColorStateList;

    private Activity mActivity;
    private MenuButton mMenuButton;
    private UpdateMenuItemHelper.MenuUiState mMenuUiState;

    @Before
    public void setUp() {
        MockitoAnnotations.initMocks(this);
        mActivity = Robolectric.buildActivity(Activity.class).setup().get();
        mActivity.setTheme(org.chromium.chrome.R.style.Theme_AppCompat);
        mMenuButton = (MenuButton) ((ViewGroup) LayoutInflater.from(mActivity).inflate(
                                            R.layout.menu_button, new LinearLayout(mActivity)))
                              .getChildAt(0);

        mMenuUiState = new UpdateMenuItemHelper.MenuUiState();
        mMenuUiState.buttonState = new UpdateMenuItemHelper.MenuButtonState();
        mMenuUiState.buttonState.menuContentDescription =
                R.string.accessibility_toolbar_btn_menu_update;
        mMenuUiState.buttonState.darkBadgeIcon = R.drawable.badge_update_dark;
        mMenuUiState.buttonState.lightBadgeIcon = R.drawable.badge_update_light;

        UpdateMenuItemHelper.setInstanceForTesting(mUpdateMenuItemHelper);
        doReturn(mMenuUiState).when(mUpdateMenuItemHelper).getUiState();
    }

    @Test
    public void testTabSwitcherAnimationOverlay_normalButton() {
        // The underlying image resource for the badge-less MenuButton is defined in XML (as opposed
        // to being selected at runtime like the badge), so it's sufficient to check that the
        // drawable for the button refers to the same bitmap as the drawable that's drawn.
        Bitmap drawnBitmap =
                ((BitmapDrawable) mMenuButton.getTabSwitcherAnimationDrawable()).getBitmap();
        Bitmap menuButtonBitmap =
                ((BitmapDrawable) mMenuButton.getImageButton().getDrawable()).getBitmap();
        assertTrue(drawnBitmap == menuButtonBitmap);
    }

    @Test
    public void testDrawTabSwitcherAnimationOverlay_updateBadge() {
        // The underlying image resource for the badged MenuButton is selected at runtime, so we
        // need to check that the drawn Drawable refers to the same resource id as the one specified
        // by UpdateMenuItemHelper.
        ShadowDrawable lightDrawable = shadowOf(ApiCompatibilityUtils.getDrawable(
                mActivity.getResources(), mMenuUiState.buttonState.lightBadgeIcon));
        ShadowDrawable darkDrawable = shadowOf(ApiCompatibilityUtils.getDrawable(
                mActivity.getResources(), mMenuUiState.buttonState.darkBadgeIcon));

        mMenuButton.showAppMenuUpdateBadgeIfAvailable(false);
        ShadowDrawable drawnDrawable = shadowOf(mMenuButton.getTabSwitcherAnimationDrawable());
        assertEquals(drawnDrawable.getCreatedFromResId(), darkDrawable.getCreatedFromResId());
        assertNotEquals(drawnDrawable.getCreatedFromResId(), lightDrawable.getCreatedFromResId());

        mMenuButton.onTintChanged(mColorStateList, true);
        drawnDrawable = shadowOf(mMenuButton.getTabSwitcherAnimationDrawable());
        assertEquals(drawnDrawable.getCreatedFromResId(), lightDrawable.getCreatedFromResId());
        assertNotEquals(drawnDrawable.getCreatedFromResId(), darkDrawable.getCreatedFromResId());
    }

    @Test
    public void testDrawTabSwitcherAnimationOverlay_updateBadgeNotAvailable() {
        mMenuUiState.buttonState = null;
        mMenuButton.showAppMenuUpdateBadgeIfAvailable(false);

        Bitmap drawnBitmap =
                ((BitmapDrawable) mMenuButton.getTabSwitcherAnimationDrawable()).getBitmap();
        Bitmap menuButtonBitmap =
                ((BitmapDrawable) mMenuButton.getImageButton().getDrawable()).getBitmap();
        assertTrue(drawnBitmap == menuButtonBitmap);
    }
}
