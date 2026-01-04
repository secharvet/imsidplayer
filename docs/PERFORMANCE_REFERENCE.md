# Performance de Référence - Avant Optimisation

## 📊 Données Capturées (Session Debug)

### Timings `renderPlaylistTree()`

| Timestamp | Temps (ms) | Nœuds Rendu | Notes |
|-----------|------------|-------------|-------|
| 13:20:06  | 110 ms     | 246 nœuds   | Premier rendu (arbre ouvert) |
| 13:20:22  | 112 ms     | 81 nœuds    | Arbre partiellement fermé |
| 13:20:23  | 124 ms     | 81 nœuds    | Arbre partiellement fermé |

**Analyse** :
- Temps moyen : **115 ms** pour ~130 nœuds visibles en moyenne
- **Problème critique** : > 100ms pour seulement ~130 nœuds rendus
- Le temps de rendu est **trop élevé** même avec peu de nœuds visibles

### Autres Timings Capturés

| Méthode | Temps | Notes |
|---------|-------|-------|
| `rebuildFilepathToHashCache()` | 39 ms | 54854 fichiers mis en cache |
| Chargement DB | - | 59324 fichiers indexés |

### Observations

1. **`renderPlaylistTree()` est le goulot d'étranglement principal**
   - 110-124 ms pour seulement 81-246 nœuds
   - Cela représente probablement > 50% du temps de frame à 60 FPS (16.67 ms/frame)

2. **Pas de logs DEBUG visibles**
   - Les timings de la boucle principale (`RENDER TIMINGS`) ne sont pas capturés
   - Ils ne s'affichent que toutes les 60 frames (1 seconde)
   - L'application n'a probablement pas tourné assez longtemps

3. **Besoin de plus de données**
   - Temps de frame total
   - Répartition du temps par composant
   - FPS moyen
   - Temps de filtrage (si filtres actifs)

## 🎯 Objectifs d'Optimisation

### Avant Optimisation (Référence)
- `renderPlaylistTree()` : **~115 ms** pour ~130 nœuds
- Temps par nœud : **~0.88 ms/nœud**
- **Inacceptable** pour une application interactive (60 FPS = 16.67 ms/frame)

### Objectifs Après Optimisation
- `renderPlaylistTree()` : **< 5 ms** pour ~130 nœuds
- Temps par nœud : **< 0.04 ms/nœud** (amélioration de **22x**)
- Temps de frame total : **< 16.67 ms** (60 FPS)

## 📝 Prochaines Étapes

1. Ajouter des logs supplémentaires pour capturer :
   - Temps de frame total
   - Temps de chaque méthode clé
   - FPS moyen
   - Temps de filtrage

2. Implémenter l'optimisation avec arbre unique + cache de visibilité

3. Comparer les performances avant/après

