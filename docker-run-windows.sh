#!/bin/bash
# Script pour lancer un shell interactif dans le conteneur Windows

set -e

IMAGE_NAME="imsidplayer-windows"

# Détecter podman ou docker
if command -v podman &> /dev/null; then
    CONTAINER_CMD="podman"
elif command -v docker &> /dev/null; then
    CONTAINER_CMD="docker"
else
    echo "❌ Ni Podman ni Docker n'est installé. Installez l'un des deux:"
    echo "   sudo apt install podman"
    echo "   ou"
    echo "   sudo apt install docker.io"
    exit 1
fi

# Vérifier que l'image existe
if ! $CONTAINER_CMD image exists "$IMAGE_NAME" 2>/dev/null; then
    echo "❌ L'image $IMAGE_NAME n'existe pas. Lancez d'abord:"
    echo "   ./docker-build-windows.sh"
    exit 1
fi

echo "=== Lancement du conteneur Windows ==="
echo "Le répertoire courant sera monté dans /workspace"
echo "Tapez 'exit' pour quitter"
echo ""

$CONTAINER_CMD run -it \
  -v "$(pwd):/workspace" \
  -w /workspace \
  "$IMAGE_NAME" \
  bash -c "export PATH=\"/mingw64/bin:/usr/bin:\$PATH\" && export SIDPLAYFP_ROOT=/mingw64 && bash"
