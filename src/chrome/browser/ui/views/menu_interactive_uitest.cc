// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "ui/views/controls/menu/menu_controller.h"

#include "base/macros.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/views/native_widget_factory.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/test_browser_window.h"
#include "content/public/test/browser_test.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/test/ui_controls.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/accessibility/ax_event_manager.h"
#include "ui/views/accessibility/ax_event_observer.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/submenu_view.h"
#include "ui/views/widget/widget.h"

namespace views {
namespace test {

namespace {

class TestAXEventObserver : public views::AXEventObserver {
 public:
  TestAXEventObserver() { views::AXEventManager::Get()->AddObserver(this); }
  ~TestAXEventObserver() override {
    views::AXEventManager::Get()->RemoveObserver(this);
  }

  void OnViewEvent(views::View*, ax::mojom::Event event_type) override {
    switch (event_type) {
      case ax::mojom::Event::kMenuStart:
        // Fired once at the very start of menu interactions.
        ++menu_start_count_;
        break;
      case ax::mojom::Event::kMenuPopupStart:
        // Fired once for each menu/submenu that is opened/shown.
        ++menu_popup_start_count_;
        break;
      case ax::mojom::Event::kMenuPopupEnd:
        // Fired once for each menu/submenu that is closed/hidden.
        ++menu_popup_end_count_;
        break;
      case ax::mojom::Event::kMenuEnd:
        // Fired once at the very end of menu interactions.
        ++menu_end_count_;
        break;
      default:
        break;
    }
  }

  int GetMenuStartCount() const { return menu_start_count_; }
  int GetMenuPopupStartCount() const { return menu_popup_start_count_; }
  int GetMenuPopupEndCount() const { return menu_popup_end_count_; }
  int GetMenuEndCount() const { return menu_end_count_; }

 protected:
  int menu_start_count_ = 0;
  int menu_popup_end_count_ = 0;
  int menu_popup_start_count_ = 0;
  int menu_end_count_ = 0;
};

}  // namespace

class MenuControllerUITest : public InProcessBrowserTest {
 public:
  MenuControllerUITest() {}

  // This method creates a MenuRunner, MenuItemView, etc, adds two menu
  // items, shows the menu so that it can calculate the position of the first
  // menu item and move the mouse there, and closes the menu.
  void SetupMenu(Widget* widget) {
    menu_delegate_ = std::make_unique<MenuDelegate>();
    MenuItemView* menu_item = new MenuItemView(menu_delegate_.get());
    menu_runner_ = std::make_unique<MenuRunner>(
        +menu_item, views::MenuRunner::CONTEXT_MENU);
    first_item_ = menu_item->AppendMenuItem(1, base::ASCIIToUTF16("One"));
    menu_item->AppendMenuItem(2, base::ASCIIToUTF16("Two"));
    // Run the menu, so that the menu item size will be calculated.
    menu_runner_->RunMenuAt(widget, nullptr, gfx::Rect(),
                            views::MenuAnchorPosition::kTopLeft,
                            ui::MENU_SOURCE_NONE);
    RunPendingMessages();
    // Figure out the middle of the first menu item.
    mouse_pos_.set_x(first_item_->width() / 2);
    mouse_pos_.set_y(first_item_->height() / 2);
    View::ConvertPointToScreen(
        menu_item->GetSubmenu()->GetWidget()->GetRootView(), &mouse_pos_);
    // Move the mouse so that it's where the menu will be shown.
    base::RunLoop run_loop;
    ui_controls::SendMouseMoveNotifyWhenDone(mouse_pos_.x(), mouse_pos_.y(),
                                             run_loop.QuitClosure());
    run_loop.Run();
    EXPECT_TRUE(first_item_->IsSelected());
    ui::AXNodeData item_node_data;
    first_item_->GetViewAccessibility().GetAccessibleNodeData(&item_node_data);
    EXPECT_EQ(item_node_data.role, ax::mojom::Role::kMenuItem);
    ui::AXNodeData menu_node_data;
    menu_item->GetSubmenu()->GetViewAccessibility().GetAccessibleNodeData(
        &menu_node_data);
    EXPECT_EQ(menu_node_data.role, ax::mojom::Role::kMenuListPopup);
    menu_runner_->Cancel();
    RunPendingMessages();
  }

  void RunPendingMessages() {
    base::RunLoop run_loop;
    run_loop.RunUntilIdle();
  }

 protected:
  MenuItemView* first_item_ = nullptr;
  std::unique_ptr<MenuRunner> menu_runner_;
  std::unique_ptr<MenuDelegate> menu_delegate_;
  // Middle of first menu item.
  gfx::Point mouse_pos_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MenuControllerUITest);
};

IN_PROC_BROWSER_TEST_F(MenuControllerUITest, TestMouseOverShownMenu) {
  // Create a parent widget.
  Widget* widget = new views::Widget;
  Widget::InitParams params(Widget::InitParams::TYPE_WINDOW);
  params.bounds = {0, 0, 200, 200};
#if !defined(OS_CHROMEOS) && !defined(OS_MACOSX)
  params.native_widget = CreateNativeWidget(
      NativeWidgetType::DESKTOP_NATIVE_WIDGET_AURA, &params, widget);
#endif
  widget->Init(std::move(params));
  widget->Show();
  widget->Activate();
  // SetupMenu leaves the mouse position where the first menu item will be
  // when we run the menu.
  TestAXEventObserver observer;
  EXPECT_EQ(observer.GetMenuStartCount(), 0);
  EXPECT_EQ(observer.GetMenuPopupStartCount(), 0);
  EXPECT_EQ(observer.GetMenuPopupEndCount(), 0);
  EXPECT_EQ(observer.GetMenuEndCount(), 0);
  SetupMenu(widget);

  EXPECT_EQ(observer.GetMenuStartCount(), 1);
  EXPECT_EQ(observer.GetMenuPopupStartCount(), 1);
  // SetupMenu creates, opens and closes a popup menu, so there will be a
  // a menu popup end. There is also a menu end since it's the last menu.
  EXPECT_EQ(observer.GetMenuPopupEndCount(), 1);
  EXPECT_EQ(observer.GetMenuEndCount(), 1);
  EXPECT_FALSE(first_item_->IsSelected());
  menu_runner_->RunMenuAt(widget, nullptr, gfx::Rect(),
                          views::MenuAnchorPosition::kTopLeft,
                          ui::MENU_SOURCE_NONE);
  EXPECT_EQ(observer.GetMenuStartCount(), 2);
  EXPECT_EQ(observer.GetMenuPopupStartCount(), 2);
  EXPECT_EQ(observer.GetMenuPopupEndCount(), 1);
  EXPECT_EQ(observer.GetMenuEndCount(), 1);
  EXPECT_FALSE(first_item_->IsSelected());
  // One or two mouse events are posted by the menu being shown.
  // Process event(s), and check what's selected in the menu.
  RunPendingMessages();
  EXPECT_FALSE(first_item_->IsSelected());
  // Move mouse one pixel to left and verify that the first menu item
  // is selected.
  mouse_pos_.Offset(-1, 0);
  base::RunLoop run_loop2;
  ui_controls::SendMouseMoveNotifyWhenDone(mouse_pos_.x(), mouse_pos_.y(),
                                           run_loop2.QuitClosure());
  run_loop2.Run();
  EXPECT_TRUE(first_item_->IsSelected());
  menu_runner_->Cancel();
  EXPECT_EQ(observer.GetMenuStartCount(), 2);
  EXPECT_EQ(observer.GetMenuPopupStartCount(), 2);
  EXPECT_EQ(observer.GetMenuPopupEndCount(), 2);
  EXPECT_EQ(observer.GetMenuEndCount(), 2);
  widget->Close();
}

// This test creates a menu without a parent widget, and tests that it
// can receive keyboard events.
// TODO(davidbienvenu): If possible, get test working for linux and
// mac. Only status_icon_win runs a menu with a null parent widget
// currently.
#ifdef OS_WIN
IN_PROC_BROWSER_TEST_F(MenuControllerUITest, FocusOnOrphanMenu) {
  // Going into full screen mode prevents pre-test focus and mouse position
  // state from affecting test, and helps ui_controls function correctly.
  chrome::ToggleFullscreenMode(browser());
  MenuDelegate menu_delegate;
  MenuItemView* menu_item = new MenuItemView(&menu_delegate);
  TestAXEventObserver observer;
  EXPECT_EQ(observer.GetMenuStartCount(), 0);
  EXPECT_EQ(observer.GetMenuPopupStartCount(), 0);
  EXPECT_EQ(observer.GetMenuPopupEndCount(), 0);
  EXPECT_EQ(observer.GetMenuEndCount(), 0);
  std::unique_ptr<MenuRunner> menu_runner(
      std::make_unique<MenuRunner>(menu_item, views::MenuRunner::CONTEXT_MENU));
  MenuItemView* first_item =
      menu_item->AppendMenuItem(1, base::ASCIIToUTF16("One"));
  menu_item->AppendMenuItem(2, base::ASCIIToUTF16("Two"));
  menu_runner->RunMenuAt(nullptr, nullptr, gfx::Rect(),
                         views::MenuAnchorPosition::kTopLeft,
                         ui::MENU_SOURCE_NONE);
  EXPECT_EQ(observer.GetMenuStartCount(), 1);
  EXPECT_EQ(observer.GetMenuPopupStartCount(), 1);
  EXPECT_EQ(observer.GetMenuPopupEndCount(), 0);
  EXPECT_EQ(observer.GetMenuEndCount(), 0);
  base::RunLoop loop;
  // SendKeyPress fails if the window doesn't have focus.
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      menu_item->GetSubmenu()->GetWidget()->GetNativeWindow(), ui::VKEY_DOWN,
      false, false, false, false, loop.QuitClosure()));
  loop.Run();
  EXPECT_TRUE(first_item->IsSelected());
  menu_runner->Cancel();
  EXPECT_EQ(observer.GetMenuStartCount(), 1);
  EXPECT_EQ(observer.GetMenuPopupStartCount(), 1);
  EXPECT_EQ(observer.GetMenuPopupEndCount(), 1);
  EXPECT_EQ(observer.GetMenuEndCount(), 1);
}
#endif  // OS_WIN

}  // namespace test
}  // namespace views
