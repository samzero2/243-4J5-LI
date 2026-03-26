// Mini-jeu "Reflexe + Equilibre" (ESP32/LilyGO)
// Composants utilises:
// - 4 LEDs: BLEU=19, VERT=23, JAUNE=21, ROUGE=22
// - Bouton MODE: GPIO18 (INPUT_PULLUP, appui=LOW)
// - Bouton ACTION: GPIO39 (input-only, appui=LOW, pull-up externe)
// - Potentiometre: GPIO36 (ADC)
// - Accelerometre I2C: SDA=33, SCL=32
//
// Regles:
// 1) Une LED cible s'allume (direction attendue)
//    BLEU=X+, VERT=X-, JAUNE=Y+, ROUGE=Y-
// 2) Oriente la carte dans la bonne direction
// 3) Quand l'orientation est correcte, appuie sur ACTION pour valider
// 4) Tu gagnes 1 point si c'est dans le temps imparti
//
// Potentiometre:
// - regle la difficulte (temps alloue par manche):
//   pot bas = plus de temps, pot haut = moins de temps
//
// Boutons:
// - MODE: start/reset partie
// - ACTION: valider la direction courante

#include <Wire.h>
#include <math.h>

// -------------------- PINS --------------------
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

// -------------------- TIMING --------------------
constexpr unsigned long DEBOUNCE_MS = 40;
constexpr unsigned long SAMPLE_PERIOD_MS = 70;
constexpr unsigned long SCORE_FLASH_MS = 180;
constexpr int MAX_ROUNDS = 10;

// Pot -> temps de manche
constexpr unsigned long ROUND_TIME_MIN_MS = 700;   // difficile
constexpr unsigned long ROUND_TIME_MAX_MS = 2800;  // facile

// Seuil de validation orientation (g)
constexpr float TILT_THRESHOLD_G = 0.32f;
constexpr float TILT_DEADZONE_G = 0.15f;

// -------------------- ACCEL --------------------
enum SensorType : uint8_t {
  SENSOR_NONE = 0,
  SENSOR_MPU6050,
  SENSOR_ADXL345,
  SENSOR_LIS3DH,
};

SensorType detectedSensor = SENSOR_NONE;
uint8_t sensorAddress = 0x00;
const uint8_t KNOWN_ADDRS[] = {0x68, 0x69, 0x53, 0x18, 0x19};

// -------------------- GAME --------------------
enum GameState : uint8_t {
  STATE_WAIT_START = 0,
  STATE_PLAYING,
  STATE_ROUND_FEEDBACK,
  STATE_GAME_OVER
};

GameState gameState = STATE_WAIT_START;
int score = 0;
int roundIndex = 0;
int targetDir = 0; // 0:X+, 1:X-, 2:Y+, 3:Y-
unsigned long roundStartMs = 0;
unsigned long roundDurationMs = 1500;
unsigned long feedbackUntilMs = 0;
bool feedbackSuccess = false;

float ax = 0.0f;
float ay = 0.0f;
float az = 0.0f;
int potRaw = 0;
unsigned long lastSampleMs = 0;
unsigned long lastStatusMs = 0;

// Boutons debounce
int modeButtonLastRead = HIGH;
int actionButtonLastRead = HIGH;
int modeButtonStable = HIGH;
int actionButtonStable = HIGH;
unsigned long modeButtonLastChangeMs = 0;
unsigned long actionButtonLastChangeMs = 0;

// -------------------- UTILS --------------------
const char* sensorName(SensorType s) {
  switch (s) {
    case SENSOR_MPU6050: return "MPU6050";
    case SENSOR_ADXL345: return "ADXL345";
    case SENSOR_LIS3DH:  return "LIS3DH";
    default:             return "Aucun";
  }
}

const char* dirName(int d) {
  switch (d) {
    case 0: return "X+";
    case 1: return "X-";
    case 2: return "Y+";
    default: return "Y-";
  }
}

void setLeds(bool blue, bool green, bool yellow, bool red) {
  digitalWrite(LED_BLUE_PIN, blue ? HIGH : LOW);
  digitalWrite(LED_GREEN_PIN, green ? HIGH : LOW);
  digitalWrite(LED_YELLOW_PIN, yellow ? HIGH : LOW);
  digitalWrite(LED_RED_PIN, red ? HIGH : LOW);
}

void setAllLedsLow() {
  setLeds(false, false, false, false);
}

void showTargetLed(int dir) {
  switch (dir) {
    case 0: setLeds(true,  false, false, false); break; // X+
    case 1: setLeds(false, true,  false, false); break; // X-
    case 2: setLeds(false, false, true,  false); break; // Y+
    default:setLeds(false, false, false, true ); break; // Y-
  }
}

void showScoreBar(int value) {
  // Bargraph 0..4
  const int level = constrain(map(value, 0, MAX_ROUNDS, 0, 4), 0, 4);
  setLeds(level >= 1, level >= 2, level >= 3, level >= 4);
}

void blinkAllOnce() {
  setLeds(true, true, true, true);
  delay(80);
  setAllLedsLow();
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
  size_t received = Wire.requestFrom((int)addr, (int)len);
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

bool configureSensor(SensorType type, uint8_t addr) {
  switch (type) {
    case SENSOR_MPU6050: return initMPU6050(addr);
    case SENSOR_ADXL345: return initADXL345(addr);
    case SENSOR_LIS3DH:  return initLIS3DH(addr);
    default: return false;
  }
}

bool tryDetectAt(uint8_t addr) {
  if (!i2cPing(addr)) return false;
  SensorType type = SENSOR_NONE;
  if (isMPU6050(addr)) type = SENSOR_MPU6050;
  else if (isADXL345(addr)) type = SENSOR_ADXL345;
  else if (isLIS3DH(addr)) type = SENSOR_LIS3DH;
  if (type == SENSOR_NONE) return false;
  if (!configureSensor(type, addr)) return false;
  detectedSensor = type;
  sensorAddress = addr;
  return true;
}

bool detectAccelerometer() {
  for (size_t i = 0; i < sizeof(KNOWN_ADDRS); ++i) {
    if (tryDetectAt(KNOWN_ADDRS[i])) return true;
  }
  for (uint8_t addr = 1; addr < 127; ++addr) {
    if (tryDetectAt(addr)) return true;
  }
  return false;
}

bool readAccel(float& x, float& y, float& z) {
  if (detectedSensor == SENSOR_NONE) return false;
  uint8_t raw[6];

  if (detectedSensor == SENSOR_MPU6050) {
    if (!i2cReadBytes(sensorAddress, 0x3B, raw, sizeof(raw))) return false;
    int16_t rx = (int16_t)((raw[0] << 8) | raw[1]);
    int16_t ry = (int16_t)((raw[2] << 8) | raw[3]);
    int16_t rz = (int16_t)((raw[4] << 8) | raw[5]);
    x = (float)rx / 16384.0f;
    y = (float)ry / 16384.0f;
    z = (float)rz / 16384.0f;
    return true;
  }

  if (detectedSensor == SENSOR_ADXL345) {
    if (!i2cReadBytes(sensorAddress, 0x32, raw, sizeof(raw))) return false;
    int16_t rx = (int16_t)((raw[1] << 8) | raw[0]);
    int16_t ry = (int16_t)((raw[3] << 8) | raw[2]);
    int16_t rz = (int16_t)((raw[5] << 8) | raw[4]);
    x = (float)rx * 0.0039f;
    y = (float)ry * 0.0039f;
    z = (float)rz * 0.0039f;
    return true;
  }

  if (!i2cReadBytes(sensorAddress, (uint8_t)(0x28 | 0x80), raw, sizeof(raw))) return false;
  int16_t rx = (int16_t)((raw[1] << 8) | raw[0]) >> 4;
  int16_t ry = (int16_t)((raw[3] << 8) | raw[2]) >> 4;
  int16_t rz = (int16_t)((raw[5] << 8) | raw[4]) >> 4;
  x = (float)rx * 0.001f;
  y = (float)ry * 0.001f;
  z = (float)rz * 0.001f;
  return true;
}

int dominantDirectionFromAccel(float x, float y) {
  if (fabsf(x) < TILT_DEADZONE_G && fabsf(y) < TILT_DEADZONE_G) {
    return -1;
  }
  if (fabsf(x) >= fabsf(y)) {
    return (x >= TILT_THRESHOLD_G) ? 0 : ((x <= -TILT_THRESHOLD_G) ? 1 : -1);
  }
  return (y >= TILT_THRESHOLD_G) ? 2 : ((y <= -TILT_THRESHOLD_G) ? 3 : -1);
}

void chooseNewRound() {
  targetDir = (int)random(0, 4);
  roundStartMs = millis();
  showTargetLed(targetDir);
}

void startGame() {
  score = 0;
  roundIndex = 0;
  feedbackSuccess = false;
  gameState = STATE_PLAYING;
  chooseNewRound();
  Serial.println();
  Serial.println("=== NOUVELLE PARTIE ===");
}

void finishGame() {
  gameState = STATE_GAME_OVER;
  Serial.println("=== PARTIE TERMINEE ===");
  Serial.print("Score final: ");
  Serial.print(score);
  Serial.print("/");
  Serial.println(MAX_ROUNDS);
  showScoreBar(score);
}

void onModePressed() {
  // Start ou reset partie
  startGame();
}

void onActionPressed() {
  if (gameState != STATE_PLAYING) {
    if (gameState == STATE_GAME_OVER) {
      startGame();
    }
    return;
  }

  int currentDir = dominantDirectionFromAccel(ax, ay);
  bool success = (currentDir == targetDir);
  if (success) {
    score++;
  }

  feedbackSuccess = success;
  feedbackUntilMs = millis() + SCORE_FLASH_MS;
  gameState = STATE_ROUND_FEEDBACK;

  Serial.print("Round ");
  Serial.print(roundIndex + 1);
  Serial.print(": cible=");
  Serial.print(dirName(targetDir));
  Serial.print(", detecte=");
  Serial.print(currentDir >= 0 ? dirName(currentDir) : "NONE");
  Serial.print(" -> ");
  Serial.println(success ? "OK" : "ECHEC");
}

void handleDebouncedButton(
    int pin,
    int& lastRead,
    int& stable,
    unsigned long& lastChangeMs,
    unsigned long now,
    void (*onPressed)()) {
  int readNow = digitalRead(pin);
  if (readNow != lastRead) {
    lastRead = readNow;
    lastChangeMs = now;
  }
  if ((now - lastChangeMs) > DEBOUNCE_MS && readNow != stable) {
    stable = readNow;
    if (stable == LOW) {
      onPressed();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);

  randomSeed((uint32_t)analogRead(POT_PIN) ^ micros());

  for (int i = 0; i < LED_COUNT; ++i) {
    pinMode(LED_PINS[i], OUTPUT);
  }
  setAllLedsLow();

  pinMode(BUTTON_MODE_PIN, INPUT_PULLUP);
  pinMode(BUTTON_ACTION_PIN, INPUT); // input-only, pull-up externe requis
  pinMode(POT_PIN, INPUT);
  analogReadResolution(12);

  modeButtonLastRead = digitalRead(BUTTON_MODE_PIN);
  actionButtonLastRead = digitalRead(BUTTON_ACTION_PIN);
  modeButtonStable = modeButtonLastRead;
  actionButtonStable = actionButtonLastRead;

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);

  Serial.println("=== JEU REFLEXE + EQUILIBRE ===");
  Serial.println("Mode=Start/Reset | Action=Valider orientation");
  Serial.println("Potentiometre: ajuste temps de manche (difficulte)");

  if (detectAccelerometer()) {
    Serial.print("Accelerometre detecte: ");
    Serial.print(sensorName(detectedSensor));
    Serial.print(" @ 0x");
    if (sensorAddress < 16) Serial.print('0');
    Serial.println(sensorAddress, HEX);
  } else {
    Serial.println("Aucun accelerometre detecte.");
  }

  // Animation d'accueil
  for (int i = 0; i < 2; ++i) {
    blinkAllOnce();
    delay(80);
  }
  showScoreBar(0);
}

void loop() {
  unsigned long now = millis();

  handleDebouncedButton(
      BUTTON_MODE_PIN,
      modeButtonLastRead,
      modeButtonStable,
      modeButtonLastChangeMs,
      now,
      onModePressed);

  handleDebouncedButton(
      BUTTON_ACTION_PIN,
      actionButtonLastRead,
      actionButtonStable,
      actionButtonLastChangeMs,
      now,
      onActionPressed);

  if ((now - lastSampleMs) >= SAMPLE_PERIOD_MS) {
    lastSampleMs = now;
    potRaw = analogRead(POT_PIN);
    // pot haut => plus difficile (moins de temps)
    roundDurationMs = map(potRaw, 0, 4095, ROUND_TIME_MAX_MS, ROUND_TIME_MIN_MS);

    if (detectedSensor == SENSOR_NONE) {
      detectAccelerometer();
    } else {
      readAccel(ax, ay, az);
    }
  }

  if (gameState == STATE_PLAYING) {
    showTargetLed(targetDir);

    if ((now - roundStartMs) > roundDurationMs) {
      // Timeout = manche perdue
      feedbackSuccess = false;
      feedbackUntilMs = now + SCORE_FLASH_MS;
      gameState = STATE_ROUND_FEEDBACK;
      Serial.print("Round ");
      Serial.print(roundIndex + 1);
      Serial.println(": TIMEOUT");
    }
  } else if (gameState == STATE_ROUND_FEEDBACK) {
    if (feedbackSuccess) {
      // Flash toutes LEDs si succes
      setLeds(true, true, true, true);
    } else {
      // Flash LED rouge si echec
      setLeds(false, false, false, true);
    }

    if (now >= feedbackUntilMs) {
      roundIndex++;
      if (roundIndex >= MAX_ROUNDS) {
        finishGame();
      } else {
        gameState = STATE_PLAYING;
        chooseNewRound();
      }
    }
  } else if (gameState == STATE_GAME_OVER) {
    // Score bar persistant
    showScoreBar(score);
  } else {
    // WAIT_START
    showScoreBar(0);
  }

  if ((now - lastStatusMs) >= 1000) {
    lastStatusMs = now;
    Serial.print("state=");
    Serial.print((int)gameState);
    Serial.print(" score=");
    Serial.print(score);
    Serial.print("/");
    Serial.print(MAX_ROUNDS);
    Serial.print(" target=");
    Serial.print(dirName(targetDir));
    Serial.print(" pot=");
    Serial.print(potRaw);
    Serial.print(" roundMs=");
    Serial.print(roundDurationMs);
    Serial.print(" accel=(");
    Serial.print(ax, 2);
    Serial.print(",");
    Serial.print(ay, 2);
    Serial.print(",");
    Serial.print(az, 2);
    Serial.println(")");
  }

  delay(5);
}
