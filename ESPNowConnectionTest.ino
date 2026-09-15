#include <WiFi.h>
#include <esp_now.h>

// حطي هنا MAC Address حق الـ ESP32 الثاني
uint8_t rightShoeMAC[] = {0x58, 0x2A, 0xBD, 0x76, 0x8B, 0x80};
//uint8_t leftShoeMAC[]  = {0x58, 0x2A, 0xBD, 0x78, 0x35, 0x74};

typedef struct struct_message {
  int testValue;
} struct_message;

struct_message myData;

// يشتغل بعد كل عملية إرسال
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.print("Send: ");

  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("Success");
  } else {
    Serial.println("Failed");
  }
}

void setup() {
  Serial.begin(115200);

  // لازم يكون ESP32 على Station Mode
  WiFi.mode(WIFI_STA);

  Serial.println();
  Serial.print("My MAC Address: ");
  Serial.println(WiFi.macAddress());

  // تشغيل ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW initialization failed");
    return;
  }

  // تسجيل Callback الإرسال
  esp_now_register_send_cb(OnDataSent);

  // إضافة الـ ESP32 الثاني كـ Peer
  esp_now_peer_info_t peerInfo = {};

  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  Serial.println("ESP-NOW Ready");
}

void loop() {

  myData.testValue = 1;

  // إرسال رسالة بسيطة
  esp_err_t result = esp_now_send(
    peerAddress,
    (uint8_t *)&myData,
    sizeof(myData)
  );

  if (result != ESP_OK) {
    Serial.println("Error starting send");
  }

  delay(2000);
}