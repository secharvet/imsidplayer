# Qui Met à Jour le Clipper ? Virtual Scrolling avec Arbre

## 🔍 Question

**Avec Virtual Scrolling, qui met à jour le clipper quand on se déplace dans l'arbre ou qu'on ouvre des nœuds ?**

## 📋 Réponse

**ImGui met à jour le clipper AUTOMATIQUEMENT à chaque frame, mais NOUS devons reconstruire la liste des nœuds visibles quand l'arbre change.**

---

## 🎯 Principe du Virtual Scrolling avec Arbre

### Étape 1 : Construire la Liste Plate des Nœuds Visibles

```cpp
// Cette liste doit être reconstruite quand :
// - Un nœud est ouvert/fermé
// - L'utilisateur scroll
// - Les filtres changent

std::vector<PlaylistNode*> visibleNodes;

void buildVisibleNodesList(PlaylistNode* root) {
    visibleNodes.clear();
    
    std::function<void(PlaylistNode*)> traverse = [&](PlaylistNode* node) {
        if (!node) return;
        
        // Ajouter le nœud à la liste
        visibleNodes.push_back(node);
        
        // Si c'est un dossier ET qu'il est ouvert
        if (node->isFolder && isNodeOpen(node)) {
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

### Étape 2 : Utiliser ImGuiListClipper

```cpp
void renderPlaylistTree() {
    // 1. Reconstruire la liste si nécessaire
    if (m_visibleNodesDirty) {
        buildVisibleNodesList(root);
        m_visibleNodesDirty = false;
    }
    
    // 2. Utiliser le clipper
    ImGuiListClipper clipper;
    clipper.Begin(visibleNodes.size());  // ← Nombre total de nœuds visibles
    
    while (clipper.Step()) {
        // clipper.DisplayStart = index du premier nœud visible à l'écran
        // clipper.DisplayEnd = index du dernier nœud visible à l'écran
        // ImGui calcule ça AUTOMATIQUEMENT à chaque frame !
        
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            renderNode(visibleNodes[i], depth);
        }
    }
}
```

---

## 🔄 Qui Met à Jour Quoi ?

### ImGuiListClipper (Automatique)

**ImGui met à jour le clipper AUTOMATIQUEMENT à chaque frame** :

```cpp
// Dans ImGui, à chaque frame :
ImGuiListClipper::Step() {
    // 1. Calcule la position de scroll actuelle
    float scroll_y = window->Scroll.y;
    
    // 2. Calcule quels items sont visibles dans la fenêtre
    float window_height = window->InnerRect.GetHeight();
    float item_height = ...;  // Hauteur d'un item
    
    // 3. Calcule DisplayStart et DisplayEnd
    DisplayStart = (int)(scroll_y / item_height);
    DisplayEnd = (int)((scroll_y + window_height) / item_height) + 1;
    
    // 4. Clamp pour ne pas dépasser la liste
    DisplayStart = ImClamp(DisplayStart, 0, ItemsCount);
    DisplayEnd = ImClamp(DisplayEnd, DisplayStart, ItemsCount);
}
```

**Important** : ImGui fait ça **automatiquement** à chaque frame, en fonction de la position de scroll.

### Notre Code (Manuel)

**NOUS devons reconstruire `visibleNodes` quand l'arbre change** :

```cpp
// Quand un nœud est ouvert/fermé
bool nodeOpen = ImGui::TreeNodeEx(...);
if (nodeOpen != wasOpen) {
    m_visibleNodesDirty = true;  // ← Marquer comme dirty
    m_openNodes[node] = nodeOpen;  // ← Sauvegarder l'état
}

// À la frame suivante, buildVisibleNodesList() sera appelé
```

---

## 📊 Exemple Concret : Ouverture d'un Dossier

### Situation Initiale

```
Liste visibleNodes (avant) :
[0] Dossier A
[1] Dossier B
[2] Dossier C

Clipper (frame N) :
DisplayStart = 0
DisplayEnd = 2
→ Rend Dossier A et Dossier B
```

### Action : Utilisateur Ouvre "Dossier A"

```
Frame N :
1. ImGui::TreeNodeEx("Dossier A") → retourne true (maintenant ouvert)
2. On détecte le changement : m_visibleNodesDirty = true
3. On sauvegarde : m_openNodes["Dossier A"] = true

Frame N+1 :
1. renderPlaylistTree() détecte m_visibleNodesDirty
2. buildVisibleNodesList() reconstruit la liste :
   
   Liste visibleNodes (après) :
   [0] Dossier A
   [1]   Fichier 1
   [2]   Fichier 2
   [3]   Fichier 3
   [4]   Fichier 4
   [5] Dossier B
   [6] Dossier C

3. clipper.Begin(7)  // ← Nouvelle taille
4. ImGui calcule automatiquement DisplayStart/DisplayEnd
5. On rend seulement les nœuds visibles à l'écran
```

---

## 🔄 Gestion des Événements

### 1. Ouverture/Fermeture d'un Nœud

```cpp
std::unordered_map<PlaylistNode*, bool> m_openNodes;  // État d'ouverture
bool m_visibleNodesDirty = false;  // Flag de reconstruction

void renderNode(PlaylistNode* node, int depth) {
    if (node->isFolder) {
        // Récupérer l'état précédent
        bool wasOpen = m_openNodes[node];
        
        // Appeler TreeNodeEx
        bool nodeOpen = ImGui::TreeNodeEx(node->name.c_str(), flags);
        
        // Si l'état a changé, marquer comme dirty
        if (nodeOpen != wasOpen) {
            m_openNodes[node] = nodeOpen;
            m_visibleNodesDirty = true;  // ← Reconstruction nécessaire
        }
        
        // Si ouvert, parcourir les enfants (mais seulement ceux visibles grâce au clipper)
        if (nodeOpen) {
            // ... rendu des enfants
        }
    }
}
```

### 2. Scroll

**ImGui gère le scroll AUTOMATIQUEMENT** :

```cpp
// L'utilisateur scroll avec la molette
// → ImGui met à jour window->Scroll.y
// → ImGuiListClipper::Step() recalcule DisplayStart/DisplayEnd
// → On rend les nouveaux nœuds visibles

// Pas besoin de code supplémentaire !
```

### 3. Changement de Filtres

```cpp
void rebuildFilteredTree() {
    // Reconstruire l'arbre filtré
    // ...
    
    // Marquer comme dirty pour reconstruire la liste
    m_visibleNodesDirty = true;
    m_openNodes.clear();  // Réinitialiser les états d'ouverture
}
```

---

## 🎯 Architecture Complète

### Structure de Données

```cpp
class UIManager {
    // État de l'arbre
    std::unordered_map<PlaylistNode*, bool> m_openNodes;  // État d'ouverture par nœud
    std::vector<PlaylistNode*> m_visibleNodes;  // Liste plate des nœuds visibles
    bool m_visibleNodesDirty = true;  // Flag de reconstruction
    
    // Méthodes
    void buildVisibleNodesList(PlaylistNode* root);
    void renderPlaylistTree();
    void renderNode(PlaylistNode* node, int depth);
};
```

### Flux Complet

```
Frame N :
┌─────────────────────────────────────────┐
│ 1. renderPlaylistTree()                 │
│    └─> if (m_visibleNodesDirty)         │
│            buildVisibleNodesList()      │
│                                         │
│ 2. clipper.Begin(visibleNodes.size())  │
│                                         │
│ 3. while (clipper.Step())              │
│    └─> ImGui calcule DisplayStart/End │
│        (automatique, basé sur scroll)   │
│                                         │
│ 4. for (i = DisplayStart; i < End)     │
│    └─> renderNode(visibleNodes[i])      │
│        └─> TreeNodeEx()                 │
│            └─> Si changement d'état    │
│                └─> m_visibleNodesDirty = true
└─────────────────────────────────────────┘

Frame N+1 :
┌─────────────────────────────────────────┐
│ 1. renderPlaylistTree()                 │
│    └─> if (m_visibleNodesDirty) ✅     │
│            buildVisibleNodesList()      │
│            (liste reconstruite)         │
│                                         │
│ 2. clipper.Begin(visibleNodes.size())  │
│    (nouvelle taille)                   │
│                                         │
│ 3. while (clipper.Step())              │
│    └─> ImGui recalcule DisplayStart/End│
│        (automatique)                    │
│                                         │
│ 4. for (i = DisplayStart; i < End)     │
│    └─> renderNode(visibleNodes[i])     │
└─────────────────────────────────────────┘
```

---

## 🔑 Points Clés

### Ce que fait ImGui (Automatique)

1. **Calcule DisplayStart/DisplayEnd** à chaque frame
2. **Gère le scroll** automatiquement
3. **Met à jour le clipper** en fonction de la position de scroll

### Ce que nous devons faire (Manuel)

1. **Construire la liste plate** `visibleNodes` des nœuds visibles
2. **Détecter les changements** (ouverture/fermeture de nœuds)
3. **Marquer comme dirty** quand l'arbre change
4. **Reconstruire la liste** à la frame suivante

---

## 📝 Exemple de Code Complet

```cpp
void UIManager::renderPlaylistTree() {
    ImGui::BeginChild("PlaylistTree", ImVec2(0, -60), true);
    
    PlaylistNode* root = m_playlist.getRoot();
    
    // 1. Reconstruire la liste si nécessaire
    if (m_visibleNodesDirty) {
        buildVisibleNodesList(root);
        m_visibleNodesDirty = false;
    }
    
    // 2. Utiliser le clipper
    ImGuiListClipper clipper;
    clipper.Begin(m_visibleNodes.size());
    
    while (clipper.Step()) {
        // ImGui a calculé DisplayStart et DisplayEnd automatiquement
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            renderNode(m_visibleNodes[i], calculateDepth(m_visibleNodes[i]));
        }
    }
    
    ImGui::EndChild();
}

void UIManager::buildVisibleNodesList(PlaylistNode* root) {
    m_visibleNodes.clear();
    
    std::function<void(PlaylistNode*)> traverse = [&](PlaylistNode* node) {
        if (!node) return;
        
        m_visibleNodes.push_back(node);
        
        // Si c'est un dossier ET qu'il est ouvert
        if (node->isFolder && m_openNodes[node]) {
            for (auto& child : node->children) {
                traverse(child.get());
            }
        }
    };
    
    for (auto& child : root->children) {
        traverse(child.get());
    }
}

void UIManager::renderNode(PlaylistNode* node, int depth) {
    if (node->isFolder) {
        bool wasOpen = m_openNodes[node];
        bool nodeOpen = ImGui::TreeNodeEx(node->name.c_str(), flags);
        
        // Détecter le changement
        if (nodeOpen != wasOpen) {
            m_openNodes[node] = nodeOpen;
            m_visibleNodesDirty = true;  // ← Reconstruction à la prochaine frame
        }
        
        // Note : On ne parcourt plus les enfants ici !
        // Le clipper s'en charge via la liste plate
    } else {
        ImGui::Selectable(node->name.c_str());
    }
}
```

---

## 🎯 Résumé

| Élément | Qui le Met à Jour ? | Quand ? |
|---------|-------------------|---------|
| **Clipper.DisplayStart/End** | ImGui (automatique) | À chaque frame, basé sur scroll |
| **visibleNodes** | Notre code (manuel) | Quand l'arbre change (nœud ouvert/fermé) |
| **m_openNodes** | Notre code (manuel) | Quand TreeNodeEx change d'état |
| **m_visibleNodesDirty** | Notre code (manuel) | Quand on détecte un changement |

**En résumé** : ImGui gère le scroll automatiquement, mais nous devons gérer la reconstruction de la liste quand l'arbre change.

