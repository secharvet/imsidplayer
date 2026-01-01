#ifndef HISTORY_MANAGER_H
#define HISTORY_MANAGER_H

#include "SidMetadata.h"
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <glaze/glaze.hpp>

// Entrée d'historique de lecture
struct HistoryEntry {
    std::string timestamp;      // Date/heure ISO (ex: "2024-01-15T14:30:45")
    std::string title;          // Titre du morceau
    std::string author;          // Auteur
    uint32_t metadataHash;      // Hash 32-bit des métadonnées
    uint32_t playCount;         // Nombre de fois que le morceau a été joué
    int rating;                 // Note (0-5 étoiles)
    
    HistoryEntry() : metadataHash(0), playCount(1), rating(0) {}
    HistoryEntry(const std::string& t, const std::string& a, uint32_t hash)
        : title(t), author(a), metadataHash(hash), playCount(1), rating(0) {
        // Générer timestamp ISO
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm* localTime = std::localtime(&time_t);
        char buffer[64];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", localTime);
        timestamp = buffer;
    }
};

// Spécialisation Glaze pour HistoryEntry
template <>
struct glz::meta<HistoryEntry> {
    using T = HistoryEntry;
    static constexpr auto value = glz::object(
        "timestamp", &T::timestamp,
        "title", &T::title,
        "author", &T::author,
        "metadataHash", &T::metadataHash,
        "playCount", &T::playCount,
        "rating", &T::rating
    );
};

class HistoryManager {
public:
    HistoryManager();
    
    // Charger l'historique depuis un fichier JSON (filepath vide = utiliser le chemin par défaut)
    bool load(const std::string& filepath = "");
    
    // Sauvegarder l'historique dans un fichier JSON (filepath vide = utiliser le chemin par défaut)
    bool save(const std::string& filepath = "");
    
    // Ajouter une entrée à l'historique (sauvegarde automatique)
    void addEntry(const std::string& title, const std::string& author, uint32_t metadataHash);
    
    // Mettre à jour le rating d'une entrée par son metadataHash
    bool updateRating(uint32_t metadataHash, int rating);
    
    // Obtenir le rating d'une entrée par son metadataHash
    int getRating(uint32_t metadataHash) const;
    
    // Obtenir toutes les entrées (triées par date, plus récent en premier)
    const std::vector<HistoryEntry>& getEntries() const { return m_entries; }
    
    // Obtenir le chemin du fichier d'historique
    std::string getHistoryFilePath() const;
    
private:
    std::vector<HistoryEntry> m_entries;
    std::string m_filepath;
    static const size_t MAX_ENTRIES = 1000; // Limiter à 1000 entrées max
};

#endif // HISTORY_MANAGER_H

