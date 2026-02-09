#!/bin/bash
# Script pour appliquer le fix RLS via l'API REST de Supabase
# Nécessite la clé service_role (pas la clé anon)

PROJECT_URL="${SUPABASE_PROJECT_URL:-https://mizxbtqltozqhuhanqyy.supabase.co}"
SERVICE_ROLE_KEY="${SUPABASE_SERVICE_ROLE_KEY}"

if [ -z "$SERVICE_ROLE_KEY" ]; then
    echo "❌ Erreur: SUPABASE_SERVICE_ROLE_KEY n'est pas défini"
    echo ""
    echo "Pour obtenir la clé service_role:"
    echo "1. Ouvrez Supabase Dashboard → Settings → API"
    echo "2. Copiez la clé 'service_role' (⚠️ SECRÈTE, ne la commitez jamais!)"
    echo "3. Exécutez: export SUPABASE_SERVICE_ROLE_KEY='votre_cle_service_role'"
    echo "4. Relancez ce script"
    exit 1
fi

echo "🔧 Application du fix RLS pour community_ratings..."
echo ""

# Fonction pour exécuter une requête SQL
execute_sql() {
    local sql="$1"
    local response=$(curl -s -X POST \
        "${PROJECT_URL}/rest/v1/rpc/exec_sql" \
        -H "apikey: ${SERVICE_ROLE_KEY}" \
        -H "Authorization: Bearer ${SERVICE_ROLE_KEY}" \
        -H "Content-Type: application/json" \
        -d "{\"query\": \"${sql}\"}")
    
    echo "$response"
}

# Alternative: utiliser l'endpoint SQL direct (si disponible)
# Note: Supabase peut avoir un endpoint différent pour exécuter du SQL

echo "⚠️  Note: L'exécution de SQL via l'API REST nécessite généralement la clé service_role"
echo "   et peut ne pas être disponible selon votre configuration Supabase."
echo ""
echo "📝 Alternative: Exécutez manuellement le script SQL dans Supabase Dashboard:"
echo "   1. Ouvrez: https://supabase.com/dashboard/project/mizxbtqltozqhuhanqyy/sql/new"
echo "   2. Copiez le contenu de: docs/RLS_POLICY_FIX_SIMPLIFIED.sql"
echo "   3. Collez et exécutez dans l'éditeur SQL"
echo ""

# Tentative d'exécution via l'API (peut ne pas fonctionner selon la config)
echo "🔄 Tentative d'exécution via l'API REST..."
echo ""

# Script SQL à exécuter (une ligne à la fois)
SQL_STATEMENTS=(
    "DROP POLICY IF EXISTS \"Users can insert their own ratings\" ON community_ratings;"
    "CREATE OR REPLACE FUNCTION set_user_id_from_auth() RETURNS TRIGGER AS \$\$ BEGIN IF NEW.user_id IS NULL OR NEW.user_id != auth.uid() THEN NEW.user_id := auth.uid(); END IF; RETURN NEW; END; \$\$ LANGUAGE plpgsql SECURITY DEFINER;"
    "DROP TRIGGER IF EXISTS set_user_id_trigger ON community_ratings;"
    "CREATE TRIGGER set_user_id_trigger BEFORE INSERT OR UPDATE ON community_ratings FOR EACH ROW EXECUTE FUNCTION set_user_id_from_auth();"
    "CREATE POLICY \"Users can insert their own ratings\" ON community_ratings FOR INSERT WITH CHECK (auth.uid() IS NOT NULL);"
)

for sql in "${SQL_STATEMENTS[@]}"; do
    echo "Exécution: ${sql:0:60}..."
    # Note: L'API REST de Supabase ne permet généralement pas d'exécuter du SQL brut
    # Il faut utiliser le Dashboard SQL Editor
done

echo ""
echo "✅ Pour appliquer le fix, utilisez le Dashboard SQL Editor de Supabase"
echo "   Le script SQL est dans: docs/RLS_POLICY_FIX_SIMPLIFIED.sql"
