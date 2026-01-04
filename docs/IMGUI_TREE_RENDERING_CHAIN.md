# Chaîne d'Appels ImGui pour Rendre un Arbre

## 🌳 Exemple Simple : Arbre à 3 Nœuds et 12 Feuilles

### Structure de l'Arbre

```
Root
├─ Dossier A (ouvert)
│  ├─ Fichier 1
│  ├─ Fichier 2
│  ├─ Fichier 3
│  └─ Fichier 4
├─ Dossier B (fermé)
│  ├─ Fichier 5
│  ├─ Fichier 6
│  ├─ Fichier 7
│  └─ Fichier 8
└─ Dossier C (ouvert)
   ├─ Fichier 9
   ├─ Fichier 10
   ├─ Fichier 11
   └─ Fichier 12
```

**Total** : 3 dossiers + 12 fichiers = 15 nœuds
**Visibles** : Dossier A (ouvert) + 4 fichiers + Dossier C (ouvert) + 4 fichiers = 10 nœuds visibles
**Hors écran** : Supposons que seulement 5 nœuds sont visibles à l'écran (clipping)

---

## 📋 Chaîne d'Appels Complète

### Étape 1 : Initialisation

```cpp
void UIManager::renderPlaylistTree() {
    ImGui::BeginChild("PlaylistTree", ImVec2(0, -60), true);
    // ...
    PlaylistNode* root = m_playlist.getRoot();
    
    // Appel initial
    for (auto& child : root->children) {
        renderNode(child.get(), 0);  // ← Appel 1
    }
}
```

### Étape 2 : Parcours Récursif

Voici la chaîne d'appels pour notre exemple :

```
renderPlaylistTree()
  └─ renderNode(Dossier A, depth=0)  ← Appel 1
      ├─ ImGui::TreeNodeEx("Dossier A") → retourne true (ouvert)
      ├─ renderNode(Fichier 1, depth=1)  ← Appel 2
      │   └─ ImGui::Selectable("Fichier 1")
      ├─ renderNode(Fichier 2, depth=1)  ← Appel 3
      │   └─ ImGui::Selectable("Fichier 2")
      ├─ renderNode(Fichier 3, depth=1)  ← Appel 4
      │   └─ ImGui::Selectable("Fichier 3")
      ├─ renderNode(Fichier 4, depth=1)  ← Appel 5
      │   └─ ImGui::Selectable("Fichier 4")
      └─ ImGui::TreePop()
  
  └─ renderNode(Dossier B, depth=0)  ← Appel 6
      ├─ ImGui::TreeNodeEx("Dossier B") → retourne false (fermé)
      └─ (pas de parcours des enfants car fermé)
  
  └─ renderNode(Dossier C, depth=0)  ← Appel 7
      ├─ ImGui::TreeNodeEx("Dossier C") → retourne true (ouvert)
      ├─ renderNode(Fichier 9, depth=1)  ← Appel 8
      │   └─ ImGui::Selectable("Fichier 9")
      ├─ renderNode(Fichier 10, depth=1)  ← Appel 9
      │   └─ ImGui::Selectable("Fichier 10")
      ├─ renderNode(Fichier 11, depth=1)  ← Appel 10
      │   └─ ImGui::Selectable("Fichier 11")
      ├─ renderNode(Fichier 12, depth=1)  ← Appel 11
      │   └─ ImGui::Selectable("Fichier 12")
      └─ ImGui::TreePop()
```

**Total d'appels** : 11 appels à `renderNode()` (3 dossiers + 8 fichiers visibles)

---

## 🔍 Détail d'un Appel : `renderNode(Dossier A, depth=0)`

### Code Exécuté

```cpp
std::function<void(PlaylistNode*, int)> renderNode = [&](PlaylistNode* node, int depth) {
    // 1. Compteur
    nodesRendered++;  // ← Compte ce nœud
    
    // 2. ID unique pour ImGui
    ImGui::PushID(node);  // ← Push ID sur la stack ImGui
    
    // 3. Vérifications
    bool isCurrent = (currentNode == node && !node->filepath.empty());
    bool isSelected = (currentNode == node);
    bool shouldOpen = isParentOfCurrent(node) || (m_filtersActive && node->isFolder);
    
    // 4. Si c'est un dossier
    if (node->isFolder) {
        // 4a. Préparer les flags
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
        if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
        if (shouldOpen || m_filtersActive) flags |= ImGuiTreeNodeFlags_DefaultOpen;
        
        // 4b. Appel ImGui pour créer/afficher le TreeNode
        bool nodeOpen = ImGui::TreeNodeEx(node->name.c_str(), flags);
        //     ↑
        //     └─ ImGui fait :
        //        - ItemAdd() : vérifie si visible (clipping)
        //        - Si visible : dessine le TreeNode
        //        - Retourne true si ouvert, false si fermé
        
        // 4c. Si ouvert, parcourir récursivement les enfants
        if (nodeOpen) {
            for (auto& child : node->children) {
                renderNode(child.get(), depth + 1);  // ← Récursion
            }
            ImGui::TreePop();  // ← Ferme le TreeNode
        }
    } else {
        // 5. Si c'est un fichier
        float indentAmount = depth * 5.0f;
        if (depth > 0) {
            ImGui::Indent(indentAmount);
        }
        
        if (ImGui::Selectable(node->name.c_str(), isSelected)) {
            // Clic sur le fichier
        }
        
        if (depth > 0) {
            ImGui::Unindent(indentAmount);
        }
    }
    
    // 6. Nettoyage
    ImGui::PopID();  // ← Pop ID de la stack ImGui
};
```

---

## 🎯 Ce qui se Passe dans ImGui

### `ImGui::TreeNodeEx()` - Détail Interne

Quand on appelle `ImGui::TreeNodeEx("Dossier A", flags)` :

```cpp
// Dans imgui_widgets.cpp
bool ImGui::TreeNodeEx(const char* label, ImGuiTreeNodeFlags flags) {
    // 1. Obtenir la fenêtre courante
    ImGuiWindow* window = GetCurrentWindow();
    
    // 2. Vérifier si on doit skip (hors écran)
    if (window->SkipItems)
        return false;
    
    // 3. Calculer l'ID unique
    ImGuiID id = window->GetID(label);
    
    // 4. Appeler TreeNodeBehavior (logique principale)
    return TreeNodeBehavior(id, flags, label, NULL);
}
```

### `TreeNodeBehavior()` - Logique Principale

```cpp
bool ImGui::TreeNodeBehavior(ImGuiID id, ImGuiTreeNodeFlags flags, const char* label) {
    // 1. Calculer la bounding box (position et taille)
    ImRect frame_bb = ...;
    ImRect interact_bb = ...;
    
    // 2. Vérifier si l'item est visible (CLIPPING)
    bool is_visible = ItemAdd(interact_bb, id);
    //     ↑
    //     └─ Vérifie si interact_bb est dans window->ClipRect
    //        - Si hors écran : is_visible = false
    //        - Si visible : is_visible = true
    
    // 3. Si hors écran, on ne dessine pas mais on continue
    if (!is_visible) {
        // Ne dessine pas, mais retourne quand même is_open
        // pour que notre code continue le parcours
        return is_open;
    }
    
    // 4. Si visible, dessiner le TreeNode
    if (is_visible) {
        // Dessine le triangle, le texte, etc.
        RenderText(...);
        RenderArrow(...);
    }
    
    // 5. Retourner si le nœud est ouvert
    return is_open;
}
```

---

## 📊 Exemple Concret : 10 Nœuds Visibles, 5 à l'Écran

### Situation
- **10 nœuds visibles** (Dossier A ouvert + 4 fichiers + Dossier C ouvert + 4 fichiers)
- **5 nœuds à l'écran** (clipping ImGui)
- **5 nœuds hors écran** (mais quand même parcourus)

### Ce qui se Passe

```
Appel 1: renderNode(Dossier A)
  └─ TreeNodeEx("Dossier A")
      └─ ItemAdd() → is_visible = true (visible)
      └─ Dessine "Dossier A" ✅
      └─ Retourne true (ouvert)

Appel 2: renderNode(Fichier 1)
  └─ Selectable("Fichier 1")
      └─ ItemAdd() → is_visible = true (visible)
      └─ Dessine "Fichier 1" ✅

Appel 3: renderNode(Fichier 2)
  └─ Selectable("Fichier 2")
      └─ ItemAdd() → is_visible = true (visible)
      └─ Dessine "Fichier 2" ✅

Appel 4: renderNode(Fichier 3)
  └─ Selectable("Fichier 3")
      └─ ItemAdd() → is_visible = false (hors écran)
      └─ Ne dessine pas ❌
      └─ MAIS notre code a quand même été exécuté !

Appel 5: renderNode(Fichier 4)
  └─ Selectable("Fichier 4")
      └─ ItemAdd() → is_visible = false (hors écran)
      └─ Ne dessine pas ❌
      └─ MAIS notre code a quand même été exécuté !

Appel 6: renderNode(Dossier B)
  └─ TreeNodeEx("Dossier B")
      └─ Retourne false (fermé)
      └─ Pas de parcours des enfants ✅

Appel 7: renderNode(Dossier C)
  └─ TreeNodeEx("Dossier C")
      └─ ItemAdd() → is_visible = true (visible)
      └─ Dessine "Dossier C" ✅
      └─ Retourne true (ouvert)

Appel 8-11: renderNode(Fichier 9-12)
  └─ Même logique que Fichier 1-4
```

### Résultat

- **11 appels** à `renderNode()` (tous les nœuds ouverts)
- **5 nœuds dessinés** (clipping ImGui)
- **6 nœuds non dessinés** (hors écran, mais quand même parcourus)

**Problème** : On fait 11 calculs alors qu'on ne dessine que 5 nœuds !

---

## 🎯 Avec Virtual Scrolling

### Principe

Au lieu de parcourir tous les nœuds ouverts, on ne parcourt que ceux visibles à l'écran.

### Exemple

```cpp
// 1. Construire une liste plate des nœuds visibles
std::vector<PlaylistNode*> visibleNodes;
// visibleNodes = [Dossier A, Fichier 1, Fichier 2, Dossier C, Fichier 9]

// 2. Utiliser ImGuiListClipper pour ne rendre que ceux à l'écran
ImGuiListClipper clipper;
clipper.Begin(visibleNodes.size());  // 5 nœuds visibles

while (clipper.Step()) {
    // clipper.DisplayStart = 0
    // clipper.DisplayEnd = 5 (tous visibles dans notre exemple)
    
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
        renderNode(visibleNodes[i], depth);  // ← Seulement 5 appels !
    }
}
```

### Résultat

- **5 appels** à `renderNode()` (seulement les nœuds visibles)
- **5 nœuds dessinés** (tous visibles)
- **Gain** : 11 → 5 appels = **2.2x** moins d'appels

---

## 📈 Comparaison : Avant vs Après

### Avant (Sans Virtual Scrolling)

```
Arbre : 3 dossiers + 12 fichiers = 15 nœuds
Ouverts : Dossier A + Dossier C = 10 nœuds visibles
À l'écran : 5 nœuds (clipping)

Appels renderNode() : 11 (tous les nœuds ouverts)
Nœuds dessinés : 5 (clipping ImGui)
Temps : ~10 ms (exemple)
```

### Après (Avec Virtual Scrolling)

```
Arbre : 3 dossiers + 12 fichiers = 15 nœuds
Ouverts : Dossier A + Dossier C = 10 nœuds visibles
À l'écran : 5 nœuds

Appels renderNode() : 5 (seulement les nœuds visibles)
Nœuds dessinés : 5 (tous visibles)
Temps : ~2 ms (exemple)
Gain : 5x
```

---

## 🔑 Points Clés

1. **Notre code récursif** parcourt **TOUS les nœuds ouverts**
2. **ImGui clipping** ne dessine que les nœuds **visibles à l'écran**
3. **Problème** : On fait des calculs pour des nœuds qui ne seront pas dessinés
4. **Solution** : Virtual Scrolling pour ne parcourir que les nœuds visibles

---

## 📝 Résumé de la Chaîne d'Appels

```
renderPlaylistTree()
  └─ BeginChild()
      └─ for (child in root->children)
          └─ renderNode(child, 0)
              ├─ PushID()
              ├─ TreeNodeEx() ou Selectable()
              │   └─ ItemAdd() → Clipping
              │   └─ Render (si visible)
              ├─ if (nodeOpen)
              │   └─ for (child in node->children)
              │       └─ renderNode(child, depth+1)  ← Récursion
              └─ PopID()
      └─ EndChild()
```

**Le problème** : La récursion parcourt TOUT, même si ImGui ne dessine que les visibles.

