# Gestion professionnelle des releases, tests et collaboration

## Objectif
Déployer une méthodologie industrielle pour le projet de surveillance environnementale : releases versionnées, suivi des issues,
pipeline de tests unitaires/coverage et génération automatique de documentation API.

## 1. Politique de versionnement et releases

1. Créer une branche stabilisée (`release/x.y.z`) à partir de `main` lorsque l'ensemble des tests est vert.
2. Mettre à jour la version logique dans `common/proto/messages.h` (champ `PROTO_VERSION`) si le format change.
3. Générer un changelog signé via [`git-cliff`](https://github.com/orhun/git-cliff) :
   ```bash
   git cliff --config tools/git-cliff.toml --tag v1.0.0 > CHANGELOG.md
   ```
4. Taguer la release :
   ```bash
   git tag -a v1.0.0 -m "Sensor/HMI v1.0.0"
   git push origin v1.0.0
   ```
5. Publier le binaire signé depuis chaque projet :
   ```bash
   cd sensor_node
   idf.py set-target esp32s3
   idf.py build
   tar czvf sensor-node-v1.0.0.tar.gz build/sensor_node.bin build/bootloader/bootloader.bin build/partition_table/partition-table.bin
   ```
   Répéter pour `hmi_node`. Ajouter les archives aux releases GitHub/Gitea/GitLab.
6. Archiver le `sdkconfig`, les artefacts de coverage (`coverage.xml`) et le rapport Doxygen dans l'asset de release.

## 2. Gestion des issues et collaboration

- **Templates** : utiliser les gabarits disponibles dans `.github/ISSUE_TEMPLATE/` et `.github/PULL_REQUEST_TEMPLATE.md`.
  - `bug.md` exige la description, le contexte matériel/logiciel, les étapes de reproduction, ainsi qu'une checklist de logs et de configuration (`sdkconfig`) pour garantir la traçabilité.
  - `feature.md` structure la proposition autour du besoin métier, de la solution envisagée, de l'analyse d'impact et des critères d'acceptation (tests, documentation).
  - `documentation.md` cadre les corrections de guides avec la portée, les sources de vérité et une checklist de conformité rédactionnelle.
  - La `PULL_REQUEST_TEMPLATE.md` impose la checklist QA minimale (builds sensor/hmi, tests unitaires, couverture, docs) et l'identification des reviewers firmware/QA/docs.
- **Tri hebdomadaire** :
  - Assigner un label `priority/{P0,P1,P2}` et `area/{sensor,hmi,common}`.
  - Renseigner les étapes de reproduction, logs, version.
- **Lien commits/issues** : imposer la syntaxe `Fixes #123` ou `Refs #456` dans chaque commit mergeable.
- **Pull requests** :
  - Rebase obligatoire avant merge (`git pull --rebase origin main`).
  - Déclencher les jobs CI : `idf.py build` (sensor + hmi), `idf.py -T`, `tools/run_coverage.py`, `clang-tidy`, `doxygen`.
  - Exiger deux approbations techniques minimum (firmware + QA).
  - Bloquer le merge si la checklist suivante n'est pas cochée :
    1. ✅ Builds `idf.py build` (sensor/hmi)
    2. ✅ Tests unitaires (`idf.py -T`)
    3. ✅ Coverage `tools/run_coverage.py --xml coverage.xml`
    4. ✅ Documentation (`README`, schémas, Doxygen)

## 3. Tests unitaires et couverture (Unity + GCOV)

### 3.1. Activer l'instrumentation

1. Depuis `sensor_node/` :
   ```bash
   idf.py menuconfig
   # Component config → common_util → Common Components Options → Enable gcov instrumentation for shared components
   # Sensor Node Options → Enable gcov instrumentation for unit tests
   idf.py save-defconfig
   ```
2. Depuis `hmi_node/` :
   ```bash
   idf.py menuconfig
   # Component config → common_util → Common Components Options → Enable gcov instrumentation for shared components
   # HMI Node Options → Enable gcov instrumentation for unit tests
   idf.py save-defconfig
   ```
3. Nettoyer et recompiler chaque projet :
   ```bash
   idf.py fullclean
   idf.py -T        # exécute Unity sur cible ou simulateur
   ```

### 3.2. Générer les rapports GCOV

1. Installer gcovr sur l'hôte : `pip install gcovr`.
2. Lancer le script fourni (depuis la racine du dépôt) :
   ```bash
   python tools/run_coverage.py --xml coverage.xml --html coverage.html --summary coverage.txt
   ```
3. Le script agrège automatiquement les artefacts des répertoires `sensor_node/build` et `hmi_node/build`, génère `coverage_badge.svg`
   (badge SVG à publier dans la CI) et alimente les sorties TXT/HTML/XML.
4. Vérifier que le taux de couverture minimal est ≥ 80 % sur `sensor_node/main`, `hmi_node/main` et `common/`.

### 3.3. Lignes de tests critiques

- Pilotes I²C (SHT20, MCP23017, PCA9685) : simulations d'erreur via `i2c_mock`.
- Bus 1-Wire : `onewire_mock` pour conversion différée et CRC.
- Modèle de données : `tests/test_data_model.c` (seuils de publication, rotation des buffers).
- Protocole (`common/proto`) : CRC et sérialisation JSON/CBOR.

### 3.4. Banc d'intégration E2E (pytest + asyncio)

1. Créer/activer un environnement Python ≥ 3.10 :
   ```bash
   python -m pip install --upgrade pip
   pip install -r tests/e2e/requirements.txt
   ```
2. Lancer le banc de tests :
   ```bash
   pytest tests/e2e
   ```
3. Le banc simule les WebSockets `common/net` (provisionnement, CRC, commandes IO) et valide l'échange `sensor_node` ↔ `hmi_node` sans matériel.
4. Ajoutez l'étape `pytest tests/e2e/test_ws_handshake_vectors.py` à vos pipelines pour fixer les vecteurs HMAC/AES utilisés par
   `common/net/ws_security`.
5. Archiver le rapport `pytest` (`.github/workflows/ci.yml`) dans la CI pour tracer les régressions d'intégration protocolaire.

## 4. Documentation automatisée (Doxygen)

### 4.1. Générer la documentation

```bash
cd documentations/doxygen
doxygen Doxyfile
```

Les pages HTML sont produites dans `documentations/doxygen/output/html/index.html`.

### 4.2. Bonnes pratiques de commentaires

- Utiliser les balises `@brief`, `@param`, `@return`, `@note` dans les headers (`data_model.h`, `i2c_bus.h`, etc.).
- Préfixer les modules par un fichier `@file` décrivant le rôle global.
- Ajouter des diagrammes UML simples (PlantUML) via :

  ```plantuml
  @startuml
  ...
  @enduml
  ```

  Ces blocs sont interprétés par Doxygen lorsqu'une extension PlantUML est configurée dans `Doxyfile`.

## 5. Intégration continue recommandée

| Job | Commande | Artefacts |
|-----|----------|-----------|
| Build Sensor | `idf.py set-target esp32s3 && idf.py build` | binaires, map |
| Build HMI | `idf.py set-target esp32s3 && idf.py build` | binaires |
| Unit Tests | `idf.py -T` | log Unity |
| E2E Python | `pytest tests/e2e` | journal pytest |
| Coverage | `python tools/run_coverage.py --xml coverage.xml` | coverage.xml, coverage.html |
| Static Analysis | `clang-tidy -p build $(git ls-files '*.c')` | rapport clang-tidy |
| Docs | `doxygen documentations/doxygen/Doxyfile` | zip documentation |

## 6. Rétrocompatibilité firmware

- Conserver des tags spécifiques pour les cartes alternatives (ex. `v1.0.0-esp32s3-wroom`, `v1.0.0-esp32s3-usb-otg`).
- Documenter dans chaque release les paramètres `sdkconfig.defaults` divergents.
- Tester un firmware sur un échantillon minimum de 3 cartes par variante matérielle.

## 7. Politique OTA sécurisée

- **Validation firmware** : toute configuration `ota_update_config_t` doit fournir un callback `validate` qui vérifie la
  version minimale approuvée et compare le hash ELF (`app_elf_sha256`). Le callback reçoit la description de l'image en cours
  d'exécution et l'empreinte précédemment appliquée (persistée dans NVS) pour permettre le blocage des builds obsolètes ou
  inconnus.
- **Persistance NVS** : le composant écrit la dernière version validée dans `NVS` (`namespace` `ota_state`, clé
  `last_version`). Cette information est réutilisée côté callback pour refuser un downgrade et auditée lors des incidents.
- **Gestion du rollback** : à chaque démarrage de la tâche OTA, `esp_ota_mark_app_valid_cancel_rollback()` est exécuté et le
  statut est loggé. Les intégrateurs doivent surveiller ce log en recette afin de repérer un firmware encore marqué
  "UNVERIFIED".
- **Backoff et quotas** : les téléchargements sont tentés au maximum 5 fois par défaut, avec un backoff exponentiel
  (5 s → 10 s → 20 s ... jusqu'à 60 s). Ces valeurs sont configurables via `initial_backoff_ms`, `max_backoff_ms` et
  `max_retries` dans la structure de configuration.
- **Tests obligatoires** : exécuter `idf.py -T` et vérifier le rapport `common/ota/tests/test_ota_update.c` qui mocke
  `esp_https_ota` pour contrôler la validation, le stockage NVS et la stratégie de retry. Toute évolution du protocole OTA
  doit ajouter de nouveaux cas Unity (par exemple signature ECDSA, nonce serveur, etc.).

## 8. Rotation des certificats et gestion sécurisée des POP/jetons

- **Rotation TLS** :
  - Générer un nouveau triplet `server_cert.pem` / `server_key.pem` signé par une autorité racine mise à jour (`ca_cert.pem`).
  - Injecter les PEM dans NVS (namespace `cert_store`) via `nvs_partition_gen.py` ou flasher l'image SPIFFS `storage` (`0x414000`).
  - Vérifier au boot via les logs `cert_store` que les surcharges NVS/SPIFFS sont détectées (`Loaded ... override`).
  - Déployer simultanément les certificats clients/HMI pour éviter les coupures de service.
- **Hygiène des preuves de possession (POP) et jetons WebSocket** :
  - Les builds de production doivent laisser `CONFIG_SENSOR_ALLOW_PLACEHOLDER_SECRETS=n` et `CONFIG_HMI_ALLOW_PLACEHOLDER_SECRETS=n`.
  - Chaque release doit modifier `CONFIG_SENSOR_WS_AUTH_TOKEN`, `CONFIG_HMI_WS_AUTH_TOKEN`, `CONFIG_SENSOR_PROV_POP`, `CONFIG_HMI_PROV_POP`, `CONFIG_SENSOR_OTA_URL` et `CONFIG_HMI_OTA_URL` avec des valeurs uniques par environnement.
  - Archiver les secrets en coffre-fort (HashiCorp Vault, Azure Key Vault, etc.) et restreindre l'accès aux binaires générés.
  - Valider lors de la recette que les assertions de démarrage échouent si un firmware placeholder est flashé (protection contre les
    fuites de builds internes).

