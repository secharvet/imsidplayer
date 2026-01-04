# Documentation : Gestion du Rendu des Tree Nodes dans ImGui

## Table des matières
1. [Comment ImGui gère le rendu des Tree Nodes](#comment-imgui-gère-le-rendu-des-tree-nodes)
2. [Architecture de notre système de playlist](#architecture-de-notre-système-de-playlist)
3. [Gestion de l'arbre complet vs arbre filtré](#gestion-de-larbre-complet-vs-arbre-filtré)
4. [Optimisations en place](#optimisations-en-place)
5. [Problèmes potentiels avec 59000 feuilles](#problèmes-potentiels-avec-59000-feuilles)
6. [Recommandations d'optimisation](#recommandations-doptimisation)

---

## Comment ImGui gère le rendu des Tree Nodes

### Principe fondamental : Clip Rect et Culling

**IMPORTANT** : ImGui ne rend PAS tous les nœuds à chaque frame, même si vous appelez `ImGui::TreeNodeEx()` pour tous.

#### 1. **Clip Rect (Rectangle de découpage)**

ImGui utilise un système de **clip rect** (rectangle de découpage) qui détermine quels éléments sont visibles à l'écran :

```cpp
// ImGui calcule automatiquement le clip rect basé sur :
// - La fenêtre visible
// - La zone de scroll
// - Les zones de découpage explicites (ImGui::PushClipRect)
```

**Ce qui se passe réellement** :
- ImGui calcule la position de chaque widget (y compris les tree nodes)
- Si le widget est **hors du clip rect**, ImGui **saute complètement son rendu**
- Seuls les widgets visibles (ou partiellement visibles) sont rendus

#### 2. **Tree Nodes et leur comportement**

Quand vous appelez `ImGui::TreeNodeEx()` :

```cpp
bool nodeOpen = ImGui::TreeNodeEx(node->name.c_str(), flags);
if (nodeOpen) {
    // Rendre les enfants
    for (auto& child : node->children) {
        renderNode(child.get(), depth + 1);
    }
    ImGui::TreePop();
}
```

**Ce qui se passe** :
1. ImGui calcule la position du nœud (basé sur le scroll et la position précédente)
2. Si le nœud est **hors du clip rect**, ImGui :
   - Ne dessine **rien** pour ce nœud
   - Ne met **pas à jour** l'état d'ouverture/fermeture
   - **Continue** à parcourir les enfants pour calculer les positions (mais ne les rend pas)
3. Si le nœud est **dans le clip rect** :
   - Le nœud est rendu
   - Si `nodeOpen == true`, les enfants sont rendus récursivement

#### 3. **Calcul des positions (même pour les nœuds non visibles)**

**Point important** : Même si un nœud n'est pas rendu, ImGui doit **calculer sa position** pour :
- Savoir où commencer le rendu quand l'utilisateur scroll
- Maintenir la cohérence de l'état d'ouverture/fermeture
- Gérer le scroll automatique vers un nœud spécifique

**Cela signifie** :
- Les appels à `ImGui::TreeNodeEx()` sont **toujours exécutés**
- Mais le **rendu réel** (appels OpenGL/DirectX) n'est fait que pour les nœuds visibles
- Le calcul des positions peut être coûteux avec beaucoup de nœuds

---

## Architecture de notre système de playlist

### Structure de données

```cpp
struct PlaylistNode {
    std::string name;
    std::string filepath;
    bool isFolder;
    PlaylistNode* parent;
    std::vector<std::unique_ptr<PlaylistNode>> children;
};
```

### Deux arbres distincts

Notre système maintient **deux arbres** :

1. **Arbre original** (`m_playlist.getRoot()`) :
   - Contient **tous** les fichiers (59000 feuilles)
   - Structure complète de la playlist
   - Jamais modifié (sauf lors du rechargement)

2. **Arbre filtré** (`m_filteredTreeRoot`) :
   - Copie **élaguée** de l'arbre original
   - Contient uniquement les fichiers qui matchent les filtres actifs
   - Reconstruit quand les filtres changent
   - **Peut être nullptr** si aucun filtre n'est actif

### Fonction de rendu principale

```cpp
void UIManager::renderPlaylistTree() {
    // Décision : utiliser l'arbre filtré ou l'arbre original
    PlaylistNode* root = (m_filtersActive && m_filteredTreeRoot) 
                        ? m_filteredTreeRoot.get() 
                        : m_playlist.getRoot();
    
    // Rendu récursif
    for (auto& child : root->children) {
        renderNode(child.get(), 0);
    }
}
```

---

## Gestion de l'arbre complet vs arbre filtré

### Quand utiliser quel arbre ?

#### 1. **Arbre original** (59000 feuilles)
- Utilisé quand **aucun filtre n'est actif** (`m_filtersActive == false`)
- **Avantage** : Pas de copie mémoire, structure originale intacte
- **Inconvénient** : Doit parcourir tous les nœuds pour calculer les positions

#### 2. **Arbre filtré** (beaucoup moins de feuilles)
- Utilisé quand **des filtres sont actifs** (`m_filtersActive == true`)
- **Avantage** : Beaucoup moins de nœuds à parcourir
- **Inconvénient** : Coût de construction de l'arbre filtré

### Construction de l'arbre filtré

```cpp
void UIManager::rebuildFilteredTree() {
    // 1. Créer la fonction de filtre
    auto filterFunc = [this](PlaylistNode* node) -> bool {
        if (node->isFolder) return true;  // Les dossiers passent toujours
        return matchesFilters(node);      // Filtrer les fichiers
    };
    
    // 2. Créer un nouveau nœud racine
    m_filteredTreeRoot = std::make_unique<PlaylistNode>("Playlist", "", true);
    
    // 3. Filtrer récursivement chaque enfant de la racine originale
    for (auto& child : originalRoot->children) {
        auto filteredChild = m_playlist.createFilteredTree(child.get(), filterFunc);
        if (filteredChild) {
            m_filteredTreeRoot->children.push_back(std::move(filteredChild));
        }
    }
}
```

### Fonction de filtrage récursive

```cpp
std::unique_ptr<PlaylistNode> PlaylistManager::createFilteredTree(
    PlaylistNode* sourceNode,
    std::function<bool(PlaylistNode*)> filterFunc) const {
    
    // Pour un FICHIER : créer une copie seulement s'il matche
    if (!sourceNode->isFolder) {
        if (filterFunc(sourceNode)) {
            return std::make_unique<PlaylistNode>(...);  // Copie
        }
        return nullptr;  // Ne matche pas
    }
    
    // Pour un DOSSIER : créer seulement s'il a des enfants qui matchent
    std::vector<std::unique_ptr<PlaylistNode>> filteredChildren;
    for (auto& child : sourceNode->children) {
        auto filteredChild = createFilteredTree(child.get(), filterFunc);
        if (filteredChild) {
            filteredChildren.push_back(std::move(filteredChild));
        }
    }
    
    // Créer le dossier seulement s'il a des enfants filtrés
    if (!filteredChildren.empty()) {
        auto filteredFolder = std::make_unique<PlaylistNode>(...);
        filteredFolder->children = std::move(filteredChildren);
        return filteredFolder;
    }
    
    return nullptr;  // Dossier vide après filtrage
}
```

**Caractéristiques importantes** :
- **Élagage automatique** : Les dossiers vides (sans enfants qui matchent) ne sont pas créés
- **Copie profonde** : L'arbre filtré est une copie complète (mais plus petite)
- **Références conservées** : Les `filepath` sont conservés pour retrouver les nœuds originaux

---

## Optimisations en place

### 1. **Cache de navigation**

```cpp
// Cache pour éviter de recalculer la liste de tous les fichiers
std::vector<PlaylistNode*> m_cachedAllFiles;
int m_cachedCurrentIndex;
bool m_navigationCacheValid;
```

**Utilisation** :
- Recalculé seulement quand nécessaire (changement de filtre, changement de nœud courant)
- Utilisé pour les boutons "Précédent/Suivant"

### 2. **Compteur de nœuds rendus**

```cpp
size_t nodesRendered = 0;
// ... dans renderNode() ...
nodesRendered++;

// Log seulement si problème de performance (> 10ms)
if (renderTime > 10) {
    LOG_WARNING("[UI] renderPlaylistTree: {} ms ({} nœuds rendus)", 
                renderTime, nodesRendered);
}
```

**Utilité** : Détecter les problèmes de performance

### 3. **Scroll intelligent**

```cpp
// Scroller seulement vers le nœud courant si nécessaire
if ((isCurrent && shouldScroll) || (isFirstMatch && m_shouldScrollToFirstMatch)) {
    ImGui::SetScrollHereY(0.5f);
}
```

**Évite** : Le scroll inutile à chaque frame

### 4. **Ouverture automatique des nœuds parents**

```cpp
bool shouldOpen = isParentOfCurrent(node) || (m_filtersActive && node->isFolder);
if (shouldOpen || m_filtersActive) {
    flags |= ImGuiTreeNodeFlags_DefaultOpen;
}
```

**Utilité** : Ouvrir automatiquement les dossiers qui contiennent le fichier courant ou tous les dossiers quand les filtres sont actifs

---

## Problèmes potentiels avec 59000 feuilles

### 1. **Parcours complet même avec clip rect**

**Problème** :
- Même si ImGui ne **rend** que les nœuds visibles, notre code **parcourt** tous les nœuds
- Chaque appel à `ImGui::TreeNodeEx()` fait des calculs (position, état, etc.)
- Avec 59000 nœuds, même un parcours simple peut être coûteux

**Exemple** :
```cpp
// Ce code parcourt TOUS les nœuds, même ceux non visibles
std::function<void(PlaylistNode*, int)> renderNode = [&](PlaylistNode* node, int depth) {
    nodesRendered++;  // Compteur incrémenté pour TOUS les nœuds
    ImGui::PushID(node);  // Appelé pour TOUS les nœuds
    
    bool nodeOpen = ImGui::TreeNodeEx(node->name.c_str(), flags);
    // TreeNodeEx() calcule la position même si le nœud n'est pas visible
    
    if (nodeOpen) {
        for (auto& child : node->children) {
            renderNode(child.get(), depth + 1);  // Récursion pour TOUS
        }
    }
};
```

### 2. **Calcul des positions pour tous les nœuds**

**Problème** :
- ImGui doit connaître la position de **tous** les nœuds pour :
  - Savoir où commencer le rendu quand l'utilisateur scroll
  - Calculer la hauteur totale de la zone scrollable
  - Gérer le scroll vers un nœud spécifique

**Impact** :
- Même si seulement 50 nœuds sont visibles, ImGui doit calculer les positions des 59000 nœuds
- Cela peut prendre plusieurs millisecondes

### 3. **État d'ouverture/fermeture**

**Problème** :
- ImGui stocke l'état d'ouverture/fermeture de chaque nœud
- Avec 59000 nœuds, cela peut consommer de la mémoire
- Les calculs d'état sont faits pour tous les nœuds

### 4. **Construction de l'arbre filtré**

**Problème** :
- Quand les filtres changent, on doit parcourir **tous** les 59000 fichiers
- Pour chaque fichier, on doit vérifier s'il matche les filtres (accès à la base de données)
- Construction d'un nouvel arbre (allocation mémoire)

**Coût** :
- Parcours de 59000 nœuds : ~O(n)
- Vérification des filtres : accès DB pour chaque fichier
- Construction de l'arbre : allocation mémoire pour chaque nœud filtré

---

## Recommandations d'optimisation

### 1. **Virtualisation du rendu (Lazy Rendering)**

**Idée** : Ne parcourir que les nœuds visibles + une marge

**Implémentation** :
```cpp
// Calculer la hauteur visible
float visibleHeight = ImGui::GetWindowHeight();
float scrollY = ImGui::GetScrollY();
float itemHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;

// Calculer quels nœuds sont visibles
int firstVisibleIndex = (int)(scrollY / itemHeight);
int lastVisibleIndex = firstVisibleIndex + (int)(visibleHeight / itemHeight) + 10; // +10 pour marge

// Parcourir seulement les nœuds visibles
```

**Problème** : Difficile à implémenter avec une structure d'arbre (pas un tableau plat)

### 2. **Pagination ou chargement progressif**

**Idée** : Charger seulement une partie de l'arbre à la fois

**Implémentation** :
- Afficher seulement les 1000 premiers fichiers
- Bouton "Charger plus" pour charger les suivants
- Ou chargement automatique lors du scroll

**Avantage** : Réduit drastiquement le nombre de nœuds à parcourir

### 3. **Flattening de l'arbre pour le rendu**

**Idée** : Convertir l'arbre en liste plate pour le rendu, mais garder l'arbre pour la navigation

**Implémentation** :
```cpp
// Créer une liste plate de tous les nœuds visibles (avec leur profondeur)
std::vector<std::pair<PlaylistNode*, int>> flatList;
// ... remplir la liste ...

// Rendre seulement les nœuds dans la zone visible
for (int i = firstVisible; i < lastVisible; i++) {
    renderNode(flatList[i].first, flatList[i].second);
}
```

**Avantage** : Permet la virtualisation du rendu

### 4. **Cache des positions**

**Idée** : Cacher les positions calculées pour éviter de les recalculer à chaque frame

**Implémentation** :
```cpp
struct CachedNode {
    PlaylistNode* node;
    float yPosition;
    bool isOpen;
};

std::map<PlaylistNode*, CachedNode> positionCache;
```

**Problème** : Doit être invalidé quand l'arbre change ou quand l'utilisateur scroll

### 5. **Utiliser ImGuiListClipper (pour les feuilles)**

**Idée** : ImGui fournit `ImGuiListClipper` pour virtualiser les listes

**Problème** : Fonctionne bien pour les listes plates, mais difficile avec des arbres imbriqués

**Exemple** :
```cpp
ImGuiListClipper clipper;
clipper.Begin(allFiles.size());
while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
        // Rendre seulement les fichiers visibles
        renderFile(allFiles[i]);
    }
}
```

### 6. **Optimisation de l'arbre filtré**

**Idée** : Construire l'arbre filtré de manière plus efficace

**Implémentation actuelle** :
- Parcourt tous les nœuds
- Vérifie chaque fichier contre les filtres
- Construit un nouvel arbre

**Optimisation possible** :
- Cache des résultats de filtrage
- Construction incrémentale (ne reconstruire que les parties qui ont changé)
- Parallélisation du filtrage

### 7. **Réduire le nombre de nœuds dans l'arbre original**

**Idée** : Ne pas charger tous les fichiers dans l'arbre au démarrage

**Implémentation** :
- Charger seulement la structure des dossiers
- Charger les fichiers d'un dossier seulement quand il est ouvert
- Lazy loading des fichiers

---

## Conclusion

### État actuel

- **Rendu** : ImGui ne rend que les nœuds visibles (grâce au clip rect)
- **Parcours** : Notre code parcourt **tous** les nœuds à chaque frame
- **Performance** : Acceptable pour < 1000 nœuds, peut être lent avec 59000

### Problème principal

Le problème n'est **pas** le rendu (ImGui gère ça bien), mais le **parcours** de tous les nœuds pour calculer les positions.

### Solutions recommandées (par ordre de priorité)

1. **Court terme** : Utiliser l'arbre filtré dès que possible (réduit le nombre de nœuds)
2. **Moyen terme** : Implémenter le flattening + virtualisation du rendu
3. **Long terme** : Lazy loading des fichiers (ne charger que les dossiers ouverts)

### Mesure de performance

Le code actuel log déjà les temps de rendu :
```cpp
if (renderTime > 10) {
    LOG_WARNING("[UI] renderPlaylistTree: {} ms ({} nœuds rendus)", 
                renderTime, nodesRendered);
}
```

**Si vous voyez des warnings** : C'est le signe qu'il faut optimiser.

---

## Références

- Code source : `src/UIManager.cpp` (fonction `renderPlaylistTree()`)
- Construction de l'arbre filtré : `src/UIManager.cpp` (fonction `rebuildFilteredTree()`)
- Documentation ImGui : https://github.com/ocornut/imgui
- ImGuiListClipper : `third_party/imgui/imgui_widgets.cpp` (ligne ~6000)


