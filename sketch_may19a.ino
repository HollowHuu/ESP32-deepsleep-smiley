/*
  Smiley Feedback Console (ESP32) - smiley-board only
  ---------------------------------------------------
  H4 - IoT og embeddede systemer, del 2
  Denne version indeholder kun selve smiley-anordningen:
    - 4 knapper med 4 tilhoerende LED
    - Software-debounce paa knapper
    - 7 sekunders laasetid efter tryk, LED for den trykkede knap
      lyser i hele perioden
    - DeepSleep mellem tryk; vaagner via EXT1 (knaptryk paa RTC GPIO)

  WiFi, NTP og MQTT er bevidst udeladt og tilfoejes senere.
*/

#include <Arduino.h>
#include "driver/rtc_io.h"
#include "esp_sleep.h"

// ---------------------------------------------------------------------------
// Pin-konfiguration
// ---------------------------------------------------------------------------
#define BUTTON_GREEN   GPIO_NUM_14   // Smiley :)  - meget tilfreds
#define BUTTON_YELLOW  GPIO_NUM_27   // Smiley :|  - neutral
#define BUTTON_RED     GPIO_NUM_26   // Smiley :(  - utilfreds
#define BUTTON_BLUE    GPIO_NUM_25   // Smiley :O  - meget utilfreds

#define LED_GREEN   18
#define LED_YELLOW  19
#define LED_RED     21
#define LED_BLUE     5

#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)
const uint64_t WAKEUP_BITMASK =
    BUTTON_PIN_BITMASK(BUTTON_GREEN)  |
    BUTTON_PIN_BITMASK(BUTTON_YELLOW) |
    BUTTON_PIN_BITMASK(BUTTON_RED)    |
    BUTTON_PIN_BITMASK(BUTTON_BLUE);

// ---------------------------------------------------------------------------
// Adfaerds-parametre
// ---------------------------------------------------------------------------
static const uint32_t LOCK_MS              = 7000;   // 7 sek. laasetid + LED on
static const uint32_t DEBOUNCE_MS          = 40;     // Software debounce
static const uint32_t IDLE_BEFORE_SLEEP_MS = 8000;   // Vent paa nyt tryk

// ---------------------------------------------------------------------------
// Persistente RTC-variable (overlever deep sleep)
// ---------------------------------------------------------------------------
RTC_DATA_ATTR int bootCount  = 0;
RTC_DATA_ATTR int pressCount = 0;

struct Button {
  gpio_num_t pin;
  uint8_t    led;
  const char* label;
};

const Button BUTTONS[4] = {
  { BUTTON_GREEN,  LED_GREEN,  "happy"   },
  { BUTTON_YELLOW, LED_YELLOW, "neutral" },
  { BUTTON_RED,    LED_RED,    "unhappy" },
  { BUTTON_BLUE,   LED_BLUE,   "angry"   },
};

// ---------------------------------------------------------------------------
// Hjaelpefunktioner
// ---------------------------------------------------------------------------
void allLedsOff() {
  digitalWrite(LED_GREEN,  LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED,    LOW);
  digitalWrite(LED_BLUE,   LOW);
}

void blinkAllLeds(uint8_t times, uint16_t delayMs) {
  for (uint8_t i = 0; i < times; ++i) {
    digitalWrite(LED_GREEN,  HIGH);
    digitalWrite(LED_YELLOW, HIGH);
    digitalWrite(LED_RED,    HIGH);
    digitalWrite(LED_BLUE,   HIGH);
    delay(delayMs);
    allLedsOff();
    delay(delayMs);
  }
}

int identifyWakeupButton() {
  uint64_t status = esp_sleep_get_ext1_wakeup_status();
  if (status == 0) return -1;
  for (int i = 0; i < 4; ++i) {
    if (status & BUTTON_PIN_BITMASK(BUTTONS[i].pin)) return i;
  }
  return -1;
}

int pollButtonsWithDebounce(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    for (int i = 0; i < 4; ++i) {
      if (digitalRead(BUTTONS[i].pin) == HIGH) {
        uint32_t t0 = millis();
        while (millis() - t0 < DEBOUNCE_MS) {
          if (digitalRead(BUTTONS[i].pin) == LOW) { t0 = millis(); break; }
        }
        if (digitalRead(BUTTONS[i].pin) == HIGH) return i;
      }
    }
    delay(5);
  }
  return -1;
}

void handleFeedback(int idx) {
  if (idx < 0 || idx >= 4) return;
  pressCount++;
  Serial.printf("Knap trykket: %s (seq=%d)\n", BUTTONS[idx].label, pressCount);

  allLedsOff();
  digitalWrite(BUTTONS[idx].led, HIGH);

  // Hold LED taendt i 7 sek. laasetid (debounce/lockout)
  uint32_t lockStart = millis();
  while (millis() - lockStart < LOCK_MS) {
    delay(50);
  }
  digitalWrite(BUTTONS[idx].led, LOW);
}

void enterDeepSleep() {
  Serial.println("Gaar i deep sleep. EXT1 wakeup aktiv.");
  allLedsOff();

  // Knaptryk vaekker enheden (knapper trækker HIGH naar trykket)
  esp_sleep_enable_ext1_wakeup(WAKEUP_BITMASK, ESP_EXT1_WAKEUP_ANY_HIGH);

  delay(50);
  esp_deep_sleep_start();
}

void printWakeupReason() {
  esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
  switch (reason) {
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("Wakeup: EXT1 (knaptryk).");
      break;
    default:
      Serial.printf("Wakeup: andet/koldstart (%d).\n", reason);
      break;
  }
}

// ---------------------------------------------------------------------------
// setup() = HELE programmet. loop() bruges ikke, vi sover i stedet.
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(150);

  pinMode(LED_GREEN,  OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED,    OUTPUT);
  pinMode(LED_BLUE,   OUTPUT);
  allLedsOff();

  pinMode(BUTTON_GREEN,  INPUT_PULLDOWN);
  pinMode(BUTTON_YELLOW, INPUT_PULLDOWN);
  pinMode(BUTTON_RED,    INPUT_PULLDOWN);
  pinMode(BUTTON_BLUE,   INPUT_PULLDOWN);

  ++bootCount;
  Serial.printf("\n=== Smiley Feedback Console - boot #%d ===\n", bootCount);
  printWakeupReason();

  esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();

  // 1) Hvis vi blev vaakket af et knaptryk -> behandl feedback
  int idx = -1;
  if (reason == ESP_SLEEP_WAKEUP_EXT1) {
    idx = identifyWakeupButton();
  } else {
    // Koldstart: kort visuel selvtest
    blinkAllLeds(2, 120);
  }

  if (idx >= 0) {
    handleFeedback(idx);
  }

  // 2) Vaer aktiv et stykke tid og lyt efter flere tryk (debounced)
  Serial.printf("Aktiv-periode: lytter efter knaptryk i %lu ms...\n",
                (unsigned long)IDLE_BEFORE_SLEEP_MS);
  uint32_t idleStart = millis();
  while (millis() - idleStart < IDLE_BEFORE_SLEEP_MS) {
    uint32_t remaining = IDLE_BEFORE_SLEEP_MS - (millis() - idleStart);
    int next = pollButtonsWithDebounce(remaining);
    if (next >= 0) {
      handleFeedback(next);
      idleStart = millis();   // nulstil idle-timeren efter et tryk
    } else {
      break;
    }
  }

  // 3) Tilbage i deep sleep
  enterDeepSleep();
}

void loop() {
  // Tom: vi anvender deep sleep-arkitektur, alt sker i setup().
}
