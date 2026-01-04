# Logs Additionnels Ajoutés pour Capture de Référence

## Modifications Effectuées

### 1. Fréquence d'affichage des timings augmentée
- **Avant** : Timings affichés toutes les 60 frames (~1 seconde)
- **Après** : Timings affichés toutes les 10 frames (~0.17 seconde à 60 FPS)
- **Fichier** : `src/UIManager.cpp` ligne 190

### 2. Logs détaillés pour `renderPlaylistTree()`
- **Toujours en DEBUG** : Affiche le temps, nombre de nœuds, et type d'arbre (filtré/original)
- **WARNING si > 10ms** : Alerte de performance critique
- **Fichier** : `src/UIManager.cpp` lignes 754-758

### 3. Logs détaillés pour `rebuildFilteredTree()`
- **Temps total** : Mesure complète de la reconstruction
- **Temps de findMatch** : Temps pour trouver le premier match
- **Nombre de fichiers filtrés** : Statistiques de filtrage
- **Fichier** : `src/UIManager.cpp` lignes 1112-1189

### 4. Logs pour `renderExplorerTab()`
- **Fréquence augmentée** : Toutes les 10 frames au lieu de 60
- **Timings séparés** : Tree, Navigation, Total
- **Format amélioré** : Affichage en ms/frame
- **Fichier** : `src/UIManager.cpp` lignes 1471-1480

## Logs Disponibles

### Timings de Frame (toutes les 10 frames)
```
=== RENDER TIMINGS (frame X) ===
NewFrame:        XX.XX us (XX.XX%)
MainPanel:       XX.XX us (XX.XX%)
PlaylistPanel:   XX.XX us (XX.XX%)
FileBrowser:     XX.XX us (XX.XX%)
ImGui::Render:   XX.XX us (XX.XX%)
Clear:           XX.XX us (XX.XX%)
Background:      XX.XX us (XX.XX%)
RenderDrawData:  XX.XX us (XX.XX%)
Present:         XX.XX us (XX.XX%)
TOTAL FRAME:     XX.XX us (XX.XX ms, XX.X FPS)
===================================
```

### Timings renderPlaylistTree() (chaque frame)
```
[UI] renderPlaylistTree: XX ms (XXX nœuds rendus, arbre: filtré/original)
```

### Timings rebuildFilteredTree() (quand filtres changent)
```
[Filter] rebuildFilteredTree: XX ms total (XXXX fichiers, findMatch: XX ms, filtres: auteur='XXX', année='XXXX')
```

### Timings ExplorerTab (toutes les 10 frames)
```
[ExplorerTab] Timings (frame X): Tree=XX.XX ms/frame avg, Nav=XX.XX ms/frame avg, Total=XX.XX ms/frame avg
```

## Prochaine Étape

Relancer l'application en mode debug et capturer les logs pendant 30 secondes pour obtenir une référence complète des performances.

