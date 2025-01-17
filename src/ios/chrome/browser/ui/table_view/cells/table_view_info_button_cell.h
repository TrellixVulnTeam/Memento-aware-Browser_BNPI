// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_UI_TABLE_VIEW_CELLS_TABLE_VIEW_INFO_BUTTON_CELL_H_
#define IOS_CHROME_BROWSER_UI_TABLE_VIEW_CELLS_TABLE_VIEW_INFO_BUTTON_CELL_H_

#import <UIKit/UIKit.h>

#import "ios/chrome/browser/ui/table_view/cells/table_view_cell.h"

// TableViewInfoButtonCell implements a TableViewCell subclass containing an
// icon, a text label, a detail text, a status text and an info button. If the
// preferred content size category is an accessibility category, the status text
// is displayed below the detail text, and the info button is below the status
// text. Otherwise, they are on the trailing side.
@interface TableViewInfoButtonCell : TableViewCell

// UILabel displayed next to the leading image icon if there is one, otherwise
// this UILabel will be at the leading position. Corresponding to |text| from
// the item.
@property(nonatomic, readonly, strong) UILabel* textLabel;

// UILabel displayed under the |textLabel| shows the description text.
// Corresponding to |detailText| from the item.
@property(nonatomic, readonly, strong) UILabel* detailTextLabel;

// UILabel displayed at the trailing side of the view, it's trailing anchor
// align with the leading anchor of the |bubbleView| below. It shows the status
// of the setting's row. Mostly show On or Off, but there is use case that shows
// a search engine name. Corresponding to |statusText| from the item.
@property(nonatomic, readonly, strong) UILabel* statusTextLabel;

// UIButton displayed aligned to the trailing of the view.
@property(nonatomic, readonly, strong) UIButton* trailingButton;

// Sets the image that should be displayed at the leading edge of the cell. If
// set to nil, the icon will be hidden and the remaining content will expand to
// fill the full width of the cell.
- (void)setIconImage:(UIImage*)image;

@end

#endif  // IOS_CHROME_BROWSER_UI_TABLE_VIEW_CELLS_TABLE_VIEW_INFO_BUTTON_CELL_H_
