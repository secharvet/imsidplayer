# Changelog v0.0.23 - Synthèse

## 🎵 Évolutions majeures

### Support des subsongs SID
- Navigation entre les subsongs d'un fichier SID (boutons Précédent/Suivant + sélection directe)
- Affichage "Subsong: X / Y" dans le player
- Respect du subsong par défaut du fichier

### Filtre de rating par étoiles
- Nouveau filtre multicritère (auteur, année, **rating**)
- Opérateur >= ou = pour filtrer par nombre d'étoiles
- Fonctionne en combinaison avec les autres filtres

## 🐛 Corrections majeures

### Bug CPU avec filtres actifs
- Correction du pic de CPU lors du lancement d'un morceau depuis l'historique/recherche avec filtres actifs
- Désactivation automatique des filtres si le fichier ne correspond pas

### Navigation playlist avec filtres
- La navigation automatique "next song" respecte maintenant les filtres actifs
- Correction de la perte de focus dans l'arbre de la playlist

## ✨ Améliorations mineures

- Labels intégrés dans les filtres ("All Author", "All Year", "All Stars") - économie d'espace
- Nom de fichier cliquable dans le player pour naviguer vers le morceau
- Icônes microchip + music pour le fichier courant
- Curseur main au survol des étoiles de notation
- Slider d'offset arc-en-ciel dans la config (0-255)
- Restauration de l'état du loop au démarrage
- Nettoyage : retrait des fichiers temporaires et workspace de git

---

**6 commits** | **~220 lignes ajoutées** | **Depuis v0.0.22**
