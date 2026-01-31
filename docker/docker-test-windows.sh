#!/bin/bash
# Script pour tester la compilation Windows avec Podman/Docker localement via cross-compilation
# Usage: ./docker/docker-test-windows.sh

set -e

# Se placer à la racine du projet
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

IMAGE_NAME="imsidplayer-windows"
DOCKERFILE="docker/Dockerfile.windows"

# Détecter podman ou docker
if command -v podman &> /dev/null; then
    CONTAINER_CMD="podman"
    echo "✅ Utilisation de Podman"
elif command -v docker &> /dev/null; then
    CONTAINER_CMD="docker"
    echo "✅ Utilisation de Docker"
else
    echo "❌ Ni Podman ni Docker n'est installé. Installez l'un des deux."
    exit 1
fi

echo "=== Test de compilation Windows avec $CONTAINER_CMD ==="
echo ""

# Construire l'image si elle n'existe pas ou forcer la reconstruction si nécessaire
echo "📦 Vérification/Construction de l'image (cela peut prendre plusieurs minutes)..."
$CONTAINER_CMD build -f "$DOCKERFILE" -t "$IMAGE_NAME" .

echo ""
echo "🧪 Lancement de la compilation dans le conteneur..."
echo ""

# Exécuter la compilation dans le conteneur
$CONTAINER_CMD run --rm \
  -v "$(pwd):/workspace" \
  -w /workspace \
  "$IMAGE_NAME" \
  bash -c "
    set -e
    
    # Configuration des variables d'environnement pour MinGW
    export PATH=\"/usr/x86_64-w64-mingw32/bin:/usr/bin:\$PATH\"
    export CC=x86_64-w64-mingw32-gcc
    export CXX=x86_64-w64-mingw32-g++
    export SIDPLAYFP_ROOT=/usr/x86_64-w64-mingw32
    
    # Configuration pkg-config pour la cross-compilation
    export PKG_CONFIG_PATH=\"/usr/x86_64-w64-mingw32/lib/pkgconfig\"
    export PKG_CONFIG_LIBDIR=\"/usr/x86_64-w64-mingw32/lib/pkgconfig\"
    export PKG_CONFIG_SYSROOT_DIR=\"/\"
    
    echo '=== Vérification des outils ==='
    \$CC --version | head -1
    \$CXX --version | head -1
    cmake --version | head -1
    echo ''
    
    echo '=== Initialisation des submodules ==='
    git submodule update --init --recursive || echo 'Submodules déjà initialisés'
    echo ''
    
    echo '=== Vérification Python ==='
    python3 --version
    python3 -c \"import jsonschema; import jinja2; print('✅ Dépendances Python disponibles')\"
    echo ''
    
    echo '=== Configuration CMake ==='
    rm -rf build-win
    mkdir -p build-win
    
    # Configuration CMake pour la cross-compilation
    cmake -B build-win -S . \
      -DCMAKE_SYSTEM_NAME=Windows \
      -DCMAKE_C_COMPILER=\$CC \
      -DCMAKE_CXX_COMPILER=\$CXX \
      -DCMAKE_CXX_STANDARD=23 \
      -DCMAKE_CXX_STANDARD_REQUIRED=ON \
      -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_CLOUD_SAVE=ON \
      -DPython3_EXECUTABLE=\"\$(which python3)\" \
      -DCMAKE_FIND_ROOT_PATH=/usr/x86_64-w64-mingw32 \
      -DSDL2_DIR=/usr/x86_64-w64-mingw32/lib/cmake/SDL2
    
    echo ''
    echo '=== Compilation ==='
    cmake --build build-win --config Release -j\$(nproc)
    
    echo ''
    echo '=== Préparation du bundle (Installation) ==='
    # On définit le prefixe d'installation dans build-win/bundle
    cmake --install build-win --prefix \"\$(pwd)/build-win/bundle\"
    
    # Nettoyage sélectif : on ne veut pas les dossiers de dev de MBed TLS
    # mais on garde tout ce qui est à la racine de bundle (exe et dlls)
    rm -rf \"\$(pwd)/build-win/bundle/include\" \"\$(pwd)/build-win/bundle/lib\" \"\$(pwd)/build-win/bundle/share\"
    
    echo ''
    echo '=== Vérification du bundle ==='
    if [ -f \"build-win/bundle/imSidPlayer.exe\" ]; then
      echo '✅ Succès : Le bundle complet est prêt dans build-win/bundle/'
      echo 'Contenu du bundle :'
      ls -F build-win/bundle/
      echo ''
      echo 'DLLs présentes :'
      ls -1 build-win/bundle/*.dll 2>/dev/null || echo 'Aucune DLL (linkage statique total ?)'
    else
      echo '❌ Erreur : imSidPlayer.exe non trouvé dans le bundle.'
      exit 1
    fi
  "

if [ $? -eq 0 ]; then
    echo ""
    echo "🎉 Test local terminé avec succès !"
    echo "Le binaire Windows est disponible dans build-win/bundle/imSidPlayer.exe"
else
    echo ""
    echo "❌ Le test a échoué."
    exit 1
fi
