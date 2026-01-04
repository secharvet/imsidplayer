# Diagramme : Chaîne d'Appels ImGui Tree Rendering

## 🌳 Exemple : Arbre Simple

```
Root
├─ Dossier A (OUVERT)
│  ├─ Fichier 1
│  ├─ Fichier 2
│  ├─ Fichier 3
│  └─ Fichier 4
├─ Dossier B (FERMÉ)
│  └─ (enfants non parcourus)
└─ Dossier C (OUVERT)
   ├─ Fichier 9
   ├─ Fichier 10
   └─ Fichier 11
```

---

## 📊 Diagramme de la Chaîne d'Appels

```
┌─────────────────────────────────────────────────────────────┐
│ renderPlaylistTree()                                        │
│   BeginChild("PlaylistTree")                                │
│                                                             │
│   ┌─────────────────────────────────────────────────────┐  │
│   │ for (child in root->children)                       │  │
│   │                                                      │  │
│   │   ┌──────────────────────────────────────────────┐  │  │
│   │   │ renderNode(Dossier A, depth=0)               │  │  │
│   │   │   PushID(Dossier A)                          │  │  │
│   │   │   TreeNodeEx("Dossier A")                    │  │  │
│   │   │     └─ ItemAdd() → visible ✅                │  │  │
│   │   │     └─ Dessine "Dossier A"                   │  │  │
│   │   │     └─ Retourne true (ouvert)                │  │  │
│   │   │                                              │  │  │
│   │   │   if (nodeOpen) {                            │  │  │
│   │   │     for (child in Dossier A->children) {    │  │  │
│   │   │                                              │  │  │
│   │   │       ┌──────────────────────────────────┐  │  │  │
│   │   │       │ renderNode(Fichier 1, depth=1)   │  │  │  │
│   │   │       │   PushID(Fichier 1)               │  │  │  │
│   │   │       │   Indent(5.0)                     │  │  │  │
│   │   │       │   Selectable("Fichier 1")         │  │  │  │
│   │   │       │     └─ ItemAdd() → visible ✅      │  │  │  │
│   │   │       │     └─ Dessine "Fichier 1"         │  │  │  │
│   │   │       │   Unindent(5.0)                    │  │  │  │
│   │   │       │   PopID()                          │  │  │  │
│   │   │       └──────────────────────────────────┘  │  │  │
│   │   │                                              │  │  │
│   │   │       ┌──────────────────────────────────┐  │  │  │
│   │   │       │ renderNode(Fichier 2, depth=1)   │  │  │  │
│   │   │       │   ... (même processus)           │  │  │  │
│   │   │       │   ItemAdd() → visible ✅          │  │  │  │
│   │   │       └──────────────────────────────────┘  │  │  │
│   │   │                                              │  │  │
│   │   │       ┌──────────────────────────────────┐  │  │  │
│   │   │       │ renderNode(Fichier 3, depth=1)   │  │  │  │
│   │   │       │   ...                             │  │  │  │
│   │   │       │   ItemAdd() → HORS ÉCRAN ❌       │  │  │  │
│   │   │       │   Ne dessine pas                  │  │  │  │
│   │   │       │   MAIS code exécuté quand même !  │  │  │  │
│   │   │       └──────────────────────────────────┘  │  │  │
│   │   │                                              │  │  │
│   │   │       ┌──────────────────────────────────┐  │  │  │
│   │   │       │ renderNode(Fichier 4, depth=1)   │  │  │  │
│   │   │       │   ItemAdd() → HORS ÉCRAN ❌       │  │  │  │
│   │   │       └──────────────────────────────────┘  │  │  │
│   │   │                                              │  │  │
│   │   │     }                                        │  │  │
│   │   │     TreePop()                                │  │  │
│   │   │   }                                          │  │  │
│   │   │   PopID()                                    │  │  │
│   │   └──────────────────────────────────────────────┘  │  │
│   │                                                      │  │
│   │   ┌──────────────────────────────────────────────┐  │  │
│   │   │ renderNode(Dossier B, depth=0)               │  │  │
│   │   │   TreeNodeEx("Dossier B")                    │  │  │
│   │   │     └─ Retourne false (fermé)                │  │  │
│   │   │   (pas de parcours des enfants) ✅           │  │  │
│   │   └──────────────────────────────────────────────┘  │  │
│   │                                                      │  │
│   │   ┌──────────────────────────────────────────────┐  │  │
│   │   │ renderNode(Dossier C, depth=0)               │  │  │
│   │   │   ... (même processus que Dossier A)         │  │  │
│   │   └──────────────────────────────────────────────┘  │  │
│   └─────────────────────────────────────────────────────┘  │
│                                                             │
│   EndChild()                                                │
└─────────────────────────────────────────────────────────────┘
```

---

## 🔍 Détail : Ce qui se Passe dans `TreeNodeEx()`

```
renderNode() appelle TreeNodeEx()
    │
    ├─> ImGui::TreeNodeEx("Dossier A", flags)
    │       │
    │       ├─> GetCurrentWindow()
    │       │       └─> Récupère la fenêtre ImGui courante
    │       │
    │       ├─> window->GetID("Dossier A")
    │       │       └─> Génère un ID unique pour ce nœud
    │       │
    │       └─> TreeNodeBehavior(id, flags, "Dossier A")
    │               │
    │               ├─> Calculer frame_bb (bounding box)
    │               │       └─> Position et taille du TreeNode
    │               │
    │               ├─> ItemAdd(interact_bb, id)
    │               │       │
    │               │       ├─> Vérifier si interact_bb est dans ClipRect
    │               │       │       └─> ClipRect = zone visible de la fenêtre
    │               │       │
    │               │       ├─> Si HORS ClipRect
    │               │       │       └─> is_visible = false
    │               │       │       └─> SkipItems = true
    │               │       │
    │               │       └─> Si DANS ClipRect
    │               │               └─> is_visible = true
    │               │
    │               ├─> if (!is_visible)
    │               │       └─> return is_open (sans dessiner)
    │               │
    │               └─> if (is_visible)
    │                       ├─> RenderText("Dossier A")
    │                       ├─> RenderArrow() (triangle)
    │                       └─> return is_open
    │
    └─> Retourne true/false (nœud ouvert ou fermé)
```

---

## ⚠️ Le Problème : Clipping vs Parcours

### Situation Réelle

```
Écran visible (ClipRect)
┌─────────────────────────┐
│ Dossier A               │ ← Visible ✅
│   Fichier 1             │ ← Visible ✅
│   Fichier 2             │ ← Visible ✅
│   Fichier 3             │ ← HORS ÉCRAN ❌
│   Fichier 4             │ ← HORS ÉCRAN ❌
│ ...                     │
│ Dossier C               │ ← HORS ÉCRAN ❌
│   Fichier 9             │ ← HORS ÉCRAN ❌
│   ...                   │
└─────────────────────────┘
```

### Ce qui se Passe

```
Appel 1: renderNode(Dossier A)
  └─ TreeNodeEx() → visible ✅ → Dessine ✅
  └─ Parcourt les enfants

Appel 2: renderNode(Fichier 1)
  └─ Selectable() → visible ✅ → Dessine ✅
  └─ Code exécuté : PushID, Indent, Selectable, Unindent, PopID

Appel 3: renderNode(Fichier 2)
  └─ Selectable() → visible ✅ → Dessine ✅
  └─ Code exécuté : PushID, Indent, Selectable, Unindent, PopID

Appel 4: renderNode(Fichier 3)
  └─ Selectable() → HORS ÉCRAN ❌ → Ne dessine pas ❌
  └─ MAIS code exécuté quand même : PushID, Indent, Selectable, Unindent, PopID
  └─ ⚠️ Calculs inutiles !

Appel 5: renderNode(Fichier 4)
  └─ Selectable() → HORS ÉCRAN ❌ → Ne dessine pas ❌
  └─ MAIS code exécuté quand même : PushID, Indent, Selectable, Unindent, PopID
  └─ ⚠️ Calculs inutiles !

Appel 6: renderNode(Dossier C)
  └─ TreeNodeEx() → HORS ÉCRAN ❌ → Ne dessine pas ❌
  └─ MAIS code exécuté quand même : PushID, TreeNodeEx, parcours enfants
  └─ ⚠️ Calculs inutiles !
```

**Résultat** :
- **6 appels** à `renderNode()` (tous les nœuds ouverts)
- **2 nœuds dessinés** (seulement ceux visibles)
- **4 appels inutiles** (nœuds hors écran mais quand même parcourus)

---

## ✅ Solution : Virtual Scrolling

### Principe

Ne parcourir que les nœuds qui seront visibles à l'écran.

### Exemple

```cpp
// 1. Construire liste plate des nœuds visibles (une seule fois)
std::vector<PlaylistNode*> visibleNodes;
// visibleNodes = [Dossier A, Fichier 1, Fichier 2]

// 2. Utiliser ImGuiListClipper
ImGuiListClipper clipper;
clipper.Begin(visibleNodes.size());  // 2 nœuds visibles

while (clipper.Step()) {
    // clipper.DisplayStart = 0
    // clipper.DisplayEnd = 2
    
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
        renderNode(visibleNodes[i], depth);  // ← Seulement 2 appels !
    }
}
```

### Résultat

- **2 appels** à `renderNode()` (seulement les nœuds visibles)
- **2 nœuds dessinés** (tous visibles)
- **0 appels inutiles** ✅
- **Gain** : 6 → 2 appels = **3x** moins d'appels

---

## 📊 Comparaison Visuelle

### Avant (Sans Virtual Scrolling)

```
┌─────────────────────────────────────────┐
│ Parcours : 6 nœuds                      │
│ ├─ Dossier A ✅                         │
│ │  ├─ Fichier 1 ✅                      │
│ │  ├─ Fichier 2 ✅                      │
│ │  ├─ Fichier 3 ❌ (hors écran)        │
│ │  └─ Fichier 4 ❌ (hors écran)        │
│ └─ Dossier C ❌ (hors écran)            │
│                                          │
│ Dessin : 2 nœuds                        │
│ ├─ Dossier A ✅                         │
│ └─ Fichier 1 ✅                         │
│                                          │
│ Calculs inutiles : 4 nœuds ❌           │
└─────────────────────────────────────────┘
```

### Après (Avec Virtual Scrolling)

```
┌─────────────────────────────────────────┐
│ Parcours : 2 nœuds                      │
│ ├─ Dossier A ✅                         │
│ └─ Fichier 1 ✅                         │
│                                          │
│ Dessin : 2 nœuds                        │
│ ├─ Dossier A ✅                         │
│ └─ Fichier 1 ✅                         │
│                                          │
│ Calculs inutiles : 0 nœuds ✅           │
└─────────────────────────────────────────┘
```

---

## 🎯 Points Clés à Retenir

1. **Notre code récursif** parcourt **TOUS les nœuds ouverts**
2. **ImGui clipping** ne dessine que les nœuds **visibles à l'écran**
3. **Gap** : On fait des calculs pour des nœuds qui ne seront pas dessinés
4. **Solution** : Virtual Scrolling pour ne parcourir que les nœuds visibles

**Le problème** : La récursion ne connaît pas le clipping d'ImGui, donc elle parcourt tout même si ImGui ne dessine que les visibles.

