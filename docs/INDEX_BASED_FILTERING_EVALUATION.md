# Évaluation : Système d'Index en Mémoire pour le Filtrage

## Résumé Exécutif

**Proposition** : Remplacer le système actuel d'arbre filtré par un système d'index en mémoire (`unordered_map`) par critère de filtre (auteur, année, titre, etc.), puis rendre conditionnellement les nœuds de l'arbre original selon leur présence dans les index actifs.

**Verdict** : ⚠️ **Approche intéressante mais avec des limitations importantes**. Recommandation : **Hybride** - utiliser les index pour les filtres simples, garder l'arbre filtré pour les combinaisons complexes.

---

## Table des matières

1. [Description de l'approche proposée](#description-de-lapproche-proposée)
2. [Système actuel (arbre filtré)](#système-actuel-arbre-filtré)
3. [Comparaison détaillée](#comparaison-détaillée)
4. [Avantages de l'approche par index](#avantages-de-lapproche-par-index)
5. [Inconvénients de l'approche par index](#inconvénients-de-lapproche-par-index)
6. [Complexité d'implémentation](#complexité-dimplémentation)
7. [Impact sur la mémoire](#impact-sur-la-mémoire)
8. [Impact sur les performances](#impact-sur-les-performances)
9. [Recommandation finale](#recommandation-finale)
10. [Plan d'implémentation suggéré](#plan-dimplémentation-suggéré)

---

## Description de l'approche proposée

### Concept

Au lieu de créer une copie complète de l'arbre filtré, créer des **index inversés** en mémoire :

```cpp
// Structure proposée
struct FilterIndexes {
    // Index par auteur : auteur -> set de PlaylistNode*
    std::unordered_map<std::string, std::unordered_set<PlaylistNode*>> byAuthor;
    
    // Index par année : année -> set de PlaylistNode*
    std::unordered_map<std::string, std::unordered_set<PlaylistNode*>> byYear;
    
    // Index par titre : titre -> set de PlaylistNode*
    std::unordered_map<std::string, std::unordered_set<PlaylistNode*>> byTitle;
    
    // Index par modèle SID : modèle -> set de PlaylistNode*
    std::unordered_map<std::string, std::unordered_set<PlaylistNode*>> bySidModel;
    
    // ... autres critères possibles
};
```

### Rendu conditionnel

Lors du rendu, au lieu de parcourir l'arbre filtré, parcourir l'arbre original et vérifier si le nœud est dans les index actifs :

```cpp
void renderNode(PlaylistNode* node) {
    // Pour un fichier : vérifier s'il est dans les index actifs
    if (!node->isFolder) {
        bool shouldRender = true;
        
        // Vérifier chaque filtre actif
        if (m_filterAuthorActive && !m_indexes.byAuthor[m_filterAuthor].contains(node)) {
            shouldRender = false;
        }
        if (m_filterYearActive && !m_indexes.byYear[m_filterYear].contains(node)) {
            shouldRender = false;
        }
        // ... autres filtres
        
        if (!shouldRender) {
            return;  // Skip ce nœud
        }
    }
    
    // Rendre le nœud normalement
    ImGui::TreeNodeEx(...);
}
```

### Construction des index

Les index sont construits une seule fois au chargement de la playlist :

```cpp
void buildIndexes() {
    auto allFiles = m_playlist.getAllFiles();
    
    for (PlaylistNode* node : allFiles) {
        if (node->isFolder || node->filepath.empty()) continue;
        
        const SidMetadata* meta = m_database.getMetadata(node->filepath);
        if (!meta) continue;
        
        // Indexer par auteur
        if (!meta->author.empty()) {
            m_indexes.byAuthor[meta->author].insert(node);
        }
        
        // Indexer par année (extraire de released)
        std::string year = extractYear(meta->released);
        if (!year.empty()) {
            m_indexes.byYear[year].insert(node);
        }
        
        // Indexer par titre
        if (!meta->title.empty()) {
            m_indexes.byTitle[meta->title].insert(node);
        }
        
        // ... autres index
    }
}
```

---

## Système actuel (arbre filtré)

### Fonctionnement

1. **Construction de l'arbre filtré** : Quand les filtres changent, on crée une copie complète de l'arbre original en élaguant les nœuds qui ne matchent pas.

2. **Rendu** : On parcourt l'arbre filtré (beaucoup moins de nœuds que l'arbre original).

3. **Mémoire** : L'arbre filtré est une copie complète (mais plus petite) de l'arbre original.

### Code actuel

```cpp
void UIManager::rebuildFilteredTree() {
    // Créer un nouvel arbre filtré
    m_filteredTreeRoot = std::make_unique<PlaylistNode>("Playlist", "", true);
    
    // Filtrer récursivement
    for (auto& child : originalRoot->children) {
        auto filteredChild = m_playlist.createFilteredTree(child.get(), filterFunc);
        if (filteredChild) {
            m_filteredTreeRoot->children.push_back(std::move(filteredChild));
        }
    }
}

void UIManager::renderPlaylistTree() {
    // Utiliser l'arbre filtré si des filtres sont actifs
    PlaylistNode* root = (m_filtersActive && m_filteredTreeRoot) 
                        ? m_filteredTreeRoot.get() 
                        : m_playlist.getRoot();
    
    // Parcourir et rendre
    for (auto& child : root->children) {
        renderNode(child.get(), 0);
    }
}
```

---

## Comparaison détaillée

### Tableau comparatif

| Critère | Arbre filtré (actuel) | Index en mémoire (proposé) |
|---------|----------------------|---------------------------|
| **Mémoire** | Copie complète de l'arbre filtré | Index uniquement (plus léger) |
| **Construction** | O(n) à chaque changement de filtre | O(n) une seule fois au chargement |
| **Rendu** | Parcourt seulement les nœuds filtrés | Parcourt tous les nœuds (mais skip rapidement) |
| **Filtres multiples** | Facile (intersection dans filterFunc) | Complexe (intersection de sets) |
| **Mise à jour** | Reconstruction complète | Mise à jour incrémentale possible |
| **Complexité code** | Simple (arbre récursif) | Plus complexe (gestion des index) |

---

## Avantages de l'approche par index

### 1. **Économie de mémoire** ✅

**Arbre filtré actuel** :
- Pour 59000 fichiers, si 1000 matchent : ~1000 nœuds copiés
- Chaque nœud contient : nom, chemin, pointeurs, enfants
- **Estimation** : ~50-100 KB pour 1000 nœuds filtrés

**Index en mémoire** :
- Pour 59000 fichiers, index par auteur : ~1000 auteurs × 8 bytes (pointeur) × moyenne 59 fichiers = ~472 KB
- Index par année : ~50 années × 8 bytes × moyenne 1180 fichiers = ~472 KB
- **Total** : ~1-2 MB pour tous les index (mais construit une seule fois)

**Gain** : Les index sont **réutilisables** pour tous les filtres, pas besoin de reconstruire à chaque changement.

### 2. **Construction unique** ✅

**Actuel** :
- Reconstruction de l'arbre filtré à **chaque changement de filtre**
- Coût : O(n) où n = nombre de nœuds dans l'arbre

**Index** :
- Construction **une seule fois** au chargement
- Mise à jour incrémentale possible quand de nouveaux fichiers sont ajoutés
- Coût initial : O(n), mais ensuite O(1) pour changer de filtre

### 3. **Filtres multiples faciles** ✅ (avec limitations)

**Actuel** :
- Filtres multiples = intersection dans `filterFunc`
- Doit reconstruire l'arbre à chaque combinaison

**Index** :
- Intersection de sets : `set_intersection(index1, index2)`
- Très rapide (O(n) où n = taille du plus petit set)
- Permet de combiner facilement plusieurs critères

### 4. **Mise à jour incrémentale** ✅

Quand un nouveau fichier est ajouté :
- **Actuel** : Doit reconstruire tout l'arbre filtré
- **Index** : Ajouter le nœud aux index pertinents (O(1) par index)

### 5. **Extensibilité** ✅

Ajouter un nouveau type de filtre :
- **Actuel** : Modifier `filterFunc`, reconstruire l'arbre
- **Index** : Ajouter un nouvel index, construction automatique

---

## Inconvénients de l'approche par index

### 1. **Parcours complet de l'arbre original** ❌

**Problème majeur** : Même avec les index, on doit **parcourir tous les nœuds** de l'arbre original pour le rendu.

```cpp
// On parcourt TOUS les 59000 nœuds
for (auto& child : root->children) {
    renderNode(child.get(), 0);  // Appelé 59000 fois
}

void renderNode(PlaylistNode* node) {
    // Vérification O(1) dans l'index, mais on doit quand même appeler cette fonction
    if (!m_indexes.byAuthor[m_filterAuthor].contains(node)) {
        return;  // Skip, mais on a déjà fait le travail de parcourir
    }
    // ...
}
```

**Impact** :
- On appelle `renderNode()` pour **tous** les nœuds
- Même si on skip rapidement, le parcours récursif de l'arbre reste coûteux
- ImGui doit calculer les positions de tous les nœuds (même non visibles)

**Comparaison** :
- **Arbre filtré** : Parcourt seulement ~1000 nœuds (si 1000 matchent)
- **Index** : Parcourt **tous** les 59000 nœuds, skip 58000

### 2. **Gestion des dossiers vides** ❌

**Problème** : Avec l'arbre filtré, les dossiers vides sont automatiquement élagués. Avec les index, on doit gérer manuellement :

```cpp
void renderNode(PlaylistNode* node) {
    if (node->isFolder) {
        // Rendre le dossier
        bool nodeOpen = ImGui::TreeNodeEx(...);
        
        if (nodeOpen) {
            bool hasVisibleChildren = false;
            for (auto& child : node->children) {
                // Vérifier si l'enfant est visible
                if (shouldRenderNode(child.get())) {
                    hasVisibleChildren = true;
                    renderNode(child.get());
                }
            }
            
            // Problème : le dossier est ouvert mais peut être vide
            // Doit-on le fermer automatiquement ?
        }
    }
}
```

**Solution nécessaire** :
- Pré-calculer quels dossiers ont des enfants visibles
- Ou rendre le dossier seulement s'il a des enfants visibles
- Complexité supplémentaire

### 3. **Intersection de filtres multiples** ⚠️

**Problème** : Quand plusieurs filtres sont actifs (ex: auteur ET année), il faut faire l'intersection :

```cpp
// Intersection de deux sets
std::unordered_set<PlaylistNode*> activeNodes;
if (m_filterAuthorActive && m_filterYearActive) {
    // Intersection : O(n) où n = taille du plus petit set
    std::set_intersection(
        m_indexes.byAuthor[m_filterAuthor].begin(),
        m_indexes.byAuthor[m_filterAuthor].end(),
        m_indexes.byYear[m_filterYear].begin(),
        m_indexes.byYear[m_filterYear].end(),
        std::inserter(activeNodes, activeNodes.begin())
    );
} else if (m_filterAuthorActive) {
    activeNodes = m_indexes.byAuthor[m_filterAuthor];
} else if (m_filterYearActive) {
    activeNodes = m_indexes.byYear[m_filterYear];
}
```

**Coût** :
- Intersection : O(n) où n = nombre de nœuds dans le plus petit set
- Pour 1000 fichiers par auteur et 500 par année : ~500 comparaisons
- Acceptable, mais plus complexe que l'approche actuelle

### 4. **Synchronisation des index** ⚠️

**Problème** : Les index doivent être synchronisés avec l'arbre original :

- Quand un fichier est supprimé de la playlist
- Quand les métadonnées changent (re-indexation)
- Quand un nouveau fichier est ajouté

**Solution** : Mise à jour incrémentale, mais ajoute de la complexité.

### 5. **Pas de réduction du parcours** ❌

**Réalité** : Même avec les index, on doit **parcourir tous les nœuds** pour le rendu. Les index ne réduisent pas le nombre de nœuds parcourus, seulement le nombre de nœuds **rendus**.

**Comparaison** :
- **Arbre filtré** : Parcourt 1000 nœuds, rend 1000
- **Index** : Parcourt 59000 nœuds, rend 1000, skip 58000

**Gain réel** : Minimal pour le rendu (on skip rapidement, mais le parcours reste).

---

## Complexité d'implémentation

### Arbre filtré (actuel)

**Complexité** : ⭐⭐ (Simple)

- Code existant fonctionnel
- Logique récursive simple
- Facile à maintenir

**Lignes de code** : ~100 lignes

### Index en mémoire (proposé)

**Complexité** : ⭐⭐⭐⭐ (Modérée à élevée)

**Nouveaux composants nécessaires** :

1. **Structure des index** (~50 lignes)
```cpp
struct FilterIndexes {
    std::unordered_map<std::string, std::unordered_set<PlaylistNode*>> byAuthor;
    std::unordered_map<std::string, std::unordered_set<PlaylistNode*>> byYear;
    // ...
};
```

2. **Construction des index** (~100 lignes)
```cpp
void buildIndexes() {
    // Parcourir tous les fichiers
    // Extraire les métadonnées
    // Construire les index
}
```

3. **Mise à jour des index** (~50 lignes)
```cpp
void updateIndexesForNode(PlaylistNode* node) {
    // Ajouter/supprimer le nœud des index pertinents
}
```

4. **Rendu conditionnel modifié** (~150 lignes)
```cpp
void renderNode(PlaylistNode* node) {
    // Vérifier si le nœud est dans les index actifs
    // Gérer les dossiers vides
    // Intersection de filtres multiples
}
```

5. **Gestion des intersections** (~50 lignes)
```cpp
std::unordered_set<PlaylistNode*> getActiveNodes() {
    // Calculer l'intersection des index actifs
}
```

**Total estimé** : ~400 lignes de code nouveau

**Risques** :
- Bugs de synchronisation entre index et arbre
- Gestion des dossiers vides complexe
- Performance de l'intersection à tester

---

## Impact sur la mémoire

### Arbre filtré (actuel)

**Mémoire utilisée** :
- Arbre original : ~2-3 MB (59000 nœuds)
- Arbre filtré : ~50-100 KB (1000 nœuds filtrés)
- **Total** : ~2-3 MB (arbre original toujours présent)

**Quand** : Arbre filtré créé seulement quand des filtres sont actifs

### Index en mémoire (proposé)

**Mémoire utilisée** :
- Arbre original : ~2-3 MB (59000 nœuds, toujours présent)
- Index par auteur : ~500 KB (1000 auteurs × 59 fichiers × 8 bytes)
- Index par année : ~500 KB (50 années × 1180 fichiers × 8 bytes)
- Index par titre : ~2-3 MB (59000 titres uniques × 8 bytes)
- **Total** : ~5-7 MB

**Quand** : Index construits une seule fois au chargement

### Comparaison

| Scénario | Arbre filtré | Index |
|----------|--------------|-------|
| **Aucun filtre** | 2-3 MB | 5-7 MB |
| **1 filtre actif** | 2-3 MB + 50-100 KB | 5-7 MB |
| **Filtres multiples** | 2-3 MB + 50-100 KB | 5-7 MB |

**Verdict** : Les index utilisent **plus de mémoire** (~2x), mais sont **réutilisables** pour tous les filtres.

---

## Impact sur les performances

### Construction

**Arbre filtré** :
- Temps : O(n) à chaque changement de filtre
- Pour 59000 fichiers : ~50-100 ms
- **Fréquence** : À chaque changement de filtre

**Index** :
- Temps : O(n) une seule fois au chargement
- Pour 59000 fichiers : ~100-200 ms (construction de tous les index)
- **Fréquence** : Une seule fois au démarrage

**Gain** : ⭐⭐⭐⭐⭐ (Excellent) - Construction unique vs reconstruction à chaque changement

### Rendu

**Arbre filtré** :
- Parcourt : ~1000 nœuds (si 1000 matchent)
- Temps : ~5-10 ms pour 1000 nœuds
- **Avantage** : Parcourt seulement les nœuds filtrés

**Index** :
- Parcourt : **59000 nœuds** (tous)
- Vérification : O(1) par nœud (lookup dans unordered_set)
- Temps estimé : ~20-30 ms pour 59000 nœuds (skip rapide)
- **Inconvénient** : Parcourt tous les nœuds

**Gain** : ⭐⭐ (Faible) - L'arbre filtré est plus rapide pour le rendu

### Changement de filtre

**Arbre filtré** :
- Reconstruction : ~50-100 ms
- **Latence** : Perceptible par l'utilisateur

**Index** :
- Pas de reconstruction nécessaire
- Calcul de l'intersection : ~1-5 ms
- **Latence** : Quasi-instantané

**Gain** : ⭐⭐⭐⭐⭐ (Excellent) - Changement de filtre instantané

### Résumé des performances

| Opération | Arbre filtré | Index | Gagnant |
|----------|-------------|-------|---------|
| **Construction initiale** | 0 ms | 100-200 ms | Arbre filtré |
| **Changement de filtre** | 50-100 ms | 1-5 ms | **Index** ⭐ |
| **Rendu (1000 match)** | 5-10 ms | 20-30 ms | Arbre filtré |
| **Rendu (59000 total)** | 5-10 ms | 20-30 ms | Arbre filtré |

**Verdict** : Les index sont **meilleurs pour les changements de filtre**, mais **moins bons pour le rendu**.

---

## Recommandation finale

### ⚠️ Approche hybride recommandée

**Pourquoi** : Les deux approches ont des avantages complémentaires.

### Stratégie proposée

1. **Utiliser les index pour les filtres simples** (1 seul filtre actif)
   - Changement de filtre instantané
   - Pas de reconstruction nécessaire
   - Rendu acceptable (20-30 ms pour 59000 nœuds)

2. **Utiliser l'arbre filtré pour les filtres multiples** (2+ filtres actifs)
   - Intersection complexe évitée
   - Rendu optimisé (5-10 ms pour 1000 nœuds)
   - Reconstruction seulement quand nécessaire

3. **Cache des intersections** pour les combinaisons fréquentes
   - Si l'utilisateur utilise souvent "Auteur X + Année Y", cacher le résultat

### Implémentation suggérée

```cpp
class FilterManager {
    FilterIndexes m_indexes;
    std::unique_ptr<PlaylistNode> m_filteredTree;
    
    // Cache des intersections fréquentes
    std::map<std::pair<std::string, std::string>, 
             std::unordered_set<PlaylistNode*>> m_intersectionCache;
    
    void updateFilters() {
        int activeFilterCount = countActiveFilters();
        
        if (activeFilterCount == 0) {
            // Aucun filtre : utiliser l'arbre original
            m_useFilteredTree = false;
        } else if (activeFilterCount == 1) {
            // 1 filtre : utiliser les index (changement instantané)
            m_useFilteredTree = false;
            m_activeNodes = getNodesFromSingleFilter();
        } else {
            // 2+ filtres : utiliser l'arbre filtré (rendu optimisé)
            rebuildFilteredTree();
            m_useFilteredTree = true;
        }
    }
};
```

### Avantages de l'approche hybride

✅ **Meilleur des deux mondes** :
- Changement de filtre rapide (index)
- Rendu optimisé pour filtres multiples (arbre filtré)

✅ **Complexité modérée** :
- Réutilise le code existant
- Ajoute seulement les index pour les cas simples

✅ **Évolutif** :
- Peut optimiser progressivement
- Cache des intersections pour les cas fréquents

---

## Plan d'implémentation suggéré

### Phase 1 : Index de base (1 semaine)

1. Créer la structure `FilterIndexes`
2. Implémenter `buildIndexes()` au chargement
3. Implémenter le rendu conditionnel avec index (1 filtre)
4. Tests avec 1 filtre actif

**Objectif** : Valider que les index fonctionnent pour les filtres simples

### Phase 2 : Intersection (1 semaine)

1. Implémenter l'intersection de 2 index
2. Gérer les dossiers vides
3. Tests avec 2 filtres actifs

**Objectif** : Valider que les intersections fonctionnent

### Phase 3 : Hybride (1 semaine)

1. Détecter le nombre de filtres actifs
2. Utiliser index pour 1 filtre, arbre filtré pour 2+
3. Cache des intersections fréquentes
4. Tests de performance

**Objectif** : Optimiser selon le contexte

### Phase 4 : Optimisations (optionnel)

1. Mise à jour incrémentale des index
2. Lazy loading des index (construire à la demande)
3. Compression des index (si mémoire critique)

**Objectif** : Affiner les performances

---

## Conclusion

### Verdict final

**Les index en mémoire sont une bonne idée**, mais **pas comme remplacement complet** de l'arbre filtré.

**Recommandation** : **Approche hybride**
- Index pour les filtres simples (1 filtre) → changement instantané
- Arbre filtré pour les filtres multiples (2+ filtres) → rendu optimisé
- Cache des intersections pour les combinaisons fréquentes

**Bénéfices attendus** :
- ✅ Changement de filtre instantané (index)
- ✅ Rendu optimisé pour filtres multiples (arbre filtré)
- ✅ Complexité modérée (réutilise le code existant)
- ✅ Évolutif (peut optimiser progressivement)

**Risques** :
- ⚠️ Complexité supplémentaire (gestion des deux systèmes)
- ⚠️ Tests nécessaires pour valider les performances
- ⚠️ Maintenance du code (deux chemins de code)

**Recommandation finale** : **Implémenter l'approche hybride** pour bénéficier des avantages des deux systèmes.


