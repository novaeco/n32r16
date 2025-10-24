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
```

#### Windows 11 (PowerShell, x64)
1. Installer [ESP-IDF Tools Installer v5.5](https://dl.espressif.com/dl/esp-idf/).
2. Cochez les composants : ESP-IDF v5.5, Git, Python 3.11, CMake, Ninja, OpenOCD.
3. À la fin, lancez l'ESP-IDF PowerShell prompt (menu Démarrer) pour obtenir l'environnement configuré.

### Vérification
Après installation (toutes plateformes) :
```bash
. $HOME/esp/esp-idf/export.sh   # ou "C:\\Espressif\\frameworks\\esp-idf-v5.5\\export.ps1" sous PowerShell
idf.py --version                 # doit afficher v5.5
python --version                 # >= 3.10
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

## Étape 5 – Compilation
### Sensor Node
```bash
cd sensor_node
idf.py set-target esp32s3
idf.py build
```
- Le succès doit afficher `Project build complete`. Les binaires sont dans `build/` (`sensor_node.bin`, `bootloader.bin`, `partition-table.bin`).

### HMI Node
```bash
cd ../hmi_node
idf.py set-target esp32s3
idf.py build
```
- Vérifier que `build/lvgl_app.bin` est généré et que la taille reste < 16 Mo.

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

## Étape 8 – Provisionnement Wi-Fi sécurisé
1. Télécharger l'application `ESP SoftAP Provisioning` (Android/iOS).
2. Alimenter le Sensor Node et l'HMI.
3. Se connecter au réseau SoftAP (`SENSOR-XXYYZZ` / `HMI-XXYYZZ`).
4. Fournir le POP (proof-of-possession) défini via `menuconfig` (`CONFIG_SENSOR_PROV_POP`).
5. Entrer le SSID et le mot de passe du réseau cible.
6. Vérifier dans la console série que la connexion Wi-Fi et le téléchargement OTA initial sont réussis.

## Étape 9 – Vérifications post-installation
- Utiliser `tools/ws_diagnostic.py` pour tester la connectivité WebSocket/TLS :
  ```bash
  ./tools/ws_diagnostic.py <HOST> --token <CONFIG_HMI_WS_AUTH_TOKEN> --expect 3
  ```
- Confirmer :
  - Découverte mDNS (`sensor_node` annonce `_hmi-sensor._tcp`).
  - HMI affiche les cartes capteurs avec données en temps réel (latence < 200 ms).
  - CRC affichés en vert (pas d'erreur d'intégrité).

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

## Dépannage
| Symptomatique | Cause probable | Correctif |
|---------------|----------------|-----------|
| `Failed to connect to ESP32: No serial data received.` | Mauvais port / permissions. | Vérifier `lsusb`, ajouter utilisateur au groupe `dialout`, utiliser câble data. |
| Erreurs `ld: region 'dram0_0_seg' overflowed.` | Binaire trop volumineux. | Activer `CONFIG_COMPILER_OPTIMIZATION_SIZE`, désactiver les logs verbeux. |
| `MBEDTLS` handshake échoue | Certificats expirés. | Regénérer `components/cert_store`, re-flasher. |
| HMI figée | Task LVGL saturée. | Vérifier `CONFIG_LV_MEM_CUSTOM=1`, PSRAM activée, réduire la fréquence de rafraîchissement. |

## Résultat attendu
À l'issue de ce guide, vous devez être capable de :
- Compiler sans erreur les deux firmwares sous ESP-IDF v5.5.
- Flasher les cartes ESP32-S3 et monitorer les logs série.
- Provisionner la connexion Wi-Fi sécurisée et valider la communication WebSocket chiffrée.
- Dépanner rapidement les erreurs courantes via les outils fournis.

