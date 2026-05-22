#include <Arduino.h>
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include <WiFi.h>
#include <PubSubClient.h>


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

static const uint32_t LOCK_MS              = 7000;   // 7 sek. laasetid + LED on
static const uint32_t DEBOUNCE_MS          = 40;     // Software debounce
static const uint32_t IDLE_BEFORE_SLEEP_MS = 8000;   // Vent paa nyt tryk


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

// Globals
String WIFI_CONNECTION_SSID = "IoT_H3/4";
String WIFI_CONNECTION_PASSWORD = "98806829";
bool WIFI_IS_CONNECTED = false;
const char *TIME_SERVER = "pool.ntp.org";
const long gmtOffset_sec = 0;     // We just assume UTC
const int daylightOffset_sec = 0;

String MQTT_SERVER = "wilsons.local";
String CLIENT_ID = "device04";
String CLIENT_PASS = "8HYcRWdQ";
String TOPIC_NAME = "/devices/device04/report";

unsigned long lastMQTTPublish = 0;
const unsigned long mqttPublishInterval = 60000;  // every minute
String mqttData = "[]";
bool dataFetched = false;
int MQTT_RECONNECT = 0;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

bool FIRST_SETUP = true;

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

void printTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return;
  }
  Serial.println(&timeinfo, "%A, %B, %d %Y %H:%M:%S zone %Z %x");
}

void handleFeedback(int idx) {
  if (idx < 0 || idx >= 4) return;
  pressCount++;
  Serial.printf("Knap trykket: %s (seq=%d)\n", BUTTONS[idx].label, pressCount);

  allLedsOff();
  digitalWrite(BUTTONS[idx].led, HIGH);


  if (!mqttClient.connected()) {
    mqttReconnect();
    publishData(BUTTONS[idx].label);
  } else {
    Serial.println("Publishing Data");
    publishData(BUTTONS[idx].label);
  }


  uint32_t lockStart = millis();
  while (millis() - lockStart < LOCK_MS) {
    delay(50);
  }
  digitalWrite(BUTTONS[idx].led, LOW);
}

void enterDeepSleep() {
  Serial.println("Gaar i deep sleep. EXT1 wakeup aktiv.");
  allLedsOff();

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

  if (FIRST_SETUP) {

    if (!WIFI_IS_CONNECTED) {
      WiFi.onEvent(WiFiEvent);
      wifi_connect();
    }

    mqttSetup();
    FIRST_SETUP = false;
  }



  printTime();

  ++bootCount;
  Serial.printf("\n=== Smiley Feedback Console - boot #%d ===\n", bootCount);
  printWakeupReason();

  esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();

  // 1) Hvis vi blev vaakket af et knaptryk -> behandl feedback
  int idx = -1;
  if (reason == ESP_SLEEP_WAKEUP_EXT1) {
    idx = identifyWakeupButton();
  } else {
    blinkAllLeds(2, 120);
  }

  if (idx >= 0) {
    handleFeedback(idx);
  }

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

  enterDeepSleep();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      mqttReconnect();
    } else {
      // Always call loop() when connected
      mqttClient.loop();
    }
  }
}
