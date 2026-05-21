void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("STA started");
      break;

    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("Connected to WiFi!");

      // Sync with time server
      configTime(gmtOffset_sec, daylightOffset_sec, TIME_SERVER);
      WIFI_IS_CONNECTED = true;

      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("STA IP Address: ");
      Serial.println(WiFi.localIP());
      Serial.print("STA Gateway: ");
      Serial.println(WiFi.gatewayIP());
      break;

    default:
      break;
  }
}


void wifi_connect() {

  if (WIFI_CONNECTION_SSID.isEmpty()) {
    Serial.println("No WiFi credentials stored.");
    return;
  }
  Serial.printf("Connecting to STA SSID: %s\n", WIFI_CONNECTION_SSID.c_str());
  WiFi.softAPdisconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_CONNECTION_SSID.c_str(), WIFI_CONNECTION_PASSWORD.c_str());
}