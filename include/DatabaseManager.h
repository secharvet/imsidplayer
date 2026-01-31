#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include "SidMetadata.h"
#include "PlaylistManager.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <functional>
#include <glaze/glaze.hpp>

namespace fs = std::filesystem;

// Structure pour représenter un groupe de fichiers SID partageant le même rootFolder
struct RootFolderEntry {
    std::string rootPath;        // Chemin absolu du dossier racine
    std::string rootFolder;      // Nom du dossier racine (pour compatibilité)
    std::vector<SidMetadata> sidList;  // Liste des fichiers SID (avec filepath relatif)
};

// Spécialisation Glaze pour RootFolderEntry
template <>
struct glz::meta<RootFolderEntry> {
    using T = RootFolderEntry;
    static constexpr auto value = glz::object(
        "rootPath", &T::rootPath,
        "rootFolder", &T::rootFolder,
        "sidList", &T::sidList
    );
};

class DatabaseManager {
public:
    DatabaseManager();
    
    // Charger la base de données depuis le fichier JSON
    bool load();
    
    // Sauvegarder la base de données dans le fichier JSON
    bool save();
    
    // Indexer tous les fichiers SID de la playlist
    int indexPlaylist(PlaylistManager& playlist, std::function<void(const std::string&, int, int)> progressCallback = nullptr);
    
    // Indexer un fichier SID spécifique
    // rootFolder: nom du dossier racine (ex: "HVSCallofthem24"), vide si non spécifié
    bool indexFile(const std::string& filepath, const std::string& rootFolder = "");
    
    // Vérifier si un fichier est déjà indexé et à jour (par filepath ou metadataHash)
    bool isIndexed(const std::string& filepath) const;
    bool isIndexedByMetadataHash(uint32_t metadataHash) const;
    
    // Obtenir les métadonnées d'un fichier indexé (par filepath ou metadataHash)
    const SidMetadata* getMetadata(const std::string& filepath) const;
    const SidMetadata* getMetadataByHash(uint32_t metadataHash) const;
    
    // Recherche floue dans la base de données
    std::vector<const SidMetadata*> search(const std::string& query) const;
    
    // Obtenir tous les metadataHash indexés (pour vérification rapide)
    std::unordered_set<uint32_t> getIndexedMetadataHashes() const;
    
    // Obtenir toutes les métadonnées (version plate pour compatibilité)
    std::vector<SidMetadata> getAllMetadata() const;
    
    // Obtenir le nombre de fichiers indexés
    size_t getCount() const;
    
    // Supprimer la base de données (en mémoire et sur disque)
    bool clear();
    
private:
    // Structure hiérarchique : groupement par rootFolder
    std::vector<RootFolderEntry> m_rootFolders;
    
    // Cache pour accès rapide (reconstruit après load/save)
    mutable std::vector<SidMetadata> m_metadataCache; // Cache des métadonnées avec chemins absolus
    mutable bool m_cacheValid;
    
    mutable std::unordered_map<std::string, size_t> m_filepathIndex; // Index rapide par filepath (absolu)
    mutable std::unordered_map<uint32_t, size_t> m_hashIndex;       // Index rapide par metadataHash (clé primaire, 32-bit)
    std::string m_databasePath;
    
    // Reconstruire le cache et les index
    void rebuildCacheAndIndexes() const;
    
    // Extraire les métadonnées d'un fichier SID sans le jouer
    SidMetadata extractMetadata(const std::string& filepath);
    
    // Récupérer les songlengths depuis SongLengthDB et les ajouter aux métadonnées
    void populateSongLengths(SidMetadata& metadata) const;
    
    // Calculer la similarité entre deux chaînes (pour la recherche floue) - version optimisée
    static double fuzzyMatchFast(const std::string& str, const std::string& query);
};

#endif // DATABASE_MANAGER_H

