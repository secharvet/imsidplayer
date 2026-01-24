#!/bin/bash
# Script pour construire et utiliser l'image Docker Windows

set -e

IMAGE_NAME="imsidplayer-windows"
DOCKERFILE="Dockerfile.windows"

echo "=== Construction de l'image Docker Windows ==="
docker build -f "$DOCKERFILE" -t "$IMAGE_NAME" .

echo ""
echo "=== Image construite avec succès ==="
echo ""
echo "Pour lancer un shell interactif dans le conteneur:"
echo "  docker run -it -v \$(pwd):/workspace $IMAGE_NAME"
echo ""
echo "Pour compiler le projet:"
echo "  docker run -it -v \$(pwd):/workspace $IMAGE_NAME bash -c '"
echo "    export PATH=\"/mingw64/bin:/usr/bin:\$PATH\" &&"
echo "    cd /workspace &&"
echo "    mkdir -p build &&"
echo "    cmake -B build -S . -G \"MinGW Makefiles\" -DCMAKE_BUILD_TYPE=Release -DENABLE_CLOUD_SAVE=ON &&"
echo "    cmake --build build --config Release"
echo "  '"
echo ""
