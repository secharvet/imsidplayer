#!/bin/bash

# Script pour récupérer les secrets Supabase depuis GitHub Secrets
# et les définir comme variables d'environnement locales
#
# Usage:
#   source scripts/fetch_supabase_secrets.sh
#   ou
#   . scripts/fetch_supabase_secrets.sh
#
# Puis lancer cmake/build normalement

set -e

# Vérifier si GitHub CLI est installé
if ! command -v gh &> /dev/null; then
    echo "❌ GitHub CLI (gh) n'est pas installé."
    echo "📦 Installation:"
    echo "   Ubuntu/Debian: sudo apt-get install gh"
    echo "   macOS: brew install gh"
    echo "   Ou voir: https://cli.github.com/"
    echo ""
    echo "⚠️  Les secrets ne seront pas récupérés automatiquement."
    echo "   Vous pouvez les définir manuellement:"
    echo "   export SUPABASE_URL='votre-url'"
    echo "   export SUPABASE_ANON_KEY='votre-clé'"
    return 1 2>/dev/null || exit 1
fi

# Vérifier si on est authentifié
if ! gh auth status &> /dev/null; then
    echo "🔐 Authentification GitHub requise..."
    echo "   Lancez: gh auth login"
    return 1 2>/dev/null || exit 1
fi

# Récupérer le nom du repo (depuis git remote)
REPO=$(git remote get-url origin 2>/dev/null | sed -E 's/.*github.com[:/]([^/]+\/[^/]+)(\.git)?$/\1/' || echo "")

if [ -z "$REPO" ]; then
    echo "❌ Impossible de déterminer le nom du repository GitHub."
    echo "   Assurez-vous d'être dans un repo git avec un remote 'origin' configuré."
    return 1 2>/dev/null || exit 1
fi

echo "🔍 Récupération des secrets depuis GitHub pour: $REPO"

# Récupérer les secrets
SUPABASE_URL_SECRET=$(gh secret list --repo "$REPO" --json name,value --jq '.[] | select(.name=="SUPABASE_URL") | .value' 2>/dev/null || echo "")
SUPABASE_ANON_KEY_SECRET=$(gh secret list --repo "$REPO" --json name,value --jq '.[] | select(.name=="SUPABASE_ANON_KEY") | .value' 2>/dev/null || echo "")

# Note: gh secret list ne peut pas lire les valeurs directement pour des raisons de sécurité
# On doit utiliser une autre approche

echo "⚠️  Note: GitHub CLI ne peut pas lire directement les valeurs des secrets (sécurité)."
echo ""
echo "💡 Solutions alternatives:"
echo ""
echo "1. Utiliser GitHub Actions secrets dans un workflow (recommandé pour CI/CD)"
echo ""
echo "2. Créer un fichier .env.local (non commité) avec vos valeurs:"
echo "   SUPABASE_URL=https://votre-projet.supabase.co"
echo "   SUPABASE_ANON_KEY=votre-clé"
echo ""
echo "3. Définir manuellement les variables d'environnement:"
echo "   export SUPABASE_URL='votre-url'"
echo "   export SUPABASE_ANON_KEY='votre-clé'"
echo ""
echo "4. Utiliser un gestionnaire de secrets local (comme direnv + sops)"

# Vérifier si un fichier .env.local existe
if [ -f ".env.local" ]; then
    echo ""
    echo "📄 Fichier .env.local trouvé, chargement..."
    set -a
    source .env.local
    set +a
    echo "✅ Variables chargées depuis .env.local"
    return 0 2>/dev/null || exit 0
fi

return 0 2>/dev/null || exit 0
