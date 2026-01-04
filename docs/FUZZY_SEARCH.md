---
cssclass: wide-content
---

# Implémentation du Fuzzy Search

> **Note d'affichage** :** Pour une meilleure lisibilité dans Obsidian, désactivez la largeur limitée dans `Paramètres → Apparence → Largeur maximale du contenu` ou utilisez le mode "Lecture" (`Ctrl+E`).

## Vue d'ensemble

Le système de recherche fuzzy (recherche approximative) d'imSid Player permet de trouver des morceaux SID même avec des fautes de frappe ou des variations dans les noms. Cette fonctionnalité est **entièrement personnalisée** et n'utilise pas de bibliothèque externe.

**Note importante** : Glaze est utilisé uniquement pour la sérialisation/désérialisation JSON. Il ne fournit **pas** de fonctionnalité de fuzzy search. Le dossier `fuzzing` dans le dépôt Glaze fait référence au "fuzz testing" (tests de robustesse), pas à la recherche approximative.

## Architecture

### Fichiers concernés

- `src/DatabaseManager.cpp` : Implémentation principale
- `include/DatabaseManager.h` : Interface publique
- `src/UIManager.cpp` : Interface utilisateur et affichage des résultats

### Fonction principale : `DatabaseManager::search()`

```cpp
std::vector<const SidMetadata*> DatabaseManager::search(const std::string& query) const
```

Cette fonction effectue une recherche en deux passes :

1. **Recherche exacte** (rapide) : Cherche la query comme sous-chaîne dans titre, auteur, et nom de fichier
2. **Recherche fuzzy** (si nécessaire) : Utilise l'algorithme `fuzzyMatchFast()` pour les correspondances approximatives

## Algorithme de Fuzzy Matching

### Fonction : `fuzzyMatchFast()`

```cpp
double DatabaseManager::fuzzyMatchFast(const std::string& str, const std::string& query)
```

#### Principe de fonctionnement

1. **Vérification de sous-chaîne exacte**
   - Si `query` est une sous-chaîne de `str` → score = 1.0 (match parfait)

2. **Subsequence Matching** (si pas de match exact)
   - Vérifie si tous les caractères de `query` sont présents dans `str` **dans l'ordre**
   - Les caractères n'ont pas besoin d'être consécutifs
   - Exemple : `query = "david"` dans `str = "David Major - Cova"` → ✅ Match

3. **Calcul du score**
   - **60%** basé sur la proportion de caractères trouvés (`matchRatio`)
   - **40%** basé sur la longueur de la séquence consécutive la plus longue (`consecutiveRatio`)
   - Score final : `(matchRatio * 0.6 + consecutiveRatio * 0.4)`

#### Complexité

- **Temps** : O(n) où n = longueur de la chaîne à rechercher
- **Espace** : O(1)
- **Avantage** : Beaucoup plus rapide que l'algorithme de Levenshtein (O(n×m))

#### Exemple

```cpp
query = "david"
str = "David Major - Cova"

// Étape 1 : Sous-chaîne exacte ?
"David Major - Cova".find("david") → Non trouvé (case-sensitive après tolower)

// Étape 2 : Subsequence matching
d → trouvé à l'index 0
a → trouvé à l'index 1
v → trouvé à l'index 2
i → trouvé à l'index 3
d → trouvé à l'index 4

// Tous les caractères trouvés dans l'ordre → Score élevé (proche de 1.0)
```

## Recherche Multi-Mots

### Parsing de la query

La query est divisée en mots séparés par des espaces :

```cpp
std::vector<std::string> queryWords;
std::istringstream iss(queryLower);
std::string word;
while (iss >> word) {
    if (word.length() >= 2) { // Ignorer les mots trop courts
        queryWords.push_back(word);
    }
}
```

### Stratégie de recherche

Si la query contient **plusieurs mots** (`queryWords.size() > 1`) :

1. **Pour chaque mot de la query** :
   - Recherche exacte dans titre, auteur, filename
   - Si non trouvé, recherche fuzzy avec seuil 0.4
   - Priorité : Titre > Auteur > Filename

2. **Calcul du score final** :
   - Si **tous les mots** sont trouvés : `score = (totalFuzzyScore / queryWords.size()) * 5.0`
   - Si **match partiel** : `score = (wordsFound / queryWords.size()) * (avgScore) * 3.0` (pénalité)

3. **Exemple** :
   ```
   Query: "David Major cova"
   → Mots: ["david", "major", "cova"]
   
   Pour chaque mot, recherche dans:
   - Titre: "Cova" (exact match pour "cova")
   - Auteur: "David Major" (exact match pour "david" et "major")
   
   → Tous les mots trouvés → Score élevé
   ```

Si la query contient **un seul mot** :

- Recherche fuzzy simple avec seuil 0.5
- Priorité : Titre (×5.0) > Auteur (×4.0) > Filename (×2.0)

## Limites et Performance

### Limite de résultats

- **Maximum 25 résultats** retournés
- Les résultats sont triés par score décroissant

### Optimisations

1. **Early exit** : Si 25+ résultats exacts sont trouvés, la recherche fuzzy est ignorée
2. **Éviter les doublons** : Utilisation d'un `std::unordered_set` pour éviter de re-tester les métadonnées déjà trouvées en exact match
3. **Seuils adaptatifs** :
   - Exact match : aucun seuil (toujours inclus)
   - Fuzzy single word : seuil 0.5
   - Fuzzy multi-word : seuil 0.4 par mot

### Logging

Les recherches lentes (> 50ms) sont loggées :

```
[SEARCH] query='David Major cova': 170 ms (DB: 59324, exact: 0, fuzzy: yes, results: 3)
```

## Utilisation dans l'interface

### Champ de recherche

- **Emplacement** : Panel "Playlist" → Champ "Search:"
- **Comportement** : Recherche en temps réel à chaque frappe
- **Affichage** : Liste scrollable sous le champ (max 25 résultats)

### Navigation clavier

- **Flèche bas** depuis le champ → Sélectionne le premier résultat
- **Flèches haut/bas** dans la liste → Navigue entre les résultats
- **Enter** → Sélectionne le résultat et charge le morceau

### Mise en évidence

- Le résultat sélectionné est mis en évidence en bleu
- Scroll automatique vers l'élément sélectionné

## Exemples d'utilisation

### Recherche simple

```
Query: "david"
→ Trouve tous les morceaux avec "david" dans le titre, auteur ou filename
→ Tolère les variations : "David", "DAVID", "dAvId"
```

### Recherche multi-mots

```
Query: "David Major cova"
→ Trouve les morceaux où :
  - "david" est présent (titre, auteur ou filename)
  - "major" est présent
  - "cova" est présent
→ Les mots peuvent être dans différents champs
```

### Recherche avec fautes de frappe

```
Query: "davd" (manque le 'i')
→ Trouve quand même "David" grâce au fuzzy matching
→ Score réduit car pas tous les caractères consécutifs
```

## Améliorations futures possibles

1. **Indexation** : Utiliser un index inversé pour accélérer les recherches
2. **Pondération** : Ajuster les poids selon le champ (titre plus important que filename)
3. **Historique** : Sauvegarder les recherches fréquentes
4. **Suggestions** : Proposer des corrections automatiques pour les queries sans résultats
5. **Recherche phonétique** : Support pour les variations phonétiques (ex: "ph" vs "f")

## Références

- **Algorithme inspiré de** : Subsequence matching (variante simplifiée)
- **Alternative plus précise** : Distance de Levenshtein (mais O(n×m) donc plus lent)
- **Bibliothèque utilisée** : Aucune (implémentation personnalisée)

