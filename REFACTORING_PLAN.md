# Plan de refactorisation de main.cpp

## Architecture proposée

### Classes créées :
1. **Utils** (✅ fait) - Fonctions utilitaires (getConfigDir)
2. **BackgroundManager** (✅ fait) - Gestion des images de fond
3. **PlaylistManager** (✅ fait) - Gestion de la playlist (arbre, recherche, navigation)
4. **FileBrowser** (✅ fait) - Gestion du file browser
5. **UIManager** (⏳ à faire) - Gestion de l'interface (onglets, oscilloscopes, boutons)
6. **Application** (⏳ à faire) - Classe principale orchestratrice

### Structure du nouveau main.cpp :
```cpp
int main(int argc, char* argv[]) {
    Application app;
    if (!app.initialize()) {
        return -1;
    }
    int result = app.run();
    app.shutdown();
    return result;
}
```

### Responsabilités de chaque classe :

#### UIManager
- Initialisation ImGui (polices, styles)
- Rendu des onglets (Player, Config)
- Rendu des oscilloscopes
- Rendu de la playlist
- Rendu du file browser
- Gestion des événements UI

#### Application
- Initialisation SDL
- Initialisation des composants (Player, Playlist, Background, FileBrowser, UI)
- Boucle principale
- Gestion des événements SDL (drag & drop, quit)
- Sauvegarde de la configuration
- Nettoyage

