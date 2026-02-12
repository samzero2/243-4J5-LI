// Controle de LEDs via Port Série
// Ce sketch ecoute sur le port serie pour les commandes "rouge" et "vert"
// et allume les LEDs correspondantes connectees aux GPIOs.

#define LED_RED_PIN 26
#define LED_GREEN_PIN 27

void setup() {
  // Initialiser la communication série
  Serial.begin(115200);
  delay(1000);

  // Configurer les pins des LEDs
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  
  // Eteindre les LEDs au demarrage
  digitalWrite(LED_RED_PIN, LOW);
  digitalWrite(LED_GREEN_PIN, LOW);

  Serial.println("=========================");
  Serial.println("LilyGO A7670G - Control LED");
  Serial.println("Commandes: 'rouge', 'vert'");
  Serial.println("=========================");
}

void loop() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim(); // Enlever les espaces et sauts de ligne
    
    if (command.equalsIgnoreCase("rouge")) {
      digitalWrite(LED_RED_PIN, HIGH);
      digitalWrite(LED_GREEN_PIN, LOW);
      Serial.println("Commande recue: ROUGE ON");
    } 
    else if (command.equalsIgnoreCase("vert")) {
      digitalWrite(LED_RED_PIN, LOW);
      digitalWrite(LED_GREEN_PIN, HIGH);
      Serial.println("Commande recue: VERT ON");
    }
    else {
      Serial.print("Commande inconnue: ");
      Serial.println(command);
    }
  }
  delay(10); // Petit delai pour stabilite
}
