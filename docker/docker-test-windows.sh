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

# Vérifier si les variables Supabase sont définies
if [ -z "$SUPABASE_URL" ] || [ -z "$SUPABASE_ANON_KEY" ]; then
    echo "⚠️ ATTENTION: SUPABASE_URL et/ou SUPABASE_ANON_KEY ne sont pas définies."
    echo "   Le build échouera probablement si ENABLE_CLOUD_SAVE est activé."
    echo "   Assurez-vous d'avoir chargé .envrc (direnv allow) ou exporté ces variables."
    exit 1
fi

# Construire l'image si elle n'existe pas ou forcer la reconstruction si nécessaire
echo "📦 Vérification/Construction de l'image (cela peut prendre plusieurs minutes)..."
$CONTAINER_CMD build -f "$DOCKERFILE" -t "$IMAGE_NAME" .

echo ""
echo "🧪 Lancement de la compilation dans le conteneur..."
echo ""

CONTAINER_NAME="imsidplayer-builder-$$"

# Exécuter la compilation dans le conteneur
# Note: :z est ajouté pour la compatibilité SELinux avec Podman
$CONTAINER_CMD run --rm --name "$CONTAINER_NAME" \
  -v "$(pwd):/workspace:z" \
  -w /workspace \
  -e SUPABASE_URL="$SUPABASE_URL" \
  -e SUPABASE_ANON_KEY="$SUPABASE_ANON_KEY" \
  "$IMAGE_NAME" \
  bash -c "
    set -e
    
    echo '=== Vérification du montage de volume ==='
    # Test d'écriture
    touch /workspace/write_check && rm /workspace/write_check || echo '⚠️ Volume non inscriptible ?'
    
    # Configuration des variables d'environnement pour MinGW
    export PATH=\"/usr/x86_64-w64-mingw32/bin:/usr/bin:\$PATH\"
    export CC=x86_64-w64-mingw32-gcc
    export CXX=x86_64-w64-mingw32-g++
    export SIDPLAYFP_ROOT=/usr/x86_64-w64-mingw32
    
    # Configuration pkg-config pour la cross-compilation
    export PKG_CONFIG_PATH=\"/usr/x86_64-w64-mingw32/lib/pkgconfig\"
    export PKG_CONFIG_LIBDIR=\"/usr/x86_64-w64-mingw32/lib/pkgconfig\"
    export PKG_CONFIG_SYSROOT_DIR=\"/\"
    
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
    echo '=== Vérification des fichiers générés ==='
    ls -la include/SupabaseConfig.h
    
    echo ''
    echo '=== Compilation ==='
    cmake --build build-win --config Release -j\$(nproc)
    
    echo ''
    echo '=== Installation (Bundle) ==='
    cmake --install build-win --prefix \"/workspace/build-win/bundle\"
    
    # Nettoyage sélectif
    rm -rf \"/workspace/build-win/bundle/include\" \"/workspace/build-win/bundle/lib\" \"/workspace/build-win/bundle/share\"
    
    echo ''
    echo '=== Vérification du bundle ==='
    if [ -f \"build-win/bundle/imSidPlayer.exe\" ]; then
      echo '✅ Succès : Le bundle complet est prêt dans build-win/bundle/'
      ls -F build-win/bundle/
    else
      echo '❌ Erreur : imSidPlayer.exe non trouvé dans le bundle.'
      exit 1
    fi
  "

BUILD_RET=$?

echo ""
if [ $BUILD_RET -eq 0 ]; then
    echo "=== Récupération des artefacts === "
    OUTPUT_DIR="windows_release"
    
    echo "Dossier de destination : $(pwd)/$OUTPUT_DIR"
    rm -rf "$OUTPUT_DIR"
    mkdir -p "$OUTPUT_DIR"
    
    # Debug : lister le contenu avant la copie
    echo "Contenu du dossier build-win/bundle local :"
    ls -la build-win/bundle/ || echo "Le dossier bundle n'existe pas localement !"

    echo "Copie depuis le dossier de build local..."
    # Copier le contenu du dossier bundle vers windows_release
    # Note: build-win est dans le volume monté, donc accessible localement
    cp -r "build-win/bundle/." "$OUTPUT_DIR/"
    
    if [ -f "$OUTPUT_DIR/imSidPlayer.exe" ]; then
        echo "🎉 Test local terminé avec succès !"
        echo "Le binaire Windows est DISPONIBLE sur l'hôte dans : $OUTPUT_DIR/imSidPlayer.exe"
        ls -la "$OUTPUT_DIR"
    else
        echo "❌ Erreur : La copie a semblé réussir mais le fichier n'est pas là."
        ls -la "$OUTPUT_DIR"
        exit 1
    fi
else
    echo "❌ Le build a échoué dans le conteneur."
fi

# Le conteneur est supprimé automatiquement avec --rm
exit $BUILD_RET
