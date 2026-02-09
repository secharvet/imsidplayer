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

# Configuration Supabase pour le build
# Les variables doivent être définies dans l'environnement (ex: via .envrc + direnv)

# Vérifier que les variables sont bien définies
if [ -z "$SUPABASE_URL" ] || [ -z "$SUPABASE_ANON_KEY" ]; then
    echo "WARNING: SUPABASE_URL or SUPABASE_ANON_KEY is not set!"
    echo "Make sure to load your environment (e.g., 'direnv allow')."
else
    echo "Supabase configuration found in environment:"
    echo "  URL: $SUPABASE_URL"
    echo "  Anon Key: ${SUPABASE_ANON_KEY:0:20}..."
fi

echo "Configuration avec CMake..."
cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DENABLE_CLOUD_SAVE=ON

echo ""
echo "Compilation..."
make -j$(nproc)

echo ""
echo "=== Build terminé avec succès! ==="
echo "L'exécutable se trouve dans: build/bin/imSidPlayer"
echo ""






