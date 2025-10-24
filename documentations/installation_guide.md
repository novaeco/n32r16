# Guide d'installation ultra-détaillé

## Objectif
Mettre en place l'environnement complet pour compiler, flasher et valider le système de surveillance environnementale double-carte basé sur ESP32-S3. Ce guide couvre Linux, macOS et Windows, avec des instructions spécifiques pour optimiser les performances, garantir la sécurité et respecter les prérequis matériels.

## Prérequis matériels
- **Cartes cibles** :
  - Sensor Node : module ESP32-S3-WROOM-2-N32R16V (32 Mo flash OPI, 16 Mo PSRAM).
  - HMI Node : Waveshare ESP32-S3 Touch LCD 7B (1024×600) ou équivalent avec écran RGB + tactile GT911.
- **Interfaces de programmation** : câble USB-C certifié data (5 Gbps recommandé) pour chaque carte.
- **Alimentation stable** : port USB capable de délivrer 5 V / 1 A.
- **Accessoires** :
  - PC avec au minimum 16 Go RAM, 20 Go d'espace libre.
  - Accès réseau pour télécharger les dépendances.
  - Pour Windows : adaptateur USB-série CP210x/VCP avec drivers à jour.

## Prérequis logiciels
- Git ≥ 2.40.
- Python ≥ 3.10 avec `pip`.
- CMake ≥ 3.23 et Ninja ≥ 1.11 (installés automatiquement par ESP-IDF, mais vérifier pour les installations natives).
- Outils systèmes :
  - **Linux (Ubuntu/Debian)** : `build-essential`, `flex`, `bison`, `gperf`, `cmake`, `ninja-build`, `ccache`, `dfu-util`, `libusb-1.0-0-dev`, `python3-dev`.
  - **macOS** : Xcode Command Line Tools (`xcode-select --install`), Homebrew.
  - **Windows** : Microsoft Visual C++ Build Tools 2022, `choco` ou `winget` pour l'installation des dépendances.

## Étape 0 – Vérifier l'hôte
- **Systèmes supportés officiellement** : Ubuntu 22.04/24.04, Debian 12, macOS Ventura 13/ Sonoma 14, Windows 11 (x64) ou Windows 11 + WSL2 Ubuntu 22.04.
- **Virtualisation** : si vous utilisez VMware/VirtualBox, passez les périphériques USB en mode passthrough haute vitesse.
- **Espace disque** : prévoir 6 Go pour l'ESP-IDF, 4 Go pour les builds, 2 Go pour `ccache`.
- **Variables locales** : configurez le nom d'hôte (ex. `n32-lab01`) pour faciliter l'identification mDNS.
- **Synchronisation NTP** : indispensable pour TLS/OTA. Sous Linux : `timedatectl set-ntp true`. Sous Windows : `w32tm /resync`.

## Étape 1 – Préparer l'environnement ESP-IDF v5.5
### Option A : Conteneur Docker recommandé (reproductibilité maximale)
1. Installer Docker Desktop (Windows/macOS) ou Docker Engine (Linux).
2. Récupérer l'image officielle :
   ```bash
   docker pull espressif/idf:release-v5.5
   ```
3. Lancer un shell dans le conteneur avec montage du projet :
   ```bash
   docker run --rm -it \
     -v "$PWD":/workspace/n32r16 \
     -w /workspace/n32r16 \
     --device=/dev/ttyUSB0 \
     espressif/idf:release-v5.5 /bin/bash
   ```
   - Adapter `--device` pour chaque port série accessible (ex. `/dev/ttyACM0`, `COM3` via `--device=/dev/ttyS3` sur WSL2).
   - Ajouter `--env IDF_CCACHE_ENABLE=1` pour activer `ccache`.
   - Monter le cache Python pour accélérer les installations répétées : `-v "$HOME/.espressif":/root/.espressif`.
   - Pour le HMI nécessitant accès vidéo (LVGL simulateur), ajouter `--device /dev/dri` et `-e DISPLAY` si vous exécutez des outils graphiques.

### Option B : Installation native
#### Linux (Ubuntu 22.04+)
```bash
sudo apt update
sudo apt install -y git wget flex bison gperf cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0-dev python3 python3-pip python3-venv python3-dev
mkdir -p $HOME/esp
cd $HOME/esp
git clone -b v5.5 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
python3 -m venv $HOME/esp/idf_py_env
source $HOME/esp/idf_py_env/bin/activate
pip install --upgrade pip
pip install -r $IDF_PATH/requirements.txt
```

#### macOS (Ventura 13+)
```bash
xcode-select --install
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
brew install git wget cmake ninja python@3.11
mkdir -p $HOME/esp
cd $HOME/esp
git clone -b v5.5 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
python3 -m venv $HOME/esp/idf_py_env
source $HOME/esp/idf_py_env/bin/activate
pip install --upgrade pip
pip install -r $IDF_PATH/requirements.txt
```

#### Windows 11 (PowerShell, x64)
1. Installer [ESP-IDF Tools Installer v5.5](https://dl.espressif.com/dl/esp-idf/).
2. Cochez les composants : ESP-IDF v5.5, Git, Python 3.11, CMake, Ninja, OpenOCD.
3. À la fin, lancez l'ESP-IDF PowerShell prompt (menu Démarrer) pour obtenir l'environnement configuré.
4. Activez la persistance Python :
   ```powershell
   python -m venv $env:USERPROFILE\esp\idf_py_env
   $env:USERPROFILE\esp\idf_py_env\Scripts\Activate.ps1
   python -m pip install --upgrade pip
   python -m pip install -r $env:IDF_PATH\requirements.txt
   ```
5. Installez les drivers USB CP210x/CH9102 depuis Espressif si votre périphérique n'est pas reconnu.

### Vérification
Après installation (toutes plateformes) :
```bash
. $HOME/esp/esp-idf/export.sh   # ou "C:\\Espressif\\frameworks\\esp-idf-v5.5\\export.ps1" sous PowerShell
idf.py --version                 # doit afficher v5.5
python --version                 # >= 3.10
idf.py doctor                    # vérifie la toolchain et les dépendances
```

## Étape 2 – Cloner le projet
1. Choisir un dossier de travail :
   ```bash
   cd /path/to/workspace
   ```
2. Cloner le dépôt (adapter l'URL à votre forge Git) :
   ```bash
   git clone https://github.com/<organisation>/n32r16.git
   # ou
   git clone git@github.com:<organisation>/n32r16.git
   ```
3. Initialiser les sous-modules :
   ```bash
   cd n32r16
git submodule update --init --recursive
   ```
4. Vérifier que `components/`, `common/` et les projets `sensor_node/`, `hmi_node/` sont présents.
5. Configurer Git selon vos conventions (ex. `git config pull.rebase false`).
6. Inspecter rapidement l'arborescence pour comprendre la séparation des projets :
   ```bash
   tree -L 2
   ```
   - `sensor_node/` et `hmi_node/` contiennent chacun leur `sdkconfig.defaults` et `main/`.
   - `common/` regroupe les composants mutualisés (drivers I2C, stack protocolaire, certificats TLS).
   - `partitions/` héberge les tables `n32_default.csv` et variantes OTA.
   - `tools/` fournit les scripts Python auxiliaires.

## Étape 3 – Configuration de l'environnement
1. Exporter l'environnement ESP-IDF à chaque nouvelle session :
   ```bash
   . $HOME/esp/esp-idf/export.sh         # Bash/Zsh
   # ou
   C:\\Espressif\\frameworks\\esp-idf-v5.5\\export.ps1   # PowerShell
   ```
2. (Optionnel) Activer `ccache` pour accélérer les builds :
   ```bash
   export IDF_CCACHE_ENABLE=1
   export CCACHE_MAXSIZE=10G
   ```
3. Pour Linux, ajouter une règle udev afin d'accéder aux ports série sans sudo :
   ```bash
   sudo usermod -a -G dialout $USER
   sudo tee /etc/udev/rules.d/99-espressif.rules <<'RULE'
   SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", MODE="0660", GROUP="dialout", TAG+="uaccess"
   RULE
   sudo udevadm control --reload-rules
   sudo udevadm trigger
   ```
4. Synchroniser l'horloge système sur toutes les cartes (important pour TLS) :
   - Sensor Node : `idf.py monitor` puis commande `rtc sync` via CLI si disponible.
   - HMI Node : s'assurer que le serveur NTP défini dans `menuconfig` est joignable.
5. Vérifier que le port série est accessible :
   ```bash
   ls -l /dev/ttyUSB*
   ```
   ou sous Windows : `Get-CimInstance Win32_SerialPort | Select-Object DeviceID, Description`.

## Étape 4 – Configuration projet (optionnelle)
- Chaque application possède un `sdkconfig.defaults`. Pour personnaliser :
  ```bash
  cd sensor_node
  idf.py menuconfig
  # ajuster Wi-Fi POP, tokens Bearer, options OTA, etc.
  idf.py save-defconfig
  cd ../hmi_node
  idf.py menuconfig
  idf.py save-defconfig
  ```
- Conserver la cohérence des options mDNS (`CONFIG_HMI_SENSOR_HOSTNAME`) et des ports WebSocket/TLS.
- Vérifier les tailles de partitions :
  ```bash
  idf.py partition-table
  python $IDF_PATH/components/partition_table/parttool.py --partition-table-file partitions/n32_default.csv get_partition_info
  ```
- Synchroniser la version LVGL (`CONFIG_LVGL_VERSION_MASTER`) avec la branche `components/lvgl` si vous apportez des modifications locales.
- Sauvegarder les certificats TLS générés dans `components/cert_store/keys/` et mettre à jour le script `tools/ws_diagnostic.py` en conséquence.

## Étape 5 – Compilation
### Sensor Node
```bash
cd sensor_node
idf.py set-target esp32s3
idf.py build
```
- Le succès doit afficher `Project build complete`. Les binaires sont dans `build/` (`sensor_node.bin`, `bootloader.bin`, `partition-table.bin`).
- Générer un rapport de taille détaillé :
  ```bash
  idf.py size-components > build/size_sensor.txt
  ```
- Pour valider les dépendances optionnelles (LoRa, BLE), activez temporairement `CONFIG_SENSOR_ENABLE_LORA` ou `CONFIG_SENSOR_ENABLE_BLE` puis rebuild.

### HMI Node
```bash
cd ../hmi_node
idf.py set-target esp32s3
idf.py build
```
- Vérifier que `build/lvgl_app.bin` est généré et que la taille reste < 16 Mo.
- Inspecter la consommation PSRAM : `idf.py size-components --files build/map.json`.
- Si vous utilisez un pipeline d'assets externe (ex. `lvgl_asset_packer`), exécutez-le avant chaque build et committez les sources générées.

### Optimisations
- Lancer `idf.py fullclean` en cas de changement majeur de configuration.
- Pour builds parallèles : `idf.py build -j $(nproc)`.
- Utiliser `idf.py size-components` pour vérifier l'occupation mémoire.

## Étape 6 – Tests unitaires
- Sensor Node : `idf.py -T` exécute les tests driver (SHT20, DS18B20, MCP23017, PCA9685) et protocoles CRC.
- Common components :
  ```bash
  cd ../common
  idf.py -T
  ```
- HMI Node : tests LVGL/transport (mock) via `idf.py -T`.
- Surveiller les rapports `Unity` dans `build/utest/`. Corriger toute assertion échouée avant déploiement.
- Pour une couverture maximale, lancer `ctest --output-on-failure` dans `build/utest` et générer un rapport HTML via `gcovr` si activé.
- En CI, exporter `TEST_REPORT.xml` pour intégration GitLab/GitHub : `idf.py -T --output junit`.

## Étape 7 – Flash & Monitor
1. Identifier le port série :
   - Linux : `ls /dev/ttyUSB*` ou `dmesg | grep tty`.
   - macOS : `ls /dev/cu.usbserial*`.
   - Windows (PowerShell) : `Get-CimInstance Win32_SerialPort`.
2. Flasher :
   ```bash
   idf.py -p /dev/ttyUSB0 flash
   ```
3. Surveiller en direct :
   ```bash
   idf.py -p /dev/ttyUSB0 monitor
   ```
   - Quitter avec `Ctrl+]`. Ajouter `--baud 115200` si nécessaire.
4. Pour exécuter flash + monitor en une commande : `idf.py -p /dev/ttyUSB0 flash monitor`.
- Si vous flashez plusieurs cartes simultanément :
  ```bash
  idf.py -p /dev/ttyUSB0 -b 921600 flash &
  idf.py -p /dev/ttyUSB1 -b 921600 flash
  wait
  ```
- Vérifier l'intégrité après flash : `idf.py verify-app`.

## Étape 8 – Provisionnement Wi-Fi sécurisé
1. Télécharger l'application `ESP SoftAP Provisioning` (Android/iOS).
2. Alimenter le Sensor Node et l'HMI.
3. Se connecter au réseau SoftAP (`SENSOR-XXYYZZ` / `HMI-XXYYZZ`).
4. Fournir le POP (proof-of-possession) défini via `menuconfig` (`CONFIG_SENSOR_PROV_POP`).
5. Entrer le SSID et le mot de passe du réseau cible.
6. Vérifier dans la console série que la connexion Wi-Fi et le téléchargement OTA initial sont réussis.
- Pour automatiser le provisionnement en ligne de commande, adaptez l'exemple `esp_prov` fourni avec l'ESP-IDF (`examples/provisioning/esp_prov/`) en ciblant vos paramètres (`--ssid`, `--passphrase`, `--pop`).
- Valider la force du mot de passe via des outils internes (ex. `cracklib-check`) ou vos politiques SSI.

## Étape 9 – Vérifications post-installation
- Utiliser `tools/ws_diagnostic.py` pour tester la connectivité WebSocket/TLS :
  ```bash
  ./tools/ws_diagnostic.py <HOST> --token <CONFIG_HMI_WS_AUTH_TOKEN> --expect 3
  ```
- Confirmer :
  - Découverte mDNS (`sensor_node` annonce `_hmi-sensor._tcp`).
  - HMI affiche les cartes capteurs avec données en temps réel (latence < 200 ms).
  - CRC affichés en vert (pas d'erreur d'intégrité).
- Optionnel : lancer le simulateur HMI (si `LV_USE_SDL=1`) :
  ```bash
  cd hmi_node
  idf.py -DIDF_TARGET=linux-sdl build
  ./build/linux_simulator/main/linux_simulator
  ```
- Effectuer un test de charge WebSocket avec `websocat` : `websocat -n1 ws://<HOST>:443 --header="Authorization: Bearer <TOKEN>"`.
- Capturer des traces réseau via `tshark` pour vérifier TLS 1.3 : `tshark -i <iface> -Y "tls"`.

## Maintenance & Mises à jour
- Mettre à jour ESP-IDF :
  ```bash
  cd $HOME/esp/esp-idf
  git pull origin v5.5
  ./install.sh esp32s3
  ```
- Synchroniser le projet :
  ```bash
  cd /path/to/n32r16
  git pull --rebase
  idf.py build
  ```
- Purger les caches en cas d'erreur : `idf.py fullclean && rm -rf build/`.
- Auditer régulièrement les licences : `python $IDF_PATH/tools/idf_tools.py check-licenses` ou intégration SPDX.
- Mettre à jour les sous-modules : `git submodule update --remote --merge` (si vous avez des pointeurs vers des dépôts externes).
- Sauvegarder les artefacts de build (`build/*.bin`, `build/flash_args`) dans votre serveur CI (Artifactory/S3).

## Dépannage
| Symptomatique | Cause probable | Correctif |
|---------------|----------------|-----------|
| `Failed to connect to ESP32: No serial data received.` | Mauvais port / permissions. | Vérifier `lsusb`, ajouter utilisateur au groupe `dialout`, utiliser câble data. |
| Erreurs `ld: region 'dram0_0_seg' overflowed.` | Binaire trop volumineux. | Activer `CONFIG_COMPILER_OPTIMIZATION_SIZE`, désactiver les logs verbeux. |
| `MBEDTLS` handshake échoue | Certificats expirés. | Regénérer `components/cert_store`, re-flasher. |
| HMI figée | Task LVGL saturée. | Vérifier `CONFIG_LV_MEM_CUSTOM=1`, PSRAM activée, réduire la fréquence de rafraîchissement. |
| `idf.py doctor` signale une toolchain manquante | Export non fait ou PATH incorrect. | Re-exécuter `export.sh` / `export.ps1`, vérifier la variable `IDF_TOOLS_PATH`. |
| OTA échoue avec `403 Forbidden` | Jeton invalide. | Re-générer `CONFIG_SENSOR_AUTH_TOKEN`, relancer la synchronisation HMI. |
| `USB CDC` instable sur Windows | Mode économie d'énergie actif. | Désactiver la mise en veille USB dans le Gestionnaire de périphériques. |

## Résultat attendu
À l'issue de ce guide, vous devez être capable de :
- Compiler sans erreur les deux firmwares sous ESP-IDF v5.5.
- Flasher les cartes ESP32-S3 et monitorer les logs série.
- Provisionner la connexion Wi-Fi sécurisée et valider la communication WebSocket chiffrée.
- Dépanner rapidement les erreurs courantes via les outils fournis.

## Annexe A – Checklist rapide (résumé)
1. ✅ OS à jour + USB passthrough configuré.
2. ✅ ESP-IDF v5.5 installé + `idf.py doctor` sans erreur.
3. ✅ Dépôt cloné + sous-modules synchronisés.
4. ✅ `idf.py build` OK pour `sensor_node` et `hmi_node`.
5. ✅ Flash + monitor validés sur chaque carte.
6. ✅ Provisionnement Wi-Fi sécurisé et diagnostic WebSocket.
7. ✅ Sauvegarde des artefacts + documentation des versions.

