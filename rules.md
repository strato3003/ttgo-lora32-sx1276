# Règles et repères — LoRa 868 MHz (France)

Ce document résume des **repères pratiques** pour l’usage de ce dépôt. **Ce n’est pas un avis juridique** : seuls les textes officiels et les tableaux de fréquences (ARCEP, ANFR, ETSI, directive RED) font foi.

---

## 1. France : cadre général

- Les bandes **868 MHz « libres »** pour dispositifs **SRD / courte portée** relèvent de l’**harmonisation européenne** (souvent **ETSI EN 300 220**, **ERC/REC 70-03**, **directive RED**), avec les **règles nationales d’affectation** (France : **ARCEP**, **ANFR**).
- Les limites portent en général sur :
  - la **sous-bande de fréquence** (chaque tranche peut avoir un **plafond d’ERP/EIRP** différent) ;
  - le **duty cycle** (ex. 1 % ou 0,1 % selon cas) ;
  - parfois la **densité spectrale** ou d’autres contraintes selon le type d’application.

**Liens utiles (à consulter pour les valeurs exactes)** :

- [ARCEP — bandes libres / courte portée](https://www.arcep.fr) (recherche sur le site : « bandes libres », « courte portée »).
- [ANFR](https://www.anfr.fr) — documents et tableaux SRD.
- **ERC/REC 70-03** (CEPT) — recommandation européenne des sous-bandes SRD.
- **ETSI EN 300 220** — exigences techniques types pour SRD.

---

## 2. Fréquence **866 MHz** (`866E6` dans les sketches)

- **866 MHz** se situe dans la grande plage **865–868 MHz** (famille SRD/ISM UE). Ce n’est **pas** l’un des trois canaux **LoRaWAN EU868** les plus cités (**868,1 / 868,3 / 868,5 MHz**), mais cela peut rester dans une ligne du tableau **si** ton usage correspond à l’application autorisée à cet endroit précis du spectre.
- **Puissance** : pour beaucoup de lignes SRD dans cette zone, l’ordre d’idée **souvent invoqué** est **~25 mW ERP** (≈ **+14 dBm ERP**). Les sous-bandes autour de **~869,4 MHz** peuvent autoriser des ERP **beaucoup plus élevés** dans certains cas, mais en général avec un **duty cycle très strict** (souvent **0,1 %**) — **à vérifier dans le tableau pour la fréquence exacte**.
- **Ne pas confondre** : la limite est en **ERP** (puissance apparente rayonnée) : **gain d’antenne** et pertes comptent. `setTxPower` sur le module n’est pas équivalent à l’ERP au bord du terrain sans tenir compte de l’antenne.

---

## 3. Puissance et lien avec **SF** / **BW**

- **Réglementairement** : en général **pas** une règle du type « si SF = 12 alors P_max = X ». Tu as un **plafond** pour la sous-bande / l’usage, et tu peux émettre **moins fort**.
- **Techniquement** : SF et BW changent surtout **sensibilité**, **débit** et **temps d’occupation** ; la réglementation te contraint surtout par **ERP** et **duty cycle**. Monter la puissance **n’augmente pas** le débit ; ça augmente la **marge de liaison** jusqu’au plafond légal.

---

## 4. Duty cycle et ce dépôt (`DUTY_CYCLE_PERCENT`)

- Le firmware calcule un **intervalle minimal** entre envois à partir du **temps en air (ToA)** et de `DUTY_CYCLE_PERCENT` (voir README).
- La **valeur légale** (1 %, 0,1 %, etc.) dépend de **ta sous-bande** et du **type d’émission** : aligne `DUTY_CYCLE_PERCENT` sur la ligne du tableau qui s’applique à **ta fréquence** et à ton usage.

---

## 5. Suite de commandes **CFG** (du plus « rapide / courte portée » vers le plus « lent / longue portée »)

Sur le moniteur série du **receiver** : `CFG <SF> <BW_kHz> <P_dBm>` (BW autorisés dans le firmware : **125**, **250**, **500**).

Ordre **simple** pour balayer proprement :

1. `CFG 7 500 <P>`  
2. `CFG 7 250 <P>`  
3. `CFG 7 125 <P>`  
4. Puis monter le SF à **BW 125** : `CFG 8 125 <P>` … jusqu’à `CFG 12 125 <P>`.

Garde **P constant et légal** pour comparer surtout **SF/BW** ; utilise le **RSSI** (et le taux de réception) pour classer les combinaisons.

**Timing** : envoyer `CFG` pendant la **fenêtre RX** de l’émetteur (~1,2 s après sa dernière émission). Le receiver applique aussi la config **localement** après avoir envoyé le paquet, pour décoder les télémesures suivantes.

---

## 6. NVS (Non-Volatile Storage) sur ESP32

- **NVS** : zone **flash** pour petites données **clé/valeur**, **persistantes** après reboot (accès courant via **`Preferences`** en Arduino ESP32).
- Dans ce projet, l’**émetteur** peut sauvegarder **SF / BW / P** en NVS après une commande reçue (namespace typique `"lora"`). Le **receiver** peut n’avoir que la config **en RAM** selon la version du sketch ; vérifier le code si besoin de persistance symétrique.

---

## 7. Sécurité et usage expérimental

- Le protocole de **configuration à distance** (paquet CFG) est **sans authentification** : tout équipement sur la même fréquence / même mot de synchronisation LoRa peut théoriquement envoyer des commandes.
- Respecte les **zones sensibles** et toute **limitation locale** (documents officiels).

---

*Dernière mise à jour : contenu de référence pour ce dépôt ; vérifier les textes officiels avant tout déploiement.*
