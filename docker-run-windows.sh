#!/bin/bash
# Script pour lancer un shell interactif dans le conteneur Windows

set -e

IMAGE_NAME="imsidplayer-windows"

# Vérifier que l'image existe
if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
    echo "L'image $IMAGE_NAME n'existe pas. Lancez d'abord:"
    echo "  ./docker-build-windows.sh"
    exit 1
fi

echo "=== Lancement du conteneur Windows ==="
echo "Le répertoire courant sera monté dans /workspace"
echo "Tapez 'exit' pour quitter"
echo ""

docker run -it \
  -v "$(pwd):/workspace" \
  -w /workspace \
  "$IMAGE_NAME" \
  bash -c "export PATH=\"/mingw64/bin:/usr/bin:\$PATH\" && export SIDPLAYFP_ROOT=/mingw64 && bash"
