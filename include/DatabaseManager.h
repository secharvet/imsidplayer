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

namespace fs = std::filesystem;

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
    bool indexFile(const std::string& filepath);
    
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
    
    // Obtenir toutes les métadonnées
    const std::vector<SidMetadata>& getAllMetadata() const { return m_metadata; }
    
    // Obtenir le nombre de fichiers indexés
    size_t getCount() const { return m_metadata.size(); }
    
private:
    std::vector<SidMetadata> m_metadata;
    std::unordered_map<std::string, size_t> m_filepathIndex; // Index rapide par filepath
    std::unordered_map<uint32_t, size_t> m_hashIndex;       // Index rapide par metadataHash (clé primaire, 32-bit)
    std::string m_databasePath;
    
    // Extraire les métadonnées d'un fichier SID sans le jouer
    SidMetadata extractMetadata(const std::string& filepath);
    
    // Calculer la similarité entre deux chaînes (pour la recherche floue) - version optimisée
    static double fuzzyMatchFast(const std::string& str, const std::string& query);
};

#endif // DATABASE_MANAGER_H

