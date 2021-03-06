// Copyright 2016 Cheng Zhao. All rights reserved.
// Use of this source code is governed by the license that can be found in the
// LICENSE file.

#include "nativeui/button.h"

#include <windowsx.h>

#include "base/strings/utf_string_conversions.h"
#include "base/win/scoped_hdc.h"
#include "nativeui/container.h"
#include "nativeui/events/win/event_win.h"
#include "nativeui/gfx/geometry/insets.h"
#include "nativeui/gfx/geometry/size_conversions.h"
#include "nativeui/gfx/geometry/vector2d_conversions.h"
#include "nativeui/gfx/win/screen_win.h"
#include "nativeui/gfx/win/text_win.h"
#include "nativeui/state.h"
#include "nativeui/win/view_win.h"
#include "nativeui/win/window_win.h"

namespace nu {

namespace {

const int kButtonPadding = 3;
const int kCheckboxPadding = 1;

class ButtonImpl : public ViewImpl {
 public:
  ButtonImpl(Button::Type type, Button* delegate)
      : ViewImpl(type == Button::Type::Normal ? ControlType::Button
                    : (type == Button::Type::Checkbox ? ControlType::Checkbox
                                                      : ControlType::Radio),
                 delegate) {
    set_focusable(true);
    OnDPIChanged();  // update component size
  }

  void SetTitle(const base::string16& title) {
    title_ = title;
    OnDPIChanged();  // update component size
  }

  base::string16 GetTitle() const {
    return title_;
  }

  SizeF GetPreferredSize() const {
    float padding =
        (type() == ControlType::Button ? kButtonPadding : kCheckboxPadding) *
        scale_factor();
    SizeF preferred_size(text_size_);
    preferred_size.Enlarge(box_size_.width() + padding * 2, padding * 2);
    return preferred_size;
  }

  void OnClick() {
    if (type() == ControlType::Checkbox)
      SetChecked(!IsChecked());
    else if (type() == ControlType::Radio && !IsChecked())
      SetChecked(true);
    Button* button = static_cast<Button*>(delegate());
    button->on_click.Emit(button);
  }

  void SetChecked(bool checked) {
    if (IsChecked() == checked)
      return;

    params_.checked = checked;
    Invalidate();

    // And flip all other radio buttons' state.
    View* parent = delegate()->GetParent();
    if (checked && type() == ControlType::Radio && parent &&
        parent->GetNative()->type() == ControlType::Container) {
      auto* container = static_cast<Container*>(parent);
      for (int i = 0; i < container->ChildCount(); ++i) {
        View* child = container->ChildAt(i);
        if (child != delegate() &&
            child->GetNative()->type() == ControlType::Radio)
          static_cast<Button*>(child)->SetChecked(false);
      }
    }
  }

  bool IsChecked() const {
    return params_.checked;
  }

  // ViewImpl:
  void Draw(PainterWin* painter, const Rect& dirty) override {
    Size size = size_allocation().size();
    Size preferred_size = ToCeiledSize(GetPreferredSize());

    NativeTheme::ExtraParams params;
    params.button = params_;

    // Draw the button background,
    if (type() == ControlType::Button)
      painter->DrawNativeTheme(NativeTheme::Part::Button,
                               state(), Rect(size), params);

    // Draw control background as a layer on button background.
    if (!background_color().transparent()) {
      ViewImpl::Draw(painter, dirty);
    }

    // Checkbox and radio are left aligned.
    Point origin;
    if (type() == ControlType::Button) {
      origin.Offset((size.width() - preferred_size.width()) / 2,
                    (size.height() - preferred_size.height()) / 2);
    } else {
      origin.Offset(0, (size.height() - preferred_size.height()) / 2);
    }

    // Draw the box.
    Point box_origin = origin;
    box_origin.Offset(0, (preferred_size.height() - box_size_.height()) / 2);
    if (type() == ControlType::Checkbox)
      painter->DrawNativeTheme(NativeTheme::Part::Checkbox,
                               state(), Rect(box_origin, box_size_), params);
    else if (type() == ControlType::Radio)
      painter->DrawNativeTheme(NativeTheme::Part::Radio,
                               state(), Rect(box_origin, box_size_), params);

    // The bounds of text.
    int padding = std::ceil(
        (type() == ControlType::Button ? kButtonPadding : kCheckboxPadding) *
        scale_factor());
    Rect text_bounds(origin, preferred_size);
    text_bounds.Inset(box_size_.width() + padding, padding, padding, padding);

    // The text.
    TextAttributes attributes(font(), color(), TextAlign::Center,
                              TextAlign::Center);
    painter->DrawTextPixel(title_, text_bounds, attributes);

    // Draw focused ring.
    if (HasFocus()) {
      Rect rect;
      if (type() == ControlType::Button) {
        rect = Rect(size);
        rect.Inset(Insets(std::ceil(1 * scale_factor())));
      } else {
        rect = text_bounds;
        rect.Inset(Insets(padding));
      }
      painter->DrawFocusRect(rect);
    }
  }

  void OnDPIChanged() override {
    NativeTheme* theme = State::GetCurrent()->GetNativeTheme();
    base::win::ScopedGetDC dc(window() ? window()->hwnd() : NULL);
    if (type() == ControlType::Checkbox)
      box_size_ = theme->GetThemePartSize(dc, NativeTheme::Part::Checkbox,
                                          state());
    else if (type() == ControlType::Radio)
      box_size_ = theme->GetThemePartSize(dc, NativeTheme::Part::Radio,
                                          state());
    text_size_ = MeasureText(dc, title_, font());
  }

  void OnMouseEnter(NativeEvent event) override {
    is_hovering_ = true;
    if (!is_capturing_) {
      set_state(ControlState::Hovered);
      Invalidate();
    }
    ViewImpl::OnMouseEnter(event);
  }

  void OnMouseLeave(NativeEvent event) override {
    is_hovering_ = false;
    if (!is_capturing_) {
      set_state(ControlState::Normal);
      Invalidate();
    }
    ViewImpl::OnMouseLeave(event);
  }

  bool OnMouseClick(NativeEvent event) override {
    // Clicking a button moves the focus to it.
    // This has to be done before handling the mouse event, because user may
    // want to move focus to other view later.
    if (event->message == WM_LBUTTONDOWN)
      window()->focus_manager()->TakeFocus(this);

    if (ViewImpl::OnMouseClick(event))
      return true;

    if (event->message == WM_LBUTTONDOWN) {
      is_capturing_ = true;
      window()->SetCapture(this);
      set_state(ControlState::Pressed);
      Invalidate();
    } else {
      if (event->message == WM_LBUTTONUP) {
        if (state() == ControlState::Pressed)
          OnClick();
        set_state(ControlState::Hovered);
        Invalidate();
      }
      window()->ReleaseCapture();
    }
    return true;
  }

  void OnCaptureLost() override {
    is_capturing_ = false;
    set_state(is_hovering_ ? ControlState::Hovered : ControlState::Normal);
    Invalidate();
  }

  NativeTheme::ButtonExtraParams* params() { return &params_; }

 private:
  NativeTheme::ButtonExtraParams params_ = {0};

  // The size of box for radio and checkbox.
  Size box_size_;

  // The size of text.
  SizeF text_size_;

  // Whether the button is capturing the mouse.
  bool is_capturing_ = false;

  // Whether the mouse is hovering the button.
  bool is_hovering_ = false;

  base::string16 title_;
};

}  // namespace

Button::Button(const std::string& title, Type type) {
  TakeOverView(new ButtonImpl(type, this));
  SetTitle(title);
}

Button::~Button() {
}

void Button::SetTitle(const std::string& title) {
  auto* button = static_cast<ButtonImpl*>(GetNative());
  base::string16 wtitle = base::UTF8ToUTF16(title);
  button->SetTitle(wtitle);

  SetDefaultStyle(ScaleSize(button->GetPreferredSize(),
                            1.0f / button->scale_factor()));
  button->Invalidate();
}

std::string Button::GetTitle() const {
  return base::UTF16ToUTF8(static_cast<ButtonImpl*>(GetNative())->GetTitle());
}

void Button::SetChecked(bool checked) {
  static_cast<ButtonImpl*>(GetNative())->SetChecked(checked);
}

bool Button::IsChecked() const {
  return static_cast<ButtonImpl*>(GetNative())->IsChecked();
}

}  // namespace nu
