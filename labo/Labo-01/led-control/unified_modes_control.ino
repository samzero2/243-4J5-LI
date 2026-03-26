// Controle unifie LEDs + Boutons + Potentiometre + Accelerometre
// Modes disponibles:
// - ACCEL: LEDs selon inclinaison (accelerometre I2C)
// - POT:   bargraphe selon potentiometre
// - BOUTON: bouton MODE change pattern, bouton ACTION pause/reprise
//
// Cablage:
// LEDs: BLEU=19, VERT=23, JAUNE=21, ROUGE=22
// Bouton MODE=18 (INPUT_PULLUP, appui LOW)
// Bouton ACTION=39 (INPUT only, pull-up externe requis)
// POT=36 (ADC)
// I2C accel: SDA=33, SCL=32
//
// Commandes serie:
// MODE ACCEL | MODE POT | MODE BOUTON | MODE STATUS
// BTN_MODE | BTN_ACTION | STATUS

#include <Wire.h>
#include <math.h>
#include <string.h>

// -------- Pins --------
constexpr int LED_BLUE_PIN = 19;
constexpr int LED_GREEN_PIN = 23;
constexpr int LED_YELLOW_PIN = 21;
constexpr int LED_RED_PIN = 22;
constexpr int LED_PINS[] = {LED_BLUE_PIN, LED_GREEN_PIN, LED_YELLOW_PIN, LED_RED_PIN};
constexpr int LED_COUNT = sizeof(LED_PINS) / sizeof(LED_PINS[0]);

constexpr int BUTTON_MODE_PIN = 18;
constexpr int BUTTON_ACTION_PIN = 39;
constexpr int POT_PIN = 36;

constexpr uint8_t I2C_SDA_PIN = 33;
constexpr uint8_t I2C_SCL_PIN = 32;
constexpr uint32_t I2C_CLOCK_HZ = 100000;

// -------- Timing --------
constexpr unsigned long DEBOUNCE_MS = 40;
constexpr unsigned long SAMPLE_PERIOD_MS = 80;
constexpr unsigned long MIN_STEP_MS = 200;
constexpr unsigned long MAX_STEP_MS = 1200;
constexpr float TILT_DEADZONE_G = 0.20f;

// -------- Sensor --------
enum SensorType : uint8_t {
  SENSOR_NONE = 0,
  SENSOR_MPU6050,
  SENSOR_ADXL345,
  SENSOR_LIS3DH,
};

SensorType detectedSensor = SENSOR_NONE;
uint8_t sensorAddress = 0x00;
const uint8_t KNOWN_ADDRS[] = {0x68, 0x69, 0x53, 0x18, 0x19};

// -------- Mode control --------
enum ControlMode : uint8_t {
  MODE_ACCEL = 0,
  MODE_POT,
  MODE_BOUTON
};

ControlMode controlMode = MODE_ACCEL;
bool runAnimation = true;
int potRaw = 0;
float ax = 0.0f, ay = 0.0f, az = 0.0f;
unsigned long stepIntervalMs = 500;
unsigned long lastStepMs = 0;
unsigned long lastSampleMs = 0;
unsigned long lastLogMs = 0;

// Mode BOUTON patterns
int buttonPattern = 0;
int chaserIndex = 0;
bool pairToggle = false;
bool allToggle = false;

// Buttons debounce
int modeButtonLastRead = HIGH;
int actionButtonLastRead = HIGH;
int modeButtonStable = HIGH;
int actionButtonStable = HIGH;
unsigned long modeButtonLastChangeMs = 0;
unsigned long actionButtonLastChangeMs = 0;

// Serial parsing
char commandBuffer[48];
int commandLength = 0;

const char* sensorName(SensorType s) {
  switch (s) {
    case SENSOR_MPU6050: return "MPU6050";
    case SENSOR_ADXL345: return "ADXL345";
    case SENSOR_LIS3DH: return "LIS3DH";
    default: return "Aucun";
  }
}

const char* modeName(ControlMode m) {
  switch (m) {
    case MODE_ACCEL: return "ACCEL";
    case MODE_POT: return "POT";
    default: return "BOUTON";
  }
}

void setLeds(bool blue, bool green, bool yellow, bool red) {
  digitalWrite(LED_BLUE_PIN, blue ? HIGH : LOW);
  digitalWrite(LED_GREEN_PIN, green ? HIGH : LOW);
  digitalWrite(LED_YELLOW_PIN, yellow ? HIGH : LOW);
  digitalWrite(LED_RED_PIN, red ? HIGH : LOW);
}

void setAllLedsLow() {
  for (int i = 0; i < LED_COUNT; ++i) {
    digitalWrite(LED_PINS[i], LOW);
  }
}

void resetPatternRuntime() {
  chaserIndex = 0;
  pairToggle = false;
  allToggle = false;
}

void setControlMode(ControlMode m) {
  controlMode = m;
  resetPatternRuntime();
  Serial.print("Mode controle -> ");
  Serial.println(modeName(controlMode));
}

void setAnimationRunning(bool running) {
  runAnimation = running;
  if (!runAnimation) {
    setAllLedsLow();
  }
}

bool i2cPing(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool i2cWriteByte(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool i2cReadBytes(uint8_t addr, uint8_t reg, uint8_t* out, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  const size_t received = Wire.requestFrom((int)addr, (int)len);
  if (received != len) return false;
  for (size_t i = 0; i < len; ++i) out[i] = (uint8_t)Wire.read();
  return true;
}

bool isMPU6050(uint8_t addr) {
  uint8_t who = 0;
  return i2cReadBytes(addr, 0x75, &who, 1) &&
         (who == 0x68 || who == 0x69 || who == 0x70 || who == 0x71 || who == 0x73);
}

bool isADXL345(uint8_t addr) {
  uint8_t devid = 0;
  return i2cReadBytes(addr, 0x00, &devid, 1) && (devid == 0xE5);
}

bool isLIS3DH(uint8_t addr) {
  uint8_t who = 0;
  return i2cReadBytes(addr, 0x0F, &who, 1) && (who == 0x33);
}

bool initMPU6050(uint8_t addr) {
  return i2cWriteByte(addr, 0x6B, 0x00) &&
         i2cWriteByte(addr, 0x1C, 0x00) &&
         i2cWriteByte(addr, 0x1B, 0x00);
}

bool initADXL345(uint8_t addr) {
  return i2cWriteByte(addr, 0x2D, 0x00) &&
         i2cWriteByte(addr, 0x2D, 0x08) &&
         i2cWriteByte(addr, 0x31, 0x08) &&
         i2cWriteByte(addr, 0x2C, 0x0A);
}

bool initLIS3DH(uint8_t addr) {
  return i2cWriteByte(addr, 0x20, 0x57) &&
         i2cWriteByte(addr, 0x23, 0x88);
}

bool configureDetectedSensor(SensorType type, uint8_t addr) {
  switch (type) {
    case SENSOR_MPU6050: return initMPU6050(addr);
    case SENSOR_ADXL345: return initADXL345(addr);
    case SENSOR_LIS3DH: return initLIS3DH(addr);
    default: return false;
  }
}

bool tryDetectAtAddress(uint8_t addr) {
  if (!i2cPing(addr)) return false;
  SensorType type = SENSOR_NONE;
  if (isMPU6050(addr)) type = SENSOR_MPU6050;
  else if (isADXL345(addr)) type = SENSOR_ADXL345;
  else if (isLIS3DH(addr)) type = SENSOR_LIS3DH;

  if (type == SENSOR_NONE || !configureDetectedSensor(type, addr)) return false;
  detectedSensor = type;
  sensorAddress = addr;
  return true;
}

bool detectAccelerometer() {
  for (size_t i = 0; i < sizeof(KNOWN_ADDRS); ++i) {
    if (tryDetectAtAddress(KNOWN_ADDRS[i])) return true;
  }
  for (uint8_t addr = 1; addr < 127; ++addr) {
    if (tryDetectAtAddress(addr)) return true;
  }
  return false;
}

bool readAccelMPU6050(float& x, float& y, float& z) {
  uint8_t raw[6];
  if (!i2cReadBytes(sensorAddress, 0x3B, raw, sizeof(raw))) return false;
  x = (float)((int16_t)((raw[0] << 8) | raw[1])) / 16384.0f;
  y = (float)((int16_t)((raw[2] << 8) | raw[3])) / 16384.0f;
  z = (float)((int16_t)((raw[4] << 8) | raw[5])) / 16384.0f;
  return true;
}

bool readAccelADXL345(float& x, float& y, float& z) {
  uint8_t raw[6];
  if (!i2cReadBytes(sensorAddress, 0x32, raw, sizeof(raw))) return false;
  x = (float)((int16_t)((raw[1] << 8) | raw[0])) * 0.0039f;
  y = (float)((int16_t)((raw[3] << 8) | raw[2])) * 0.0039f;
  z = (float)((int16_t)((raw[5] << 8) | raw[4])) * 0.0039f;
  return true;
}

bool readAccelLIS3DH(float& x, float& y, float& z) {
  uint8_t raw[6];
  if (!i2cReadBytes(sensorAddress, (uint8_t)(0x28 | 0x80), raw, sizeof(raw))) return false;
  x = (float)(((int16_t)((raw[1] << 8) | raw[0])) >> 4) * 0.001f;
  y = (float)(((int16_t)((raw[3] << 8) | raw[2])) >> 4) * 0.001f;
  z = (float)(((int16_t)((raw[5] << 8) | raw[4])) >> 4) * 0.001f;
  return true;
}

bool readAcceleration(float& x, float& y, float& z) {
  switch (detectedSensor) {
    case SENSOR_MPU6050: return readAccelMPU6050(x, y, z);
    case SENSOR_ADXL345: return readAccelADXL345(x, y, z);
    case SENSOR_LIS3DH: return readAccelLIS3DH(x, y, z);
    default: return false;
  }
}

void updateLedsFromTilt(float x, float y) {
  if (fabsf(x) < TILT_DEADZONE_G && fabsf(y) < TILT_DEADZONE_G) {
    setAllLedsLow();
    return;
  }
  if (fabsf(x) >= fabsf(y)) {
    setLeds(x >= 0, x < 0, false, false);
  } else {
    setLeds(false, false, y >= 0, y < 0);
  }
}

void applyButtonPatternStep() {
  switch (buttonPattern) {
    case 0:
      setAllLedsLow();
      digitalWrite(LED_PINS[chaserIndex], HIGH);
      chaserIndex = (chaserIndex + 1) % LED_COUNT;
      break;
    case 1: {
      pairToggle = !pairToggle;
      if (pairToggle) setLeds(true, false, true, false);
      else setLeds(false, true, false, true);
      break;
    }
    default:
      allToggle = !allToggle;
      setLeds(allToggle, allToggle, allToggle, allToggle);
      break;
  }
}

void onModeButtonPressed() {
  if (controlMode == MODE_BOUTON) {
    buttonPattern = (buttonPattern + 1) % 3;
    resetPatternRuntime();
    Serial.print("Pattern bouton -> ");
    Serial.println(buttonPattern);
    return;
  }
  if (controlMode == MODE_POT) {
    setControlMode(MODE_ACCEL);
  } else {
    setControlMode(MODE_POT);
  }
}

void onActionButtonPressed() {
  if (controlMode == MODE_BOUTON) {
    setAnimationRunning(!runAnimation);
    Serial.print("Run bouton -> ");
    Serial.println(runAnimation ? "1" : "0");
    return;
  }
  setControlMode(MODE_BOUTON);
}

void printStatusLine() {
  Serial.print("control=");
  Serial.print(modeName(controlMode));
  Serial.print(" pot=");
  Serial.print(potRaw);
  Serial.print(" stepMs=");
  Serial.print(stepIntervalMs);
  Serial.print(" btnPattern=");
  Serial.print(buttonPattern);
  Serial.print(" run=");
  Serial.print(runAnimation ? "1" : "0");
  Serial.print(" accel=(");
  Serial.print(ax, 2);
  Serial.print(",");
  Serial.print(ay, 2);
  Serial.print(",");
  Serial.print(az, 2);
  Serial.println(")");
}

void handleSerialCommand(char* line) {
  if (strcmp(line, "STATUS") == 0 || strcmp(line, "MODE STATUS") == 0) {
    printStatusLine();
    return;
  }

  if (strcmp(line, "BTN_MODE") == 0) {
    onModeButtonPressed();
    return;
  }

  if (strcmp(line, "BTN_ACTION") == 0) {
    onActionButtonPressed();
    return;
  }

  if (strcmp(line, "MODE ACCEL") == 0) {
    setControlMode(MODE_ACCEL);
    return;
  }

  if (strcmp(line, "MODE POT") == 0) {
    setControlMode(MODE_POT);
    return;
  }

  if (strcmp(line, "MODE BOUTON") == 0 || strcmp(line, "MODE BUTTON") == 0) {
    setControlMode(MODE_BOUTON);
    return;
  }

  Serial.print("ERR commande inconnue: ");
  Serial.println(line);
}

void processSerialCommands() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (commandLength > 0) {
        commandBuffer[commandLength] = '\0';
        handleSerialCommand(commandBuffer);
        commandLength = 0;
      }
      continue;
    }

    if (commandLength < (int)sizeof(commandBuffer) - 1) {
      if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
      commandBuffer[commandLength++] = c;
    } else {
      commandLength = 0;
      Serial.println("ERR commande trop longue");
    }
  }
}

void handleDebouncedButton(
    int pin,
    int& lastRead,
    int& stable,
    unsigned long& lastChangeMs,
    unsigned long now,
    void (*onPressed)()) {
  const int readNow = digitalRead(pin);
  if (readNow != lastRead) {
    lastRead = readNow;
    lastChangeMs = now;
  }
  if ((now - lastChangeMs) > DEBOUNCE_MS && readNow != stable) {
    stable = readNow;
    if (stable == LOW) onPressed();
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  for (int i = 0; i < LED_COUNT; ++i) pinMode(LED_PINS[i], OUTPUT);
  setAllLedsLow();

  pinMode(BUTTON_MODE_PIN, INPUT_PULLUP);
  pinMode(BUTTON_ACTION_PIN, INPUT);
  pinMode(POT_PIN, INPUT);
  analogReadResolution(12);

  modeButtonLastRead = digitalRead(BUTTON_MODE_PIN);
  actionButtonLastRead = digitalRead(BUTTON_ACTION_PIN);
  modeButtonStable = modeButtonLastRead;
  actionButtonStable = actionButtonLastRead;

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);

  if (detectAccelerometer()) {
    Serial.print("Accel detecte: ");
    Serial.print(sensorName(detectedSensor));
    Serial.print(" @ 0x");
    if (sensorAddress < 16) Serial.print('0');
    Serial.println(sensorAddress, HEX);
  } else {
    Serial.println("Aucun accelerometre detecte.");
  }

  Serial.println("=== UNIFIED MODES CONTROL ===");
  Serial.println("Modes: ACCEL | POT | BOUTON");
  Serial.println("Cmd: MODE ACCEL | MODE POT | MODE BOUTON | BTN_MODE | BTN_ACTION | STATUS");
  printStatusLine();
}

void loop() {
  const unsigned long now = millis();
  processSerialCommands();

  handleDebouncedButton(
      BUTTON_MODE_PIN,
      modeButtonLastRead,
      modeButtonStable,
      modeButtonLastChangeMs,
      now,
      onModeButtonPressed);

  handleDebouncedButton(
      BUTTON_ACTION_PIN,
      actionButtonLastRead,
      actionButtonStable,
      actionButtonLastChangeMs,
      now,
      onActionButtonPressed);

  if ((now - lastSampleMs) >= SAMPLE_PERIOD_MS) {
    lastSampleMs = now;
    potRaw = analogRead(POT_PIN);
    stepIntervalMs = map(potRaw, 0, 4095, MIN_STEP_MS, MAX_STEP_MS);
    if (detectedSensor != SENSOR_NONE) {
      readAcceleration(ax, ay, az);
    } else {
      detectAccelerometer();
    }
  }

  if (controlMode == MODE_ACCEL) {
    updateLedsFromTilt(ax, ay);
  } else if (controlMode == MODE_POT) {
    const int level = map(potRaw, 0, 4095, 0, 4);
    setLeds(level >= 1, level >= 2, level >= 3, level >= 4);
  } else if (runAnimation && (now - lastStepMs) >= stepIntervalMs) {
    lastStepMs = now;
    applyButtonPatternStep();
  }

  if ((now - lastLogMs) >= 1000) {
    lastLogMs = now;
    printStatusLine();
  }

  delay(5);
}
