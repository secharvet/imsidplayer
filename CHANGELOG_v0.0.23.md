# Changelog - Version 0.0.23 (depuis v0.0.22)

## 🎵 Nouvelles fonctionnalités majeures

### Support complet des subsongs SID
Les fichiers SID peuvent contenir plusieurs morceaux (subsongs) dans un même fichier. Cette fonctionnalité permet de naviguer entre tous les subsongs d'un fichier.

**Fonctionnalités ajoutées :**
- **Navigation entre subsongs** : Boutons Précédent/Suivant pour naviguer
- **Sélection directe** : Combo box pour choisir directement un subsong (1 à N)
- **Affichage informatif** : "Subsong: X / Y" dans l'onglet Player
- **Respect du subsong par défaut** : Les fichiers chargent maintenant leur subsong par défaut au lieu de forcer le subsong 1
- **Navigation circulaire** : Retour automatique au premier subsong après le dernier
- **Reprise automatique** : La lecture reprend automatiquement après changement de subsong

**Modifications techniques :**
- Nouvelles méthodes dans `SidPlayer` : `getTotalSongs()`, `getDefaultSong()`, `selectSong()`, `hasMultipleSongs()`
- Refactorisation de `nextSong()` et `prevSong()` pour utiliser `selectSong()`
- Rechargement automatique des engines audio lors du changement de subsong

### Filtre de rating par étoiles
Nouveau filtre multicritère pour filtrer la playlist selon le nombre d'étoiles attribuées aux morceaux.

**Fonctionnalités ajoutées :**
- **Combo box de sélection** : Choisir entre 5, 4, 3, 2, 1 étoiles ou "All Stars"
- **Opérateur de comparaison** : Bouton à 2 états pour choisir l'opérateur :
  - **">=" (supérieur ou égal)** : Affiche les morceaux avec rating >= sélectionné
  - **"=" (égal)** : Affiche uniquement les morceaux avec rating exact
- **Filtrage combiné** : Fonctionne avec les filtres auteur et année (ET logique)
- **Intégration complète** : S'intègre dans la logique de filtrage existante

**Exemples d'utilisation :**
- Rating >= 4 : Affiche tous les morceaux avec 4 ou 5 étoiles
- Rating = 5 : Affiche uniquement les morceaux avec exactement 5 étoiles

## 🎨 Améliorations de l'interface utilisateur

### Optimisation de l'espace des filtres
Réorganisation de l'interface des filtres pour économiser l'espace et améliorer la lisibilité.

**Modifications :**
- **Labels intégrés** : Suppression des labels séparés ("Author:", "Year:", "Rating:")
- **Hints améliorés** : Les labels sont maintenant intégrés dans les hints des champs :
  - "All Author" au lieu de "All" pour le filtre auteur
  - "All Year" au lieu de "All" pour le filtre année
  - "All Stars" au lieu de "All" pour le filtre rating
- **Largeurs fixes** : Tous les filtres ont maintenant des largeurs fixes pour éviter l'élargissement dynamique :
  - Author: 200px
  - Year: 150px
  - Rating combo: 120px
  - Rating button: 40px

### Amélioration de l'affichage du fichier courant
- **Icônes mises à jour** : Remplacement de l'icône pointeur par microchip + music
- **Nom de fichier cliquable** : Le nom du fichier dans l'onglet Player est maintenant cliquable pour naviguer vers le morceau dans l'arbre de la playlist
- **Curseur main** : Curseur main au survol pour indiquer que c'est cliquable

### Amélioration des étoiles de notation
- **Curseur main au survol** : Les étoiles affichent un curseur main au survol pour indiquer qu'elles sont cliquables
- **Slider d'offset arc-en-ciel** : Nouveau slider dans la config (0-255) pour décaler les indices de couleur des étoiles arc-en-ciel

## 🐛 Corrections de bugs

### Correction du bug CPU avec filtres actifs
**Problème** : Pic de CPU quand un filtre était actif et qu'on lançait un morceau depuis l'historique ou les résultats de recherche.

**Solution** :
- Désactivation automatique des filtres si le fichier sélectionné ne correspond pas aux filtres actifs
- Focus automatique sur la fenêtre playlist après sélection
- Désactivation du scroll si le fichier courant n'est pas visible dans la liste filtrée
- Correction de la navigation automatique "next song" pour respecter les filtres actifs

### Correction de la navigation dans la playlist avec filtres
**Problème** : La navigation automatique vers le morceau suivant ne respectait pas les filtres actifs.

**Solution** :
- Utilisation du cache filtré (`m_cachedAllFiles`) au lieu de la liste complète
- Utilisation de `findNodeByPath()` pour s'assurer d'utiliser le bon pointeur de nœud
- Enregistrement dans l'historique lors de la navigation automatique

### Correction de l'état du loop au démarrage
**Problème** : L'état du loop n'était pas restauré depuis la config au démarrage.

**Solution** :
- Restauration de l'état du loop depuis la config au démarrage
- Application de l'état au player immédiatement

### Correction de la conversion index/valeur dans le filtre rating
**Problème** : L'ordre était inversé (1 = 5 étoiles, 2 = 4 étoiles, etc.)

**Solution** :
- Correction de la conversion entre l'index de la combo box (0-5) et la valeur du rating (0-5)
- Utilisation de la formule : `rating = (index == 0) ? 0 : (6 - index)`

## 🔧 Améliorations techniques

### Gestion des filtres
- Amélioration de la logique de filtrage pour gérer trois critères (auteur, année, rating)
- Invalidation correcte des caches lors des changements de filtres
- Réinitialisation cohérente de tous les filtres ensemble

### Gestion de la navigation
- Cache de navigation pour améliorer les performances
- Validation du cache avant utilisation
- Gestion correcte des cas limites (fichiers sans métadonnées, etc.)

### Nettoyage du code
- Suppression des fichiers temporaires du projet (12345.sid, Songlengths.faq, Songlengths.md5, REFACTORING_PLAN.md)
- Retrait du workspace de git (ajout de `*.code-workspace` au `.gitignore`)
- Amélioration de la structure du code

## 📊 Statistiques

**Fichiers modifiés :**
- `include/SidPlayer.h` : +6 lignes
- `include/UIManager.h` : +2 lignes
- `src/SidPlayer.cpp` : +70 lignes
- `src/UIManager.cpp` : +129 lignes
- `src/FilterWidget.cpp` : +15 lignes
- `CMakeLists.txt` : Modifications pour les logs de debug
- `.gitignore` : Ajout de `*.code-workspace`

**Total :** ~220 lignes ajoutées, ~25 lignes supprimées

## 🎯 Résumé des commits

1. **774544f** - Feature: Support complet des subsongs SID + Filtre de rating + Amélioration UI filtres
2. **e8fada0** - Fix: Correction bug CPU avec filtres actifs depuis l'historique + retrait workspace de git
3. **47a952e** - Update screenshot
4. **a7b6835** - Fix loop state restoration at startup and update current file icon
5. **5aad9a6** - Fix playlist navigation with filters and improve star rating UI
6. **2a610ef** - Remove temporary files from project root

---

**Version cible :** v0.0.23  
**Date :** Janvier 2026  
**Dernier tag :** v0.0.22
