// led_state.h — single-pixel status LED (M5 Atom WS2812 on G27).
//   blue   = boot / no link
//   red    = link up but no DHCP lease
//   yellow = link + lease but MQTT not connected (when MQTT host configured)
//   green  = all good (or MQTT not configured and link+lease)
//   white  = brief flash on each successful publish

#pragma once

#include <Arduino.h>
#include <FastLED.h>

static const int   PIN_NEOPIXEL = 27;
static const int   NEOPIXEL_NUM = 1;
static const uint8_t LED_BRIGHTNESS = 30;

extern CRGB g_led[NEOPIXEL_NUM];

enum LedState {
  LED_BOOT,
  LED_NO_LINK,
  LED_NO_LEASE,
  LED_NO_MQTT,
  LED_OK,
  LED_PUB
};

extern LedState g_led_state;

inline void ledBegin() {
  FastLED.addLeds<WS2812, PIN_NEOPIXEL, GRB>(g_led, NEOPIXEL_NUM);
  FastLED.setBrightness(LED_BRIGHTNESS);
  g_led_state = LED_BOOT;
}

inline void ledApply() {
  switch (g_led_state) {
    case LED_BOOT:     g_led[0] = CRGB(0, 0, 50);  break;
    case LED_NO_LINK:  g_led[0] = CRGB(80, 0, 0);  break;
    case LED_NO_LEASE: g_led[0] = CRGB(80, 0, 0);  break;
    case LED_NO_MQTT:  g_led[0] = CRGB(60, 40, 0); break;
    case LED_OK:       g_led[0] = CRGB(0, 30, 0);  break;
    case LED_PUB:      g_led[0] = CRGB(60, 60, 60); break;
  }
  FastLED.show();
}

inline void ledFlashPublish() {
  LedState prev = g_led_state;
  g_led_state = LED_PUB;
  ledApply();
  delay(40);
  g_led_state = prev;
  ledApply();
}
