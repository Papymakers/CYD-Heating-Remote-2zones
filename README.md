# CYD Heating Remote — 2 zones

**Télécommande tactile murale pour chauffage électrique fil pilote 2 zones**  
Projet open-source — [Papy Makers](https://github.com/Papymakers)

---

## Présentation

Ce projet transforme un module **ESP32-2432S028R** (alias *Cheap Yellow Display* — CYD)
en télécommande murale connectée pour piloter un système de chauffage électrique
par **fil pilote** sur **2 zones indépendantes**, via MQTT.

L'interface est organisée en **deux pages** accessibles par simple toucher d'écran.

---

## Page 0 — Tableau de bord

Page d'accueil affichée en permanence. **Toucher n'importe où** bascule vers la page commandes.

```
┌─────────────────────────────────────────┐
│  23:16        Dim 07/06          WiFi   │
│─────────────────────────────────────────│
│  Aujourd'hui:    Jour BLEU              │
│  Demain:         Jour BLANC             │
│─────────────────────────────────────────│
│  Ext.   18.5°C     Courant   12 A       │
│  Zone 1 21.3°C     Cmd Z1    ECO        │
│  Zone 2 20.1°C     Cmd Z2    HG         │
│─────────────────────────────────────────│
│       ⚙  Toucher pour commander         │
└─────────────────────────────────────────┘
```

### Informations affichées

| Zone | Information | Source MQTT |
|---|---|---|
| En-tête | Heure `HH:MM` | `astroClock/#` |
| En-tête | Date `Jjj JJ/MM` | `astroClock/#` |
| En-tête | Indicateur WiFi | — |
| Tempo | Aujourd'hui | `newHeating/linkyValues` |
| Tempo | Demain | `newHeating/linkyValues` |
| Mesures | Temp. extérieure / Zone 1 / Zone 2 | `ecowittDatas` |
| Mesures | Courant Iinst | `newHeating/IinstVal` |
| Commandes | Cmd active Zone 1 / Zone 2 | `newHeating/cmdStatus` |

### Codes couleur Tempo

| Période reçue | Texte affiché | Couleur |
|---|---|---|
| `HPJR` / `HCJR` | Jour ROUGE | Rouge `#C00000` |
| `HPJB` / `HCJB` | Jour BLEU | Bleu `#0040C0` |
| `HPJW` / `HCJW` | Jour BLANC | Gris `#505050` |

---

## Page 1 — Commandes chauffage

Accessible par toucher depuis la page 0. Retour automatique après **3 s**
ou via le bouton **Retour**. Timeout **30 s** sans action.

```
┌─────────────────────────────────────────┐
│         Commandes chauffage             │
│                                         │
│  ZONE 1                                 │
│  ┌───────┬───────┬───────┬───────┬────┐ │
│  │   S   │  HG   │   E   │   C   │ -2 │ │
│  ├───────┼───────┼───────┼───────┼────┤ │
│  │   S   │  HG   │   E   │   C   │ -2 │ │
│  └───────┴───────┴───────┴───────┴────┘ │
│                                  ZONE 2 │
│              [ ← Retour ]               │
└─────────────────────────────────────────┘
```

### Commandes publiées

| Bouton | Mode fil pilote | Topic | JSON |
|---|---|---|---|
| **S** | Arrêt | `heatingCmd` | `{"zone":N,"cmd":"STOP"}` |
| **HG** | Hors-gel | `heatingCmd` | `{"zone":N,"cmd":"HG"}` |
| **E** | Économique | `heatingCmd` | `{"zone":N,"cmd":"ECO"}` |
| **C** | Confort | `heatingCmd` | `{"zone":N,"cmd":"CONF"}` |
| **-2** | Confort −2°C | `heatingCmd` | `{"zone":N,"cmd":"293S"}` |

### Sécurité anti-commandes fantômes

- Filtre strict `LV_EVENT_VALUE_CHANGED`
- Garde temporelle **800 ms** après chargement de la page
- `lv_buttonmatrix_set_one_checked(false)`
- `LV_BUTTONMATRIX_BUTTON_NONE` vérifié avant publication

---

## Topics MQTT

| Topic | Dir. | Format JSON |
|---|---|---|
| `ecowittDatas` | RX | `{"tempExt":18.5,"tempZ1":21.3,"tempZ2":20.1}` |
| `astroClock/#` | RX | `{"hours":23,"minutes":16,"day":7,"month":6,"weekday":7}` |
| `newHeating/linkyValues` | RX | `{"PTEC":"HPJB","DEMAIN":"----"}` |
| `newHeating/IinstVal` | RX | `{"Iinst":12,"overload":false}` |
| `newHeating/cmdStatus` | RX | `{"zone":1,"cmd":"ECO"}` |
| `heatingCmd` | TX | `{"zone":1,"cmd":"ECO"}` |

---

## Firmware

### Dépendances (Arduino IDE)

| Bibliothèque | Version testée |
|---|---|
| LVGL | 9.x |
| TFT_eSPI | ≥ 2.5 |
| XPT2046_Touchscreen | ≥ 1.4 |
| ArduinoJson | ≥ 7.x |
| PubSubClient | ≥ 2.8 |

### Partition Arduino IDE

`Outils → Partition Scheme → Huge APP (3MB No OTA / 1MB SPIFFS)`

### Paramètres à adapter

```cpp
static const char* SSID        = "votre_ssid";
static const char* WIFI_PASS   = "votre_mot_de_passe";
static const char* MQTT_SERVER = "adresse_ip_broker";
static const char* CLIENT_ID   = "cydRemote01";
```

---

## Hardware

Voir [`hardware/README.md`](hardware/README.md) pour :
- Description du module CYD
- Modification optionnelle strap IO22 (contrôle PWM luminosité)
- Façade avant et boîtier encastrable

---

## Commander

Ce projet est le fruit de plusieurs années de développement, de prototypage
et de tests en conditions réelles. Le firmware est open source — si vous
souhaitez soutenir le projet ou gagner du temps, les éléments sont disponibles
à la commande.

| Option | Contenu | Prix indicatif |
|---|---|---|
| **Façade seule** | Façade avant 86×86mm | 8 € |

Frais de port en sus. Expédition depuis la France.

📧 Commandes et questions : support@papymakers.com — [papymakers.com](https://papymakers.com)

---

## Contact & Support

- **Bug / question technique** → ouvrir une [Issue](../../issues)
- **Commandes** → support@papymakers.com
- **Discussions générales** → onglet [Discussions](../../discussions)

---

## Projets associés

| Projet | Description |
|---|---|
| [esp32-cyd-home-monitor](https://github.com/Papymakers/esp32-cyd-home-monitor) | Afficheur domotique simple |
| [Alimentation-230VAC-5V-9V-DC-5W](https://github.com/Papymakers/Alimentation-230VAC-5V-9V-DC-5W) | Cartes d'alimentation compatibles |

---

## Licence

MIT — voir [`LICENSE`](LICENSE)

## Auteur

**Papy Makers** — Normandie, France  
[github.com/Papymakers](https://github.com/Papymakers)
