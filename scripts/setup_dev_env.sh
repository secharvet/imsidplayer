#!/bin/bash

# Script de setup pour l'environnement de développement
# Charge les secrets Supabase depuis .env.local ou invite à les définir

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

echo "🔧 Configuration de l'environnement de développement"
echo ""

# Vérifier si .env.local existe
if [ -f ".env.local" ]; then
    echo "📄 Chargement des variables depuis .env.local..."
    set -a
    source .env.local
    set +a
    echo "✅ Variables chargées"
else
    echo "📝 Fichier .env.local non trouvé."
    echo ""
    echo "Création d'un fichier .env.local template..."
    cat > .env.local << 'EOF'
# Configuration Supabase pour développement local
# Ces valeurs ne seront PAS commitées (déjà dans .gitignore)

# URL de votre projet Supabase
SUPABASE_URL=

# Clé publique (anon key) de votre projet Supabase
SUPABASE_ANON_KEY=
EOF
    echo "✅ Fichier .env.local créé"
    echo ""
    echo "⚠️  Veuillez éditer .env.local et ajouter vos valeurs Supabase:"
    echo "   nano .env.local"
    echo "   ou"
    echo "   code .env.local"
    echo ""
    echo "Puis relancez ce script ou sourcez .env.local manuellement:"
    echo "   source .env.local"
    exit 1
fi

# Vérifier si les variables sont définies
if [ -z "$SUPABASE_URL" ] || [ -z "$SUPABASE_ANON_KEY" ]; then
    echo ""
    echo "⚠️  SUPABASE_URL ou SUPABASE_ANON_KEY non définis dans .env.local"
    echo "   Veuillez les ajouter dans .env.local"
    exit 1
fi

echo ""
echo "✅ Configuration prête:"
echo "   SUPABASE_URL: ${SUPABASE_URL:0:30}..."
echo "   SUPABASE_ANON_KEY: ${SUPABASE_ANON_KEY:0:20}..."
echo ""
echo "💡 Pour utiliser ces variables dans votre shell:"
echo "   source scripts/setup_dev_env.sh"
echo ""
echo "💡 Ou pour un build CMake:"
echo "   source scripts/setup_dev_env.sh"
echo "   cd build && cmake .. && make"
