# Changelog — CYD-Heating-Remote-2zones

Format : [Keep a Changelog](https://keepachangelog.com/fr/1.0.0/)

---

## [1.0.0] — 2026-06-08

### Ajouté

**Page 0 — Tableau de bord :**
- Heure `HH:MM` et date `Jjj JJ/MM` depuis `astroClock/#`
  (parsing entiers `hours`/`minutes`/`day`/`month`/`weekday` ISO 1–7)
- Période Tempo EDF aujourd'hui / demain — fond coloré rouge/bleu/gris
- Températures Ext. / Zone 1 / Zone 2 depuis `ecowittDatas`
- Courant instantané Iinst — texte orange si `overload: true`
- Commande active Zone 1 et Zone 2 depuis `newHeating/cmdStatus`
- Indicateur WiFi (label vert, masqué si déconnecté)
- Fond noir `#101018`, palette cohérente fond sombre

**Page 1 — Commandes :**
- Buttonmatrix STOP / HG / ECO / CONF / −2 × 2 zones
- Publication JSON `{"zone":N,"cmd":"XXX"}` sur `heatingCmd`
- Retour automatique page 0 après **3 s** (non bloquant)
- Bouton Retour explicite
- Timeout 30 s sans action → retour page 0
- Label ZONE 1 au-dessus de la buttonmatrix
- Label ZONE 2 positionné sous la buttonmatrix

**Sécurité anti-commandes fantômes :**
- Filtre `LV_EVENT_VALUE_CHANGED` strict
- Garde temporelle 800 ms après `lv_scr_load(pageCmd)`
- `lv_buttonmatrix_set_one_checked(false)`
- `LV_BUTTONMATRIX_BUTTON_NONE` vérifié avant publication

**MQTT :**
- Reconnexion non bloquante (intervalle 2 s)
- `setKeepAlive(60)`, WDT reset avant tentative
- Parsing robuste `.is<int>()` / `.is<const char*>()` / `.is<bool>()`

**Système :**
- Watchdog matériel 10 s
- WiFi reconnexion non bloquante (intervalle 10 s)
- Rétroéclairage PWM GPIO 22

### Problèmes résolus

- Commandes nocturnes fantômes → garde 800 ms
- `LV_BUTTONMATRIX_BTN_NONE` → `LV_BUTTONMATRIX_BUTTON_NONE` (LVGL v9)
- Topic `tempoValues` → `newHeating/linkyValues`, clés `PTEC`/`DEMAIN` majuscules
- Topic `Monitoring` → `newHeating/IinstVal`
- Heure depuis `astroClock/#` (int) au lieu de `clockDatas` (string)

---

## [À venir]

- Dégradé couleur température extérieure
- Adaptation rétroéclairage via LDR (GPIO 34)
- Affichage Sunrise / Sunset en page 0
- OTA (Over The Air update)
