const int flexPin = 34;

// ==========================================
// MOVEMENT SETTINGS
// ==========================================

// حركة قوية تشبه انحناء الخطوة
const int STRONG_DIFF_THRESHOLD = 20;

// حركة متوسطة تستخدم فقط للمحافظة على WALKING
const int KEEP_WALKING_THRESHOLD = 10;

// نحتاج 3 حركات قوية
const int REQUIRED_EVENTS = 3;

// خلال 1.5 ثانية
const unsigned long EVENT_WINDOW_MS = 1500;

// إذا ما فيه حركة لمدة ثانيتين → STANDING
const unsigned long STANDING_TIMEOUT_MS = 2000;

// ==========================================

int previousFlex = -1;

int strongEventCount = 0;

unsigned long eventWindowStart = 0;
unsigned long lastMovementTime = 0;

bool walking = false;

// ==========================================
// MEDIAN FILTER
// ==========================================

int readFlexMedian() {

  const int N = 7;
  int values[N];

  for (int i = 0; i < N; i++) {
    values[i] = analogRead(flexPin);
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

// ==========================================
// SETUP
// ==========================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  previousFlex = readFlexMedian();

  eventWindowStart = millis();

  Serial.println();
  Serial.println("HUDA - RIGHT SHOE");
  Serial.println("WALKING / STANDING TEST V3");
}

// LOOP

void loop() {

  unsigned long now = millis();

  int flexValue = readFlexMedian();


  // Ignore clearly invalid values

  if (flexValue < 50) {

    Serial.print("Flex: ");
    Serial.print(flexValue);
    Serial.println(" | INVALID");

    delay(100);
    return;
  }

  // Calculate change

  int diff = abs(flexValue - previousFlex);

  previousFlex = flexValue;


  // Reset expired detection window

  if (
    strongEventCount > 0 &&
    now - eventWindowStart > EVENT_WINDOW_MS
  ) {

    strongEventCount = 0;
  }

  // Strong movement event

  if (diff >= STRONG_DIFF_THRESHOLD) {

    if (strongEventCount == 0) {
      eventWindowStart = now;
    }

    strongEventCount++;

    // لو إحنا أصلاً نمشي، هذه أكيد حركة
    if (walking) {
      lastMovementTime = now;
    }

    // 3 حركات قوية خلال النافذة
    if (
      strongEventCount >= REQUIRED_EVENTS &&
      now - eventWindowStart <= EVENT_WINDOW_MS
    ) {

      walking = true;

      lastMovementTime = now;

      strongEventCount = 0;
    }
  }

  // While already walking:
  // moderate flex movement keeps state alive

  if (
    walking &&
    diff >= KEEP_WALKING_THRESHOLD
  ) {

    lastMovementTime = now;
  }

  // No movement → STANDING

  if (
    walking &&
    now - lastMovementTime >= STANDING_TIMEOUT_MS
  ) {

    walking = false;

    strongEventCount = 0;
  }

  // SERIAL Print

  Serial.print("Flex: ");
  Serial.print(flexValue);

  Serial.print(" | Diff: ");
  Serial.print(diff);

  Serial.print(" | Events: ");
  Serial.print(strongEventCount);

  Serial.print(" | State: ");

  if (walking) {
    Serial.println("WALKING");
  } else {
    Serial.println("STANDING");
  }

  delay(100);
}