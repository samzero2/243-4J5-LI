// Demo accelerometre -> LEDs selon l'inclinaison
// Cablage:
//   I2C: SDA=33, SCL=32
//   LEDs: BLEU=19, VERT=23, JAUNE=21, ROUGE=22
//
// Fonctionnement:
// - Detection auto: MPU6050 / ADXL345 / LIS3DH
// - Affiche X/Y/Z en g au moniteur serie
// - Pilote les LEDs selon l'axe dominant:
//     X+ : LED BLEUE
//     X- : LED VERTE
//     Y+ : LED JAUNE
//     Y- : LED ROUGE
// - Si inclinaison faible (zone morte), toutes LEDs eteintes

#include <Wire.h>
#include <math.h>

constexpr uint8_t I2C_SDA_PIN = 33;
constexpr uint8_t I2C_SCL_PIN = 32;
constexpr uint32_t I2C_CLOCK_HZ = 100000;
constexpr uint32_t SAMPLE_PERIOD_MS = 120;

constexpr int LED_BLUE_PIN = 19;
constexpr int LED_GREEN_PIN = 23;
constexpr int LED_YELLOW_PIN = 21;
constexpr int LED_RED_PIN = 22;
constexpr float TILT_DEADZONE_G = 0.20f;

enum SensorType : uint8_t {
  SENSOR_NONE = 0,
  SENSOR_MPU6050,
  SENSOR_ADXL345,
  SENSOR_LIS3DH,
};

SensorType detectedSensor = SENSOR_NONE;
uint8_t sensorAddress = 0x00;
const uint8_t KNOWN_ADDRS[] = {0x68, 0x69, 0x53, 0x18, 0x19};

const char* sensorName(SensorType sensor) {
  switch (sensor) {
    case SENSOR_MPU6050:
      return "MPU6050";
    case SENSOR_ADXL345:
      return "ADXL345";
    case SENSOR_LIS3DH:
      return "LIS3DH";
    default:
      return "Aucun";
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
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const size_t received = Wire.requestFrom((int)addr, (int)len);
  if (received != len) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    out[i] = (uint8_t)Wire.read();
  }
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
    case SENSOR_MPU6050:
      return initMPU6050(addr);
    case SENSOR_ADXL345:
      return initADXL345(addr);
    case SENSOR_LIS3DH:
      return initLIS3DH(addr);
    default:
      return false;
  }
}

bool tryDetectAtAddress(uint8_t addr) {
  if (!i2cPing(addr)) {
    return false;
  }

  SensorType type = SENSOR_NONE;
  if (isMPU6050(addr)) {
    type = SENSOR_MPU6050;
  } else if (isADXL345(addr)) {
    type = SENSOR_ADXL345;
  } else if (isLIS3DH(addr)) {
    type = SENSOR_LIS3DH;
  }

  if (type == SENSOR_NONE || !configureDetectedSensor(type, addr)) {
    return false;
  }

  detectedSensor = type;
  sensorAddress = addr;
  return true;
}

bool detectAccelerometer() {
  for (size_t i = 0; i < sizeof(KNOWN_ADDRS); ++i) {
    if (tryDetectAtAddress(KNOWN_ADDRS[i])) {
      return true;
    }
  }
  for (uint8_t addr = 1; addr < 127; ++addr) {
    if (tryDetectAtAddress(addr)) {
      return true;
    }
  }
  return false;
}

bool readAccelMPU6050(float& ax, float& ay, float& az) {
  uint8_t raw[6];
  if (!i2cReadBytes(sensorAddress, 0x3B, raw, sizeof(raw))) {
    return false;
  }
  const int16_t x = (int16_t)((raw[0] << 8) | raw[1]);
  const int16_t y = (int16_t)((raw[2] << 8) | raw[3]);
  const int16_t z = (int16_t)((raw[4] << 8) | raw[5]);
  ax = (float)x / 16384.0f;
  ay = (float)y / 16384.0f;
  az = (float)z / 16384.0f;
  return true;
}

bool readAccelADXL345(float& ax, float& ay, float& az) {
  uint8_t raw[6];
  if (!i2cReadBytes(sensorAddress, 0x32, raw, sizeof(raw))) {
    return false;
  }
  const int16_t x = (int16_t)((raw[1] << 8) | raw[0]);
  const int16_t y = (int16_t)((raw[3] << 8) | raw[2]);
  const int16_t z = (int16_t)((raw[5] << 8) | raw[4]);
  ax = (float)x * 0.0039f;
  ay = (float)y * 0.0039f;
  az = (float)z * 0.0039f;
  return true;
}

bool readAccelLIS3DH(float& ax, float& ay, float& az) {
  uint8_t raw[6];
  if (!i2cReadBytes(sensorAddress, (uint8_t)(0x28 | 0x80), raw, sizeof(raw))) {
    return false;
  }
  const int16_t xRaw = (int16_t)((raw[1] << 8) | raw[0]);
  const int16_t yRaw = (int16_t)((raw[3] << 8) | raw[2]);
  const int16_t zRaw = (int16_t)((raw[5] << 8) | raw[4]);
  const int16_t x = (int16_t)(xRaw >> 4);
  const int16_t y = (int16_t)(yRaw >> 4);
  const int16_t z = (int16_t)(zRaw >> 4);
  ax = (float)x * 0.001f;
  ay = (float)y * 0.001f;
  az = (float)z * 0.001f;
  return true;
}

bool readAcceleration(float& ax, float& ay, float& az) {
  switch (detectedSensor) {
    case SENSOR_MPU6050:
      return readAccelMPU6050(ax, ay, az);
    case SENSOR_ADXL345:
      return readAccelADXL345(ax, ay, az);
    case SENSOR_LIS3DH:
      return readAccelLIS3DH(ax, ay, az);
    default:
      return false;
  }
}

void updateLedsFromTilt(float ax, float ay) {
  if (fabsf(ax) < TILT_DEADZONE_G && fabsf(ay) < TILT_DEADZONE_G) {
    setAllLedsLow();
    return;
  }

  if (fabsf(ax) >= fabsf(ay)) {
    if (ax >= 0.0f) {
      setLeds(true, false, false, false);   // X+ -> BLEU
    } else {
      setLeds(false, true, false, false);   // X- -> VERT
    }
  } else {
    if (ay >= 0.0f) {
      setLeds(false, false, true, false);   // Y+ -> JAUNE
    } else {
      setLeds(false, false, false, true);   // Y- -> ROUGE
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);

  pinMode(LED_BLUE_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_YELLOW_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);
  setAllLedsLow();

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);

  Serial.println("=== DEMO TILT -> LEDs ===");
  Serial.println("I2C: SDA=33, SCL=32");

  if (detectAccelerometer()) {
    Serial.print("Capteur detecte: ");
    Serial.print(sensorName(detectedSensor));
    Serial.print(" @ 0x");
    if (sensorAddress < 16) {
      Serial.print('0');
    }
    Serial.println(sensorAddress, HEX);
  } else {
    Serial.println("Aucun accelerometre detecte.");
    Serial.println("Verifie cablage/alimentation.");
  }
}

void loop() {
  static unsigned long lastSampleMs = 0;
  const unsigned long now = millis();

  if ((now - lastSampleMs) < SAMPLE_PERIOD_MS) {
    return;
  }
  lastSampleMs = now;

  if (detectedSensor == SENSOR_NONE) {
    detectAccelerometer();
    return;
  }

  float ax = 0.0f;
  float ay = 0.0f;
  float az = 0.0f;
  if (!readAcceleration(ax, ay, az)) {
    Serial.println("Erreur lecture accelerometre.");
    setAllLedsLow();
    return;
  }

  updateLedsFromTilt(ax, ay);

  Serial.print("X=");
  Serial.print(ax, 3);
  Serial.print(" Y=");
  Serial.print(ay, 3);
  Serial.print(" Z=");
  Serial.print(az, 3);
  Serial.print(" |A|=");
  Serial.println(sqrtf(ax * ax + ay * ay + az * az), 3);
}
