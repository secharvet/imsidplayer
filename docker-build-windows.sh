#!/bin/bash
# Script pour construire et utiliser l'image Docker Windows

set -e

IMAGE_NAME="imsidplayer-windows"
DOCKERFILE="Dockerfile.windows"

# Vérifier que Docker est installé
if ! command -v docker &> /dev/null; then
    echo "❌ Docker n'est pas installé. Installez-le avec:"
    echo "   sudo apt install docker.io"
    echo "   ou"
    echo "   sudo snap install docker"
    exit 1
fi

# Vérifier que Docker fonctionne
if ! docker info &> /dev/null; then
    echo "❌ Docker n'est pas en cours d'exécution. Démarrez-le avec:"
    echo "   sudo systemctl start docker"
    echo "   ou"
    echo "   sudo service docker start"
    exit 1
fi

echo "=== Construction de l'image Docker Windows ==="
echo "Cela peut prendre plusieurs minutes lors de la première construction..."
docker build -f "$DOCKERFILE" -t "$IMAGE_NAME" .

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Image construite avec succès !"
    echo ""
    echo "Pour lancer un shell interactif dans le conteneur:"
    echo "  ./docker-run-windows.sh"
    echo ""
    echo "Ou manuellement:"
    echo "  docker run -it -v \$(pwd):/workspace $IMAGE_NAME"
    echo ""
else
    echo ""
    echo "❌ Échec de la construction de l'image"
    exit 1
fi
