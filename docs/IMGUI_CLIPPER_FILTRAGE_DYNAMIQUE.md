# Virtual Scrolling avec Filtrage Dynamique

## 🎯 Différence Clé : Arbre Unique vs Double Arbre

### Avec Double Arbre (Actuel)

```
Filtre change → rebuildFilteredTree() → Nouvel arbre créé
→ visibleNodes doit être reconstruite
```

### Avec Filtrage Dynamique (Proposé)

```
Filtre change → Rien ! L'arbre reste le même
→ visibleNodes reste la même (tous les nœuds)
→ On filtre juste au rendu avec matchesFilters()
```

---

## 🔄 Comment ça Fonctionne avec Filtrage Dynamique

### Structure de Données

```cpp
class UIManager {
    // L'arbre ne change JAMAIS (structure fixe)
    PlaylistNode* m_root;  // Arbre original, jamais modifié
    
    // État d'ouverture des nœuds (change quand utilisateur ouvre/ferme)
    std::unordered_map<PlaylistNode*, bool> m_openNodes;
    
    // Liste plate de TOUS les nœuds (reconstruite seulement quand ouverture change)
    std::vector<PlaylistNode*> m_flatList;  // Tous les nœuds de l'arbre
    bool m_flatListDirty = false;  // Reconstruire seulement si ouverture change
    
    // Filtres (changent indépendamment de l'arbre)
    std::string m_filterAuthor;
    std::string m_filterYear;
};
```

### Construction de la Liste Plate

```cpp
void buildFlatList(PlaylistNode* root) {
    m_flatList.clear();
    
    std::function<void(PlaylistNode*)> traverse = [&](PlaylistNode* node) {
        if (!node) return;
        
        // Ajouter TOUS les nœuds à la liste (pas de filtrage ici)
        m_flatList.push_back(node);
        
        // Si c'est un dossier ET qu'il est ouvert
        if (node->isFolder && m_openNodes[node]) {
            // Parcourir les enfants
            for (auto& child : node->children) {
                traverse(child.get());
            }
        }
    };
    
    for (auto& child : root->children) {
        traverse(child.get());
    }
}
```

**Important** : Cette liste contient **TOUS les nœuds ouverts**, pas seulement ceux qui matchent les filtres.

---

## 🎨 Rendu avec Filtrage Dynamique

### Principe

1. **Liste plate** : Contient tous les nœuds ouverts (structure de l'arbre)
2. **Clipper** : Sélectionne seulement ceux visibles à l'écran (scroll)
3. **Filtrage** : Vérifie `matchesFilters()` pour chaque nœud rendu

### Code

```cpp
void UIManager::renderPlaylistTree() {
    ImGui::BeginChild("PlaylistTree", ImVec2(0, -60), true);
    
    PlaylistNode* root = m_playlist.getRoot();  // Arbre unique, jamais modifié
    
    // 1. Reconstruire la liste plate SEULEMENT si ouverture change
    if (m_flatListDirty) {
        buildFlatList(root);
        m_flatListDirty = false;
    }
    
    // 2. Utiliser le clipper (gère le scroll automatiquement)
    ImGuiListClipper clipper;
    clipper.Begin(m_flatList.size());  // Tous les nœuds ouverts
    
    while (clipper.Step()) {
        // ImGui calcule DisplayStart/DisplayEnd automatiquement (scroll)
        
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            PlaylistNode* node = m_flatList[i];
            
            // 3. Filtrer au rendu (dynamique)
            if (m_filtersActive && !matchesFilters(node)) {
                // Ne pas rendre ce nœud
                continue;
            }
            
            // 4. Rendre le nœud
            renderNode(node, calculateDepth(node));
        }
    }
    
    ImGui::EndChild();
}
```

---

## 🔄 Quand Reconstruire la Liste ?

### Cas 1 : Ouverture/Fermeture d'un Nœud

```cpp
void renderNode(PlaylistNode* node, int depth) {
    if (node->isFolder) {
        bool wasOpen = m_openNodes[node];
        bool nodeOpen = ImGui::TreeNodeEx(node->name.c_str(), flags);
        
        // Si l'état change, reconstruire la liste
        if (nodeOpen != wasOpen) {
            m_openNodes[node] = nodeOpen;
            m_flatListDirty = true;  // ← Reconstruction nécessaire
        }
        
        // Note : On ne parcourt plus les enfants ici !
        // Le clipper s'en charge via la liste plate
    }
}
```

**Pourquoi** : Quand on ouvre/ferme un dossier, la structure de la liste change (enfants ajoutés/retirés).

### Cas 2 : Changement de Filtres

```cpp
void onFilterChange() {
    m_filterAuthor = newValue;
    m_filtersActive = !m_filterAuthor.empty() || !m_filterYear.empty();
    
    // L'arbre ne change pas !
    // La liste plate ne change pas !
    // On filtre juste au rendu avec matchesFilters()
    
    // Pas besoin de m_flatListDirty = true
}
```

**Pourquoi** : Les filtres ne changent pas la structure de l'arbre, juste la visibilité au rendu.

### Cas 3 : Scroll

```cpp
// L'utilisateur scroll avec la molette
// → ImGui met à jour window->Scroll.y automatiquement
// → clipper.Step() recalcule DisplayStart/DisplayEnd automatiquement
// → On rend les nouveaux nœuds visibles

// Pas besoin de code supplémentaire !
```

**Pourquoi** : Le clipper gère le scroll automatiquement.

---

## 📊 Exemple Concret

### Situation Initiale

```
Arbre :
Root
├─ Dossier A (fermé)
│  ├─ Fichier 1 (auteur: "Rob Hubbard")
│  └─ Fichier 2 (auteur: "Ben Daglish")
├─ Dossier B (ouvert)
│  ├─ Fichier 3 (auteur: "Rob Hubbard")
│  └─ Fichier 4 (auteur: "Ben Daglish")
└─ Dossier C (fermé)

Liste plate (m_flatList) :
[0] Dossier B
[1]   Fichier 3
[2]   Fichier 4

Filtre : auteur = "Rob Hubbard"
```

### Rendu

```cpp
clipper.Begin(3);  // 3 nœuds dans la liste plate

clipper.Step() :
  DisplayStart = 0
  DisplayEnd = 3
  
  for (i = 0; i < 3; i++) {
    node = m_flatList[i]
    
    i=0: Dossier B
      → matchesFilters() ? (dossier, toujours true)
      → Rendre ✅
    
    i=1: Fichier 3
      → matchesFilters() ? (auteur="Rob Hubbard" ✅)
      → Rendre ✅
    
    i=2: Fichier 4
      → matchesFilters() ? (auteur="Ben Daglish" ❌)
      → continue (ne pas rendre) ❌
  }
```

**Résultat** : Dossier B et Fichier 3 sont rendus, Fichier 4 est filtré.

### Action : Utilisateur Ouvre "Dossier A"

```
Frame N :
1. TreeNodeEx("Dossier A") → retourne true
2. m_openNodes["Dossier A"] = true
3. m_flatListDirty = true

Frame N+1 :
1. buildFlatList() reconstruit :
   
   Liste plate (m_flatList) :
   [0] Dossier A  ← Nouveau
   [1]   Fichier 1  ← Nouveau
   [2]   Fichier 2  ← Nouveau
   [3] Dossier B
   [4]   Fichier 3
   [5]   Fichier 4

2. clipper.Begin(6)  // Nouvelle taille
3. Rendu avec filtrage dynamique
```

### Action : Utilisateur Change le Filtre

```
Frame N :
1. m_filterAuthor = "Ben Daglish"
2. m_filtersActive = true

Frame N+1 :
1. buildFlatList() ? NON ! Liste inchangée
2. clipper.Begin(6)  // Même taille
3. Rendu avec nouveau filtre :
   
   i=0: Dossier A → Rendre ✅
   i=1: Fichier 1 → matchesFilters() ? (auteur="Rob Hubbard" ❌) → skip
   i=2: Fichier 2 → matchesFilters() ? (auteur="Ben Daglish" ✅) → Rendre ✅
   i=3: Dossier B → Rendre ✅
   i=4: Fichier 3 → matchesFilters() ? (auteur="Rob Hubbard" ❌) → skip
   i=5: Fichier 4 → matchesFilters() ? (auteur="Ben Daglish" ✅) → Rendre ✅
```

**Résultat** : Même liste, filtrage différent au rendu.

---

## 🎯 Avantages du Filtrage Dynamique

### 1. Pas de Reconstruction de Liste

```cpp
// Avant (double arbre) :
rebuildFilteredTree() → 50-100 ms (parcourt 57k nœuds)

// Après (filtrage dynamique) :
// Rien ! Liste inchangée
// Filtrage au rendu : < 1 ms (seulement nœuds visibles)
```

### 2. Changement de Filtre Instantané

```cpp
// Avant : 50-100 ms pour reconstruire l'arbre filtré
// Après : 0 ms (juste changer m_filterAuthor)
```

### 3. Liste Plate Stable

```cpp
// La liste ne change que quand :
// - Ouverture/fermeture de nœuds (rare)
// - Ajout/suppression de fichiers (très rare)

// La liste ne change PAS quand :
// - Changement de filtres ✅
// - Scroll ✅
```

---

## 🔑 Points Clés

### Ce qui Change la Liste

1. **Ouverture/Fermeture de nœuds** → `m_flatListDirty = true`
2. **Ajout/Suppression de fichiers** → `m_flatListDirty = true`

### Ce qui NE Change PAS la Liste

1. **Changement de filtres** → Liste inchangée, filtrage au rendu
2. **Scroll** → Géré par clipper automatiquement
3. **Sélection de nœud** → Liste inchangée

---

## 📝 Résumé

| Événement | Liste Plate | Clipper | Filtrage |
|-----------|-------------|---------|----------|
| **Ouverture nœud** | Reconstruire ✅ | Auto | Au rendu |
| **Fermeture nœud** | Reconstruire ✅ | Auto | Au rendu |
| **Changement filtre** | Inchangée ✅ | Auto | Au rendu |
| **Scroll** | Inchangée ✅ | Auto (recalcule) | Au rendu |

**En résumé** : Avec filtrage dynamique, la liste plate est stable et ne change que quand la structure de l'arbre change (ouverture/fermeture), pas quand les filtres changent.

