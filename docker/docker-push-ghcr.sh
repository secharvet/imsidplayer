#!/bin/bash
# Script pour pousser l'image MinGW vers GitHub Container Registry (GHCR)
# Usage: ./docker/docker-push-ghcr.sh <votre_username_github>

if [ -z "$1" ]; then
    echo "Usage: ./docker/docker-push-ghcr.sh <votre_username_github>"
    exit 1
fi

# Se placer à la racine du projet
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

USERNAME=$(echo "$1" | tr '[:upper:]' '[:lower:]')
IMAGE_NAME="imsidplayer-windows"
REPO_NAME="imsidplayer" # Changez si votre dépôt a un autre nom
TAG="ghcr.io/$USERNAME/$REPO_NAME/$IMAGE_NAME:latest"

echo "=== Préparation de l'image pour GHCR ==="
echo "Tag: $TAG"

# Construire localement
podman build -f docker/Dockerfile.windows -t "$IMAGE_NAME" .

# Tagger pour GHCR
podman tag "$IMAGE_NAME" "$TAG"

echo ""
echo "=== Connexion à GHCR ==="
echo "Utilisez votre Personal Access Token (PAT) comme mot de passe."
podman login ghcr.io -u "$USERNAME"

echo ""
echo "=== Push de l'image ==="
podman push "$TAG"

echo ""
echo "✅ Image poussée avec succès !"
echo "Vous pourrez l'utiliser dans votre workflow avec : $TAG"
