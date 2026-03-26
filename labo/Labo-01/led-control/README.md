# Exercice 7.6 - Contrôle de LEDs via Port Série

## Objectif
Mettre en place un circuit avec deux LEDs (rouge et verte) contrôlables via des commandes envoyées depuis l'interface tactile sur le port série.

## Demo complète (écran + toutes LEDs + boutons + potentiomètre)

Une version complète est maintenant disponible avec:

- `demo_leds_boutons_pot.ino` (ESP32/LilyGO)
- `touch_ui_demo.py` (interface tactile Raspberry Pi)
- `launch_demo.sh` (script de lancement TTY1)

### Mapping matériel utilisé

- LEDs: BLEU=`GPIO19`, VERT=`GPIO23`, JAUNE=`GPIO21`, ROUGE=`GPIO22`
- Bouton mode: `GPIO18` (appui = LOW, `INPUT_PULLUP`)
- Bouton action: `GPIO39` (appui = LOW, entrée seulement)
- Potentiomètre: `GPIO36` (ADC)

### Compilation / téléversement de la version complète

```bash
cd ~/243-4J5-LI/labo/Labo-01/led-control
arduino-cli compile --fqbn esp32:esp32:esp32 demo_leds_boutons_pot.ino
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32 demo_leds_boutons_pot.ino
```

### Lancement de l'interface tactile

```bash
cd ~/243-4J5-LI/labo/Labo-01/led-control
sudo ./launch_demo.sh
```

### Contrôles disponibles dans l'UI

- `MODE -` / `MODE +` : change le mode d'animation
- `PAUSE/RESUME` : stop/reprend l'animation
- `STATUS` : lit l'état (`mode`, `pot`, `stepMs`, `run`)
- `QUIT` (ou touche `q`) : ferme l'interface

## Architecture du système

```
[Interface Tactile] → [Port Série USB] → [LilyGO A7670G] → [LEDs Rouge/Verte]
   (Raspberry Pi)      (/dev/ttyUSB0)        (ESP32)           (GPIO 25/26)
```

## Matériel requis

- LilyGO A7670G
- Plaquette de prototypage (breadboard)
- 1 LED rouge
- 1 LED verte
- 2 résistances 220Ω ou 330Ω (pour limiter le courant)
- Fils de connexion (jumper wires)
- Câble USB-A vers USB-C

## Montage du circuit

### Connexions des LEDs

**LED Rouge:**
- **Anode (+)** → Résistance 220Ω → **GPIO 25** du LilyGO
- **Cathode (-)** → **GND** du LilyGO

**LED Verte:**
- **Anode (+)** → Résistance 220Ω → **GPIO 26** du LilyGO
- **Cathode (-)** → **GND** du LilyGO

### Schéma de branchement

```
LilyGO A7670G
┌─────────────────┐
│                 │
│  GPIO 25 ○──────┼──[220Ω]──→|──┐  (LED Rouge)
│                 │              │
│  GPIO 26 ○──────┼──[220Ω]──→|──┤  (LED Verte)
│                 │              │
│  GND     ○──────┼──────────────┘
│                 │
└─────────────────┘
```

### Notes importantes

1. **Identification de la LED:**
   - La patte longue = Anode (+)
   - La patte courte = Cathode (-)
   - Le côté plat du boîtier = Cathode (-)

2. **Résistances de limitation:**
   - Les résistances de 220Ω à 330Ω protègent les LEDs
   - Sans résistance, les LEDs peuvent brûler!
   - Tension nominale LED: ~2V, courant: ~20mA

3. **Modification des pins:**
   - Si GPIO 25 ou 26 ne sont pas disponibles, vous pouvez utiliser d'autres pins
   - Modifiez alors les définitions dans `led-control.ino`:
     ```cpp
     #define LED_ROUGE 25  // Changez ce numéro
     #define LED_VERTE 26  // Changez ce numéro
     ```

## Compilation et téléversement

### 1. Compiler le sketch Arduino

```bash
cd ~/243-4J5-LI/labo1/led-control
arduino-cli compile --fqbn esp32:esp32:esp32 led-control.ino
```

### 2. Téléverser vers le LilyGO

```bash
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 led-control.ino
```

### 3. Tester avec le moniteur série (optionnel)

```bash
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

Dans le moniteur série, tapez:
- `rouge` → LED rouge s'allume, verte s'éteint
- `vert` → LED verte s'allume, rouge s'éteint
- `off` → Toutes les LEDs s'éteignent

**Quitter:** `Ctrl+C`

## Test avec l'interface tactile

### 1. Fermer le moniteur série

Si le moniteur série est ouvert, fermez-le avec `Ctrl+C`. Le port série ne peut être utilisé que par un programme à la fois.

### 2. Lancer l'interface tactile

```bash
sudo chvt 1
sudo setsid sh -c 'exec </dev/tty1 >/dev/tty1 2>&1 python3 /home/fpoisson/243-4J5-LI/labo1/led-control/touch_ui_led.py'
```

### 3. Utiliser les boutons

- Appuyez sur le bouton **ROUGE** → LED rouge s'allume
- Appuyez sur le bouton **VERT** → LED verte s'allume
- Appuyez sur **QUIT** ou tapez `q` pour quitter

### 4. Vérifier le statut

Le message de statut en bas de l'écran affiche:
- La commande envoyée
- L'état de la connexion série
- Les erreurs éventuelles

## Dépannage

### Le port série n'est pas trouvé

```bash
# Lister les ports disponibles
ls -la /dev/tty*

# Vérifier que le LilyGO est bien connecté
arduino-cli board list
```

### Permission refusée sur /dev/ttyUSB0

```bash
# Ajouter votre utilisateur au groupe dialout
sudo usermod -a -G dialout $USER

# Déconnectez-vous et reconnectez-vous, puis vérifiez
groups
```

### Les LEDs ne s'allument pas

1. **Vérifier le circuit:**
   - Les résistances sont-elles bien placées?
   - Les LEDs sont-elles dans le bon sens?
   - Les connexions GND sont-elles bonnes?

2. **Tester avec le moniteur série:**
   - Envoyez `rouge` manuellement
   - Vérifiez les messages de debug

3. **Vérifier les pins:**
   - GPIO 25 et 26 sont-ils disponibles sur votre LilyGO?
   - Testez avec d'autres pins si nécessaire

### L'interface tactile ne répond pas

1. **Vérifier que le touchscreen est détecté:**
   ```bash
   sudo evtest
   ```

2. **Vérifier que le port série n'est pas occupé:**
   ```bash
   sudo lsof /dev/ttyUSB0
   ```

## Améliorations possibles

1. **Ajout d'états:**
   - Afficher l'état actuel des LEDs sur l'interface
   - Indicateur visuel sur les boutons

2. **Bouton OFF:**
   - Ajouter un bouton pour éteindre toutes les LEDs

3. **Lecture du port série:**
   - Lire les messages retournés par le LilyGO
   - Afficher les confirmations dans l'interface

4. **Plus de LEDs:**
   - Ajouter d'autres couleurs
   - Créer des patterns de clignotement

## À remettre

- [ ] Photo du circuit sur la plaquette de prototypage
- [ ] Photo de l'interface tactile avec les boutons ROUGE et VERT
- [ ] Code Python modifié (touch_ui_led.py)
- [ ] Code Arduino (led-control.ino)
- [ ] Photo montrant les LEDs allumées lors d'un test
- [ ] Tous les fichiers dans votre dépôt Git

## Commandes Git pour sauvegarder

```bash
cd ~/243-4J5-LI/labo1/led-control
git add .
cd ~/243-4J5-LI
git commit -m "Ajout du contrôle de LEDs via interface tactile et port série (Exercice 7.6)"
git push origin prenom-nom/labo1
```
