#ifndef PLAYLIST_MANAGER_H
#define PLAYLIST_MANAGER_H

#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <functional>
#include "Config.h"

namespace fs = std::filesystem;

// Nœud de la playlist (arbre)
struct PlaylistNode {
    std::string name;
    std::string filepath;  // Vide pour les dossiers
    bool isFolder;
    std::vector<std::unique_ptr<PlaylistNode>> children;
    PlaylistNode* parent = nullptr;
    
    PlaylistNode(const std::string& n, const std::string& path = "", bool folder = false) 
        : name(n), filepath(path), isFolder(folder) {}
};

class PlaylistManager {
public:
    PlaylistManager();
    
    // Charger la playlist depuis la config
    void loadFromConfig(const Config& config);
    
    // Sauvegarder la playlist vers la config
    void saveToConfig(Config& config) const;
    
    // Trouver un nœud par chemin de fichier
    PlaylistNode* findNodeByPath(const std::string& filepath);
    
    // Ajouter un fichier ou dossier (drag & drop)
    void addFile(const fs::path& path);
    void addDirectory(const fs::path& path);
    
    // Navigation
    PlaylistNode* getCurrentNode() const { return m_currentNode; }
    void setCurrentNode(PlaylistNode* node) { m_currentNode = node; }
    
    PlaylistNode* getRoot() const { return m_root.get(); }
    
    // Collecter tous les fichiers de la playlist
    std::vector<PlaylistNode*> getAllFiles() const;
    
    // Navigation précédent/suivant
    PlaylistNode* getPreviousFile();
    PlaylistNode* getNextFile();
    
    // Vider la playlist
    void clear();
    
    // Flag pour le scroll automatique
    bool shouldScrollToCurrent() const { return m_shouldScrollToCurrent; }
    void setScrollToCurrent(bool value) { m_shouldScrollToCurrent = value; }
    
    // Créer une copie filtrée de l'arbre (élague les nœuds vides)
    // Retourne nullptr si le nœud ne correspond pas au filtre et n'a pas d'enfants correspondants
    std::unique_ptr<PlaylistNode> createFilteredTree(
        PlaylistNode* sourceNode,
        std::function<bool(PlaylistNode*)> filterFunc) const;
    
    // Trouver le premier nœud qui correspond au filtre (pour scroll)
    PlaylistNode* findFirstMatchingNode(
        PlaylistNode* node,
        std::function<bool(PlaylistNode*)> filterFunc) const;
    
private:
    std::unique_ptr<PlaylistNode> m_root;
    PlaylistNode* m_currentNode;
    bool m_shouldScrollToCurrent;
    
    // Fonctions internes
    std::unique_ptr<PlaylistNode> convertFromConfigItem(const Config::PlaylistItem& item, PlaylistNode* parent);
    Config::PlaylistItem convertToConfigItem(const PlaylistNode* node) const;
    void sortNode(PlaylistNode* node);
    void addDirectoryRecursive(const fs::path& dir, PlaylistNode* parent);
};

#endif // PLAYLIST_MANAGER_H

