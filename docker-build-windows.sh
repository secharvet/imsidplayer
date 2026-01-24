#!/bin/bash
# Script pour construire et utiliser l'image Docker/Podman Windows

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

# Vérifier que le conteneur fonctionne
if [ "$CONTAINER_CMD" = "docker" ]; then
    if ! $CONTAINER_CMD info &> /dev/null; then
        echo "❌ Docker n'est pas en cours d'exécution. Démarrez-le avec:"
        echo "   sudo systemctl start docker"
        exit 1
    fi
fi

echo "=== Construction de l'image Windows ==="
echo "Cela peut prendre plusieurs minutes lors de la première construction..."
$CONTAINER_CMD build -f "$DOCKERFILE" -t "$IMAGE_NAME" .

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Image construite avec succès !"
    echo ""
    echo "Pour lancer un shell interactif dans le conteneur:"
    echo "  ./docker-run-windows.sh"
    echo ""
    echo "Ou manuellement:"
    echo "  $CONTAINER_CMD run -it -v \$(pwd):/workspace $IMAGE_NAME"
    echo ""
else
    echo ""
    echo "❌ Échec de la construction de l'image"
    exit 1
fi
