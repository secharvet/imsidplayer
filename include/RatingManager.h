#ifndef RATING_MANAGER_H
#define RATING_MANAGER_H

#include <string>
#include <map>
#include <cstdint>
#include <glaze/glaze.hpp>

// Structure pour une entrée de rating
struct RatingEntry {
    uint32_t metadataHash;
    int rating;      // 0-5 étoiles
    uint32_t playCount; // Nombre de fois que le morceau a été joué
    
    RatingEntry() : metadataHash(0), rating(0), playCount(0) {}
    RatingEntry(uint32_t hash, int r, uint32_t count = 0) 
        : metadataHash(hash), rating(r), playCount(count) {}
};

// Spécialisation Glaze pour RatingEntry
template <>
struct glz::meta<RatingEntry> {
    using T = RatingEntry;
    static constexpr auto value = glz::object(
        "metadataHash", &T::metadataHash,
        "rating", &T::rating,
        "playCount", &T::playCount
    );
};

// Structure pour le fichier rating.json
struct RatingData {
    std::vector<RatingEntry> ratings;
};

// Spécialisation Glaze pour RatingData
template <>
struct glz::meta<RatingData> {
    using T = RatingData;
    static constexpr auto value = glz::object(
        "ratings", &T::ratings
    );
};

class RatingManager {
public:
    RatingManager();
    
    // Charger les ratings depuis rating.json (filepath vide = utiliser le chemin par défaut)
    bool load(const std::string& filepath = "");
    
    // Sauvegarder les ratings dans rating.json (filepath vide = utiliser le chemin par défaut)
    bool save(const std::string& filepath = "");
    
    // Mettre à jour le rating d'un morceau par son metadataHash
    bool updateRating(uint32_t metadataHash, int rating);
    
    // Obtenir le rating d'un morceau par son metadataHash
    int getRating(uint32_t metadataHash) const;
    
    // Gérer le playCount
    uint32_t getPlayCount(uint32_t metadataHash) const;
    void incrementPlayCount(uint32_t metadataHash);
    
    // Obtenir tous les ratings (pour debug ou autres usages)
    struct InternalData {
        int rating;
        uint32_t playCount;
    };
    const std::map<uint32_t, InternalData>& getAllData() const { return m_ratings; }
    
    // Obtenir le chemin du fichier de ratings
    std::string getRatingFilePath() const;
    
private:
    std::map<uint32_t, InternalData> m_ratings;  // metadataHash -> {rating, playCount}
    std::string m_filepath;
};

#endif // RATING_MANAGER_H
