#include "RatingManager.h"
#include "Utils.h"
#include "Logger.h"
#include <fstream>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

RatingManager::RatingManager() {
    m_filepath = getRatingFilePath();
    load(); // Charger les ratings au démarrage
}

std::string RatingManager::getRatingFilePath() const {
    fs::path configDir = getConfigDir();
    return (configDir / "rating.json").string();
}

bool RatingManager::load(const std::string& filepath) {
    m_filepath = filepath.empty() ? getRatingFilePath() : filepath;
    
    if (!fs::exists(m_filepath)) {
        m_ratings.clear();
        return true; // Pas d'erreur si le fichier n'existe pas
    }
    
    try {
        std::ifstream file(m_filepath);
        if (!file.is_open()) {
            return false;
        }
        
        std::string buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        
        if (buffer.empty()) {
            m_ratings.clear();
            return true;
        }
        
        RatingData data;
        auto error = glz::read_json(data, buffer);
        if (error) {
            LOG_ERROR("Error reading ratings: {}", glz::format_error(error, buffer));
            return false;
        }
        
        // Convertir le vecteur en map pour accès rapide
        m_ratings.clear();
        for (const auto& entry : data.ratings) {
            // Valider le rating (0-5)
            int rating = entry.rating;
            if (rating < 0) rating = 0;
            if (rating > 5) rating = 5;
            
            // Ne garder que les ratings > 0
            if (rating > 0) {
                m_ratings[entry.metadataHash] = rating;
            }
        }
        
        LOG_INFO("Ratings loaded: {} entries", m_ratings.size());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while loading ratings: {}", e.what());
        return false;
    }
}

bool RatingManager::save(const std::string& filepath) {
    std::string savePath = filepath.empty() ? m_filepath : filepath;
    
    try {
        // Créer le répertoire si nécessaire
        fs::path dir = fs::path(savePath).parent_path();
        if (!dir.empty() && !fs::exists(dir)) {
            fs::create_directories(dir);
        }
        
        // Convertir la map en vecteur pour la sérialisation
        RatingData data;
        for (const auto& pair : m_ratings) {
            if (pair.second > 0) {  // Ne sauvegarder que les ratings > 0
                data.ratings.push_back(RatingEntry(pair.first, pair.second));
            }
        }
        
        // Trier par metadataHash pour cohérence
        std::sort(data.ratings.begin(), data.ratings.end(),
            [](const RatingEntry& a, const RatingEntry& b) {
                return a.metadataHash < b.metadataHash;
            });
        
        auto result = glz::write_json(data);
        if (result.has_value()) {
            std::ofstream file(savePath);
            if (!file.is_open()) {
                return false;
            }
            file << result.value();
            file.close();
            return true;
        } else {
            LOG_ERROR("Error serializing ratings");
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while saving ratings: {}", e.what());
        return false;
    }
}

bool RatingManager::updateRating(uint32_t metadataHash, int rating) {
    // Limiter le rating entre 0 et 5
    if (rating < 0) rating = 0;
    if (rating > 5) rating = 5;
    
    if (rating == 0) {
        // Supprimer le rating si on met 0
        auto it = m_ratings.find(metadataHash);
        if (it != m_ratings.end()) {
            m_ratings.erase(it);
            save(); // Sauvegarder immédiatement
            return true;
        }
        return false; // Pas de rating à supprimer
    }
    
    // Mettre à jour ou ajouter le rating
    m_ratings[metadataHash] = rating;
    save(); // Sauvegarder immédiatement
    return true;
}

int RatingManager::getRating(uint32_t metadataHash) const {
    auto it = m_ratings.find(metadataHash);
    if (it != m_ratings.end()) {
        return it->second;
    }
    
    return 0; // Pas de rating par défaut
}
