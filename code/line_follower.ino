/*
  LINE FOLLOWER BOT — Final Code
  ==============================
  Matches the Cirkit Designer circuit:

    ESP32 pin   ->  Connected to
    ---------       ------------------------------
    GPIO36 (VP) ->  IR sensor 1 AOUT  (far left)
    GPIO39 (VN) ->  IR sensor 2 AOUT
    GPIO34      ->  IR sensor 3 AOUT
    GPIO35      ->  IR sensor 4 AOUT
    GPIO32      ->  IR sensor 5 AOUT
    GPIO33      ->  IR sensor 6 AOUT  (far right)
    GPIO25      ->  TB6612 PWMA
    GPIO26      ->  TB6612 AI1
    GPIO27      ->  TB6612 AI2
    GPIO18      ->  TB6612 PWMB
    GPIO19      ->  TB6612 BI1
    GPIO21      ->  TB6612 BI2
    GPIO22      ->  TB6612 STBY
    3V3         ->  TB6612 VCC + all sensor VCC
    5V          ->  Buck converter output (5 V)
    GND         ->  Common ground
    GPIO0       ->  Onboard BOOT button (start/stop)
    GPIO2       ->  Onboard LED (status)

    TB6612 A01/A02 -> Left motor,  B01/B02 -> Right motor
    (If a motor spins backwards, swap its two wires.)

  Board: "ESP32 Dev Module", ESP32 Arduino core 3.x
*/

/*
  HOW TO USE
  ----------
  The bot is always in exactly one state (a simple state machine):

        [IDLE] --press--> [CALIBRATING] --done--> [READY] --press--> [RUNNING]
                                                     ^                   |
                                                     +------press--------+

    - LED shows the state:
        IDLE         -> slow blink
        CALIBRATING  -> solid on
        READY        -> fast blink
        RUNNING      -> off (on during sharp turns)

  How to use:
    1. Power on (IDLE, slow blink). Place the bot on the line.
    2. Press BOOT -> it calibrates (wiggles 3 s).
    3. Fast blink = READY. Press BOOT -> it runs.
    4. Press BOOT again -> it stops (back to READY). Press again to run.
*/

// ---------- Tuning ----------
float KP = 0.05;
float KD = 0.5;
int BASE_SPEED = 100;
int TURN_SPEED = 110;
int SHARP_THRESHOLD = 700;

// ---------- Pins ----------
const int NUM_SENSORS = 6;
const int SENSOR_PINS[NUM_SENSORS] = {36, 39, 34, 35, 32, 33};
const int PWMA = 25, AIN1 = 26, AIN2 = 27;   // left motor
const int PWMB = 18, BIN1 = 19, BIN2 = 21;   // right motor
const int STBY = 22;
const int LED  = 2;
const int BUTTON = 0;                         // BOOT button, reads LOW when pressed

// ---------- States ----------
enum State { IDLE, CALIBRATING, READY, RUNNING };
State state = IDLE;

int minVal[NUM_SENSORS], maxVal[NUM_SENSORS];
int value[NUM_SENSORS];
int lastError = 0;
int lastPosition = 2500;

// ---------- Motors ----------
void setMotor(int pwmPin, int in1, int in2, int speed) {
  speed = constrain(speed, -255, 255);
  digitalWrite(in1, speed >= 0 ? HIGH : LOW);
  digitalWrite(in2, speed >= 0 ? LOW : HIGH);
  ledcWrite(pwmPin, abs(speed));
}

void drive(int left, int right) {
  setMotor(PWMA, AIN1, AIN2, left);
  setMotor(PWMB, BIN1, BIN2, right);
}

// ---------- Button ----------
// Returns true once per press (waits for release, simple debounce).
bool buttonPressed() {
  if (digitalRead(BUTTON) == LOW) {
    delay(30);                                   // debounce
    if (digitalRead(BUTTON) == LOW) {
      drive(0, 0);                               // stop while the button is held
      while (digitalRead(BUTTON) == LOW) delay(5);
      delay(30);
      return true;
    }
  }
  return false;
}

// ---------- LED patterns ----------
void blinkLed(int periodMs) {
  digitalWrite(LED, (millis() / periodMs) % 2);
}

// ---------- Calibration (from Step 5) ----------
void calibrate() {
  for (int i = 0; i < NUM_SENSORS; i++) { minVal[i] = 4095; maxVal[i] = 0; }
  digitalWrite(LED, HIGH);
  unsigned long start = millis();
  while (millis() - start < 3000) {
    bool turnLeft = ((millis() - start) / 400) % 2;
    if (turnLeft) drive(-100, 100);
    else          drive(100, -100);
    for (int i = 0; i < NUM_SENSORS; i++) {
      int v = analogRead(SENSOR_PINS[i]);
      minVal[i] = min(minVal[i], v);
      maxVal[i] = max(maxVal[i], v);
    }
  }
  drive(0, 0);
  digitalWrite(LED, LOW);
  for (int i = 0; i < NUM_SENSORS; i++) {
    if (maxVal[i] - minVal[i] < 50) maxVal[i] = minVal[i] + 50;
  }
}

// ---------- Read line (from Step 6) ----------
int readLine() {
  long weightedSum = 0, total = 0;
  for (int i = 0; i < NUM_SENSORS; i++) {
    int v = map(analogRead(SENSOR_PINS[i]), minVal[i], maxVal[i], 0, 1000);
    v = constrain(v, 0, 1000);
    if (v < 100) v = 0;
    value[i] = v;
    weightedSum += (long)v * (i * 1000);
    total += v;
  }
  if (total < 300) return -1;
  return weightedSum / total;
}

// ---------- Sharp turn (from Step 6) ----------
void pivot(bool toLeft) {
  digitalWrite(LED, HIGH);
  if (toLeft) drive(-TURN_SPEED, TURN_SPEED);
  else        drive(TURN_SPEED, -TURN_SPEED);
  unsigned long start = millis();
  while (millis() - start < 1500) {
    readLine();
    if (value[2] > 500 || value[3] > 500) break;
  }
  digitalWrite(LED, LOW);
  lastError = 0;
}

// ---------- One step of line following (Steps 5 + 6) ----------
void followLine() {
  int position = readLine();

  bool centreSeesLine = (value[2] > 500 || value[3] > 500);
  if (!centreSeesLine && value[0] > SHARP_THRESHOLD) { pivot(true);  return; }
  if (!centreSeesLine && value[5] > SHARP_THRESHOLD) { pivot(false); return; }

  if (position == -1) position = (lastPosition < 2500) ? 0 : 5000;
  else lastPosition = position;

  int error = position - 2500;
  float correction = KP * error + KD * (error - lastError);
  lastError = error;

  drive(BASE_SPEED + correction, BASE_SPEED - correction);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT);
  pinMode(STBY, OUTPUT); digitalWrite(STBY, HIGH);
  ledcAttach(PWMA, 20000, 8);
  ledcAttach(PWMB, 20000, 8);
  analogReadResolution(12);
  Serial.println("IDLE - press BOOT to calibrate");
}

void loop() {
  switch (state) {
    case IDLE:
      blinkLed(500);
      if (buttonPressed()) state = CALIBRATING;
      break;

    case CALIBRATING:
      Serial.println("CALIBRATING");
      calibrate();
      Serial.println("READY - press BOOT to run");
      state = READY;
      break;

    case READY:
      drive(0, 0);
      blinkLed(100);
      if (buttonPressed()) {
        lastError = 0;
        digitalWrite(LED, LOW);
        Serial.println("RUNNING - press BOOT to stop");
        state = RUNNING;
      }
      break;

    case RUNNING:
      followLine();
      if (buttonPressed()) {
        drive(0, 0);
        Serial.println("READY - press BOOT to run");
        state = READY;
      }
      delay(2);
      break;
  }
}
