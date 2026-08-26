// Copyright 2012 Olivier Gillet.
//
// Author: Olivier Gillet (ol.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// User interface.

#include "braids/ui.h"

#include <cstring>

#include "stmlib/system/system_clock.h"

namespace braids {

using namespace stmlib;

const uint32_t kEncoderLongPressTime = 800;
const uint32_t kQuickOctavePressTime = 250;
const uint32_t kQuickOctaveDisplayDelay = 100;

void Ui::Init() {
  encoder_.Init();
  display_.Init();
  display_.set_brightness(settings.GetValue(SETTING_BRIGHTNESS) + 1);
  queue_.Init();
  sub_clock_ = 0;
  value_ = 0;
  mode_ = MODE_SPLASH;
  setting_ = SETTING_OSCILLATOR_SHAPE;
  setting_index_ = 0;
  quick_octave_ = false;
  quick_octave_changed_ = false;
  quick_octave_display_refresh_ = false;
  quick_octave_release_time_ = 0;
  menu_entry_time_ = 0;
  invisible_finger_return_setting_ = SETTING_OSCILLATOR_SHAPE;
  invisible_finger_return_index_ = 0;
  invisible_finger_active_ = false;
}

void Ui::Poll() {
  system_clock.Tick();  // Tick global ms counter.
  ++sub_clock_;
  encoder_.Debounce();

  if (quick_octave_ && encoder_.released()) {
    quick_octave_ = false;
    quick_octave_release_time_ = system_clock.milliseconds();

    if (quick_octave_changed_) {
      settings.Save();
      quick_octave_changed_ = false;
    }
  }

  if (!quick_octave_ &&
      quick_octave_release_time_ != 0 &&
      system_clock.milliseconds() - quick_octave_release_time_ >=
          kQuickOctaveDisplayDelay) {
    quick_octave_display_refresh_ = true;
    quick_octave_release_time_ = 0;
  }

  if (encoder_.just_pressed()) {
    encoder_press_time_ = system_clock.milliseconds();
    inhibit_further_switch_events_ = false;
  } 
  if (!inhibit_further_switch_events_ &&
      !quick_octave_ &&
      mode_ == MODE_EDIT &&
      setting_ == SETTING_OSCILLATOR_SHAPE &&
      encoder_.pressed() &&
      system_clock.milliseconds() - encoder_press_time_ >=
          kQuickOctavePressTime) {
    queue_.AddEvent(CONTROL_ENCODER_LONG_CLICK, 0, 0);
    inhibit_further_switch_events_ = true;
  }

  if (!inhibit_further_switch_events_) {
    if (encoder_.pressed()) {
      uint32_t duration = system_clock.milliseconds() - encoder_press_time_;
      if (duration >= kEncoderLongPressTime) {
        queue_.AddEvent(CONTROL_ENCODER_LONG_CLICK, 0, 0);
        inhibit_further_switch_events_ = true;
      }
    } else if (encoder_.released()) {
      queue_.AddEvent(CONTROL_ENCODER_CLICK, 0, 0);
    }
  }
  int32_t increment = encoder_.increment();
  if (increment != 0) {
    if (settings.invert_encoder()) {
      increment *= -1;
    }
    queue_.AddEvent(CONTROL_ENCODER, 0, increment);
  }
  

  UpdateMenuTimeout();
  if ((sub_clock_ & 1) == 0) {
    display_.Refresh();
  }
}

void Ui::UpdateMenuTimeout() {
  uint8_t timeout = settings.GetValue(SETTING_MENU_TIMEOUT);
  if (timeout == 0) return;

  const uint32_t now = system_clock.milliseconds();
  if (now - menu_entry_time_ < 5000) return;

  // Invisible Finger: back out one menu level after 5 seconds.
  if (mode_ == MODE_EDIT &&
      setting_ != SETTING_OSCILLATOR_SHAPE) {
    mode_ = MODE_MENU;
    menu_entry_time_ = now;
    return;
  }

  if (mode_ == MODE_MENU) {
    // Remember the last main-menu position so the next encoder
    // press from WAVE returns to the same setting.
    invisible_finger_return_setting_ = setting_;
    invisible_finger_return_index_ = setting_index_;
    invisible_finger_active_ = true;

    // ON* invokes the global save when the Invisible Finger
    // exits the main settings screen. ON does not.
    if (timeout == 2) {
      settings.Save();
    }

    setting_ = SETTING_OSCILLATOR_SHAPE;
    setting_index_ = 0;
    mode_ = MODE_EDIT;
    menu_entry_time_ = 0;
  }
}

void Ui::FlushEvents() {
  queue_.Flush();
}




void Ui::RefreshDisplay() {
  switch (mode_) {
    case MODE_SPLASH:
      {
        char text[] = "    ";
        text[0] = '\x98' + (splash_frame_ & 0x7);
        display_.Print(text);
      }
      break;
    
    case MODE_EDIT:
      {
        if (quick_octave_) {
          uint8_t octave = settings.GetValue(SETTING_PITCH_OCTAVE);
          display_.Print(
              settings.metadata(SETTING_PITCH_OCTAVE).strings[octave]);
          break;
        }

        uint8_t value = settings.GetValue(setting_);
        if (setting_ == SETTING_OSCILLATOR_SHAPE &&
            settings.meta_modulation()) {
          value = meta_shape_;
        }
        display_.Print(settings.metadata(setting_).strings[value]);
      }
      break;
      
    case MODE_MENU:
      {
        if (setting_ == SETTING_CV_TESTER) {
          char text[] = "    ";
          if (!blink_) {
            for (uint8_t i = 0; i < kDisplayWidth; ++i) {
              text[i] = '\x90' + (cv_[i] * 7 >> 12);
            }
          }
          display_.Print(text);
        } else {
          display_.Print(settings.metadata(setting_).name);
        }
      }
      break;
      
    case MODE_CALIBRATION_STEP_1:
      display_.Print(">C2 ");
      break;

    case MODE_CALIBRATION_STEP_2:
      display_.Print(">C4 ");
      break;

    default:
      break;
  }
}

void Ui::OnLongClick() {
  switch (mode_) {
    case MODE_EDIT:
      if (setting_ != SETTING_OSCILLATOR_SHAPE) {
        break;
      }
      quick_octave_ = true;
      quick_octave_changed_ = false;
      break;

    case MODE_MENU:
      menu_entry_time_ = system_clock.milliseconds();
      if (setting_ == SETTING_CALIBRATION) {
        mode_ = MODE_CALIBRATION_STEP_1;
      }
      break;
    
    default:
      break;
  }
}

void Ui::OnClick() {
  switch (mode_) {
    case MODE_EDIT:
      if (invisible_finger_active_) {
        mode_ = MODE_MENU;
        setting_ = invisible_finger_return_setting_;
        setting_index_ = invisible_finger_return_index_;
        invisible_finger_active_ = false;
        menu_entry_time_ = system_clock.milliseconds();
      } else {
        mode_ = MODE_MENU;
        menu_entry_time_ = system_clock.milliseconds();
      }
      break;
      
    case MODE_MENU:
      if (setting_ <= SETTING_LAST_EDITABLE_SETTING) {
        mode_ = MODE_EDIT;
        if (setting_ == SETTING_OSCILLATOR_SHAPE) {
          settings.Save();
        }
      } else if (setting_ == SETTING_VERSION) {
        mode_ = MODE_SPLASH;
      }
      break;
      
    case MODE_CALIBRATION_STEP_1:
      adc_code_c2_ = cv_[2];
      adc_code_min_[0] = cv_[0];
      adc_code_min_[1] = cv_[1];
      mode_ = MODE_CALIBRATION_STEP_2;
      break;
      
    case MODE_CALIBRATION_STEP_2:
      settings.Calibrate(
          adc_code_c2_,
          cv_[2],
          cv_[3],
          adc_code_min_[0],
          cv_[0],
          adc_code_min_[1],
          cv_[1]);
      mode_ = MODE_MENU;
      break;
      
    default:
      break;
  }
}

void Ui::OnIncrement(const Event& e) {
  if (quick_octave_) {
    int16_t value = settings.GetValue(SETTING_PITCH_OCTAVE);
    value += e.data;

    if (value < 0) {
      value = 0;
    } else if (value > 4) {
      value = 4;
    }

    settings.SetValue(SETTING_PITCH_OCTAVE, value);
    quick_octave_changed_ = true;
    return;
  }

  switch (mode_) {

    case MODE_EDIT:
      {
        int16_t value = settings.GetValue(setting_);
        value = settings.metadata(setting_).Clip(value + e.data);
        settings.SetValue(setting_, value);
        menu_entry_time_ = system_clock.milliseconds();
        display_.set_brightness(settings.GetValue(SETTING_BRIGHTNESS) + 1);
      }
      break;
      
    case MODE_MENU:
      {
        menu_entry_time_ = system_clock.milliseconds();
        setting_index_ += e.data;
        if (setting_index_ < 0) {
          setting_index_ = 0;
        } else if (setting_index_ >= SETTING_LAST) {
          setting_index_ = SETTING_LAST - 1;
        }
        setting_ = settings.setting_at_index(setting_index_);
      }
      break;
      
    default:
      break;
  }
}

void Ui::DoEvents() {
  bool refresh_display_ = false;

  if (quick_octave_display_refresh_) {
    refresh_display_ = true;
    quick_octave_display_refresh_ = false;
  }

  while (queue_.available()) {
    Event e = queue_.PullEvent();
    if (e.control_type == CONTROL_ENCODER_CLICK) {
      OnClick();
    } else if (e.control_type == CONTROL_ENCODER_LONG_CLICK) {
      OnLongClick();
    } else if (e.control_type == CONTROL_ENCODER) {
      OnIncrement(e);
    }
    refresh_display_ = true;
  }

  if (queue_.idle_time() > 1000) {
    refresh_display_ = true;
  }
  if (queue_.idle_time() >= 50 && mode_ == MODE_SPLASH) {
    ++splash_frame_;
    if (splash_frame_ == 8) {
      splash_frame_ = 0;
      mode_ = MODE_EDIT;
      setting_ = SETTING_OSCILLATOR_SHAPE;
    }
    refresh_display_ = true;
  }
  if (queue_.idle_time() >= 50 &&
      (setting_ == SETTING_CV_TESTER)) {
    refresh_display_ = true;
  }
  if (queue_.idle_time() >= 50 &&
      setting_ == SETTING_OSCILLATOR_SHAPE &&
      mode_ == MODE_EDIT &&
      settings.meta_modulation()) {
    refresh_display_ = true;
  }
  if (refresh_display_) {
    queue_.Touch();
    RefreshDisplay();
    blink_ = false;
  }
}

}  // namespace braids
