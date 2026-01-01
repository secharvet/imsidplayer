#!/bin/bash

# Script de build pour imSid Player

set -e

# Déterminer le type de build (Debug par défaut, Release si --release est passé)
BUILD_TYPE="Debug"
if [ "$1" == "--release" ] || [ "$1" == "-r" ]; then
    BUILD_TYPE="Release"
fi

echo "=== imSid Player - Script de build ==="
echo "Mode de build: $BUILD_TYPE"
echo ""

# Vérifier que CMake est installé
if ! command -v cmake &> /dev/null; then
    echo "Erreur: CMake n'est pas installé"
    exit 1
fi

# Créer le dossier build s'il n'existe pas
if [ ! -d "build" ]; then
    echo "Création du dossier build..."
    mkdir build
fi

cd build

echo "Configuration avec CMake..."
cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE

echo ""
echo "Compilation..."
make -j$(nproc)

echo ""
echo "=== Build terminé avec succès! ==="
echo "L'exécutable se trouve dans: build/bin/imSidPlayer"
echo ""






