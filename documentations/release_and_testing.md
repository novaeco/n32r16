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

- **Templates** : créer des modèles `ISSUE_TEMPLATE/*.md` et `PULL_REQUEST_TEMPLATE.md` (non inclus ici) couvrant bug, feature, documentation.
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
   # Component config → Sensor Node Options → Enable gcov instrumentation for unit tests
   idf.py save-defconfig
   ```
2. Nettoyer et recompiler :
   ```bash
   idf.py fullclean
   idf.py -T        # exécute Unity sur cible ou simulateur
   ```

### 3.2. Générer les rapports GCOV

1. Installer gcovr sur l'hôte : `pip install gcovr`.
2. Lancer le script fourni :
   ```bash
   python tools/run_coverage.py --build-dir sensor_node/build --xml coverage.xml --html coverage.html
   ```
3. Publier `coverage.xml` dans la CI (GitLab/GitHub) pour le badge.
4. Vérifier que le taux de couverture minimal est ≥ 80 % sur `sensor_node/main` et `common/`.

### 3.3. Lignes de tests critiques

- Pilotes I²C (SHT20, MCP23017, PCA9685) : simulations d'erreur via `i2c_mock`.
- Bus 1-Wire : `onewire_mock` pour conversion différée et CRC.
- Modèle de données : `tests/test_data_model.c` (seuils de publication, rotation des buffers).
- Protocole (`common/proto`) : CRC et sérialisation JSON/CBOR.

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
| Coverage | `python tools/run_coverage.py --xml coverage.xml` | coverage.xml, coverage.html |
| Static Analysis | `clang-tidy -p build $(git ls-files '*.c')` | rapport clang-tidy |
| Docs | `doxygen documentations/doxygen/Doxyfile` | zip documentation |

## 6. Rétrocompatibilité firmware

- Conserver des tags spécifiques pour les cartes alternatives (ex. `v1.0.0-esp32s3-wroom`, `v1.0.0-esp32s3-usb-otg`).
- Documenter dans chaque release les paramètres `sdkconfig.defaults` divergents.
- Tester un firmware sur un échantillon minimum de 3 cartes par variante matérielle.

