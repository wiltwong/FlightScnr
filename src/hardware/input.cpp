#include "hardware/input.h"

#include <Wire.h>

#include <cmath>

//#include "TouchDrvCHSC5816.hpp"
#include <Adafruit_CST8XX.h>
#include "config.h"
#include "global_vars.h"
//#include "hardware/buzzer.h"
#include "hardware/pin_config.h"
#include "services/wifi_setup.h"
#include <Adafruit_PCF8574.h>

namespace {

TaskHandle_t encTaskHandle = NULL;
TaskHandle_t swTaskHandle = NULL;
TaskHandle_t tsTaskHandle = NULL;

portMUX_TYPE s_input_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_encoder_step_pending = false;
volatile int8_t s_encoder_pending_delta = 0;
int8_t s_encoder_accum = 0;

/** Quadrature transition: -1, 0, or +1 half-step. */
constexpr int8_t kEncoderQuad[16] = {
    0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0,
};
constexpr int8_t kEncoderDetentThreshold = 2;
volatile bool s_knob_tap_pending = false;
volatile bool s_knob_press_pending = false;
volatile int16_t s_tap_x = -1;
volatile int16_t s_tap_y = -1;
volatile SwipeGesture s_swipe_pending = SwipeNone;
volatile bool s_knob_is_down = false;
volatile unsigned long s_knob_down_ms = 0;
bool s_long_press_handled = false;

uint8_t s_knob_previous = 0;

//TouchDrvCHSC5816 s_touch;
Adafruit_CST8XX s_touch = Adafruit_CST8XX();
bool s_touch_ready = false;
bool s_touch_was_down = false;
bool s_touch_tracking = false;
int16_t s_touch_start_x = 0;
int16_t s_touch_start_y = 0;
int16_t s_touch_last_x = 0;
int16_t s_touch_last_y = 0;

constexpr int kSwipeMinPx = 70;
constexpr int kTapMaxPx = 25;

void swTask(void *pvParameters) {
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(10));
    
    const unsigned long s_knob_start_time = millis();
    volatile unsigned long s_knob_stop_time = s_knob_start_time;

    //portENTER_CRITICAL_ISR(&s_input_mux);
    while (pcf8574.digitalRead(config::kKnobKeyPin) == LOW && !s_long_press_handled) {
      vTaskDelay(pdMS_TO_TICKS(20));
      s_knob_is_down = true;
      s_knob_stop_time = millis();
      if (s_knob_stop_time - s_knob_start_time > config::kKnobResetHoldMs) {
        s_long_press_handled = true;
        continue;
      }
    }
    s_knob_is_down = false;

    if ((s_knob_stop_time - s_knob_start_time > config::kKnobResetHoldMs) || s_long_press_handled) {
      // Handle long press action here
      Serial.println("Knob held, resetting Wi-Fi");
      wifiResetCredentialsAndReboot();
      s_long_press_handled = false;
    } else if (s_knob_stop_time - s_knob_start_time >= config::kKnobTapMinMs) {
      // Handle short press action here
      Serial.println("Short Press Detected");
      s_knob_tap_pending = true;
    }
    //portEXIT_CRITICAL_ISR(&s_input_mux);
  }
}

void initKnobButton() {
  pcf8574.pinMode(config::kKnobKeyPin, INPUT_PULLUP);
}

void initEncoder() {
  pinMode(config::kKnobPinA, INPUT);
  pinMode(config::kKnobPinB, INPUT);
  s_knob_previous = 0;
  if (digitalRead(config::kKnobPinA)) {
    s_knob_previous |= 0x02;
  }
  if (digitalRead(config::kKnobPinB)) {
    s_knob_previous |= 0x01;
  }
}

void encTask(void *pvParameters) {
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t state = 0;
    if (digitalRead(config::kKnobPinA)) {
      state |= 0x02;
    }
    if (digitalRead(config::kKnobPinB)) {
      state |= 0x01;
    }

    if (state == s_knob_previous) {
      continue;
    }

    const int8_t movement = kEncoderQuad[(s_knob_previous << 2) | state];
    s_knob_previous = state;
    if (movement == 0) {
      continue;
    }

    s_encoder_accum += movement;
    //portENTER_CRITICAL(&s_input_mux);
    if (s_encoder_accum >= kEncoderDetentThreshold) {
    s_encoder_step_pending = true;
    s_encoder_pending_delta = 1;
    s_encoder_accum -= kEncoderDetentThreshold;
    } else if (s_encoder_accum <= -kEncoderDetentThreshold) {
      s_encoder_step_pending = true;
      s_encoder_pending_delta = -1;
      s_encoder_accum += kEncoderDetentThreshold;
    }
    //portEXIT_CRITICAL(&s_input_mux);
  }
}

void initTouch() {
  //s_touch.setPins(TOUCH_RST, TOUCH_INT);
  //if (!s_touch.begin(Wire, CHSC5816_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
  if (!s_touch.begin(&Wire, CST8XX_SLAVE_ADDRESS)) {
    Serial.println("CHSC5816 touch init failed — encoder/knob only");
    s_touch_ready = false;
    return;
  }
  s_touch_ready = true;
  Serial.println("CHSC5816 touch ready");
}

void queueSwipe(SwipeGesture gesture) {
  portENTER_CRITICAL(&s_input_mux);
  s_swipe_pending = gesture;
  portEXIT_CRITICAL(&s_input_mux);
}

void queueTap(int16_t x, int16_t y) {
  portENTER_CRITICAL(&s_input_mux);
  s_tap_x = x;
  s_tap_y = y;
  portEXIT_CRITICAL(&s_input_mux);
}

void finishTouchGesture() {
  const int dx = s_touch_last_x - s_touch_start_x;
  const int dy = s_touch_last_y - s_touch_start_y;
  const int adx = std::abs(dx);
  const int ady = std::abs(dy);

  if (dx <= -kSwipeMinPx && ady * 2 < adx) {
    queueSwipe(SwipeLeft);
  } else if (dx >= kSwipeMinPx && ady * 2 < adx) {
    queueSwipe(SwipeRight);
  } else if (dy >= kSwipeMinPx && adx * 2 < ady) {
    queueSwipe(SwipeDown);
  } else if (dy <= -kSwipeMinPx && adx * 2 < ady) {
    queueSwipe(SwipeUp);
  } else if (adx <= kTapMaxPx && ady <= kTapMaxPx) {
    queueTap(s_touch_last_x, s_touch_last_y);
  }
}

void tsTask(void *pvParameters) {
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(5));
    if (!s_touch_ready) {
      continue;
    }

    CST_TS_Point points;
    bool down = s_touch.touched();

    if (down) {
      vTaskDelay(pdMS_TO_TICKS(10));
      down = s_touch.touched(); // did we really get a touch, or was it some noise
      if (down)
        points = s_touch.getPoint(0);
    }

    //if(down)
    //  Serial.println("****** TOUCHED ******");

    if (down && !s_touch_was_down) {
      s_touch_start_x = points.x;
      s_touch_start_y = points.y;
      s_touch_last_x = points.x;
      s_touch_last_y = points.y;
      s_touch_tracking = true;
      //hardware::buzzerClick();
    } else if (down && s_touch_tracking) {
      s_touch_last_x = points.x;
      s_touch_last_y = points.y;
    } else if (!down && s_touch_was_down && s_touch_tracking) {
      finishTouchGesture();
      s_touch_tracking = false;
    }

    s_touch_was_down = down;
  }
}

}  // namespace

void inputInit() {
  initKnobButton();
  xTaskCreatePinnedToCore(swTask, "SWITCH", 2048, NULL, 1, &swTaskHandle, 0);

  initEncoder();
  xTaskCreatePinnedToCore(encTask, "ENC", 2048, NULL, 1, &encTaskHandle, 0);
  
  initTouch();
  xTaskCreatePinnedToCore(tsTask, "TOUCHSCREEN", 2048, NULL, 1, &tsTaskHandle, 0);
}

int8_t inputConsumeEncoderDelta() {
  portENTER_CRITICAL(&s_input_mux);
  const int8_t delta = s_encoder_step_pending ? s_encoder_pending_delta : 0;
  if (delta != 0) {
    s_encoder_step_pending = false;
    s_encoder_pending_delta = 0;
  }
  portEXIT_CRITICAL(&s_input_mux);
  return delta;
}

bool inputConsumeKnobTap() {
  portENTER_CRITICAL(&s_input_mux);
  const bool tap = s_knob_tap_pending;
  if (tap) {
    s_knob_tap_pending = false;
  }
  portEXIT_CRITICAL(&s_input_mux);
  return tap;
}

bool inputConsumeKnobPress() {
  portENTER_CRITICAL(&s_input_mux);
  const bool press = s_knob_press_pending;
  if (press) {
    s_knob_press_pending = false;
  }
  portEXIT_CRITICAL(&s_input_mux);
  return press;
}

bool inputConsumeScreenTap(int16_t* x, int16_t* y) {
  portENTER_CRITICAL(&s_input_mux);
  const bool tap = s_tap_x >= 0 && s_tap_y >= 0;
  if (tap) {
    if (x != nullptr) {
      *x = s_tap_x;
    }
    if (y != nullptr) {
      *y = s_tap_y;
    }
    s_tap_x = -1;
    s_tap_y = -1;
  }
  portEXIT_CRITICAL(&s_input_mux);
  return tap;
}

SwipeGesture inputConsumeSwipe() {
  portENTER_CRITICAL(&s_input_mux);
  const SwipeGesture swipe = s_swipe_pending;
  if (swipe != SwipeNone) {
    s_swipe_pending = SwipeNone;
  }
  portEXIT_CRITICAL(&s_input_mux);
  return swipe;
}

void inputDiscardPendingInteractions() {
  portENTER_CRITICAL(&s_input_mux);
  s_encoder_step_pending = false;
  s_encoder_pending_delta = 0;
  s_encoder_accum = 0;
  s_knob_tap_pending = false;
  s_knob_press_pending = false;
  s_swipe_pending = SwipeNone;
  s_tap_x = -1;
  s_tap_y = -1;
  portEXIT_CRITICAL(&s_input_mux);
}
/*
void inputPollLongPress() {
  if (digitalRead(config::kKnobKeyPin) == LOW) {
    portENTER_CRITICAL(&s_input_mux);
    if (!s_knob_is_down) {
      s_knob_is_down = true;
      s_knob_down_ms = millis();
    }
    const unsigned long down_ms = s_knob_down_ms;
    portEXIT_CRITICAL(&s_input_mux);

    if (!s_long_press_handled && millis() - down_ms >= config::kKnobResetHoldMs) {
      s_long_press_handled = true;
      Serial.println("Knob held, resetting Wi-Fi");
      wifiResetCredentialsAndReboot();
    }
  } else {
    portENTER_CRITICAL(&s_input_mux);
    s_knob_is_down = false;
    portEXIT_CRITICAL(&s_input_mux);
    s_long_press_handled = false;
  }
}
*/
