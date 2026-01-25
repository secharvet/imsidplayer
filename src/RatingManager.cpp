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
        return true; 
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
        
        m_ratings.clear();
        for (const auto& entry : data.ratings) {
            int rating = entry.rating;
            if (rating < 0) rating = 0;
            if (rating > 5) rating = 5;
            
            // On garde même si rating est 0, si playCount > 0
            if (rating > 0 || entry.playCount > 0) {
                m_ratings[entry.metadataHash] = {rating, entry.playCount};
            }
        }
        
        LOG_INFO("Ratings/Stats loaded: {} entries", m_ratings.size());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while loading ratings: {}", e.what());
        return false;
    }
}

bool RatingManager::save(const std::string& filepath) {
    std::string savePath = filepath.empty() ? m_filepath : filepath;
    
    try {
        fs::path dir = fs::path(savePath).parent_path();
        if (!dir.empty() && !fs::exists(dir)) {
            fs::create_directories(dir);
        }
        
        RatingData data;
        for (const auto& [hash, info] : m_ratings) {
            if (info.rating > 0 || info.playCount > 0) {
                data.ratings.push_back(RatingEntry(hash, info.rating, info.playCount));
            }
        }
        
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
    if (rating < 0) rating = 0;
    if (rating > 5) rating = 5;
    
    auto& info = m_ratings[metadataHash];
    info.rating = rating;
    
    // Si tout est à zéro, on peut supprimer l'entrée pour gagner de la place
    if (info.rating == 0 && info.playCount == 0) {
        m_ratings.erase(metadataHash);
    }
    
    save();
    return true;
}

int RatingManager::getRating(uint32_t metadataHash) const {
    auto it = m_ratings.find(metadataHash);
    if (it != m_ratings.end()) {
        return it->second.rating;
    }
    return 0;
}

uint32_t RatingManager::getPlayCount(uint32_t metadataHash) const {
    auto it = m_ratings.find(metadataHash);
    if (it != m_ratings.end()) {
        return it->second.playCount;
    }
    return 0;
}

void RatingManager::incrementPlayCount(uint32_t metadataHash) {
    m_ratings[metadataHash].playCount++;
    save();
}
