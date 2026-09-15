#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  delay(1000);
}

void loop() {
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
  delay(2000);
}