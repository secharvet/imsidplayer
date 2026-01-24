#!/bin/bash
# Script pour tester la compilation Windows avec Podman/Docker localement
# Usage: ./docker-test-windows.sh

set -e

IMAGE_NAME="imsidplayer-windows"
DOCKERFILE="Dockerfile.windows"

# Détecter podman ou docker
if command -v podman &> /dev/null; then
    CONTAINER_CMD="podman"
    echo "✅ Utilisation de Podman"
elif command -v docker &> /dev/null; then
    CONTAINER_CMD="docker"
    echo "✅ Utilisation de Docker"
else
    echo "❌ Ni Podman ni Docker n'est installé. Installez l'un des deux:"
    echo "   sudo apt install podman"
    echo "   ou"
    echo "   sudo apt install docker.io"
    exit 1
fi

echo "=== Test de compilation Windows avec $CONTAINER_CMD ==="
echo ""

# Vérifier que le conteneur fonctionne
if ! $CONTAINER_CMD info &> /dev/null; then
    if [ "$CONTAINER_CMD" = "docker" ]; then
        echo "❌ Docker n'est pas en cours d'exécution. Démarrez-le avec:"
        echo "   sudo systemctl start docker"
    else
        echo "⚠️  Podman devrait fonctionner sans daemon"
    fi
    exit 1
fi

# Construire l'image si elle n'existe pas
if ! $CONTAINER_CMD image exists "$IMAGE_NAME" 2>/dev/null; then
    echo "📦 Construction de l'image (cela peut prendre plusieurs minutes)..."
    $CONTAINER_CMD build -f "$DOCKERFILE" -t "$IMAGE_NAME" .
    if [ $? -ne 0 ]; then
        echo "❌ Échec de la construction de l'image"
        exit 1
    fi
    echo "✅ Image construite avec succès"
else
    echo "✅ Image $IMAGE_NAME existe déjà"
fi

echo ""
echo "🧪 Test de compilation dans le conteneur..."
echo ""

# Tester la compilation
$CONTAINER_CMD run --rm \
  -v "$(pwd):/workspace" \
  -w /workspace \
  "$IMAGE_NAME" \
  bash -c "
    set -e
    export PATH=\"/mingw64/bin:/usr/bin:\$PATH\"
    export SIDPLAYFP_ROOT=/mingw64
    
    echo '=== Vérification des outils ==='
    which gcc && gcc --version | head -1
    which g++ && g++ --version | head -1
    which cmake && cmake --version | head -1
    which pkg-config && pkg-config --version
    which perl && perl --version | head -1
    which python && python --version
    echo ''
    
    echo '=== Initialisation des submodules ==='
    git submodule update --init --recursive || echo 'Submodules déjà initialisés'
    echo ''
    
    echo '=== Création du venv Python ==='
    python -m venv venv
    source venv/bin/activate
    pip install --upgrade pip --quiet
    pip install \"jsonschema>=3.0.0,<4.0.0\" --quiet
    echo '✅ venv créé avec jsonschema'
    echo ''
    
    echo '=== Configuration CMake ==='
    mkdir -p build
    cmake -B build -S . \
      -G \"MinGW Makefiles\" \
      -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_CLOUD_SAVE=ON \
      -DPython3_EXECUTABLE=\"\$(pwd)/venv/bin/python\" \
      2>&1 | tail -20
    echo ''
    
    echo '=== Compilation ==='
    cmake --build build --config Release -j\$(nproc) 2>&1 | tail -30
    echo ''
    
    echo '=== Vérification des binaires ==='
    if [ -f \"build/bin/imSidPlayer.exe\" ]; then
      echo '✅ imSidPlayer.exe compilé avec succès !'
      ls -lh build/bin/imSidPlayer.exe
      file build/bin/imSidPlayer.exe
    else
      echo '❌ imSidPlayer.exe non trouvé'
      find build -name '*.exe' -type f || echo 'Aucun .exe trouvé'
      exit 1
    fi
  "

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Test de compilation réussi !"
    echo ""
    echo "Le fichier imSidPlayer.exe se trouve dans:"
    echo "  build/bin/imSidPlayer.exe"
    echo ""
    echo "Vous pouvez le copier et l'utiliser sur Windows."
else
    echo ""
    echo "❌ Test de compilation échoué"
    echo "Vérifiez les erreurs ci-dessus"
    exit 1
fi
