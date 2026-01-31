#!/bin/bash
# Script pour installer Podman ou Docker

set -e

echo "=== Installation de Podman ou Docker ==="
echo ""

# Vérifier si on est root
if [ "$EUID" -ne 0 ]; then 
    echo "Ce script nécessite les droits sudo"
    echo "Exécutez: sudo ./docker-setup.sh"
    exit 1
fi

# Préférer Podman (pas de daemon nécessaire)
if ! command -v podman &> /dev/null; then
    echo "📦 Installation de Podman..."
    apt update
    apt install -y podman
    echo "✅ Podman installé"
else
    echo "✅ Podman est déjà installé"
fi

# Vérifier l'installation
if command -v podman &> /dev/null; then
    echo ""
    echo "✅ Podman est prêt à être utilisé"
    podman --version
    echo ""
    echo "Vous pouvez maintenant utiliser:"
    echo "  ./docker/docker-build-windows.sh"
    echo "  ./docker/docker-test-windows.sh"
else
    echo ""
    echo "⚠️  Podman n'a pas pu être installé, essayons Docker..."
    if ! command -v docker &> /dev/null; then
        echo "📦 Installation de Docker..."
        apt install -y docker.io
        systemctl enable docker
        systemctl start docker
        echo "✅ Docker installé et démarré"
    fi
fi
