// Demo complet LilyGO/ESP32:
// - Ecran tactile Raspberry Pi via liaison serie
// - 4 LEDs externes
// - 2 boutons
// - 1 potentiometre
//
// Mapping utilise:
//   LEDs:  BLEU=19, VERT=23, JAUNE=21, ROUGE=22
//   BTN mode: 18 (INPUT_PULLUP, appui -> LOW)
//   BTN action: 39 (INPUT only, appui -> LOW, pull-up externe requis)
//   POT: 36 (ADC)
//
// Commandes serie:
//   MODE_NEXT | MODE_PLUS | MODE_MINUS | MODE 0..3
//   PAUSE | RESUME | TOGGLE_RUN | STATUS

#include <stdlib.h>
#include <string.h>

const int LED_BLUE_PIN = 19;
const int LED_GREEN_PIN = 23;
const int LED_YELLOW_PIN = 21;
const int LED_RED_PIN = 22;
const int LED_PINS[] = {LED_BLUE_PIN, LED_GREEN_PIN, LED_YELLOW_PIN, LED_RED_PIN};
const int LED_COUNT = sizeof(LED_PINS) / sizeof(LED_PINS[0]);

const int BUTTON_MODE_PIN = 18;
const int BUTTON_ACTION_PIN = 39;
const int POT_PIN = 36;

const unsigned long DEBOUNCE_MS = 40;
const int MIN_STEP_MS = 200;
const int MAX_STEP_MS = 1200;

int modeButtonLastRead = HIGH;
int actionButtonLastRead = HIGH;
int modeButtonStable = HIGH;
int actionButtonStable = HIGH;
unsigned long modeButtonLastChangeMs = 0;
unsigned long actionButtonLastChangeMs = 0;

int currentMode = 0;
bool runAnimation = true;
int potRaw = 0;
unsigned long stepIntervalMs = 500;
unsigned long lastStepMs = 0;
unsigned long lastLogMs = 0;

int chaserIndex = 0;
bool pairToggle = false;
bool allToggle = false;
char commandBuffer[48];
int commandLength = 0;

void setAllLedsLow() {
  for (int i = 0; i < LED_COUNT; ++i) {
    digitalWrite(LED_PINS[i], LOW);
  }
}

void setLeds(bool blue, bool green, bool yellow, bool red) {
  digitalWrite(LED_BLUE_PIN, blue ? HIGH : LOW);
  digitalWrite(LED_GREEN_PIN, green ? HIGH : LOW);
  digitalWrite(LED_YELLOW_PIN, yellow ? HIGH : LOW);
  digitalWrite(LED_RED_PIN, red ? HIGH : LOW);
}

const char* modeName(int mode) {
  switch (mode) {
    case 0:
      return "Chenillard";
    case 1:
      return "Bargraphe pot";
    case 2:
      return "Paires alternees";
    default:
      return "Toggle global";
  }
}

void resetModeRuntimeState() {
  chaserIndex = 0;
  pairToggle = false;
  allToggle = false;
}

void setMode(int mode) {
  currentMode = mode;
  resetModeRuntimeState();
  Serial.print("Mode -> ");
  Serial.print(currentMode);
  Serial.print(" (");
  Serial.print(modeName(currentMode));
  Serial.println(")");
}

void setAnimationRunning(bool running) {
  runAnimation = running;
  if (!runAnimation) {
    setAllLedsLow();
  }
  Serial.print("Animation -> ");
  Serial.println(runAnimation ? "ACTIVE" : "PAUSE");
}

void printStatusLine() {
  Serial.print("mode=");
  Serial.print(currentMode);
  Serial.print(" (");
  Serial.print(modeName(currentMode));
  Serial.print("), pot=");
  Serial.print(potRaw);
  Serial.print(", stepMs=");
  Serial.print(stepIntervalMs);
  Serial.print(", run=");
  Serial.println(runAnimation ? "1" : "0");
}

void onModeButtonPressed() {
  setMode((currentMode + 1) % 4);
}

void onActionButtonPressed() {
  setAnimationRunning(!runAnimation);
}

void handleSerialCommand(char* line) {
  if (strcmp(line, "MODE_NEXT") == 0 || strcmp(line, "MODE_PLUS") == 0) {
    onModeButtonPressed();
    return;
  }

  if (strcmp(line, "MODE_MINUS") == 0) {
    setMode((currentMode + 3) % 4);
    return;
  }

  if (strcmp(line, "PAUSE") == 0) {
    setAnimationRunning(false);
    return;
  }

  if (strcmp(line, "RESUME") == 0) {
    setAnimationRunning(true);
    return;
  }

  if (strcmp(line, "TOGGLE_RUN") == 0) {
    onActionButtonPressed();
    return;
  }

  if (strcmp(line, "STATUS") == 0) {
    printStatusLine();
    return;
  }

  if (strncmp(line, "MODE ", 5) == 0) {
    char* endPtr = NULL;
    const long parsedMode = strtol(line + 5, &endPtr, 10);
    if ((line + 5) == endPtr || *endPtr != '\0' || parsedMode < 0 || parsedMode > 3) {
      Serial.println("ERR MODE attendu: MODE 0..3");
      return;
    }
    setMode((int)parsedMode);
    return;
  }

  Serial.print("ERR commande inconnue: ");
  Serial.println(line);
}

void processSerialCommands() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (commandLength > 0) {
        commandBuffer[commandLength] = '\0';
        handleSerialCommand(commandBuffer);
        commandLength = 0;
      }
      continue;
    }

    if (commandLength < (int)sizeof(commandBuffer) - 1) {
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
    if (stable == LOW) {
      onPressed();
    }
  }
}

void applyCurrentModeStep() {
  switch (currentMode) {
    case 0:
      setAllLedsLow();
      digitalWrite(LED_PINS[chaserIndex], HIGH);
      chaserIndex = (chaserIndex + 1) % LED_COUNT;
      break;

    case 1: {
      const int level = map(potRaw, 0, 4095, 0, 4);
      setLeds(level >= 1, level >= 2, level >= 3, level >= 4);
      break;
    }

    case 2:
      pairToggle = !pairToggle;
      if (pairToggle) {
        setLeds(true, false, true, false);   // bleu + jaune
      } else {
        setLeds(false, true, false, true);   // vert + rouge
      }
      break;

    default:
      allToggle = !allToggle;
      setLeds(allToggle, allToggle, allToggle, allToggle);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  for (int i = 0; i < LED_COUNT; ++i) {
    pinMode(LED_PINS[i], OUTPUT);
  }
  setAllLedsLow();

  pinMode(BUTTON_MODE_PIN, INPUT_PULLUP);
  pinMode(BUTTON_ACTION_PIN, INPUT);  // GPIO39: pas de pull-up interne
  pinMode(POT_PIN, INPUT);
  analogReadResolution(12);

  modeButtonLastRead = digitalRead(BUTTON_MODE_PIN);
  actionButtonLastRead = digitalRead(BUTTON_ACTION_PIN);
  modeButtonStable = modeButtonLastRead;
  actionButtonStable = actionButtonLastRead;

  Serial.println("=== Demo LEDs + Boutons + Potentiometre ===");
  Serial.println("BTN18 (LOW): change le mode");
  Serial.println("BTN39 (LOW): pause/reprend l'animation");
  Serial.println("POT36: regle la vitesse (200-1200 ms)");
  Serial.println("Mode 0: chenillard");
  Serial.println("Mode 1: bargraphe selon potentiometre");
  Serial.println("Mode 2: paires alternees");
  Serial.println("Mode 3: toggle de toutes les LEDs");
  Serial.println("Commandes serie: MODE_NEXT | MODE_PLUS | MODE_MINUS | MODE 0..3 | PAUSE | RESUME | TOGGLE_RUN | STATUS");
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

  potRaw = analogRead(POT_PIN);
  stepIntervalMs = map(potRaw, 0, 4095, MIN_STEP_MS, MAX_STEP_MS);

  if (runAnimation && (now - lastStepMs) >= stepIntervalMs) {
    lastStepMs = now;
    applyCurrentModeStep();
  }

  if ((now - lastLogMs) >= 1000) {
    lastLogMs = now;
    printStatusLine();
  }

  delay(5);
}
