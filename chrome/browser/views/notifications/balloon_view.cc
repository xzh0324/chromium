// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/views/notifications/balloon_view.h"

#include <vector>

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "base/message_loop.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/browser_theme_provider.h"
#include "chrome/browser/notifications/balloon.h"
#include "chrome/browser/notifications/balloon_collection.h"
#include "chrome/browser/notifications/desktop_notification_service.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/browser/renderer_host/render_widget_host_view.h"
#include "chrome/browser/views/bubble_border.h"
#include "chrome/browser/views/notifications/balloon_view_host.h"
#include "chrome/common/notification_details.h"
#include "chrome/common/notification_source.h"
#include "chrome/common/notification_type.h"
#include "gfx/canvas_skia.h"
#include "gfx/insets.h"
#include "gfx/native_widget_types.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "views/controls/button/button.h"
#include "views/controls/button/image_button.h"
#include "views/controls/button/text_button.h"
#include "views/controls/menu/menu_2.h"
#include "views/controls/native/native_view_host.h"
#include "views/painter.h"
#include "views/widget/root_view.h"
#if defined(OS_WIN)
#include "views/widget/widget_win.h"
#endif
#if defined(OS_LINUX)
#include "views/widget/widget_gtk.h"
#endif

using views::Widget;

namespace {

const int kTopMargin = 2;
const int kBottomMargin = 0;
const int kLeftMargin = 4;
const int kRightMargin = 4;
const int kShelfBorderTopOverlap = 0;

// Properties of the dismiss button.
const int kDismissButtonWidth = 14;
const int kDismissButtonHeight = 14;
const int kDismissButtonRightMargin = 10;

// Properties of the options menu.
const int kOptionsButtonWidth = 26;
const int kOptionsButtonHeight = 14;
const int kOptionsButtonRightMargin = 10;

// Properties of the origin label.
const int kLeftLabelMargin = 10;

// Size of the drop shadow.  The shadow is provided by BubbleBorder,
// not this class.
const int kLeftShadowWidth = 0;
const int kRightShadowWidth = 0;
const int kTopShadowWidth = 0;
const int kBottomShadowWidth = 6;

// Optional animation.
const bool kAnimateEnabled = true;

// The shelf height for the system default font size.  It is scaled
// with changes in the default font size.
const int kDefaultShelfHeight = 22;
const int kShelfTopMargin = 6;

// Menu commands
const int kRevokePermissionCommand = 0;

// Colors
const SkColor kControlBarBackgroundColor = SkColorSetRGB(245, 245, 245);
const SkColor kControlBarTextColor = SkColorSetRGB(125, 125, 125);
const SkColor kControlBarSeparatorLineColor = SkColorSetRGB(180, 180, 180);

}  // namespace

BalloonViewImpl::BalloonViewImpl(BalloonCollection* collection)
    : balloon_(NULL),
      collection_(collection),
      frame_container_(NULL),
      html_container_(NULL),
      html_contents_(NULL),
      method_factory_(this),
      close_button_(NULL),
      animation_(NULL),
      options_menu_contents_(NULL),
      options_menu_menu_(NULL),
      options_menu_button_(NULL) {
  // This object is not to be deleted by the views hierarchy,
  // as it is owned by the balloon.
  set_parent_owned(false);

  BubbleBorder* bubble_border = new BubbleBorder(BubbleBorder::FLOAT);
  set_border(bubble_border);
}

BalloonViewImpl::~BalloonViewImpl() {
}

void BalloonViewImpl::Close(bool by_user) {
  MessageLoop::current()->PostTask(FROM_HERE,
      method_factory_.NewRunnableMethod(
          &BalloonViewImpl::DelayedClose, by_user));
}

gfx::Size BalloonViewImpl::GetSize() const {
  // BalloonView has no size if it hasn't been shown yet (which is when
  // balloon_ is set).
  if (!balloon_)
    return gfx::Size(0, 0);

  return gfx::Size(GetTotalWidth(), GetTotalHeight());
}

void BalloonViewImpl::RunMenu(views::View* source, const gfx::Point& pt) {
  RunOptionsMenu(pt);
}

void BalloonViewImpl::DisplayChanged() {
  collection_->DisplayChanged();
}

void BalloonViewImpl::WorkAreaChanged() {
  collection_->DisplayChanged();
}

void BalloonViewImpl::ButtonPressed(views::Button* sender,
                                    const views::Event&) {
  // The only button currently is the close button.
  DCHECK(sender == close_button_);
  Close(true);
}

void BalloonViewImpl::DelayedClose(bool by_user) {
  html_contents_->Shutdown();
  html_container_->CloseNow();
  // The BalloonViewImpl has to be detached from frame_container_ now
  // because CloseNow on linux/views destroys the view hierachy
  // asynchronously.
  frame_container_->GetRootView()->RemoveAllChildViews(true);
  frame_container_->CloseNow();
  balloon_->OnClose(by_user);
}

void BalloonViewImpl::DidChangeBounds(const gfx::Rect& previous,
                                      const gfx::Rect& current) {
  SizeContentsWindow();
}

void BalloonViewImpl::SizeContentsWindow() {
  if (!html_container_ || !frame_container_)
    return;

  gfx::Rect contents_rect = GetContentsRectangle();
  html_container_->SetBounds(contents_rect);
  html_container_->MoveAbove(frame_container_);

  gfx::Path path;
  GetContentsMask(contents_rect, &path);
  html_container_->SetShape(path.CreateNativeRegion());

  close_button_->SetBounds(GetCloseButtonBounds());
  options_menu_button_->SetBounds(GetOptionsButtonBounds());
  source_label_->SetBounds(GetLabelBounds());
}

void BalloonViewImpl::RepositionToBalloon() {
  DCHECK(frame_container_);
  DCHECK(html_container_);
  DCHECK(balloon_);

  if (!kAnimateEnabled) {
    frame_container_->SetBounds(
        gfx::Rect(balloon_->GetPosition().x(), balloon_->GetPosition().y(),
                  GetTotalWidth(), GetTotalHeight()));
    gfx::Rect contents_rect = GetContentsRectangle();
    html_container_->SetBounds(contents_rect);
    html_contents_->SetPreferredSize(contents_rect.size());
    RenderWidgetHostView* view = html_contents_->render_view_host()->view();
    if (view)
      view->SetSize(contents_rect.size());
    return;
  }

  anim_frame_end_ = gfx::Rect(
      balloon_->GetPosition().x(), balloon_->GetPosition().y(),
      GetTotalWidth(), GetTotalHeight());
  frame_container_->GetBounds(&anim_frame_start_, false);
  animation_.reset(new SlideAnimation(this));
  animation_->Show();
}

void BalloonViewImpl::AnimationProgressed(const Animation* animation) {
  DCHECK(animation == animation_.get());

  // Linear interpolation from start to end position.
  double e = animation->GetCurrentValue();
  double s = (1.0 - e);

  gfx::Rect frame_position(
    static_cast<int>(s * anim_frame_start_.x() +
                     e * anim_frame_end_.x()),
    static_cast<int>(s * anim_frame_start_.y() +
                     e * anim_frame_end_.y()),
    static_cast<int>(s * anim_frame_start_.width() +
                     e * anim_frame_end_.width()),
    static_cast<int>(s * anim_frame_start_.height() +
                     e * anim_frame_end_.height()));
  frame_container_->SetBounds(frame_position);

  gfx::Path path;
  gfx::Rect contents_rect = GetContentsRectangle();
  html_container_->SetBounds(contents_rect);
  GetContentsMask(contents_rect, &path);
  html_container_->SetShape(path.CreateNativeRegion());

  html_contents_->SetPreferredSize(contents_rect.size());
  RenderWidgetHostView* view = html_contents_->render_view_host()->view();
  if (view)
    view->SetSize(contents_rect.size());
}

gfx::Rect BalloonViewImpl::GetCloseButtonBounds() const {
  return gfx::Rect(
      width() - kDismissButtonWidth -
          kDismissButtonRightMargin - kRightShadowWidth,
      kShelfTopMargin,
      kDismissButtonWidth,
      kDismissButtonHeight);
}

gfx::Rect BalloonViewImpl::GetOptionsButtonBounds() const {
  gfx::Rect close_rect = GetCloseButtonBounds();

  return gfx::Rect(
      close_rect.x() - kOptionsButtonWidth - kOptionsButtonRightMargin,
      kShelfTopMargin,
      kOptionsButtonWidth,
      kOptionsButtonHeight);
}

gfx::Rect BalloonViewImpl::GetLabelBounds() const {
  return gfx::Rect(
      kLeftShadowWidth + kLeftLabelMargin,
      kShelfTopMargin,
      std::max(0, width() - kOptionsButtonWidth -
               kRightMargin),
      kOptionsButtonHeight);
}

void BalloonViewImpl::Show(Balloon* balloon) {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();

  const std::wstring source_label_text = l10n_util::GetStringF(
      IDS_NOTIFICATION_BALLOON_SOURCE_LABEL,
      balloon->notification().display_source());
  const std::wstring dismiss_text =
      l10n_util::GetString(IDS_NOTIFICATION_BALLOON_DISMISS_LABEL);

  balloon_ = balloon;

  SetBounds(balloon_->GetPosition().x(), balloon_->GetPosition().y(),
            GetTotalWidth(), GetTotalHeight());

  source_label_ = new views::Label(source_label_text);
  AddChildView(source_label_);
  options_menu_button_ = new views::MenuButton(NULL, L"", this, false);
  AddChildView(options_menu_button_);
  close_button_ = new views::ImageButton(this);
  AddChildView(close_button_);

  // We have to create two windows: one for the contents and one for the
  // frame.  Why?
  // * The contents is an html window which cannot be a
  //   layered window (because it may have child windows for instance).
  // * The frame is a layered window so that we can have nicely rounded
  //   corners using alpha blending (and we may do other alpha blending
  //   effects).
  // Unfortunately, layered windows cannot have child windows. (Well, they can
  // but the child windows don't render).
  //
  // We carefully keep these two windows in sync to present the illusion of
  // one window to the user.
  //
  // We don't let the OS manage the RTL layout of these widgets, because
  // this code is already taking care of correctly reversing the layout.
  gfx::Rect contents_rect = GetContentsRectangle();
  html_contents_.reset(new BalloonViewHost(balloon));
  html_contents_->SetPreferredSize(gfx::Size(10000, 10000));
  html_container_ = Widget::CreatePopupWidget(Widget::NotTransparent,
                                              Widget::AcceptEvents,
                                              Widget::DeleteOnDestroy,
                                              Widget::DontMirrorOriginInRTL);
  html_container_->SetAlwaysOnTop(true);
  html_container_->Init(NULL, contents_rect);
  html_container_->SetContentsView(html_contents_->view());

  gfx::Rect balloon_rect(x(), y(), GetTotalWidth(), GetTotalHeight());
  frame_container_ = Widget::CreatePopupWidget(Widget::Transparent,
                                               Widget::AcceptEvents,
                                               Widget::DeleteOnDestroy,
                                               Widget::DontMirrorOriginInRTL);
  frame_container_->SetWidgetDelegate(this);
  frame_container_->SetAlwaysOnTop(true);
  frame_container_->Init(NULL, balloon_rect);
  frame_container_->SetContentsView(this);
  frame_container_->MoveAbove(html_container_);

  close_button_->SetImage(views::CustomButton::BS_NORMAL,
      rb.GetBitmapNamed(IDR_BALLOON_CLOSE));
  close_button_->SetImage(views::CustomButton::BS_HOT,
      rb.GetBitmapNamed(IDR_BALLOON_CLOSE_HOVER));
  close_button_->SetImage(views::CustomButton::BS_PUSHED,
      rb.GetBitmapNamed(IDR_BALLOON_CLOSE_HOVER));
  close_button_->SetBounds(GetCloseButtonBounds());

  options_menu_button_->SetIcon(*rb.GetBitmapNamed(IDR_BALLOON_WRENCH));
  options_menu_button_->SetHoverIcon(*rb.GetBitmapNamed(IDR_BALLOON_WRENCH));
  options_menu_button_->set_alignment(views::TextButton::ALIGN_CENTER);
  options_menu_button_->SetBounds(GetOptionsButtonBounds());

  source_label_->SetFont(rb.GetFont(ResourceBundle::SmallFont));
  source_label_->SetColor(kControlBarTextColor);
  source_label_->SetHorizontalAlignment(views::Label::ALIGN_LEFT);
  source_label_->SetBounds(GetLabelBounds());

  SizeContentsWindow();
  html_container_->Show();
  frame_container_->Show();

  notification_registrar_.Add(this,
    NotificationType::NOTIFY_BALLOON_DISCONNECTED, Source<Balloon>(balloon));
}

void BalloonViewImpl::RunOptionsMenu(const gfx::Point& pt) {
  CreateOptionsMenu();
  options_menu_menu_->RunMenuAt(pt, views::Menu2::ALIGN_TOPRIGHT);
}

void BalloonViewImpl::CreateOptionsMenu() {
  if (options_menu_contents_.get())
    return;

  const string16 label_text = WideToUTF16Hack(l10n_util::GetStringF(
      IDS_NOTIFICATION_BALLOON_REVOKE_MESSAGE,
      this->balloon_->notification().display_source()));

  options_menu_contents_.reset(new menus::SimpleMenuModel(this));
  options_menu_contents_->AddItem(kRevokePermissionCommand, label_text);

  options_menu_menu_.reset(new views::Menu2(options_menu_contents_.get()));
}

void BalloonViewImpl::GetContentsMask(const gfx::Rect& rect,
                                      gfx::Path* path) const {
  // This rounds the corners, and we also cut out a circle for the close
  // button, since we can't guarantee the ordering of two top-most windows.
  SkScalar radius = SkIntToScalar(BubbleBorder::GetCornerRadius());
  SkScalar spline_radius = radius -
      SkScalarMul(radius, (SK_ScalarSqrt2 - SK_Scalar1) * 4 / 3);
  SkScalar left = SkIntToScalar(0);
  SkScalar top = SkIntToScalar(0);
  SkScalar right = SkIntToScalar(rect.width());
  SkScalar bottom = SkIntToScalar(rect.height());

  path->moveTo(left, top);
  path->lineTo(right, top);
  path->lineTo(right, bottom - radius);
  path->cubicTo(right, bottom - spline_radius,
                right - spline_radius, bottom,
                right - radius, bottom);
  path->lineTo(left + radius, bottom);
  path->cubicTo(left + spline_radius, bottom,
                left, bottom - spline_radius,
                left, bottom - radius);
  path->lineTo(left, top);
  path->close();
}

void BalloonViewImpl::GetFrameMask(const gfx::Rect& rect,
                                   gfx::Path* path) const {
  SkScalar radius = SkIntToScalar(BubbleBorder::GetCornerRadius());
  SkScalar spline_radius = radius -
      SkScalarMul(radius, (SK_ScalarSqrt2 - SK_Scalar1) * 4 / 3);
  SkScalar left = SkIntToScalar(rect.x());
  SkScalar top = SkIntToScalar(rect.y());
  SkScalar right = SkIntToScalar(rect.right());
  SkScalar bottom = SkIntToScalar(rect.bottom());

  path->moveTo(left, bottom);
  path->lineTo(left, top + radius);
  path->cubicTo(left, top + spline_radius,
                left + spline_radius, top,
                left + radius, top);
  path->lineTo(right - radius, top);
  path->cubicTo(right - spline_radius, top,
                right, top + spline_radius,
                right, top + radius);
  path->lineTo(right, bottom);
  path->lineTo(left, bottom);
  path->close();
}

gfx::Point BalloonViewImpl::GetContentsOffset() const {
  return gfx::Point(kLeftShadowWidth + kLeftMargin,
                    kTopShadowWidth + kTopMargin);
}

int BalloonViewImpl::GetShelfHeight() const {
  // TODO(johnnyg): add scaling here.
  return kDefaultShelfHeight;
}

int BalloonViewImpl::GetBalloonFrameHeight() const {
  return GetTotalHeight() - GetShelfHeight();
}

int BalloonViewImpl::GetTotalWidth() const {
  return balloon_->content_size().width()
      + kLeftMargin + kRightMargin + kLeftShadowWidth + kRightShadowWidth;
}

int BalloonViewImpl::GetTotalHeight() const {
  return balloon_->content_size().height()
      + kTopMargin + kBottomMargin + kTopShadowWidth + kBottomShadowWidth
      + GetShelfHeight();
}

gfx::Rect BalloonViewImpl::GetContentsRectangle() const {
  if (!frame_container_)
    return gfx::Rect();

  gfx::Size content_size = balloon_->content_size();
  gfx::Point offset = GetContentsOffset();
  gfx::Rect frame_rect;
  frame_container_->GetBounds(&frame_rect, true);
  return gfx::Rect(frame_rect.x() + offset.x(),
                   frame_rect.y() + GetShelfHeight() + offset.y(),
                   content_size.width(),
                   content_size.height());
}

void BalloonViewImpl::Paint(gfx::Canvas* canvas) {
  DCHECK(canvas);
  // Paint the menu bar area white, with proper rounded corners.
  gfx::Path path;
  gfx::Rect rect = GetLocalBounds(false);
  rect.set_height(GetShelfHeight());
  GetFrameMask(rect, &path);

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(kControlBarBackgroundColor);
  canvas->AsCanvasSkia()->drawPath(path, paint);

  // Draw a 1-pixel gray line between the content and the menu bar.
  int line_width = GetTotalWidth() - kLeftMargin - kRightMargin;
  canvas->FillRectInt(kControlBarSeparatorLineColor,
      kLeftMargin, 1 + GetShelfHeight(), line_width, 1);

  View::Paint(canvas);
  PaintBorder(canvas);
}

// menus::SimpleMenuModel::Delegate methods
bool BalloonViewImpl::IsCommandIdChecked(int /* command_id */) const {
  // Nothing in the menu is checked.
  return false;
}

bool BalloonViewImpl::IsCommandIdEnabled(int /* command_id */) const {
  // All the menu options are always enabled.
  return true;
}

bool BalloonViewImpl::GetAcceleratorForCommandId(
    int /* command_id */, menus::Accelerator* /* accelerator */) {
  // Currently no accelerators.
  return false;
}

void BalloonViewImpl::ExecuteCommand(int command_id) {
  DesktopNotificationService* service =
      balloon_->profile()->GetDesktopNotificationService();
  switch (command_id) {
    case kRevokePermissionCommand:
      service->DenyPermission(balloon_->notification().origin_url());
      break;
    default:
      NOTIMPLEMENTED();
  }
}

void BalloonViewImpl::Observe(NotificationType type,
                              const NotificationSource& source,
                              const NotificationDetails& details) {
  if (type != NotificationType::NOTIFY_BALLOON_DISCONNECTED) {
    NOTREACHED();
    return;
  }

  // If the renderer process attached to this balloon is disconnected
  // (e.g., because of a crash), we want to close the balloon.
  notification_registrar_.Remove(this,
      NotificationType::NOTIFY_BALLOON_DISCONNECTED, Source<Balloon>(balloon_));
  Close(false);
}
