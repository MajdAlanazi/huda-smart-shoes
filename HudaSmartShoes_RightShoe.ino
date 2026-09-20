#include <WiFi.h>
#include <esp_now.h>

// HUDA SMART SHOES - RIGHT SHOE
// يمين 

// MAC Addresses

uint8_t rightShoeMAC[] = {
  0x58, 0x2A, 0xBD, 0x76, 0x8B, 0x80
};

uint8_t leftShoeMAC[] = {
  0x58, 0x2A, 0xBD, 0x78, 0x35, 0x74
};


// PIN CONFIGURATION
 
// Front
const int trigFront  = 22;
const int echoFront  = 21;
const int motorFront = 23;

// Outer - RIGHT SHOE
const int trigOuter  = 18;
const int echoOuter  = 5;
const int motorOuter = 19;

// Inner - READ ONLY
const int trigInner  = 27;
const int echoInner  = 26;
const int motorInner = 25;

// Back
const int trigBack  = 16;
const int echoBack  = 4;
const int motorBack = 17;

// Buzzer
const int buzzerPin = 33;

// Flex
const int flexPin = 34;


// DISTANCE SETTINGS

const int dangerDistance = 15;


// VIBRATION LEVELS

const int vibrationSoft   = 120;
const int vibrationLight  = 170;
const int vibrationMedium = 220;
const int vibrationStrong = 255;


// PWM SETTINGS

const int pwmFrequency  = 5000;
const int pwmResolution = 8;


// ESP-NOW DATA

typedef struct {

  int flexValue;
  uint8_t alive;

} ShoeStatus;

ShoeStatus myStatus;
ShoeStatus otherStatus;

unsigned long lastReceiveTime = 0;


// ESP-NOW RECEIVE CALLBACK

void onDataRecv(
  const esp_now_recv_info_t *info,
  const uint8_t *incomingData,
  int len
) {

  if (len == sizeof(ShoeStatus)) {

    memcpy(
      &otherStatus,
      incomingData,
      sizeof(otherStatus)
    );

    lastReceiveTime = millis();
  }
}

// ULTRASONIC DISTANCE

int getDistance(int trigPin, int echoPin) {

  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);

  digitalWrite(trigPin, LOW);

  long duration =
    pulseIn(echoPin, HIGH, 30000);

  if (duration == 0) {
    return 400;
  }

  int distance =
    (int)round(duration * 0.0343 / 2.0);

  if (distance <= 0 || distance > 400) {
    return 400;
  }

  return distance;
}


// VIBRATION CONTROL

void controlMotor(int motorPin, int distance) {

  // More than 150 cm
  if (distance <= 0 || distance > 150) {

    ledcWrite(motorPin, 0);
  }

  // 101 - 150 cm
  else if (distance > 100) {

    ledcWrite(
      motorPin,
      vibrationSoft
    );
  }

  // 51 - 100 cm
  else if (distance > 50) {

    ledcWrite(
      motorPin,
      vibrationLight
    );
  }

  // 16 - 50 cm
  else if (distance > 15) {

    ledcWrite(
      motorPin,
      vibrationMedium
    );
  }

  // 15 cm or less
  else {

    ledcWrite(
      motorPin,
      vibrationStrong
    );
  }
}

// BUZZER

void updateAlarm(bool dangerDetected) {

  static unsigned long lastToggle = 0;
  static bool buzzerOn = false;

  if (dangerDetected) {

    // Fast danger alarm
    if (millis() - lastToggle >= 120) {

      lastToggle = millis();
      buzzerOn = !buzzerOn;

      if (buzzerOn) {
        ledcWriteTone(buzzerPin, 4000);
      } else {
        ledcWriteTone(buzzerPin, 0);
      }
    }

  } else {

    buzzerOn = false;
    ledcWriteTone(buzzerPin, 0);
  }
}


void setup() {

  Serial.begin(115200);

  // Ultrasonic sensors
  pinMode(trigFront, OUTPUT);
  pinMode(echoFront, INPUT);

  pinMode(trigOuter, OUTPUT);
  pinMode(echoOuter, INPUT);

  pinMode(trigInner, OUTPUT);
  pinMode(echoInner, INPUT);

  pinMode(trigBack, OUTPUT);
  pinMode(echoBack, INPUT);

  // Flex
  pinMode(flexPin, INPUT);

  analogReadResolution(12);


  // Motors
  ledcAttach(
    motorFront,
    pwmFrequency,
    pwmResolution
  );

  ledcAttach(
    motorOuter,
    pwmFrequency,
    pwmResolution
  );

  ledcAttach(
    motorInner,
    pwmFrequency,
    pwmResolution
  );

  ledcAttach(
    motorBack,
    pwmFrequency,
    pwmResolution
  );


  // Buzzer
  ledcAttach(
    buzzerPin,
    3000,
    pwmResolution
  );


  // Start OFF
  ledcWrite(motorFront, 0);
  ledcWrite(motorOuter, 0);
  ledcWrite(motorInner, 0);
  ledcWrite(motorBack, 0);

  ledcWriteTone(
    buzzerPin,
    0
  );


  // ESP-NOW

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  delay(500);


  if (esp_now_init() != ESP_OK) {

    Serial.println(
      "ESP-NOW initialization failed"
    );

    return;
  }


  esp_now_register_recv_cb(
    onDataRecv
  );


  esp_now_peer_info_t peerInfo = {};

  memcpy(
    peerInfo.peer_addr,
    leftShoeMAC,
    6
  );

  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;


  if (
    esp_now_add_peer(&peerInfo)
    != ESP_OK
  ) {

    Serial.println(
      "Failed to add ESP-NOW peer"
    );

    return;
  }


  Serial.println(
    "Huda RIGHT shoe ready"
  );
}



void loop() {

  // Read distances

  int frontDistance =
    getDistance(
      trigFront,
      echoFront
    );

  delay(50);


  int outerDistance =
    getDistance(
      trigOuter,
      echoOuter
    );

  delay(50);


  int innerDistance =
    getDistance(
      trigInner,
      echoInner
    );

  delay(50);


  int backDistance =
    getDistance(
      trigBack,
      echoBack
    );

  delay(50);


  // Directional vibration

  controlMotor(
    motorFront,
    frontDistance
  );

  controlMotor(
    motorOuter,
    outerDistance
  );

  controlMotor(
    motorBack,
    backDistance
  );


  // Inner sensor = read only
  ledcWrite(
    motorInner,
    0
  );


  // Critical audio alert

  bool dangerDetected =

    (
      frontDistance > 0 &&
      frontDistance <= dangerDistance
    )

    ||

    (
      outerDistance > 0 &&
      outerDistance <= dangerDistance
    )

    ||

    (
      backDistance > 0 &&
      backDistance <= dangerDistance
    );


  updateAlarm(
    dangerDetected
  );


  // Flex

  myStatus.flexValue =
    analogRead(flexPin);

  myStatus.alive = 1;


  // Send data to LEFT shoe

  esp_now_send(
    leftShoeMAC,
    (uint8_t *)&myStatus,
    sizeof(myStatus)
  );


  // ESP Status

  bool espConnected =
    (
      lastReceiveTime > 0 &&
      millis() - lastReceiveTime < 3000
    );


  // Serial Monitor

  Serial.print(
    "RIGHT | Front: "
  );

  Serial.print(
    frontDistance
  );

  Serial.print(
    " cm | Outer: "
  );

  Serial.print(
    outerDistance
  );

  Serial.print(
    " cm | Inner: "
  );

  Serial.print(
    innerDistance
  );

  Serial.print(
    " cm | Back: "
  );

  Serial.print(
    backDistance
  );

  Serial.print(
    " cm | Flex: "
  );

  Serial.print(
    myStatus.flexValue
  );

  Serial.print(
    " | ESP: "
  );


  if (espConnected) {

    Serial.println(
      "OK"
    );

  } else {

    Serial.println(
      "WAITING"
    );
  }


  delay(200);
}