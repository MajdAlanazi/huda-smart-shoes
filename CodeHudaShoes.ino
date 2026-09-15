#include <WiFi.h>
#include <esp_now.h>

// 0 = الجزمة اليمين
// 1 = الجزمة اليسار
#define IS_RIGHT_SHOE 0

uint8_t rightShoeMAC[] = {0x58, 0x2A, 0xBD, 0x76, 0x8B, 0x80};
uint8_t leftShoeMAC[]  = {0x58, 0x2A, 0xBD, 0x78, 0x35, 0x74};

// الحساس الأمامي والخلفي
const int trigFront = 22;
const int echoFront = 21;
const int trigBack  = 16;
const int echoBack  = 4;

// المحركات الأمامي والخلفي
const int motorFront = 23;
const int motorBack  = 17;

// البزر والفلكس
const int buzzerPin = 33;
const int flexPin   = 34;

// اليمين: 27/26 خارجي، 18/5 داخلي
// اليسار: 18/5 خارجي، 27/26 داخلي
#if IS_RIGHT_SHOE
  const int trigOuter  = 27;
  const int echoOuter  = 26;
  const int motorOuter = 25;

  const int trigInner  = 18;
  const int echoInner  = 5;
  const int motorInner = 19;
#else
  const int trigOuter  = 18;
  const int echoOuter  = 5;
  const int motorOuter = 19;

  const int trigInner  = 27;
  const int echoInner  = 26;
  const int motorInner = 25;
#endif

// المسافات بالسنتيمتر
const int noAlertDistance = 150;
const int softDistance    = 100;
const int lightDistance   = 50;
const int mediumDistance  = 15;
const int dangerDistance  = 15;

// قوة الاهتزاز
const int vibrationSoft   = 120;
const int vibrationLight  = 170;
const int vibrationMedium = 220;
const int vibrationStrong = 255;

// PWM
const int pwmFrequency = 5000;
const int pwmResolution = 8;

// بيانات الاتصال بين الجزمتين
typedef struct {
  int flexValue;
  uint8_t alive;
} ShoeStatus;

ShoeStatus myStatus;
ShoeStatus otherStatus;

void onDataSent(
  const wifi_tx_info_t *info,
  esp_now_send_status_t status
) {
}

void onDataRecv(
  const esp_now_recv_info_t *info,
  const uint8_t *incomingData,
  int len
) {
  if (len == sizeof(ShoeStatus)) {
    memcpy(&otherStatus, incomingData, sizeof(otherStatus));
  }
}

int getDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);

  if (duration == 0) {
    return 400;
  }

  // القراءة تظهر كرقم صحيح، بلا كسور
  return (int)round(duration * 0.0343 / 2.0);
}

void controlMotor(int motorPin, int distance) {
  if (distance <= 0 || distance > 150) {
    ledcWrite(motorPin, 0);              // أكثر من 150: إيقاف
  }
  else if (distance > 100) {
    ledcWrite(motorPin, vibrationSoft);  // 101–150: اهتزاز ناعم
  }
  else if (distance > 50) {
    ledcWrite(motorPin, vibrationLight); // 51–100: اهتزاز خفيف
  }
  else if (distance > 15) {
    ledcWrite(motorPin, vibrationMedium);// 16–50: اهتزاز متوسط
  }
  else {
    ledcWrite(motorPin, vibrationStrong);// 15 وأقل: قوي
  }
}

// إنذار حاد مستمر عند 15 سم أو أقل
void updateAlarm(bool dangerDetected) {
  if (dangerDetected) {
    ledcWriteTone(buzzerPin, 3000);
  } else {
    ledcWriteTone(buzzerPin, 0);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(trigFront, OUTPUT);
  pinMode(echoFront, INPUT);

  pinMode(trigOuter, OUTPUT);
  pinMode(echoOuter, INPUT);

  pinMode(trigInner, OUTPUT);
  pinMode(echoInner, INPUT);

  pinMode(trigBack, OUTPUT);
  pinMode(echoBack, INPUT);

  pinMode(flexPin, INPUT);
  analogReadResolution(12);

  ledcAttach(motorFront, pwmFrequency, pwmResolution);
  ledcAttach(motorOuter, pwmFrequency, pwmResolution);
  ledcAttach(motorInner, pwmFrequency, pwmResolution);
  ledcAttach(motorBack, pwmFrequency, pwmResolution);
  ledcAttach(buzzerPin, 3000, pwmResolution);

  ledcWrite(motorFront, 0);
  ledcWrite(motorOuter, 0);
  ledcWrite(motorInner, 0);
  ledcWrite(motorBack, 0);
  ledcWriteTone(buzzerPin, 0);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(500);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW failed");
    return;
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peerInfo = {};

  #if IS_RIGHT_SHOE
    memcpy(peerInfo.peer_addr, leftShoeMAC, 6);
  #else
    memcpy(peerInfo.peer_addr, rightShoeMAC, 6);
  #endif

  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add ESP-NOW peer");
    return;
  }

  Serial.println(IS_RIGHT_SHOE ? "RIGHT shoe ready" : "LEFT shoe ready");
}

void loop() {
  int frontDistance = getDistance(trigFront, echoFront);
  delay(50);

  int outerDistance = getDistance(trigOuter, echoOuter);
  delay(50);

  int innerDistance = getDistance(trigInner, echoInner);
  delay(50);

  int backDistance = getDistance(trigBack, echoBack);
  delay(50);

  // التنبيه: الأمام والخارج والخلف
  controlMotor(motorFront, frontDistance);
  controlMotor(motorOuter, outerDistance);
  controlMotor(motorBack, backDistance);

  // الحساس الداخلي يقرأ فقط، ولا يصدر تنبيهًا
  ledcWrite(motorInner, 0);

  bool dangerDetected =
    (frontDistance > 0 && frontDistance <= dangerDistance) ||
    (outerDistance > 0 && outerDistance <= dangerDistance) ||
    (backDistance > 0 && backDistance <= dangerDistance);

  updateAlarm(dangerDetected);

  // إرسال قراءة الفلكس للجزمة الثانية
  myStatus.flexValue = analogRead(flexPin);
  myStatus.alive = 1;

  #if IS_RIGHT_SHOE
    esp_now_send(leftShoeMAC, (uint8_t *)&myStatus, sizeof(myStatus));
  #else
    esp_now_send(rightShoeMAC, (uint8_t *)&myStatus, sizeof(myStatus));
  #endif

  Serial.print(IS_RIGHT_SHOE ? "RIGHT | " : "LEFT | ");

  Serial.print("Front: ");
  Serial.print(frontDistance);

  Serial.print(" | Outer: ");
  Serial.print(outerDistance);

  Serial.print(" | Inner ignored: ");
  Serial.print(innerDistance);

  Serial.print(" | Back: ");
  Serial.print(backDistance);

  Serial.print(" | Flex: ");
  Serial.print(myStatus.flexValue);

  Serial.print(" | Other flex: ");
  Serial.println(otherStatus.flexValue);

  delay(200);
}