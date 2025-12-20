# imSid Player

Un lecteur de fichiers SID (Commodore 64) pour Linux avec une interface graphique moderne utilisant ImGui.

## Fonctionnalités

### Lecture audio
- ✅ Lecture de fichiers SID (.sid)
- ✅ Contrôles Play/Pause/Stop
- ✅ Support de 4 moteurs SID en parallèle :
  - Moteur audio principal (toutes les voix)
  - 3 moteurs d'analyse pour isoler chaque voix individuellement
- ✅ Sélection de l'engine audio (mixé ou voix individuelles)
- ✅ Contrôle individuel des voix (mute/unmute)

### Interface graphique
- ✅ Interface moderne avec ImGui
- ✅ Onglets Player et Config
- ✅ Oscilloscopes en temps réel pour chaque voix (3 canaux)
- ✅ Affichage des informations de la chanson
- ✅ Images de fond personnalisables (PNG, JPG) avec transparence réglable
- ✅ Fenêtre redimensionnable

### Playlist
- ✅ Playlist hiérarchique avec structure arborescente
- ✅ Drag & Drop de fichiers et dossiers
- ✅ Navigation précédent/suivant
- ✅ Tri automatique alphabétique
- ✅ Support des dossiers imbriqués

### Configuration
- ✅ Sauvegarde automatique de la configuration
- ✅ Restauration de l'état au démarrage :
  - Fichier en cours de lecture
  - Playlist complète
  - Position et taille de la fenêtre
  - Image de fond sélectionnée
  - État des voix (actives/mutées)
  - Engine audio sélectionné
  - Transparence de l'image de fond

## Prérequis

### Dépendances système

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libsdl2-dev \
    libsdl2-image-dev \
    libsidplayfp-dev \
    pkg-config
```

### Dépendances du projet

- **ImGui** : Cloné automatiquement dans `third_party/imgui`
- **SDL2** : Pour la fenêtre et l'audio
- **SDL2_image** : Pour le chargement des images de fond (PNG, JPG)
- **sidplayfp** : Pour la lecture des fichiers SID

## Compilation

```bash
# Créer le dossier de build
mkdir build
cd build

# Configurer avec CMake
cmake ..

# Compiler
make -j$(nproc)

# L'exécutable sera dans build/bin/imSidPlayer
```

## Utilisation

### Lancement

```bash
./build/bin/imSidPlayer
```

### Interface

L'interface est divisée en deux panneaux :

**Panneau gauche (Player/Config) :**
- **Onglet Player** :
  - Informations sur le fichier actuel
  - Boutons Play/Pause/Stop
  - État de lecture
  - Sélection de l'engine audio
  - Contrôles des voix (Voice 1, 2, 3)
  - Oscilloscopes en temps réel (3 canaux)

- **Onglet Config** :
  - Configuration de l'image de fond
  - Slider de transparence
  - Navigation entre les images

**Panneau droit (Playlist) :**
- Zone de drag & drop pour ajouter des fichiers/dossiers
- Arborescence de la playlist
- Boutons Précédent/Suivant

### Images de fond

Placez vos images de fond (PNG, JPG) dans le répertoire `background/` à côté de l'exécutable :
```
build/bin/
├── imSidPlayer
└── background/
    ├── image1.png
    ├── image2.jpg
    └── ...
```

Les images seront chargées automatiquement au démarrage.

### Configuration

Le fichier de configuration `config.txt` est créé automatiquement dans le répertoire de l'exécutable. Il sauvegarde :
- Le fichier en cours de lecture
- La playlist complète
- La position et taille de la fenêtre
- L'image de fond sélectionnée
- L'état des voix
- L'engine audio sélectionné
- La transparence de l'image de fond

La configuration est sauvegardée automatiquement à la fermeture de l'application.

## Structure du projet

```
imSid/
├── CMakeLists.txt          # Configuration CMake principale
├── README.md               # Ce fichier
├── include/
│   ├── SidPlayer.h         # Classe principale du lecteur SID
│   └── Config.h             # Classe de gestion de la configuration
├── src/
│   ├── main.cpp             # Point d'entrée et interface ImGui
│   ├── SidPlayer.cpp        # Implémentation du lecteur SID
│   └── Config.cpp           # Implémentation de la configuration
└── third_party/
    └── imgui/               # Bibliothèque ImGui (git submodule)
```

## Développement

### Debug avec Cursor/VS Code

Le projet est configuré pour le debug avec Cursor. Utilisez la configuration `.vscode/launch.json` pour lancer le debug.

### LSP (Language Server Protocol)

Le projet génère automatiquement `compile_commands.json` pour le LSP. Le fichier `.vscode/c_cpp_properties.json` est configuré pour utiliser ce fichier.

## Fonctionnalités techniques

### Architecture multi-moteurs

Le lecteur utilise 4 moteurs SID en parallèle :
- **Moteur #0** : Audio réel (toutes les voix, pour la sortie audio)
- **Moteur #1** : Analyse voix 1 (voix 2+3 mutées)
- **Moteur #2** : Analyse voix 2 (voix 1+3 mutées)
- **Moteur #3** : Analyse voix 3 (voix 1+2 mutées)

Cette architecture permet d'isoler chaque voix pour l'analyse oscilloscope tout en maintenant un rendu audio de qualité.

### Oscilloscopes

Les oscilloscopes utilisent un buffer circulaire optimisé pour des performances en temps réel (60 FPS minimum). Les données sont capturées directement depuis les moteurs d'analyse.

### Format de configuration

Le fichier de configuration utilise un format simple inspiré de YAML, sans dépendance externe. Le parsing est fait manuellement avec la bibliothèque standard C++.

## Licence

Ce projet utilise :
- **ImGui** : MIT License
- **SDL2** : zlib License
- **SDL2_image** : zlib License
- **sidplayfp** : GPL v2+
