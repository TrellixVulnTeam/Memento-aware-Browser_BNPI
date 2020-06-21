// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/quick_answers/ui/quick_answers_view.h"

#include "ash/public/cpp/assistant/assistant_interface_binder.h"
#include "ash/public/cpp/vector_icons/vector_icons.h"
#include "ash/quick_answers/quick_answers_ui_controller.h"
#include "ash/quick_answers/ui/quick_answers_pre_target_handler.h"
#include "ash/resources/vector_icons/vector_icons.h"
#include "ash/shell.h"
#include "ash/strings/grit/ash_strings.h"
#include "chromeos/components/quick_answers/quick_answers_model.h"
#include "chromeos/constants/chromeos_features.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button_controller.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_config.h"
#include "ui/views/controls/menu/menu_controller.h"
#include "ui/views/focus/focus_search.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/painter.h"
#include "ui/views/widget/tooltip_manager.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/coordinate_conversion.h"

namespace ash {
namespace {

using chromeos::quick_answers::QuickAnswer;
using chromeos::quick_answers::QuickAnswerText;
using chromeos::quick_answers::QuickAnswerUiElement;
using chromeos::quick_answers::QuickAnswerUiElementType;
using views::Button;
using views::Label;
using views::View;

// Spacing between this view and the anchor view.
constexpr int kMarginDip = 10;

constexpr gfx::Insets kMainViewInsets(4, 0);
constexpr gfx::Insets kContentViewInsets(8, 0, 8, 16);
constexpr float kHoverStateAlpha = 0.06f;
constexpr int kMaxRows = 3;

// Assistant icon.
constexpr int kAssistantIconSizeDip = 16;
constexpr gfx::Insets kAssistantIconInsets(10, 10, 0, 8);

// Spacing between lines in the main view.
constexpr int kLineSpacingDip = 4;
constexpr int kLineHeightDip = 20;

// Spacing between labels in the horizontal elements view.
constexpr int kLabelSpacingDip = 2;

// TODO(llin): Move to grd after confirming specs (b/149758492).
constexpr char kDefaultLoadingStr[] = "Loading...";
constexpr char kDefaultRetryStr[] = "Retry";
constexpr char kNetworkErrorStr[] = "Cannot connect to internet.";

// Dogfood button.
constexpr int kDogfoodButtonMarginDip = 4;
constexpr int kDogfoodButtonSizeDip = 20;
constexpr SkColor kDogfoodButtonColor = gfx::kGoogleGrey500;

// Accessibility.
constexpr char kA11yNameTemplate[] = "Quick Answer: %s";

// Maximum height QuickAnswersView can expand to.
int MaximumViewHeight() {
  return kMainViewInsets.height() + kContentViewInsets.height() +
         kMaxRows * kLineHeightDip + (kMaxRows - 1) * kLineSpacingDip;
}

// Adds |text_element| as label to the container.
Label* AddTextElement(const QuickAnswerText& text_element, View* container) {
  auto* label =
      container->AddChildView(std::make_unique<Label>(text_element.text));
  label->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
  label->SetEnabledColor(text_element.color);
  label->SetLineHeight(kLineHeightDip);
  return label;
}

// Adds the list of |QuickAnswerUiElement| horizontally to the container.
View* AddHorizontalUiElements(
    const std::vector<std::unique_ptr<QuickAnswerUiElement>>& elements,
    View* container) {
  auto* labels_container =
      container->AddChildView(std::make_unique<views::View>());
  labels_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
      kLabelSpacingDip));

  for (const auto& element : elements) {
    switch (element->type) {
      case QuickAnswerUiElementType::kText:
        AddTextElement(*static_cast<QuickAnswerText*>(element.get()),
                       labels_container);
        break;
      case QuickAnswerUiElementType::kImage:
        // TODO(yanxiao): Add image view
        break;
      default:
        break;
    }
  }

  return labels_container;
}

}  // namespace

// QuickAnswersFocusSearch ----------------------------------------------------

// This class manages the focus traversal order for elements inside
// QuickAnswersView.
// TODO(siabhijeet): QuickAnswersView is a menu-companion, so ideally should
// avoid disturbing existing focus. Explore other ways for keyboard
// accessibility.
class QuickAnswersFocusSearch : public views::FocusSearch {
 public:
  explicit QuickAnswersFocusSearch(QuickAnswersView* view)
      : FocusSearch(/*root=*/view, /*cycle=*/true, /*accessibility_mode=*/true),
        view_(view) {}

  ~QuickAnswersFocusSearch() override = default;

  // views::FocusSearch:
  views::View* FindNextFocusableView(
      views::View* starting_view,
      SearchDirection search_direction,
      TraversalDirection traversal_direction,
      StartingViewPolicy check_starting_view,
      AnchoredDialogPolicy can_go_into_anchored_dialog,
      views::FocusTraversable** focus_traversable,
      views::View** focus_traversable_view) override {
    DCHECK_EQ(root(), view_);

    std::vector<views::View*> focusable_views;
    // |view_| is not included in focus loop for retry-view.
    if (!view_->retry_label_)
      focusable_views.push_back(view_);
    if (view_->retry_label_ && view_->retry_label_->GetVisible())
      focusable_views.push_back(view_->retry_label_);
    if (view_->dogfood_button_ && view_->dogfood_button_->GetVisible())
      focusable_views.push_back(view_->dogfood_button_);
    if (focusable_views.empty())
      return nullptr;

    int delta =
        search_direction == FocusSearch::SearchDirection::kForwards ? 1 : -1;
    int focusable_views_size = int{focusable_views.size()};
    for (int i = 0; i < focusable_views_size; ++i) {
      // If current view from the set is found to be focused, return the view
      // next (or previous) to it as next focusable view.
      if (focusable_views[i] == starting_view) {
        int next_index =
            (i + delta + focusable_views_size) % focusable_views_size;
        return focusable_views[next_index];
      }
    }

    // Case when none of the views are already focused.
    return (search_direction == FocusSearch::SearchDirection::kForwards)
               ? focusable_views.front()
               : focusable_views.back();
  }

 private:
  QuickAnswersView* const view_;
};

// QuickAnswersView -----------------------------------------------------------

QuickAnswersView::QuickAnswersView(const gfx::Rect& anchor_view_bounds,
                                   const std::string& title,
                                   QuickAnswersUiController* controller)
    : Button(this),
      anchor_view_bounds_(anchor_view_bounds),
      controller_(controller),
      title_(title),
      quick_answers_view_handler_(
          std::make_unique<QuickAnswersPreTargetHandler>(this)),
      focus_search_(std::make_unique<QuickAnswersFocusSearch>(this)) {
  InitLayout();
  InitWidget();

  // Accessibility.
  GetViewAccessibility().OverrideRole(ax::mojom::Role::kMenuItem);
  GetViewAccessibility().OverrideName(
      base::StringPrintf(kA11yNameTemplate, title_.c_str()));

  // Focus.
  SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  SetInstallFocusRingOnFocus(false);

  // This is because waiting for mouse-release to fire buttons would be too
  // late, since mouse-press dismisses the menu.
  SetButtonNotifyActionToOnPress(this);

  // Allow tooltips to be shown despite menu-controller owning capture.
  GetWidget()->SetNativeWindowProperty(
      views::TooltipManager::kGroupingPropertyKey,
      reinterpret_cast<void*>(views::MenuConfig::kMenuControllerGroupingId));
}

QuickAnswersView::~QuickAnswersView() = default;

const char* QuickAnswersView::GetClassName() const {
  return "QuickAnswersView";
}

void QuickAnswersView::OnFocus() {
  SetBackgroundState(true);
  View* wants_focus = focus_search_->FindNextFocusableView(
      nullptr, views::FocusSearch::SearchDirection::kForwards,
      views::FocusSearch::TraversalDirection::kDown,
      views::FocusSearch::StartingViewPolicy::kCheckStartingView,
      views::FocusSearch::AnchoredDialogPolicy::kSkipAnchoredDialog, nullptr,
      nullptr);
  if (wants_focus != this)
    wants_focus->RequestFocus();
  else
    NotifyAccessibilityEvent(ax::mojom::Event::kFocus, true);
}

void QuickAnswersView::OnBlur() {
  SetBackgroundState(false);
}

views::FocusTraversable* QuickAnswersView::GetPaneFocusTraversable() {
  return this;
}

void QuickAnswersView::StateChanged(views::Button::ButtonState old_state) {
  switch (state()) {
    case Button::ButtonState::STATE_NORMAL: {
      SetBackgroundState(false);
      break;
    }
    case Button::ButtonState::STATE_HOVERED: {
      SetBackgroundState(true);
      break;
    }
    default:
      break;
  }
}

void QuickAnswersView::ButtonPressed(views::Button* sender,
                                     const ui::Event& event) {
  if (sender == dogfood_button_) {
    controller_->OnDogfoodButtonPressed();
    return;
  }
  if (sender == retry_label_) {
    controller_->OnRetryLabelPressed();
    return;
  }
  if (sender == this) {
    SendQuickAnswersQuery();
    return;
  }
}

void QuickAnswersView::SetButtonNotifyActionToOnPress(views::Button* button) {
  DCHECK(button);
  button->button_controller()->set_notify_action(
      views::ButtonController::NotifyAction::kOnPress);
}

views::FocusSearch* QuickAnswersView::GetFocusSearch() {
  return focus_search_.get();
}

views::FocusTraversable* QuickAnswersView::GetFocusTraversableParent() {
  return nullptr;
}

views::View* QuickAnswersView::GetFocusTraversableParentView() {
  return nullptr;
}

void QuickAnswersView::SendQuickAnswersQuery() {
  controller_->OnQuickAnswersViewPressed();
}

void QuickAnswersView::UpdateAnchorViewBounds(
    const gfx::Rect& anchor_view_bounds) {
  anchor_view_bounds_ = anchor_view_bounds;
  UpdateBounds();
}

void QuickAnswersView::UpdateView(const gfx::Rect& anchor_view_bounds,
                                  const QuickAnswer& quick_answer) {
  has_second_row_answer_ = !quick_answer.second_answer_row.empty();
  anchor_view_bounds_ = anchor_view_bounds;
  retry_label_ = nullptr;

  UpdateQuickAnswerResult(quick_answer);
  UpdateBounds();
}

void QuickAnswersView::ShowRetryView() {
  if (retry_label_)
    return;

  ResetContentView();
  main_view_->SetBackground(views::CreateSolidBackground(SK_ColorTRANSPARENT));

  // Add title.
  AddTextElement({title_}, content_view_);

  // Add error label.
  std::vector<std::unique_ptr<QuickAnswerUiElement>> description_labels;
  description_labels.push_back(
      std::make_unique<QuickAnswerText>(kNetworkErrorStr, gfx::kGoogleGrey700));
  auto* description_container =
      AddHorizontalUiElements(description_labels, content_view_);

  // Add retry label.
  retry_label_ =
      description_container->AddChildView(std::make_unique<views::LabelButton>(
          /*listener=*/this, base::UTF8ToUTF16(kDefaultRetryStr)));
  retry_label_->SetEnabledTextColors(gfx::kGoogleBlue600);
  retry_label_->SetFocusForPlatform();
  SetButtonNotifyActionToOnPress(retry_label_);
}

void QuickAnswersView::AddAssistantIcon() {
  // Add Assistant icon.
  auto* assistant_icon =
      main_view_->AddChildView(std::make_unique<views::ImageView>());
  assistant_icon->SetBorder(views::CreateEmptyBorder(kAssistantIconInsets));
  assistant_icon->SetImage(gfx::CreateVectorIcon(
      kAssistantIcon, kAssistantIconSizeDip, gfx::kPlaceholderColor));
}

void QuickAnswersView::AddDogfoodButton() {
  auto* dogfood_view = AddChildView(std::make_unique<View>());
  auto* layout =
      dogfood_view->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical,
          gfx::Insets(kDogfoodButtonMarginDip)));
  layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kEnd);
  auto dogfood_button = std::make_unique<views::ImageButton>(/*listener=*/this);
  dogfood_button->SetImage(
      views::Button::ButtonState::STATE_NORMAL,
      gfx::CreateVectorIcon(kDogfoodIcon, kDogfoodButtonSizeDip,
                            kDogfoodButtonColor));
  dogfood_button->SetTooltipText(l10n_util::GetStringUTF16(
      IDS_ASH_QUICK_ANSWERS_DOGFOOD_BUTTON_TOOLTIP_TEXT));
  dogfood_button_ = dogfood_view->AddChildView(std::move(dogfood_button));
  SetButtonNotifyActionToOnPress(dogfood_button_);
}

void QuickAnswersView::InitLayout() {
  SetLayoutManager(std::make_unique<views::FillLayout>());
  SetBackground(views::CreateSolidBackground(SK_ColorWHITE));

  main_view_ = AddChildView(std::make_unique<View>());
  auto* layout =
      main_view_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, kMainViewInsets));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStart);

  // Add Assistant icon.
  AddAssistantIcon();

  // Add content view.
  content_view_ = main_view_->AddChildView(std::make_unique<View>());
  content_view_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, kContentViewInsets,
      kLineSpacingDip));
  AddTextElement({title_}, content_view_);
  AddTextElement({kDefaultLoadingStr, gfx::kGoogleGrey700}, content_view_);

  // Add dogfood button, if in dogfood.
  if (chromeos::features::IsQuickAnswersDogfood())
    AddDogfoodButton();
}

void QuickAnswersView::InitWidget() {
  views::Widget::InitParams params;
  params.activatable = views::Widget::InitParams::Activatable::ACTIVATABLE_NO;
  params.shadow_elevation = 2;
  params.shadow_type = views::Widget::InitParams::ShadowType::kDrop;
  params.type = views::Widget::InitParams::TYPE_POPUP;
  params.z_order = ui::ZOrderLevel::kFloatingUIElement;

  // Parent the widget depending on the context.
  auto* active_menu_controller = views::MenuController::GetActiveInstance();
  if (active_menu_controller && active_menu_controller->owner()) {
    params.parent = active_menu_controller->owner()->GetNativeView();
    params.child = true;
  } else {
    params.context = Shell::Get()->GetRootWindowForNewWindows();
  }

  views::Widget* widget = new views::Widget();
  widget->Init(std::move(params));
  widget->SetContentsView(this);
  UpdateBounds();
}

void QuickAnswersView::UpdateBounds() {
  int desired_width = anchor_view_bounds_.width();

  // Multi-line labels need to be resized to be compatible with |desired_width|.
  if (first_answer_label_) {
    int label_desired_width =
        desired_width - kMainViewInsets.width() - kContentViewInsets.width() -
        kAssistantIconInsets.width() - kAssistantIconSizeDip;
    first_answer_label_->SizeToFit(label_desired_width);
  }

  int height = GetHeightForWidth(desired_width);
  int y = anchor_view_bounds_.y() - kMarginDip - height;

  // Reserve space at the top since the view might expand for two-line answers.
  int y_min = anchor_view_bounds_.y() - kMarginDip - MaximumViewHeight();
  if (y_min < display::Screen::GetScreen()
                  ->GetDisplayMatching(anchor_view_bounds_)
                  .bounds()
                  .y()) {
    // The Quick Answers view will be off screen if showing above the anchor.
    // Show below the anchor instead.
    y = anchor_view_bounds_.bottom() + kMarginDip;
  }

  gfx::Rect bounds = {{anchor_view_bounds_.x(), y}, {desired_width, height}};
  wm::ConvertRectFromScreen(GetWidget()->GetNativeWindow()->parent(), &bounds);
  GetWidget()->SetBounds(bounds);
}

void QuickAnswersView::UpdateQuickAnswerResult(
    const QuickAnswer& quick_answer) {
  ResetContentView();

  // Add title.
  AddHorizontalUiElements(quick_answer.title, content_view_);

  // Add first row answer.
  View* first_answer_view = nullptr;
  if (!quick_answer.first_answer_row.empty()) {
    first_answer_view =
        AddHorizontalUiElements(quick_answer.first_answer_row, content_view_);
  }
  bool first_answer_is_single_label =
      first_answer_view->children().size() == 1 &&
      first_answer_view->children().front()->GetClassName() ==
          views::Label::kViewClassName;
  if (first_answer_is_single_label) {
    // Update answer announcement.
    auto* answer_label =
        static_cast<Label*>(first_answer_view->children().front());
    GetViewAccessibility().OverrideDescription(answer_label->GetText());
  }

  // Add second row answer.
  if (!quick_answer.second_answer_row.empty()) {
    AddHorizontalUiElements(quick_answer.second_answer_row, content_view_);
  } else {
    // If secondary-answer does not exist and primary-answer is a single label,
    // allow that label to wrap through to the row intended for the former.
    if (first_answer_is_single_label) {
      // Cache multi-line label for resizing when view bounds change.
      first_answer_label_ =
          static_cast<Label*>(first_answer_view->children().front());
      first_answer_label_->SetMultiLine(true);
      first_answer_label_->SetMaxLines(kMaxRows - /*exclude title*/ 1);
    }
  }
}

void QuickAnswersView::SetBackgroundState(bool highlight) {
  if (highlight && !retry_label_) {
    main_view_->SetBackground(views::CreateBackgroundFromPainter(
        views::Painter::CreateSolidRoundRectPainter(
            SkColorSetA(SK_ColorBLACK, kHoverStateAlpha * 0xFF),
            /*radius=*/0, kMainViewInsets)));
  } else if (!highlight) {
    main_view_->SetBackground(views::CreateSolidBackground(SK_ColorWHITE));
  }
}

void QuickAnswersView::ResetContentView() {
  content_view_->RemoveAllChildViews(true);
  first_answer_label_ = nullptr;
}

}  // namespace ash
