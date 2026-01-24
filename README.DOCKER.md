# Compilation Windows avec Docker

Ce guide explique comment utiliser Docker pour tester la compilation Windows localement, sans avoir besoin d'un système Windows.

## Comment ça fonctionne ?

**Ce n'est PAS Wine !** On utilise **MSYS2/MinGW** qui est un environnement de **compilation croisée** (cross-compilation) :

- ✅ **Compilation croisée** : Compile des binaires Windows (.exe) natifs depuis Linux
- ✅ **Pas d'émulation** : Les binaires sont compilés directement pour Windows, pas via Wine
- ✅ **Outils natifs** : GCC/MinGW produit des exécutables Windows réels
- ✅ **Même résultat** : Les binaires sont identiques à ceux compilés sur Windows

Le conteneur Docker utilise l'image `msys2/msys2` qui contient l'environnement MinGW complet avec tous les outils nécessaires pour compiler des applications Windows.

## Prérequis

- Docker installé et fonctionnel
- Git (pour cloner les submodules)

## Construction de l'image

Construire l'image Docker une fois :

```bash
./docker-build-windows.sh
```

Ou manuellement :

```bash
docker build -f Dockerfile.windows -t imsidplayer-windows .
```

## Utilisation

### Shell interactif

Pour lancer un shell interactif dans le conteneur :

```bash
./docker-run-windows.sh
```

Le répertoire courant sera monté dans `/workspace` dans le conteneur.

### Compilation manuelle

Une fois dans le conteneur :

```bash
# Activer l'environnement MinGW
export PATH="/mingw64/bin:/usr/bin:$PATH"
export SIDPLAYFP_ROOT=/mingw64

# Aller dans le répertoire de travail
cd /workspace

# Initialiser les submodules si nécessaire
git submodule update --init --recursive

# Créer le venv Python pour MBed TLS
python -m venv venv
source venv/bin/activate
pip install --upgrade pip
pip install "jsonschema>=3.0.0,<4.0.0"

# Configurer CMake
mkdir -p build
cmake -B build -S . \
  -G "MinGW Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_CLOUD_SAVE=ON \
  -DPython3_EXECUTABLE="$(pwd)/venv/bin/python"

# Compiler
cmake --build build --config Release -j$(nproc)
```

### Compilation en une commande

```bash
docker run -it -v $(pwd):/workspace imsidplayer-windows bash -c '
  export PATH="/mingw64/bin:/usr/bin:$PATH"
  export SIDPLAYFP_ROOT=/mingw64
  cd /workspace
  git submodule update --init --recursive
  python -m venv venv
  source venv/bin/activate
  pip install --upgrade pip
  pip install "jsonschema>=3.0.0,<4.0.0"
  mkdir -p build
  cmake -B build -S . -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DENABLE_CLOUD_SAVE=ON -DPython3_EXECUTABLE="$(pwd)/venv/bin/python"
  cmake --build build --config Release
'
```

## Avantages

- ✅ Tester la compilation Windows sans avoir Windows
- ✅ Reproduire l'environnement GitHub Actions localement
- ✅ Déboguer les problèmes de build plus facilement
- ✅ Environnement isolé et reproductible

## Notes

- L'image est basée sur `msys2/msys2:latest` qui contient MSYS2/MinGW
- Toutes les dépendances sont pré-installées dans l'image
- Le répertoire source est monté en volume, donc les modifications sont visibles immédiatement
- Les binaires compilés seront dans `build/bin/` sur votre machine hôte
