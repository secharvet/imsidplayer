# Scripts de développement

## Setup de l'environnement de développement

### Méthode 1 : Fichier .env.local (Recommandé)

1. Créer un fichier `.env.local` à la racine du projet :
```bash
# Configuration Supabase pour développement local
SUPABASE_URL=https://votre-projet.supabase.co
SUPABASE_ANON_KEY=votre-clé-anon
```

2. Charger les variables dans votre shell :
```bash
source scripts/setup_dev_env.sh
# ou
source .env.local
```

3. Lancer CMake/Build :
```bash
cd build
cmake ..
make
```

### Méthode 2 : Variables d'environnement manuelles

```bash
export SUPABASE_URL="https://votre-projet.supabase.co"
export SUPABASE_ANON_KEY="votre-clé-anon"
cd build
cmake ..
make
```

### Méthode 3 : GitHub CLI (limité)

⚠️ **Note importante** : GitHub CLI ne peut **pas** lire directement les valeurs des secrets GitHub pour des raisons de sécurité. Les secrets sont chiffrés et ne peuvent être décryptés que par GitHub Actions lors de l'exécution des workflows.

**Alternatives** :
- Utiliser `.env.local` (méthode 1) - **Recommandé**
- Utiliser un gestionnaire de secrets local (direnv, sops, etc.)
- Copier manuellement les valeurs depuis le dashboard Supabase

## Scripts disponibles

- `setup_dev_env.sh` : Configure l'environnement depuis `.env.local`
- `fetch_supabase_secrets.sh` : Tentative de récupération via GitHub CLI (limité)

## Sécurité

⚠️ **Important** : Le fichier `.env.local` est dans `.gitignore` et ne sera **jamais** commité. Ne commitez jamais vos clés Supabase dans le code source.
