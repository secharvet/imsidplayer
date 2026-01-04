# ImGui Clipping : Nœuds Ouverts vs Nœuds Visibles

## 🔍 Question

**Est-ce qu'ImGui parcourt tous les nœuds ouverts ou seulement les nœuds visibles ?**

## 📋 Réponse

**ImGui fait du clipping pour le RENDU, mais NOTRE CODE parcourt TOUS les nœuds ouverts.**

### Comment ça fonctionne

#### 1. Notre Code (UIManager.cpp)

```cpp
std::function<void(PlaylistNode*, int)> renderNode = [&](PlaylistNode* node, int depth) {
    // ...
    if (node->isFolder) {
        bool nodeOpen = ImGui::TreeNodeEx(node->name.c_str(), flags);
        
        if (nodeOpen) {  // ← Si le dossier est ouvert
            for (auto& child : node->children) {
                renderNode(child.get(), depth + 1);  // ← On parcourt TOUS les enfants
            }
            ImGui::TreePop();
        }
    }
};
```

**Problème** : Notre fonction récursive `renderNode` est appelée pour **TOUS les nœuds ouverts**, même ceux hors écran.

#### 2. ImGui Clipping (imgui_widgets.cpp)

Dans `TreeNodeBehavior`, ImGui fait :

```cpp
is_visible = ItemAdd(interact_bb, id);  // ← Vérifie si l'item est visible dans ClipRect

if (!is_visible) {
    // Si hors écran, on ne dessine pas mais on continue quand même
    // ...
}
```

**Ce que fait ImGui** :
- ✅ **Clipping automatique** : Ne dessine pas les nœuds hors écran
- ❌ **Ne skip pas le parcours** : Notre code récursif est quand même exécuté

### Conséquence

**Avec 1318 nœuds ouverts** :
1. ✅ ImGui ne dessine que les ~50-100 nœuds visibles à l'écran (clipping)
2. ❌ Notre code appelle `renderNode()` **1318 fois** (une fois par nœud ouvert)
3. ❌ Chaque appel fait des calculs (PushID, vérifications, etc.)
4. ❌ Résultat : **115-116 ms** même si seulement 50-100 sont dessinés

### Exemple Concret

```
Arbre avec 1318 nœuds ouverts :
├─ Dossier A (ouvert)
│  ├─ Dossier A1 (ouvert)
│  │  ├─ Fichier 1 (hors écran)
│  │  ├─ Fichier 2 (hors écran)
│  │  └─ ... (1000 fichiers hors écran)
│  └─ ...
└─ ...

Ce qui se passe :
1. renderNode() appelé 1318 fois (tous les nœuds ouverts)
2. ImGui::TreeNodeEx() appelé 1318 fois
3. ItemAdd() vérifie la visibilité 1318 fois
4. Seulement ~50-100 sont dessinés (clipping)
5. Mais on a quand même fait 1318 calculs !
```

## 🎯 Solution : Virtual Scrolling

**Avec Virtual Scrolling**, on ne parcourt que les nœuds visibles :

```cpp
// Au lieu de parcourir tous les nœuds ouverts
for (auto& child : node->children) {
    renderNode(child.get(), depth + 1);  // ← 1318 appels
}

// On parcourt seulement les nœuds visibles
ImGuiListClipper clipper;
clipper.Begin(visibleNodes.size());
while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
        renderNode(visibleNodes[i], depth);  // ← Seulement ~50-100 appels
    }
}
```

**Gain** :
- Avant : 1318 appels à `renderNode()` → 115 ms
- Après : ~75 appels à `renderNode()` → ~6-7 ms
- **Amélioration : ~17x**

## 📊 Résumé

| Aspect | ImGui | Notre Code |
|--------|-------|------------|
| **Clipping** | ✅ Automatique (ne dessine pas hors écran) | ❌ Aucun (parcourt tout) |
| **Parcours** | ✅ Optimisé (skip si hors écran) | ❌ Complet (tous les nœuds ouverts) |
| **Performance** | ✅ Bonne pour le rendu | ❌ Mauvaise pour le parcours |

**Conclusion** : ImGui optimise le **rendu**, mais pas le **parcours**. C'est à nous d'optimiser le parcours avec Virtual Scrolling.

