// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/credential_provider_extension/ui/credential_details_view_controller.h"

#import "base/mac/foundation_util.h"
#include "ios/chrome/common/app_group/app_group_metrics.h"
#import "ios/chrome/common/credential_provider/credential.h"
#import "ios/chrome/common/ui/colors/semantic_color_names.h"
#import "ios/chrome/credential_provider_extension/metrics_util.h"
#import "ios/chrome/credential_provider_extension/ui/tooltip_view.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

NSString* kCellIdentifier = @"cdvcCell";

NSString* const kMaskedPassword = @"••••••••";

typedef NS_ENUM(NSInteger, RowIdentifier) {
  RowIdentifierURL,
  RowIdentifierUsername,
  RowIdentifierPassword,
  NumRows
};

}  // namespace

@interface CredentialDetailsViewController () <UITableViewDataSource>

// Current credential.
@property(nonatomic, weak) id<Credential> credential;

// Current clear password or nil (while locked).
@property(nonatomic, strong) NSString* clearPassword;

@end

@implementation CredentialDetailsViewController

@synthesize delegate;

- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = [UIColor colorNamed:kBackgroundColor];
  self.navigationController.navigationBar.translucent = NO;
  self.navigationController.navigationBar.backgroundColor =
      [UIColor colorNamed:kBackgroundColor];
  self.navigationItem.rightBarButtonItem = [self navigationCancelButton];
  self.tableView.tableFooterView = [[UIView alloc] initWithFrame:CGRectZero];

  NSNotificationCenter* defaultCenter = [NSNotificationCenter defaultCenter];
  [defaultCenter addObserver:self
                    selector:@selector(hidePassword)
                        name:UIApplicationDidEnterBackgroundNotification
                      object:nil];
}

#pragma mark - CredentialDetailsConsumer

- (void)presentCredential:(id<Credential>)credential {
  self.credential = credential;
  self.clearPassword = nil;
  self.title = credential.serviceName;
  [self.tableView reloadData];
}

#pragma mark - UITableViewDataSource

- (NSInteger)tableView:(UITableView*)tableView
    numberOfRowsInSection:(NSInteger)section {
  return RowIdentifier::NumRows;
}

- (UITableViewCell*)tableView:(UITableView*)tableView
        cellForRowAtIndexPath:(NSIndexPath*)indexPath {
  UITableViewCell* cell =
      [tableView dequeueReusableCellWithIdentifier:kCellIdentifier];
  if (!cell) {
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1
                                  reuseIdentifier:kCellIdentifier];
  }

  cell.selectionStyle = UITableViewCellSelectionStyleNone;
  cell.textLabel.textColor = [UIColor colorNamed:kTextPrimaryColor];
  cell.detailTextLabel.textColor = [UIColor colorNamed:kTextSecondaryColor];
  cell.contentView.backgroundColor = [UIColor colorNamed:kBackgroundColor];
  cell.backgroundColor = [UIColor colorNamed:kBackgroundColor];

  switch (indexPath.row) {
    case RowIdentifier::RowIdentifierURL:
      cell.accessoryView = nil;
      cell.textLabel.text =
          NSLocalizedString(@"IDS_IOS_CREDENTIAL_PROVIDER_DETAILS_URL", @"URL");
      cell.detailTextLabel.text = self.credential.serviceIdentifier;
      break;
    case RowIdentifier::RowIdentifierUsername:
      cell.accessoryView = nil;
      cell.textLabel.text = NSLocalizedString(
          @"IDS_IOS_CREDENTIAL_PROVIDER_DETAILS_USERNAME", @"Username");
      cell.detailTextLabel.text = self.credential.user;
      break;
    case RowIdentifier::RowIdentifierPassword:
      cell.accessoryView = [self passwordIconButton];
      cell.textLabel.text = NSLocalizedString(
          @"IDS_IOS_CREDENTIAL_PROVIDER_DETAILS_PASSWORD", @"Password");
      cell.detailTextLabel.text = [self password];
      break;
    default:
      break;
  }

  return cell;
}

#pragma mark - UITableViewDelegate

- (void)tableView:(UITableView*)tableView
    didSelectRowAtIndexPath:(NSIndexPath*)indexPath {
  // Callout menu don't show up in the extension in iOS13, even
  // though you can find it in the View Hierarchy. Using custom one.
  UITableViewCell* cell = [tableView cellForRowAtIndexPath:indexPath];

  switch (indexPath.row) {
    case RowIdentifier::RowIdentifierURL:
      [self showTootip:NSLocalizedString(
                           @"IDS_IOS_CREDENTIAL_PROVIDER_DETAILS_COPY", @"Copy")
            atBottomOf:cell
                action:@selector(copyURL)];
      break;
    case RowIdentifier::RowIdentifierUsername:
      [self showTootip:NSLocalizedString(
                           @"IDS_IOS_CREDENTIAL_PROVIDER_DETAILS_COPY", @"Copy")
            atBottomOf:cell
                action:@selector(copyUsername)];
      break;
    case RowIdentifier::RowIdentifierPassword:
      if (self.clearPassword) {
        [self
            showTootip:NSLocalizedString(
                           @"IDS_IOS_CREDENTIAL_PROVIDER_DETAILS_COPY", @"Copy")
            atBottomOf:cell
                action:@selector(copyPassword)];
      } else {
        [self
            showTootip:NSLocalizedString(
                           @"IDS_IOS_CREDENTIAL_PROVIDER_DETAILS_SHOW_PASSWORD",
                           @"Show Password")
            atBottomOf:cell
                action:@selector(showPassword)];
      }
      break;
    default:
      break;
  }
}

#pragma mark - Private

// Copy credential URL to clipboard.
- (void)copyURL {
  UIPasteboard* generalPasteboard = [UIPasteboard generalPasteboard];
  generalPasteboard.string = self.credential.serviceName;
  UpdateUMACountForKey(app_group::kCredentialExtensionCopyURLCount);
}

// Copy credential Username to clipboard.
- (void)copyUsername {
  UIPasteboard* generalPasteboard = [UIPasteboard generalPasteboard];
  generalPasteboard.string = self.credential.user;
  UpdateUMACountForKey(app_group::kCredentialExtensionCopyUsernameCount);
}

// Copy password to clipboard.
- (void)copyPassword {
  UIPasteboard* generalPasteboard = [UIPasteboard generalPasteboard];
  generalPasteboard.string = self.clearPassword;
  UpdateUMACountForKey(app_group::kCredentialExtensionCopyPasswordCount);
}

// Initiate process to show password unobfuscated.
- (void)showPassword {
  [self passwordIconButtonTapped:nil event:nil];
}

// Creates a cancel button for the navigation item.
- (UIBarButtonItem*)navigationCancelButton {
  UIBarButtonItem* cancelButton = [[UIBarButtonItem alloc]
      initWithBarButtonSystemItem:UIBarButtonSystemItemCancel
                           target:self.delegate
                           action:@selector(navigationCancelButtonWasPressed:)];
  cancelButton.tintColor = [UIColor colorNamed:kBlueColor];
  return cancelButton;
}

// Returns the string to display as password.
- (NSString*)password {
  return self.clearPassword ? self.clearPassword : kMaskedPassword;
}

// Creates a button to be displayed as accessory of the password row item.
- (UIView*)passwordIconButton {
  UIImage* image =
      [UIImage imageNamed:self.clearPassword ? @"password_hide_icon"
                                             : @"password_reveal_icon"];
  image = [image imageWithRenderingMode:UIImageRenderingModeAlwaysTemplate];

  UIButton* button = [UIButton buttonWithType:UIButtonTypeCustom];
  button.frame = CGRectMake(0.0, 0.0, image.size.width, image.size.height);
  button.backgroundColor = [UIColor colorNamed:kBackgroundColor];
  [button setBackgroundImage:image forState:UIControlStateNormal];
  [button setTintColor:[UIColor colorNamed:kBlueColor]];
  [button addTarget:self
                action:@selector(passwordIconButtonTapped:event:)
      forControlEvents:UIControlEventTouchUpInside];

#if defined(__IPHONE_13_4)
  if (@available(iOS 13.4, *)) {
    button.pointerInteractionEnabled = YES;
    button.pointerStyleProvider = ^UIPointerStyle*(
        UIButton* button, __unused UIPointerEffect* proposedEffect,
        __unused UIPointerShape* proposedShape) {
      UITargetedPreview* preview =
          [[UITargetedPreview alloc] initWithView:button];
      UIPointerHighlightEffect* effect =
          [UIPointerHighlightEffect effectWithPreview:preview];
      UIPointerShape* shape =
          [UIPointerShape shapeWithRoundedRect:button.frame
                                  cornerRadius:button.frame.size.width / 2];
      return [UIPointerStyle styleWithEffect:effect shape:shape];
    };
  }
#endif  // defined(__IPHONE_13_4)

  return button;
}

// Called when show/hine password icon is tapped.
- (void)passwordIconButtonTapped:(id)sender event:(id)event {
  // Only password reveal / hide is an accessory, so no need to check
  // indexPath.
  if (self.clearPassword) {
    self.clearPassword = nil;
    [self updatePasswordRow];
  } else {
    UpdateUMACountForKey(app_group::kCredentialExtensionShowPasswordCount);
    [self.delegate unlockPasswordForCredential:self.credential
                             completionHandler:^(NSString* password) {
                               self.clearPassword = password;
                               [self updatePasswordRow];
                             }];
  }
}

// Hides the password and toggles the "Show/Hide" button.
- (void)hidePassword {
  if (!self.clearPassword)
    return;
  self.clearPassword = nil;
  [self updatePasswordRow];
}

// Updates the password row.
- (void)updatePasswordRow {
  NSIndexPath* indexPath =
      [NSIndexPath indexPathForRow:RowIdentifier::RowIdentifierPassword
                         inSection:0];
  [self.tableView reloadRowsAtIndexPaths:@[ indexPath ]
                        withRowAnimation:UITableViewRowAnimationAutomatic];
}

- (void)showTootip:(NSString*)message
        atBottomOf:(UITableViewCell*)cell
            action:(SEL)action {
  TooltipView* tooltip = [[TooltipView alloc] initWithKeyWindow:self.view
                                                         target:self
                                                         action:action];
  [tooltip showMessage:message atBottomOf:cell];
}

@end
