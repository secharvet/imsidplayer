# Optimisation Virtual Scrolling pour l'Arbre de Playlist

## 📊 Analyse du Problème Actuel

### Résultats de Mesure
- **Total** : 57 333 nœuds dans l'arbre complet
- **Visibles** : ~230 nœuds (ceux actuellement visibles à l'écran)
- **Rendu** : 230 nœuds (ImGui fait déjà du clipping)
- **Temps** : 1-5 ms par frame

### Problème Identifié
**Correction importante** : En fait, le code actuel ne parcourt **que les nœuds visibles** grâce à `TreeNodeEx` qui ne rend les enfants que si le dossier est ouvert. 

Cependant, il y a quand même des opportunités d'optimisation :
- **Récursion à chaque frame** : Même si on ne parcourt que les visibles, on fait une récursion complète à chaque frame
- **Pas de virtual scrolling** : On rend tous les nœuds visibles (~230), même ceux hors écran
- **Pas de cache** : On recalcule tout à chaque frame, même si rien n'a changé
- **Filtrage** : Si on utilise les filtres, on reconstruit l'arbre filtré à chaque changement

L'optimisation avec liste plate + virtual scrolling permettra de :
- Ne rendre que les ~50-100 éléments visibles à l'écran (au lieu de 230)
- Mettre en cache la liste plate et ne la reconstruire que quand nécessaire
- Éviter la récursion à chaque frame

## 🎯 Principe de l'Optimisation

### Concept Clé : Liste Plate des Nœuds Visibles

Au lieu de parcourir récursivement tout l'arbre à chaque frame, on va :

1. **Construire une liste plate** des nœuds visibles (uniquement ceux dont tous les parents sont ouverts)
2. **Mettre à jour cette liste** uniquement quand l'état d'ouverture change (ouverture/fermeture de dossiers)
3. **Utiliser ImGuiListClipper** pour ne rendre que les éléments visibles à l'écran dans cette liste plate

### Avantages
- ✅ **Parcours unique** : On ne parcourt l'arbre qu'une seule fois par changement d'état
- ✅ **Virtual scrolling** : ImGuiListClipper ne rend que ~50-100 éléments visibles à l'écran
- ✅ **Cache intelligent** : La liste plate est mise en cache et invalidée seulement quand nécessaire
- ✅ **Performance** : Gain estimé de **10-50x** en fonction du nombre de nœuds

## 📐 Architecture et Structures de Données

### Structure `FlatNode`
Représente un nœud dans la liste plate avec ses informations de rendu :

```cpp
struct FlatNode {
    PlaylistNode* node;        // Pointeur vers le nœud original
    int depth;                 // Profondeur dans l'arbre (pour l'indentation)
    size_t index;              // Index dans la liste plate
    bool cachedYPositionValid; // Cache pour la position Y (optionnel)
    float cachedYPosition;      // Position Y calculée (optionnel)
};
```

### Structure `Indexes`
Index inversés pour le filtrage rapide (optionnel, pour plus tard) :

```cpp
struct Indexes {
    std::unordered_map<std::string, std::vector<size_t>> byAuthor; // Auteur -> indices
    std::unordered_map<std::string, std::vector<size_t>> byYear;  // Année -> indices
};
```

### Membres de Classe à Ajouter dans `UIManager`

```cpp
class UIManager {
private:
    // Liste plate des nœuds visibles
    std::vector<FlatNode> m_flatList;
    bool m_flatListValid;
    
    // État d'ouverture des dossiers (pour savoir quels dossiers sont ouverts)
    std::unordered_set<PlaylistNode*> m_openFolders;
    
    // Index inversés pour filtrage (optionnel)
    Indexes m_indexes;
    bool m_indexesValid;
    
    // Indices actifs après filtrage
    std::unordered_set<size_t> m_activeIndices;
    
    // Hauteur d'un élément (pour le virtual scrolling)
    float m_itemHeight;
    
    // Méthodes
    void buildFlatList();              // Construire la liste plate
    void invalidateFlatList();         // Invalider le cache
    void applyFiltersOptimized();      // Appliquer les filtres avec index (optionnel)
};
```

## 🔧 Étapes d'Implémentation

### Étape 1 : Construire la Liste Plate des Nœuds Visibles

**Fonction `buildFlatList()`** :
- Parcourt l'arbre récursivement
- Ajoute uniquement les nœuds dont **tous les parents sont ouverts**
- Stocke la profondeur pour l'indentation
- Met à jour `m_openFolders` pour suivre l'état d'ouverture

**Pseudo-code** :
```cpp
void UIManager::buildFlatList() {
    if (m_flatListValid) return;
    
    m_flatList.clear();
    PlaylistNode* root = m_playlist.getRoot();
    
    std::function<void(PlaylistNode*, int)> flatten = [&](PlaylistNode* node, int depth) {
        if (!node) return;
        
        // Vérifier si le nœud est visible (tous ses parents sont ouverts)
        bool isVisible = true;
        if (node->parent) {
            PlaylistNode* parent = node->parent;
            while (parent) {
                if (m_openFolders.find(parent) == m_openFolders.end()) {
                    isVisible = false;
                    break;
                }
                parent = parent->parent;
            }
        }
        
        // Ajouter seulement si visible
        if (isVisible) {
            FlatNode flatNode;
            flatNode.node = node;
            flatNode.depth = depth;
            flatNode.index = m_flatList.size();
            m_flatList.push_back(flatNode);
            
            // Si c'est un dossier ouvert, continuer avec les enfants
            if (node->isFolder && m_openFolders.find(node) != m_openFolders.end()) {
                for (auto& child : node->children) {
                    flatten(child.get(), depth + 1);
                }
            }
        }
    };
    
    for (auto& child : root->children) {
        flatten(child.get(), 0);
    }
    
    m_flatListValid = true;
}
```

### Étape 2 : Gérer l'État d'Ouverture des Dossiers

**Mise à jour de `m_openFolders`** :
- Quand un dossier est ouvert via `TreeNodeEx`, ajouter à `m_openFolders`
- Quand un dossier est fermé, retirer de `m_openFolders` et invalider la liste plate
- Initialiser avec les dossiers racine ouverts par défaut

**Dans `renderPlaylistTree()`** :
```cpp
if (node->isFolder) {
    bool nodeOpen = ImGui::TreeNodeEx(...);
    
    if (nodeOpen) {
        m_openFolders.insert(node);
        // Rendre les enfants...
        ImGui::TreePop();
    } else {
        if (m_openFolders.find(node) != m_openFolders.end()) {
            m_openFolders.erase(node);
            invalidateFlatList(); // Invalider car la liste a changé
        }
    }
}
```

### Étape 3 : Utiliser ImGuiListClipper pour le Virtual Scrolling

**Fonction `renderPlaylistTree()` modifiée** :
- Construire la liste plate si nécessaire
- Utiliser `ImGuiListClipper` pour ne rendre que les éléments visibles
- Calculer l'indentation basée sur `depth`

**Pseudo-code** :
```cpp
void UIManager::renderPlaylistTree() {
    ImGui::BeginChild("PlaylistTree", ImVec2(0, -60), true);
    
    // Construire la liste plate si nécessaire
    if (!m_flatListValid) {
        buildFlatList();
    }
    
    // Calculer la hauteur d'un élément
    m_itemHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;
    
    // Utiliser ImGuiListClipper pour le virtual scrolling
    ImGuiListClipper clipper;
    clipper.Begin(m_flatList.size(), m_itemHeight);
    
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            const FlatNode& flatNode = m_flatList[i];
            PlaylistNode* node = flatNode.node;
            
            // Rendre le nœud avec l'indentation appropriée
            renderFlatNode(flatNode);
        }
    }
    
    clipper.End();
    ImGui::EndChild();
}
```

### Étape 4 : Fonction de Rendu d'un Nœud Plat

**Fonction `renderFlatNode()`** :
- Gère l'indentation basée sur `depth`
- Rend les dossiers avec `TreeNodeEx` (et met à jour `m_openFolders`)
- Rend les fichiers avec `Selectable`

**Pseudo-code** :
```cpp
void UIManager::renderFlatNode(const FlatNode& flatNode) {
    PlaylistNode* node = flatNode.node;
    if (!node) return;
    
    // Indentation
    float indentAmount = flatNode.depth * 15.0f;
    if (flatNode.depth > 0) {
        ImGui::Indent(indentAmount);
    }
    
    ImGui::PushID(node);
    
    if (node->isFolder) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
        bool nodeOpen = ImGui::TreeNodeEx(node->name.c_str(), flags);
        
        if (nodeOpen) {
            m_openFolders.insert(node);
            ImGui::TreePop();
        } else {
            if (m_openFolders.find(node) != m_openFolders.end()) {
                m_openFolders.erase(node);
                invalidateFlatList();
            }
        }
    } else {
        // Rendre un fichier
        if (ImGui::Selectable(node->name.c_str(), ...)) {
            // Gérer la sélection...
        }
    }
    
    ImGui::PopID();
    
    if (flatNode.depth > 0) {
        ImGui::Unindent(indentAmount);
    }
}
```

### Étape 5 : Invalidation Intelligente

**Quand invalider la liste plate** :
- Quand un dossier est ouvert/fermé
- Quand la playlist change (ajout/suppression de fichiers)
- Quand les filtres changent (si on utilise les index)

**Fonction `invalidateFlatList()`** :
```cpp
void UIManager::invalidateFlatList() {
    m_flatListValid = false;
    m_indexesValid = false;
    m_activeIndices.clear();
}
```

## 🎨 Intégration avec le Système de Filtrage (Optionnel)

Si on veut optimiser aussi le filtrage, on peut utiliser des **index inversés** :

1. **Construire les index** : Parcourir la liste plate et indexer par auteur/année
2. **Appliquer les filtres** : Utiliser les index pour trouver rapidement les nœuds correspondants
3. **Mettre à jour `m_activeIndices`** : Stocker les indices des nœuds qui matchent les filtres
4. **Rendre seulement les actifs** : Dans `renderPlaylistTree()`, ne rendre que les nœuds dans `m_activeIndices`

## 📈 Gains de Performance Attendus

### Avant Optimisation
- Parcourt **57 333 nœuds** à chaque frame
- Temps : 1-5 ms (dépend du nombre de nœuds)

### Après Optimisation
- Parcourt **~230 nœuds visibles** une seule fois (quand l'état change)
- Rendu de **~50-100 éléments** visibles à l'écran avec ImGuiListClipper
- Temps estimé : **0.1-0.5 ms** (gain de **10-50x**)

## 🔄 Ordre d'Implémentation Recommandé

1. ✅ **Étape 1** : Ajouter les structures de données (`FlatNode`, membres de classe)
2. ✅ **Étape 2** : Implémenter `buildFlatList()` (sans virtual scrolling d'abord)
3. ✅ **Étape 3** : Modifier `renderPlaylistTree()` pour utiliser la liste plate
4. ✅ **Étape 4** : Gérer `m_openFolders` et invalidation
5. ✅ **Étape 5** : Ajouter `ImGuiListClipper` pour le virtual scrolling
6. ✅ **Étape 6** : Tester et optimiser

## ⚠️ Points d'Attention

1. **Synchronisation** : S'assurer que `m_openFolders` reste synchronisé avec l'état réel
2. **Scroll** : Gérer le scroll vers le nœud courant avec `ImGuiListClipper`
3. **Sélection** : Maintenir la sélection courante lors de l'invalidation
4. **Filtres** : Si on utilise les filtres, s'assurer que la liste plate est filtrée correctement

## 📝 Résumé

Cette optimisation transforme le rendu de l'arbre d'un **parcours récursif complet** à chaque frame en un **rendu de liste plate avec virtual scrolling**, ce qui réduit drastiquement le nombre de nœuds traités et améliore significativement les performances.

