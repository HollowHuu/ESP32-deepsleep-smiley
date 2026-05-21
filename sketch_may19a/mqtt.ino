
void mqttReconnect() {
  while (!mqttClient.connected() && MQTT_RECONNECT < 3) {
    MQTT_RECONNECT += 1;
    Serial.print("Attempting MQTT connection...");
    
    if (mqttClient.connect(CLIENT_ID.c_str(), CLIENT_ID.c_str(), CLIENT_PASS.c_str())) {
      Serial.println("connected!");
      mqttClient.publish("devices/device04/status", "ESP32 connected");
      mqttClient.subscribe(TOPIC_NAME.c_str());
      
      MQTT_RECONNECT = 0;
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying in 5 seconds...");
      delay(5000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.println("Callback!!");
  if (String(topic) == TOPIC_NAME) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
      message += (char)payload[i];
    }
    mqttData = message;
    dataFetched = true;
    Serial.println("Received MQTT data: " + message);
  } else {
    Serial.println(String(topic));
  }
}

void mqttSetup() {
  mqttClient.setServer(MQTT_SERVER.c_str(), 1883);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(50000); // This is insanely large
}

void publishData() {
  unsigned long now = millis();
  if (now - lastMQTTPublish > mqttPublishInterval) {
    lastMQTTPublish = now;
    
    time_t current_time;
    time(&current_time);
    unsigned long long epochMillis = (unsigned long long)current_time * 1000ULL;
    
    if (mqttClient.connected()) {

      char payload[128];
      snprintf(payload, sizeof(payload),
              "{\"temperature\": %.2f, \"timestamp\": %llu}",
              0.0, epochMillis);
      
      if (mqttClient.publish(TOPIC_NAME.c_str(), payload)) {
        Serial.print("Published to MQTT: ");
        Serial.println(payload);
      } else {
        Serial.println("Failed to publish - adding to cache");
      }
    } else {
      Serial.println("MQTT not connected - caching data");
    }
  }
}
