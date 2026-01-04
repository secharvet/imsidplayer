# Analyse du Système de Filtrage - Arbre Unique vs Double Arbre

## 📋 État Actuel : Système à Double Arbre

### Architecture Actuelle

Le système actuel utilise **deux arbres en mémoire** :

1. **Arbre Général (`m_playlist.getRoot()`)**
   - Arbre complet avec tous les 57 333 nœuds
   - **Jamais modifié** après le chargement initial
   - Source de vérité unique
   - Structure : `PlaylistNode` avec `name`, `filepath`, `isFolder`, `children`, `parent`

2. **Arbre Filtré (`m_filteredTreeRoot`)**
   - **Créé dynamiquement** à chaque changement de filtre
   - Copie partielle de l'arbre général
   - Contient uniquement les nœuds qui matchent les filtres actifs
   - **Détruit** quand les filtres sont désactivés

### Flux de Filtrage Actuel

```
┌─────────────────────────────────────────────────────────────┐
│ 1. Utilisateur change un filtre (auteur/année)              │
└─────────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────────┐
│ 2. rebuildFilteredTree() appelé                              │
│    - Parcourt TOUT l'arbre général (57 333 nœuds)           │
│    - Crée une copie filtrée avec createFilteredTree()       │
│    - Élague les dossiers vides                               │
└─────────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────────┐
│ 3. m_filteredTreeRoot créé (nouveau std::unique_ptr)        │
│    - Structure complète mais filtrée                         │
│    - Tous les parents/enfants recréés                       │
└─────────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────────┐
│ 4. renderPlaylistTree() utilise m_filteredTreeRoot          │
│    - Parcourt seulement les nœuds visibles (~230)            │
│    - TreeNodeEx gère l'ouverture/fermeture                  │
└─────────────────────────────────────────────────────────────┘
```

### Code Clé

**Création de l'arbre filtré** (`src/UIManager.cpp:1112-1200`) :
```cpp
void UIManager::rebuildFilteredTree() {
    // Créer un nœud racine pour l'arbre filtré
    m_filteredTreeRoot = std::make_unique<PlaylistNode>("Playlist", "", true);
    
    // Filtrer chaque enfant de la racine
    for (auto& child : originalRoot->children) {
        auto filteredChild = m_playlist.createFilteredTree(child.get(), filterFunc);
        if (filteredChild) {
            m_filteredTreeRoot->children.push_back(std::move(filteredChild));
        }
    }
}
```

**Utilisation au rendu** (`src/UIManager.cpp:652`) :
```cpp
PlaylistNode* root = (m_filtersActive && m_filteredTreeRoot) 
    ? m_filteredTreeRoot.get() 
    : m_playlist.getRoot();
```

**Création récursive** (`src/PlaylistManager.cpp:209-253`) :
```cpp
std::unique_ptr<PlaylistNode> createFilteredTree(
    PlaylistNode* sourceNode,
    std::function<bool(PlaylistNode*)> filterFunc) const {
    
    // Pour les fichiers : copie seulement si match
    if (!sourceNode->isFolder) {
        if (filterFunc(sourceNode)) {
            return std::make_unique<PlaylistNode>(...);
        }
        return nullptr;
    }
    
    // Pour les dossiers : copie seulement s'ils ont des enfants filtrés
    // Parcourt récursivement TOUS les enfants
    for (auto& child : sourceNode->children) {
        auto filteredChild = createFilteredTree(child.get(), filterFunc);
        // ...
    }
}
```

### Problèmes Identifiés

1. **Coût de Reconstruction**
   - À chaque changement de filtre, on parcourt **tous les 57 333 nœuds**
   - On crée une nouvelle structure complète en mémoire
   - Allocation/désallocation de nombreux `PlaylistNode`

2. **Gestion de `currentNode` Complexe**
   - Quand on désactive les filtres, `currentNode` pointe vers l'arbre filtré qui va être détruit
   - Il faut chercher le nœud correspondant dans l'arbre original par `filepath`
   - Code complexe avec `findNodeByPath()` (lignes 1231-1240, 1267-1276)

3. **Double Mémoire**
   - Quand les filtres sont actifs, on a **deux arbres en mémoire**
   - L'arbre filtré peut contenir plusieurs milliers de nœuds

4. **Synchronisation**
   - Si l'arbre général change (drag & drop, ajout), l'arbre filtré devient obsolète
   - Il faut le reconstruire

---

## 💡 Proposition : Système à Arbre Unique avec Filtrage au Rendu

### Concept

**Un seul arbre en mémoire** (l'arbre général), et on décide de la visibilité au moment du rendu.

### Principe

```
┌─────────────────────────────────────────────────────────────┐
│ 1. Un seul arbre : m_playlist.getRoot() (57 333 nœuds)      │
│    - Jamais modifié                                          │
│    - Source de vérité unique                                 │
└─────────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────────┐
│ 2. renderPlaylistTree() parcourt l'arbre                    │
│    - À chaque nœud, vérifie matchesFilters(node)           │
│    - Si fichier : affiche seulement si match                │
│    - Si dossier : affiche seulement s'il a des enfants      │
│      visibles (récursion)                                   │
└─────────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────────┐
│ 3. TreeNodeEx gère l'ouverture/fermeture                    │
│    - Seulement ~230 nœuds visibles sont rendus              │
│    - Les nœuds non-visibles ne sont pas parcourus          │
└─────────────────────────────────────────────────────────────┘
```

### Implémentation Proposée

**Modification de `renderNode`** :
```cpp
std::function<void(PlaylistNode*, int)> renderNode = [&](PlaylistNode* node, int depth) {
    if (!node) return;
    
    // Vérifier si le nœud doit être visible
    bool shouldShow = true;
    if (m_filtersActive) {
        if (node->isFolder) {
            // Pour un dossier : vérifier s'il a des enfants visibles
            // (on peut optimiser avec un cache)
            shouldShow = hasVisibleChildren(node);
        } else {
            // Pour un fichier : vérifier directement le filtre
            shouldShow = matchesFilters(node);
        }
    }
    
    // Si le nœud n'est pas visible, ne pas le rendre
    if (!shouldShow) {
        return; // ⚠️ MAIS : on doit quand même parcourir les enfants pour les dossiers !
    }
    
    // Rendu normal...
    if (node->isFolder) {
        bool nodeOpen = ImGui::TreeNodeEx(...);
        if (nodeOpen) {
            for (auto& child : node->children) {
                renderNode(child.get(), depth + 1);
            }
            ImGui::TreePop();
        }
    } else {
        ImGui::Selectable(...);
    }
};
```

### Problème Identifié : Parcours des Dossiers

**⚠️ Problème critique** : Pour savoir si un dossier doit être affiché, il faut vérifier s'il a des enfants visibles. Cela nécessite de **parcourir récursivement tous ses enfants**, même s'ils ne sont pas visibles.

**Exemple** :
```
Dossier A/
  ├─ Fichier 1 (match)
  ├─ Fichier 2 (ne match pas)
  └─ Dossier B/
      ├─ Fichier 3 (ne match pas)
      └─ Fichier 4 (match)
```

Pour afficher "Dossier A", il faut parcourir tous ses enfants pour trouver "Fichier 1" ou "Fichier 4".

**Solution possible** : Cache de visibilité par nœud
```cpp
// Cache : nœud -> bool (a des enfants visibles)
std::unordered_map<PlaylistNode*, bool> m_visibilityCache;

bool hasVisibleChildren(PlaylistNode* node) {
    if (m_visibilityCache.find(node) != m_visibilityCache.end()) {
        return m_visibilityCache[node];
    }
    
    // Parcourir récursivement pour trouver un enfant visible
    bool hasVisible = false;
    for (auto& child : node->children) {
        if (child->isFolder) {
            hasVisible = hasVisibleChildren(child.get());
        } else {
            hasVisible = matchesFilters(child.get());
        }
        if (hasVisible) break;
    }
    
    m_visibilityCache[node] = hasVisible;
    return hasVisible;
}
```

---

## ⚖️ Comparaison : Double Arbre vs Arbre Unique

### Double Arbre (Actuel)

#### ✅ Avantages
1. **Rendu simple** : On parcourt seulement l'arbre filtré (~230 nœuds visibles)
2. **Pas de vérification de filtre au rendu** : Tout est déjà filtré
3. **Structure claire** : L'arbre filtré est une copie exacte de la structure visible
4. **Navigation simple** : `currentNode` pointe directement vers l'arbre filtré

#### ❌ Inconvénients
1. **Coût de reconstruction** : Parcourt 57 333 nœuds à chaque changement de filtre
2. **Double mémoire** : Deux arbres en mémoire quand filtres actifs
3. **Gestion complexe de `currentNode`** : Conversion arbre filtré ↔ arbre original
4. **Synchronisation** : Si l'arbre général change, l'arbre filtré devient obsolète

### Arbre Unique avec Filtrage au Rendu

#### ✅ Avantages
1. **Un seul arbre** : Pas de duplication, économie mémoire
2. **Pas de reconstruction** : Pas besoin de recréer l'arbre à chaque changement
3. **`currentNode` simple** : Pointe toujours vers l'arbre original
4. **Synchronisation automatique** : L'arbre est toujours à jour

#### ❌ Inconvénients
1. **Vérification au rendu** : `matchesFilters()` appelé pour chaque nœud visible
2. **Parcours des dossiers** : Pour savoir si un dossier est visible, il faut parcourir ses enfants
3. **Cache nécessaire** : Pour éviter de parcourir récursivement à chaque frame
4. **Complexité du rendu** : La logique de filtrage est dans `renderNode`

---

## 🎯 Analyse de Performance

### Scénario 1 : Changement de Filtre

**Double Arbre** :
- Parcourt **57 333 nœuds** pour créer l'arbre filtré
- Temps : ~50-100ms (estimation)
- Mémoire : +X Mo (arbre filtré)

**Arbre Unique** :
- Invalide le cache de visibilité
- Temps : ~1ms (juste invalider)
- Mémoire : +Y Ko (cache de visibilité)

**Gagnant** : Arbre Unique ✅

### Scénario 2 : Rendu (Frame)

**Double Arbre** :
- Parcourt ~230 nœuds visibles
- Pas de vérification de filtre
- Temps : ~1-5ms

**Arbre Unique** :
- Parcourt ~230 nœuds visibles
- Vérifie `matchesFilters()` pour chaque nœud (~230 appels)
- Vérifie `hasVisibleChildren()` pour chaque dossier ouvert (~50 appels)
- Avec cache : ~230 appels à `matchesFilters()` + ~50 lookups cache
- Temps : ~2-6ms (légèrement plus lent)

**Gagnant** : Double Arbre (mais la différence est minime) ⚠️

### Scénario 3 : Scroll/Ouverture de Dossier

**Double Arbre** :
- Parcourt seulement les nœuds visibles (~230)
- Pas de vérification de filtre

**Arbre Unique** :
- Parcourt seulement les nœuds visibles (~230)
- Vérifie `matchesFilters()` pour les nouveaux nœuds
- Avec cache : lookup rapide

**Gagnant** : Égalité ⚖️

---

## 💭 Recommandation

### Option Recommandée : **Arbre Unique avec Cache de Visibilité**

**Raisons** :
1. **Performance globale meilleure** : Le changement de filtre est beaucoup plus rapide (1ms vs 50-100ms)
2. **Économie mémoire** : Pas de duplication d'arbre
3. **Simplicité** : `currentNode` pointe toujours vers l'arbre original
4. **Scalabilité** : Si on ajoute plus de filtres, pas besoin de reconstruire l'arbre

**Implémentation** :
1. Supprimer `m_filteredTreeRoot` et `rebuildFilteredTree()`
2. Ajouter un cache de visibilité : `std::unordered_map<PlaylistNode*, bool> m_visibilityCache`
3. Invalider le cache quand les filtres changent
4. Modifier `renderNode` pour vérifier `matchesFilters()` et `hasVisibleChildren()`
5. Utiliser le cache pour éviter les parcours récursifs répétés

**Optimisations supplémentaires** :
- Cache des métadonnées : Éviter les appels répétés à `m_database.getMetadata()`
- Cache par nœud : `m_visibilityCache[node] = hasVisible`
- Invalidation sélective : Invalider seulement les nœuds affectés

### Option Alternative : **Hybride**

Garder le double arbre mais optimiser :
- Ne reconstruire que si nécessaire (dirty flag)
- Utiliser un pool d'allocations pour éviter les allocations répétées
- Cache des résultats de filtrage pour éviter de refiltrer les mêmes nœuds

**Mais** : La complexité augmente sans gain significatif.

---

## 📊 Conclusion

**La proposition de l'utilisateur est excellente** : utiliser un seul arbre et décider de la visibilité au rendu.

**Avantages principaux** :
- ✅ Pas de reconstruction coûteuse à chaque changement de filtre
- ✅ Économie mémoire (pas de duplication)
- ✅ Simplicité (`currentNode` toujours valide)
- ✅ Performance au rendu similaire (avec cache)

**Points d'attention** :
- ⚠️ Nécessite un cache de visibilité pour éviter les parcours récursifs répétés
- ⚠️ La logique de rendu devient légèrement plus complexe
- ⚠️ Il faut gérer l'invalidation du cache

**Recommandation finale** : **Implémenter l'arbre unique avec cache de visibilité** 🎯

