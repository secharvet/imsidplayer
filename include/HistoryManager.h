#ifndef HISTORY_MANAGER_H
#define HISTORY_MANAGER_H

#include "SidMetadata.h"
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <glaze/glaze.hpp>

// Entrée d'historique de lecture (format léger pour append rapide)
struct HistoryEntry {
    std::string timestamp;      // Date/heure ISO (ex: "2024-01-15T14:30:45")
    std::string title;          // Titre du morceau
    std::string author;         // Auteur
    uint32_t metadataHash;      // Hash 32-bit des métadonnées
    
    HistoryEntry() : metadataHash(0) {}
    HistoryEntry(const std::string& t, const std::string& a, uint32_t hash)
        : title(t), author(a), metadataHash(hash) {
        // Générer timestamp ISO
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm* localTime = std::localtime(&time_t);
        char buffer[64];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", localTime);
        timestamp = buffer;
    }
};

// Spécialisation Glaze pour HistoryEntry (utilisée pour le Cloud Sync uniquement)
template <>
struct glz::meta<HistoryEntry> {
    using T = HistoryEntry;
    static constexpr auto value = glz::object(
        "timestamp", &T::timestamp,
        "title", &T::title,
        "author", &T::author,
        "metadataHash", &T::metadataHash
    );
};

class HistoryManager {
public:
    HistoryManager();
    
    // Charger l'historique depuis le fichier texte (format line-based)
    bool load(const std::string& filepath = "");
    
    // Sauvegarder tout l'historique (pour rotation ou merge)
    bool save(const std::string& filepath = "");
    
    // Ajouter une entrée à l'historique (append rapide)
    void addEntry(const std::string& title, const std::string& author, uint32_t metadataHash);
    
    // Obtenir toutes les entrées
    const std::vector<HistoryEntry>& getEntries() const { return m_entries; }
    
    // Remplacer toutes les entrées (utile après un merge cloud)
    void setEntries(std::vector<HistoryEntry>&& entries);
    
    // Obtenir le chemin du fichier d'historique
    std::string getHistoryFilePath() const;
    
private:
    std::vector<HistoryEntry> m_entries;
    std::string m_filepath;
    static const size_t MAX_ENTRIES = 10000; // Limite portée à 10 000
    
    // Helpers pour format line-based
    std::string entryToLine(const HistoryEntry& entry) const;
    HistoryEntry lineToEntry(const std::string& line) const;
};

#endif // HISTORY_MANAGER_H
