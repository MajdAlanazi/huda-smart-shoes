#include <WiFi.h>
#include <esp_now.h>
#include <esp_system.h>


// HUDA SMART SHOES - RIGHT SHOE  (يمين)
// V4 = Movement Logic V3 (unchanged)
//    + MOVING obstacle behavior (original tested code)
//    + STATIONARY obstacle decision (new, confirmed)

const char *SHOE_NAME = "RIGHT";

// Peer = LEFT SHOE MAC
uint8_t peerMAC[] = {
  0x58, 0x2A, 0xBD, 0x78, 0x35, 0x74
};

// PIN CONFIGURATION (from original tested code)


// Front
const int trigFront  = 22;
const int echoFront  = 21;
const int motorFront = 23;

// Outer
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
const int FLEX_PIN = 34;

// MOVEMENT SETTINGS - V3 (UNCHANGED)

const int MOVEMENT_THRESHOLD = 20;
const int MIN_VALID_FLEX = 5;

const unsigned long EVENT_COOLDOWN_MS = 150;

// تجاهل أول 3 ثواني بعد التشغيل
const unsigned long STARTUP_IGNORE_MS = 3000;

// للدخول إلى MOVING: نحتاج حركتين من كل قدم داخل هذه النافذة
const unsigned long ENTER_WINDOW_MS = 2500;
const int EVENTS_REQUIRED = 2;

// بعد الدخول إلى MOVING: 1.2 ثانية بدون أي حركة من القدمين = STATIONARY
const unsigned long MOVING_TIMEOUT_MS = 1500;

// حالة اتصال ESP-NOW
// 3000 ms (the value you specified): a short radio drop (feet / body
// blocking the signal for a moment) no longer shows NOT CONNECTED
// and no longer blocks WALKING detection.
const unsigned long ESP_TIMEOUT_MS = 3000;

// How often the V3 movement logic runs.
// The V3 loop was: flex median (~21 ms) + print + delay(100) = ~120 ms.
// Kept the same rate, so MOVEMENT_THRESHOLD behaves exactly as tested.
const unsigned long MOVEMENT_TASK_INTERVAL_MS = 120;

// OBSTACLE SETTINGS - ORIGINAL (UNCHANGED)

const int dangerDistance = 15;

const int vibrationSoft   = 120;
const int vibrationLight  = 170;
const int vibrationMedium = 220;
const int vibrationStrong = 255;

// PWM SETTINGS
const int pwmFrequency  = 5000;
const int pwmResolution = 8;

// Original sensor timing (same as the tested code)
const unsigned long SENSOR_GAP_MS  = 50;   // delay(50) after each sensor
const unsigned long CYCLE_DELAY_MS = 200;  // delay(200) at end of loop

// =====================================================
// VIBRATION RHYTHM (so the distance levels are easy to feel)
// Each level has its own pulse speed, like a parking sensor:
//   101-150 cm : slow pulses      (120 ms on, every 700 ms)
//    51-100 cm : medium pulses    (120 ms on, every 400 ms)
//    16- 50 cm : fast pulses      (120 ms on, every 220 ms)
//     <= 15 cm : continuous 255 + buzzer
// Set to false to go back to continuous vibration.
// =====================================================
const bool USE_PULSES = false;   // false = continuous PWM (original feel)
const unsigned long PULSE_ON_MS = 120;

// ===== STATIONARY LOGIC BEGIN =====

// =====================================================
// STATIONARY SETTINGS (NEW)
// =====================================================

const int ALERT_RANGE_CM   = 150;  // objects are tracked up to 150 cm
const int NOISE_CM         = 15;    // change of 5 cm or less = noise (stable)
const int JUMP_CM          = 30;   // change > 30 cm must be confirmed by the next reading
const int CONFIRM_MATCH_CM = 10;   // next reading within 10 cm of the jump = confirmed
const int APPROACH_CONFIRM = 2;    // 2 real decreases = APPROACHING
const int DANGER_CONFIRM   = 2;    // 2 consecutive readings <= 15 cm = DANGER
const unsigned long STATIONARY_DANGER_ALERT_MS = 1000; // one confirmed danger alert while stationary
const int DANGER_REARM_CM = 25; // re-arm only after obstacle moves farther than 25 cm
const unsigned long STOP_CONFIRM_MS = 800;  // object stopped for this long -> FIXED (~0.85 s with the 0.45 s cycle)
const int WOBBLE_CM        = 15;   // while APPROACHING: a step back of <= 15 cm = shake, not moving away

const unsigned long NEW_ALERT_MS = 1000;  // NEW object alert duration


// MOVING -> STATIONARY: keep the current alert for this long
const unsigned long STOP_HOLD_MS = 1000;

// Safety limit for a sensor still undecided after the hold
const unsigned long MAX_WAIT_AFTER_STOP_MS = 2000;

// =====================================================
// OBSTACLE TRACKER (one per sensor: Front, Outer, Back)
// =====================================================

enum ObstacleStatus {
  ST_CLEAR,        // nothing within 150 cm
  ST_FIXED,        // object present, not moving
  ST_NEW,          // new object confirmed
  ST_APPROACHING,  // object confirmed approaching
  ST_AWAY,         // object moving away
  ST_DIRECT        // MOVING mode: original direct behavior
};

struct SensorTracker {
  const char *name;

  int distance;        // latest raw reading
  int accepted;        // last trusted distance (-1 = not set yet)
  int candidate;       // big jump waiting for confirmation (-1 = none)

  int approachCount;   // real decreases counted
  int stableCount;     // stable readings counted
  int dangerCount;     // consecutive readings <= 15 cm

  ObstacleStatus status;
  unsigned long newAlertUntil;

  bool waitAfterStop;  // was vibrating when I stopped, not decided yet
  int heldPwm;         // vibration at the moment I stopped
  int lastFixed;       // distance of the last known FIXED object (-1 = none)

  int pwm;             // vibration decided this cycle
  bool danger;         // buzzer request
  bool alert;          // vibration active

  unsigned long stableSince;  // time the object stopped changing
  unsigned long lastReadTime; // time of the previous reading

  // STATIONARY danger one-shot control
  unsigned long dangerAlertUntil;
  bool dangerLatched;
};

const char *statusName(ObstacleStatus s) {
  switch (s) {
    case ST_CLEAR:       return "CLEAR";
    case ST_FIXED:       return "FIXED";
    case ST_NEW:         return "NEW";
    case ST_APPROACHING: return "APPROACHING";
    case ST_AWAY:        return "AWAY";
    case ST_DIRECT:      return "DIRECT";
  }
  return "?";
}

const char *displayStatus(const SensorTracker &t) {
  if (t.danger) {
    return "DANGER";
  }
  if (t.waitAfterStop) {
    return "HOLD";
  }
  return statusName(t.status);
}

// Same thresholds and PWM values as the original controlMotor()
int pwmForDistance(int distance) {

  // More than 150 cm
  if (distance <= 0 || distance > 150) {
    return 0;
  }

  // 101 - 150 cm
  else if (distance > 100) {
    return vibrationSoft;
  }

  // 51 - 100 cm
  else if (distance > 50) {
    return vibrationLight;
  }

  // 16 - 50 cm
  else if (distance > 15) {
    return vibrationMedium;
  }

  // 15 cm or less
  else {
    return vibrationStrong;
  }
}

ObstacleStatus restStatus(int distance) {
  return (distance > ALERT_RANGE_CM) ? ST_CLEAR : ST_FIXED;
}

// Start fresh from a reading (startup, or the moment I stop walking)
void seedTracker(SensorTracker &t, int distance) {
  t.accepted = distance;
  t.candidate = -1;
  t.approachCount = 0;
  t.stableCount = 0;
  t.newAlertUntil = 0;
  t.status = restStatus(distance);

  if (t.status == ST_FIXED) {
    t.lastFixed = distance;
  }
}

// Runs every cycle in BOTH states, so a danger that was
// already confirmed while walking is ready right after stopping.
void updateDangerCount(SensorTracker &t) {
  if (t.distance > 0 && t.distance <= dangerDistance) {
    if (t.dangerCount < 100) {
      t.dangerCount++;
    }
  } else {
    t.dangerCount = 0;
  }
}

// STATIONARY: classify the newest reading
void stationaryUpdate(SensorTracker &t, unsigned long now) {

  int d = t.distance;

  unsigned long prevReadTime = t.lastReadTime;
  t.lastReadTime = now;

  if (t.accepted < 0) {
    seedTracker(t, d);
    return;
  }

  int delta = d - t.accepted;

  // ---------------------------------------------
  // ALREADY APPROACHING and still coming closer:
  // follow the current distance directly (no lag),
  // even if it moved more than 30 cm since last reading
  // ---------------------------------------------
  if (t.status == ST_APPROACHING && delta < -NOISE_CM) {
    t.accepted = d;
    t.candidate = -1;
    t.stableCount = 0;
    if (t.approachCount < 100) {
      t.approachCount++;
    }
    return;
  }

  // ---------------------------------------------
  // BIG JUMP (> 30 cm): never trust one reading
  // ---------------------------------------------
  if (abs(delta) > JUMP_CM) {

    // Confirmed if the next reading agrees with the jump...
    bool confirmed =
      (t.candidate >= 0) &&
      (abs(d - t.candidate) <= CONFIRM_MATCH_CM);

    // ...or keeps coming closer (fast approach, e.g. a person walking
    // toward me covers ~50 cm between two readings)
    bool keepsComing =
      (t.candidate >= 0) &&
      (t.candidate < t.accepted) &&
      (d < t.candidate - NOISE_CM);

    if (!confirmed && !keepsComing) {
      // First jump: wait for the next reading (spike protection)
      t.candidate = d;
      return;
    }

    if (keepsComing) {
      // Two real decreases in a row = APPROACHING
      t.accepted = d;
      t.candidate = -1;
      t.stableCount = 0;
      t.approachCount = APPROACH_CONFIRM;
      t.status = ST_APPROACHING;
      t.waitAfterStop = false;
      return;
    }

    // Confirmed: two readings agree on the new distance
    t.accepted = d;
    t.candidate = -1;
    t.approachCount = 0;
    t.stableCount = 0;
    t.waitAfterStop = false;

    if (delta < 0 && d <= ALERT_RANGE_CM &&
        t.lastFixed >= 0 &&
        abs(d - t.lastFixed) <= CONFIRM_MATCH_CM) {
      // Same fixed object as before (its echo was lost for a moment,
      // e.g. an angled surface): NOT a new object, no alert
      t.status = ST_FIXED;
    }
    else if (delta < 0 && d <= ALERT_RANGE_CM) {
      // Something appeared, closer than before
      t.status = ST_NEW;
      t.newAlertUntil = now + NEW_ALERT_MS;
    } else if (delta < 0) {
      t.status = restStatus(d);
    } else {
      // Object left / moved far away
      t.status = ST_AWAY;
    }

    return;
  }

  // Not a big jump: any pending spike is discarded
  t.candidate = -1;

  // ---------------------------------------------
  // DECREASE (more than 5 cm closer)
  // ---------------------------------------------
  if (delta < -NOISE_CM) {
    t.accepted = d;
    t.stableCount = 0;
    t.approachCount++;

    if (t.approachCount >= APPROACH_CONFIRM) {
      t.status = ST_APPROACHING;
      t.waitAfterStop = false;
    }
    // 1 decrease only: still undecided, keep waiting
  }

  // ---------------------------------------------
  // INCREASE (more than 5 cm farther)
  // While APPROACHING, a small step back (<= 15 cm) is only
  // a shake (hand / body movement): treated like stable below.
  // ---------------------------------------------
  else if (delta > NOISE_CM &&
           !(t.status == ST_APPROACHING && delta <= WOBBLE_CM)) {
    t.accepted = d;
    t.stableCount = 0;
    t.approachCount = 0;
    t.status = ST_AWAY;
    t.waitAfterStop = false;
  }

  // ---------------------------------------------
  // STABLE (5 cm or less) = noise.
  // "accepted" is NOT updated, so a slow approach
  // (e.g. 3 cm per reading) still adds up to a
  // real decrease after a couple of readings.
  // ---------------------------------------------
  else {
    if (t.stableCount == 0) {
      // the object stopped changing at the previous reading
      t.stableSince = (prevReadTime > 0) ? prevReadTime : now;
    }
    if (t.stableCount < 1000) {
      t.stableCount++;
    }
    t.waitAfterStop = false;

    if (now - t.stableSince >= STOP_CONFIRM_MS) {
      t.approachCount = 0;

      if (t.status != ST_NEW) {
        t.status = restStatus(t.accepted);

        if (t.status == ST_FIXED) {
          t.lastFixed = t.accepted;
        }
      }
    }
  }
}

// STATIONARY: decide the vibration for this sensor
void stationaryDecision(SensorTracker &t, unsigned long now, unsigned long stopTime) {

  // NEW alert ends after NEW_ALERT_MS
  if (t.status == ST_NEW && (long)(now - t.newAlertUntil) >= 0) {
    t.status = restStatus(t.accepted);

    if (t.status == ST_FIXED) {
      t.lastFixed = t.accepted;
    }
  }

  // Safety: never wait forever after stopping
  if (t.waitAfterStop && now - stopTime >= MAX_WAIT_AFTER_STOP_MS) {
    t.waitAfterStop = false;
  }

  t.pwm = 0;
  t.danger = false;

  // Re-arm a stationary danger alert only after the obstacle moves away.
  if (t.distance > DANGER_REARM_CM) {
    t.dangerLatched = false;
  }

  // While STATIONARY, a confirmed <=15 cm danger alerts once for 1 second.
  // Keeping the same fixed object close does not keep the buzzer/vibration on forever.
  if (!t.dangerLatched && t.dangerCount >= DANGER_CONFIRM) {
    t.dangerAlertUntil = now + STATIONARY_DANGER_ALERT_MS;
    t.dangerLatched = true;
  }

  if ((long)(t.dangerAlertUntil - now) > 0) {
    t.pwm = vibrationStrong;
    t.danger = true;
  }
  else if (t.status == ST_APPROACHING || t.status == ST_NEW) {
    // Same vibration levels, by the current distance.
    // 255 is kept for CONFIRMED danger only.
    t.pwm = pwmForDistance(t.accepted);

    if (t.pwm > vibrationMedium) {
      t.pwm = vibrationMedium;
    }
  }

  // Was vibrating when I stopped and not decided yet:
  // keep the same vibration until the next reading decides
  if (t.waitAfterStop && !t.danger && t.heldPwm > t.pwm) {
    t.pwm = t.heldPwm;
  }

  t.alert = (t.pwm > 0);
}

// ===== STATIONARY LOGIC END =====

// MOVING: original behavior, directly from the distance
void movingDecision(SensorTracker &t) {
  t.status = ST_DIRECT;
  t.waitAfterStop = false;
  t.pwm = pwmForDistance(t.distance);
  t.danger = (t.distance > 0 && t.distance <= dangerDistance);
  t.alert = (t.pwm > 0);
}

// =====================================================
// ESP-NOW PACKET (V3)
// =====================================================

struct MotionPacket {
  uint32_t eventCounter;
  int flexValue;
  uint8_t alive;
};

MotionPacket myPacket;

// =====================================================
// LOCAL FLEX VARIABLES (V3)
// =====================================================

int previousFlex = -1;
int currentDiff = 0;

uint32_t localEventCounter = 0;

unsigned long lastLocalEventTime = 0;
unsigned long lastLocalEventTrigger = 0;

// =====================================================
// REMOTE SHOE VARIABLES (V3)
// =====================================================

volatile uint32_t lastRemoteCounter = 0;
volatile unsigned long lastReceiveTime = 0;
volatile uint32_t remoteEventSignalCounter = 0;
volatile bool remoteCounterInitialized = false;

uint32_t processedRemoteEventSignalCounter = 0;

// =====================================================
// MOVEMENT STATE (V3)
// =====================================================

bool moving = false;

unsigned long startupTime = 0;
unsigned long lastAnyMovementTime = 0;

unsigned long entryWindowStart = 0;
int localEntryCount = 0;
int remoteEntryCount = 0;

// For debug printing
int lastFlexValue = 0;
bool espOK = false;
uint32_t localEventsSincePrint = 0;
uint32_t remoteEventsSincePrint = 0;

unsigned long nextMovementTaskTime = 0;

// =====================================================
// OBSTACLE STATE
// =====================================================

SensorTracker frontT = { "Front", 400, -1, -1, 0, 0, 0, ST_CLEAR, 0, false, 0, -1, 0, false, false, 0, 0 };
SensorTracker outerT = { "Outer", 400, -1, -1, 0, 0, 0, ST_CLEAR, 0, false, 0, -1, 0, false, false, 0, 0 };
SensorTracker backT  = { "Back",  400, -1, -1, 0, 0, 0, ST_CLEAR, 0, false, 0, -1, 0, false, false, 0, 0 };

int innerDistance = 400;

enum SensorStep {
  STEP_FRONT,
  STEP_OUTER,
  STEP_INNER,
  STEP_BACK,
  STEP_PROCESS
};

SensorStep sensorStep = STEP_FRONT;
unsigned long nextSensorActionTime = 0;

bool lastMovingState = false;

// Walking mode used by the OBSTACLE logic = the validated V3 state only
bool walkMode = false;

bool holding = false;
unsigned long holdUntil = 0;
unsigned long stopTime = 0;
bool heldDanger = false;

// =====================================================
// MEDIAN FILTER (V3)
// =====================================================

int readFlexMedian() {
  const int N = 7;
  int values[N];

  for (int i = 0; i < N; i++) {
    values[i] = analogRead(FLEX_PIN);
    delay(3);
  }

  for (int i = 0; i < N - 1; i++) {
    for (int j = i + 1; j < N; j++) {
      if (values[j] < values[i]) {
        int temp = values[i];
        values[i] = values[j];
        values[j] = temp;
      }
    }
  }

  return values[N / 2];
}

// =====================================================
// ESP-NOW RECEIVE (V3)
// =====================================================

void onDataRecv(
  const esp_now_recv_info_t *info,
  const uint8_t *incomingData,
  int len
) {
  if (len != sizeof(MotionPacket)) {
    return;
  }

  MotionPacket receivedPacket;

  memcpy(
    &receivedPacket,
    incomingData,
    sizeof(receivedPacket)
  );

  lastReceiveTime = millis();

  // أول Packet فقط للمزامنة، حتى لا نحسب Event وهمي
  if (!remoteCounterInitialized) {
    lastRemoteCounter = receivedPacket.eventCounter;
    remoteCounterInitialized = true;
    return;
  }

  // The other shoe rebooted (its counter restarted from 0):
  // resynchronize, do NOT count this as events
  // (unsigned subtraction would give a huge fake number)
  if (receivedPacket.eventCounter < lastRemoteCounter) {
    lastRemoteCounter = receivedPacket.eventCounter;
    return;
  }

  // إذا تغيّر العداد، فالجزمة الثانية سجلت Event جديد
  if (receivedPacket.eventCounter != lastRemoteCounter) {
    uint32_t difference =
      receivedPacket.eventCounter - lastRemoteCounter;

    lastRemoteCounter = receivedPacket.eventCounter;

    // نحفظ عدد الأحداث، وليس فقط YES/NO
    remoteEventSignalCounter += difference;
  }
}

// =====================================================
// RESET ENTRY CONFIRMATION (V3)
// =====================================================

void resetEntryConfirmation() {
  entryWindowStart = 0;
  localEntryCount = 0;
  remoteEntryCount = 0;
}

// =====================================================
// MOVEMENT TASK - V3 LOGIC (UNCHANGED)
// Same body as the V3 loop; only Serial + delay(100)
// moved out (the scheduler in loop() keeps the ~120 ms rate).
// =====================================================

void movementTask() {
  unsigned long now = millis();

  bool startupComplete =
    (now - startupTime >= STARTUP_IGNORE_MS);

  // 1. READ FLEX

  int flexValue = readFlexMedian();
  lastFlexValue = flexValue;

  bool localEvent = false;

  if (flexValue > MIN_VALID_FLEX) {

    if (previousFlex < 0) {
      previousFlex = flexValue;
      currentDiff = 0;
    } else {
      currentDiff = abs(flexValue - previousFlex);
      previousFlex = flexValue;
    }

    // أثناء أول 3 ثواني لا ننشئ Events
    if (
      startupComplete &&
      currentDiff >= MOVEMENT_THRESHOLD &&
      now - lastLocalEventTrigger >= EVENT_COOLDOWN_MS
    ) {
      localEventCounter++;
      lastLocalEventTime = now;
      lastLocalEventTrigger = now;
      localEvent = true;
    }

  } else {
    currentDiff = 0;
  }

  // 2. SEND TO OTHER SHOE

  myPacket.eventCounter = localEventCounter;
  myPacket.flexValue = flexValue;
  myPacket.alive = 1;

  esp_now_send(
    peerMAC,
    reinterpret_cast<uint8_t *>(&myPacket),
    sizeof(myPacket)
  );

  // 3. READ REMOTE EVENTS

  uint32_t remoteSignalSnapshot =
    remoteEventSignalCounter;

  uint32_t remoteEventsThisLoop = 0;

  if (!startupComplete) {
    // تجاهل أي أحداث وصلت خلال أول 3 ثواني
    processedRemoteEventSignalCounter =
      remoteSignalSnapshot;
  } else {
    remoteEventsThisLoop =
      remoteSignalSnapshot -
      processedRemoteEventSignalCounter;

    processedRemoteEventSignalCounter =
      remoteSignalSnapshot;
  }

  bool remoteEvent =
    (remoteEventsThisLoop > 0);

  // For debug printing
  if (localEvent) {
    localEventsSincePrint++;
  }
  remoteEventsSincePrint += remoteEventsThisLoop;

  // 4. ESP STATUS

  unsigned long receiveSnapshot =
    lastReceiveTime;

  espOK =
    (
      receiveSnapshot > 0 &&
      now - receiveSnapshot <= ESP_TIMEOUT_MS
    );

  // 5. MOVEMENT LOGIC V3

  // STARTUP IGNORE
  if (!startupComplete) {
    moving = false;
    resetEntryConfirmation();
  }

  // ALREADY MOVING
  else if (moving) {

    // أي Event من أي قدم يجدد MOVING
    if (localEvent || remoteEvent) {
      lastAnyMovementTime = now;
    }

    // لا يوجد أي Event من القدمين لمدة MOVING_TIMEOUT_MS
    if (
      now - lastAnyMovementTime >= MOVING_TIMEOUT_MS
    ) {
      moving = false;
      resetEntryConfirmation();
    }
  }

  // CURRENTLY STATIONARY
  else {

    bool anyEvent =
      localEvent || remoteEvent;

    // إذا انتهت نافذة الدخول، نمسح العدادات
    if (
      entryWindowStart > 0 &&
      now - entryWindowStart > ENTER_WINDOW_MS
    ) {
      resetEntryConfirmation();
    }

    // أول Event يبدأ نافذة 2.5 ثانية
    if (
      anyEvent &&
      entryWindowStart == 0
    ) {
      entryWindowStart = now;
    }

    // نحسب Events داخل نافذة الدخول فقط
    if (entryWindowStart > 0) {

      if (localEvent) {
        localEntryCount++;
      }

      if (remoteEventsThisLoop > 0) {
        remoteEntryCount += remoteEventsThisLoop;
      }

      // نحتاج حركتين من كل قدم
      if (
        espOK &&
        localEntryCount >= EVENTS_REQUIRED &&
        remoteEntryCount >= EVENTS_REQUIRED
      ) {
        moving = true;
        lastAnyMovementTime = now;
        resetEntryConfirmation();
      }
    }
  }
}

// =====================================================
// ULTRASONIC DISTANCE (ORIGINAL - UNCHANGED)
// =====================================================

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

// =====================================================
// BUZZER (ORIGINAL - UNCHANGED)
// Called once per sensor cycle, like the original loop,
// so the beep pattern sounds the same.
// =====================================================

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

// =====================================================
// OUTPUTS
// =====================================================

// Target vibration for each motor (Front, Outer, Back)
int targetPwm[3] = { 0, 0, 0 };
int writtenPwm[3] = { -1, -1, -1 };

unsigned long pulsePeriod(int pwm) {
  if (pwm >= vibrationStrong) return 0;    // continuous
  if (pwm >= vibrationMedium) return 220;  // fast
  if (pwm >= vibrationLight)  return 400;  // medium
  return 700;                              // slow
}

// Called all the time from loop(): turns each motor on/off
// following the rhythm of its level
void applyPulses() {

  const int pins[3] = { motorFront, motorOuter, motorBack };
  unsigned long now = millis();

  for (int i = 0; i < 3; i++) {

    int p = targetPwm[i];
    int out;

    if (p <= 0) {
      out = 0;
    } else if (!USE_PULSES || pulsePeriod(p) == 0) {
      out = p;
    } else {
      out = ((now % pulsePeriod(p)) < PULSE_ON_MS) ? p : 0;
    }

    if (out != writtenPwm[i]) {
      ledcWrite(pins[i], out);
      writtenPwm[i] = out;
    }
  }
}

void writeOutputs(int pwmFront, int pwmOuter, int pwmBack, bool danger) {

  targetPwm[0] = pwmFront;
  targetPwm[1] = pwmOuter;
  targetPwm[2] = pwmBack;

  applyPulses();

  // Inner sensor = read only
  ledcWrite(motorInner, 0);

  updateAlarm(danger);
}

bool anyTrackerDanger() {
  return frontT.danger || outerT.danger || backT.danger;
}

void writeTrackerOutputs() {
  writeOutputs(frontT.pwm, outerT.pwm, backT.pwm, anyTrackerDanger());
}

void writeHeldOutputs() {
  writeOutputs(frontT.heldPwm, outerT.heldPwm, backT.heldPwm, heldDanger);
}

// =====================================================
// STATE CHANGES
// =====================================================

// MOVING -> STATIONARY: keep the current alert for STOP_HOLD_MS,
// and start the stationary tracking from the current readings.
void onStopWalking(unsigned long now) {

  SensorTracker *all[3] = { &frontT, &outerT, &backT };

  heldDanger = anyTrackerDanger();

  for (int i = 0; i < 3; i++) {
    SensorTracker &t = *all[i];
    t.heldPwm = t.pwm;                 // vibration at the moment I stopped
    t.waitAfterStop = (t.pwm > 0);     // decide only after new readings
    seedTracker(t, t.distance);
  }

  holding = true;
  stopTime = now;
  holdUntil = now + STOP_HOLD_MS;

  // Prepare the decision used when the hold ends
  for (int i = 0; i < 3; i++) {
    stationaryDecision(*all[i], now, stopTime);
  }
}

// STATIONARY -> MOVING: original behavior right away
void onStartWalking() {

  holding = false;

  movingDecision(frontT);
  movingDecision(outerT);
  movingDecision(backT);

  writeTrackerOutputs();
}

// =====================================================
// SERIAL DEBUG
// =====================================================

void printTracker(const SensorTracker &t) {

  Serial.printf(
    "%-5s: %3d cm | Acc: %3d | Status: %-11s | Confirm A/S/D: %d/%d/%d | Cand: ",
    t.name,
    t.distance,
    t.accepted,
    displayStatus(t),
    t.approachCount,
    t.stableCount,
    t.dangerCount
  );

  if (t.candidate >= 0) {
    Serial.printf("%3d", t.candidate);
  } else {
    Serial.print("  -");
  }

  Serial.printf(
    " | Alert: %-3s (PWM %d)\n",
    t.alert ? "YES" : "NO",
    t.pwm
  );
}

// Simple one-line output (set to true for the detailed debug view)
const bool DETAILED_DEBUG = true;

// How long since the last packet from the other shoe (only shown when NOT CONNECTED)
const char *lastPacketAgeText() {
  static char buf[32];
  unsigned long last = lastReceiveTime;
  if (espOK) {
    buf[0] = 0;
  } else if (last == 0) {
    snprintf(buf, sizeof(buf), " (never received)");
  } else {
    snprintf(buf, sizeof(buf), " (last %lu ms ago)", millis() - last);
  }
  return buf;
}

// Why did this board (re)start? BROWNOUT = power dropped (battery / motors)
const char *resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "POWER ON (normal)";
    case ESP_RST_SW:       return "SOFTWARE / UPLOAD (normal)";
    case ESP_RST_BROWNOUT: return "BROWNOUT !! power dropped - check battery / wiring";
    case ESP_RST_PANIC:    return "CRASH !!";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "WATCHDOG !!";
    default:               return "OTHER";
  }
}

void printSimple(bool buzzerActive) {

  // Steps detected since the last line (this shoe = local, other shoe = remote)
  bool isLeftShoe = (strcmp(SHOE_NAME, "LEFT") == 0);
  bool leftStep  = isLeftShoe ? (localEventsSincePrint > 0) : (remoteEventsSincePrint > 0);
  bool rightStep = isLeftShoe ? (remoteEventsSincePrint > 0) : (localEventsSincePrint > 0);

  localEventsSincePrint = 0;
  remoteEventsSincePrint = 0;

  Serial.printf(
    "%s | %s | ESP: %s%s | Flex: %d  Diff: %d | Front: %d  Outer: %d  Inner: %d  Back: %d cm | Step L: %s  R: %s\n",
    SHOE_NAME,
    walkMode ? "WALKING " : "STANDING",
    espOK ? "OK" : "NOT CONNECTED",
    lastPacketAgeText(),
    lastFlexValue,
    currentDiff,
    frontT.distance,
    outerT.distance,
    innerDistance,
    backT.distance,
    leftStep ? "YES" : "NO ",
    rightStep ? "YES" : "NO "
  );
}

void printDebug(bool buzzerActive) {

  if (!DETAILED_DEBUG) {
    printSimple(buzzerActive);
    return;
  }

  Serial.println();
  Serial.printf("===== %s | State: %s", SHOE_NAME, moving ? "MOVING" : "STATIONARY");

  if (holding) {
    long remaining = (long)(holdUntil - millis());
    if (remaining < 0) remaining = 0;
    Serial.printf(" (STOP HOLD %ld ms)", remaining);
  }

  Serial.println(" =====");

  Serial.printf(
    "ESP: %s%s | Local Event: %s (%lu) | Remote Event: %s (%lu) | Flex: %d | Diff: %d | Entry L/R: %d/%d\n",
    espOK ? "OK" : "WAITING",
    lastPacketAgeText(),
    localEventsSincePrint > 0 ? "YES" : "NO",
    (unsigned long)localEventsSincePrint,
    remoteEventsSincePrint > 0 ? "YES" : "NO",
    (unsigned long)remoteEventsSincePrint,
    lastFlexValue,
    currentDiff,
    localEntryCount,
    remoteEntryCount
  );

  localEventsSincePrint = 0;
  remoteEventsSincePrint = 0;

  printTracker(frontT);
  printTracker(outerT);
  printTracker(backT);

  Serial.printf("Inner: %3d cm (read only)\n", innerDistance);
  Serial.printf("Buzzer: %s\n", buzzerActive ? "ON" : "OFF");
}

// =====================================================
// OBSTACLE CYCLE (after Back + gap, like the original)
// =====================================================

void processObstacleCycle() {

  unsigned long now = millis();

  updateDangerCount(frontT);
  updateDangerCount(outerT);
  updateDangerCount(backT);

  bool buzzerActive;

  if (walkMode) {

    // ---------- MOVING: original behavior ----------
    movingDecision(frontT);
    movingDecision(outerT);
    movingDecision(backT);

    writeTrackerOutputs();
    buzzerActive = anyTrackerDanger();

  } else {

    // ---------- STATIONARY ----------
    stationaryUpdate(frontT, now);
    stationaryUpdate(outerT, now);
    stationaryUpdate(backT,  now);

    stationaryDecision(frontT, now, stopTime);
    stationaryDecision(outerT, now, stopTime);
    stationaryDecision(backT,  now, stopTime);

    if (holding) {
      // Still inside the stop hold: keep the alert from walking
      writeHeldOutputs();
      buzzerActive = heldDanger;
    } else {
      writeTrackerOutputs();
      buzzerActive = anyTrackerDanger();
    }
  }

  printDebug(buzzerActive);
}

// =====================================================
// SENSOR TASK
// Same order as the original:
// Front, gap, Outer, gap, Inner, gap, Back, gap, process, pause.
// (Non-blocking, so the flex keeps its tested ~120 ms rate.)
// =====================================================

void sensorTask() {

  unsigned long now = millis();

  if ((long)(now - nextSensorActionTime) < 0) {
    return;
  }

  switch (sensorStep) {

    case STEP_FRONT:
      frontT.distance = getDistance(trigFront, echoFront);
      sensorStep = STEP_OUTER;
      nextSensorActionTime = millis() + SENSOR_GAP_MS;
      break;

    case STEP_OUTER:
      outerT.distance = getDistance(trigOuter, echoOuter);
      sensorStep = STEP_INNER;
      nextSensorActionTime = millis() + SENSOR_GAP_MS;
      break;

    case STEP_INNER:
      innerDistance = getDistance(trigInner, echoInner);
      sensorStep = STEP_BACK;
      nextSensorActionTime = millis() + SENSOR_GAP_MS;
      break;

    case STEP_BACK:
      backT.distance = getDistance(trigBack, echoBack);
      sensorStep = STEP_PROCESS;
      nextSensorActionTime = millis() + SENSOR_GAP_MS;
      break;

    case STEP_PROCESS:
      processObstacleCycle();
      sensorStep = STEP_FRONT;
      nextSensorActionTime = millis() + CYCLE_DELAY_MS;
      break;
  }
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  // Bigger TX buffer so the debug print does not slow the loop
  Serial.setTxBufferSize(1024);
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
  pinMode(FLEX_PIN, INPUT);
  analogReadResolution(12);

  // Motors
  ledcAttach(motorFront, pwmFrequency, pwmResolution);
  ledcAttach(motorOuter, pwmFrequency, pwmResolution);
  ledcAttach(motorInner, pwmFrequency, pwmResolution);
  ledcAttach(motorBack,  pwmFrequency, pwmResolution);

  // Buzzer
  ledcAttach(buzzerPin, 3000, pwmResolution);

  // Start OFF
  ledcWrite(motorFront, 0);
  ledcWrite(motorOuter, 0);
  ledcWrite(motorInner, 0);
  ledcWrite(motorBack,  0);
  ledcWriteTone(buzzerPin, 0);

  delay(1000);

  previousFlex = readFlexMedian();

  // ESP-NOW (V3)

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  delay(500);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW INIT FAILED");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peerInfo = {};

  memcpy(
    peerInfo.peer_addr,
    peerMAC,
    6
  );

  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("FAILED TO ADD PEER SHOE");
  }

  // يبدأ تجاهل أول 3 ثواني من هنا
  startupTime = millis();

  nextMovementTaskTime = millis();
  nextSensorActionTime = millis();

  Serial.println();
  Serial.println("==============================");
  Serial.printf("HUDA %s SHOE\n", SHOE_NAME);
  Serial.println("V4: MOVEMENT V3 + OBSTACLE LOGIC");
  Serial.printf("Start reason: %s\n", resetReasonText());
  Serial.println("==============================");
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  unsigned long now = millis();

  // 1. Movement logic V3 at its tested rate
  if ((long)(now - nextMovementTaskTime) >= 0) {
    nextMovementTaskTime = now + MOVEMENT_TASK_INTERVAL_MS;
    movementTask();
  }

  // 2. React to WALKING / STANDING changes immediately
  walkMode = moving;

  if (walkMode != lastMovingState) {

    if (walkMode) {
      onStartWalking();
    } else {
      onStopWalking(millis());
    }

    lastMovingState = walkMode;
  }

  // 3. End of the stop hold: apply the stationary decision
  if (holding && (long)(millis() - holdUntil) >= 0) {
    holding = false;
    writeTrackerOutputs();
  }

  // 4. Ultrasonic sensors, original order
  sensorTask();

  // 5. Vibration rhythm
  applyPulses();
}
