// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_WINDOW_ACTIVITIES_WINDOW_ACTIVITY_HELPERS_H_
#define IOS_CHROME_BROWSER_WINDOW_ACTIVITIES_WINDOW_ACTIVITY_HELPERS_H_

#import <Foundation/Foundation.h>

#import "ios/web/public/navigation/referrer.h"
#import "url/gurl.h"

struct UrlLoadParams;

// Window activity origins.  Please add new origins at the end, to keep
// numeric values of existing origins.
typedef NS_ENUM(NSInteger, WindowActivityOrigin) {
  WindowActivityUnknownOrigin = 0,
  // The command origin comes outside of chrome.
  WindowActivityExternalOrigin,
  // The command origin comes from restoring a session.
  WindowActivityRestoredOrigin,
  // The command origin comes from the context menu.
  WindowActivityContextMenuOrigin,
  // The command origin comes from the reading list.
  WindowActivityReadingListOrigin,
  // The command origin comes from bookmarks.
  WindowActivityBookmarksOrigin,
  // The command origin comes from history.
  WindowActivityHistoryOrigin,
  // The command origin comes from tools.
  WindowActivityToolsOrigin,
  // Size of enum.
  kMaxValue = WindowActivityToolsOrigin
};

// Helper functions to create NSUserActivity instances that encode specific
// actions in the browser, and to decode those actions from those activities.

// Create a new activity that opens a new, empty tab. |in_incognito| indicates
// if the new tab should be incognito.
NSUserActivity* ActivityToOpenNewTab(bool in_incognito);

// Create a new activity that opens a new tab, loading |url| with the referrer
// |referrer|. |in_incognito| indicates if the new tab should be incognito.
NSUserActivity* ActivityToLoadURL(WindowActivityOrigin origin,
                                  const GURL& url,
                                  const web::Referrer& referrer,
                                  bool in_incognito);

// Create a new activity that opens a new regular (non-incognito) tab, loading
// |url|.
NSUserActivity* ActivityToLoadURL(WindowActivityOrigin origin, const GURL& url);

// Create a new activity that moves a tab either between browsers, or reorders
// within a browser.
NSUserActivity* ActivityToMoveTab(NSString* tab_id);

// true if |activity| is one that indicates a URL load (including loading the
// new tab page in a new tab).
bool ActivityIsURLLoad(NSUserActivity* activity);

// true if |activity| is one that indicates a tab move.
bool ActivityIsTabMove(NSUserActivity* activity);

// The URLLoadParams needed to perform the load defined in |activity|, if any.
// If |activity| is not a URL load activity, the default UrlLoadParams are
// returned.
UrlLoadParams LoadParamsFromActivity(NSUserActivity* activity);

// Returns the recorded origin for the given activity.
WindowActivityOrigin OriginOfActivity(NSUserActivity* activity);

#endif  // IOS_CHROME_BROWSER_WINDOW_ACTIVITIES_WINDOW_ACTIVITY_HELPERS_H_
