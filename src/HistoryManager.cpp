#include "HistoryManager.h"
#include "Utils.h"
#include "Logger.h"
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

HistoryManager::HistoryManager() {
    m_filepath = getHistoryFilePath();
    load(); // Charger l'historique au démarrage
}

std::string HistoryManager::getHistoryFilePath() const {
    fs::path configDir = getConfigDir();
    return (configDir / "history.json").string();
}

bool HistoryManager::load(const std::string& filepath) {
    m_filepath = filepath.empty() ? getHistoryFilePath() : filepath;
    
    if (!fs::exists(m_filepath)) {
        m_entries.clear();
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
            m_entries.clear();
            return true;
        }
        
        auto error = glz::read_json(m_entries, buffer);
        if (error) {
            LOG_ERROR("Erreur lecture historique: {}", glz::format_error(error, buffer));
            return false;
        }
        
        // Trier par timestamp décroissant (plus récent en premier)
        std::sort(m_entries.begin(), m_entries.end(), 
            [](const HistoryEntry& a, const HistoryEntry& b) {
                return a.timestamp > b.timestamp;
            });
        
        // Limiter à MAX_ENTRIES
        if (m_entries.size() > MAX_ENTRIES) {
            m_entries.resize(MAX_ENTRIES);
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception lors du chargement de l'historique: {}", e.what());
        return false;
    }
}

bool HistoryManager::save(const std::string& filepath) {
    std::string savePath = filepath.empty() ? m_filepath : filepath;
    
    try {
        // Créer le répertoire si nécessaire
        fs::path dir = fs::path(savePath).parent_path();
        if (!dir.empty() && !fs::exists(dir)) {
            fs::create_directories(dir);
        }
        
        auto result = glz::write_json(m_entries);
        if (result.has_value()) {
            std::ofstream file(savePath);
            if (!file.is_open()) {
                return false;
            }
            file << result.value();
            file.close();
            return true;
        } else {
            LOG_ERROR("Erreur sérialisation historique");
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception lors de la sauvegarde de l'historique: {}", e.what());
        return false;
    }
}

void HistoryManager::addEntry(const std::string& title, const std::string& author, uint32_t metadataHash) {
    // Vérifier si cette entrée existe déjà (même hash)
    // Si oui, mettre à jour le timestamp et déplacer en haut
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
        [metadataHash](const HistoryEntry& e) {
            return e.metadataHash == metadataHash;
        });
    
    if (it != m_entries.end()) {
        // Mettre à jour le timestamp, incrémenter le compteur et déplacer en haut
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm* localTime = std::localtime(&time_t);
        char buffer[64];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", localTime);
        it->timestamp = buffer;
        it->playCount++; // Incrémenter le compteur de lectures
        
        // Déplacer en haut (début de la liste)
        HistoryEntry entry = *it;
        m_entries.erase(it);
        m_entries.insert(m_entries.begin(), entry);
    } else {
        // Nouvelle entrée
        HistoryEntry entry(title, author, metadataHash);
        m_entries.insert(m_entries.begin(), entry);
        
        // Limiter à MAX_ENTRIES
        if (m_entries.size() > MAX_ENTRIES) {
            m_entries.resize(MAX_ENTRIES);
        }
    }
    
    // Sauvegarder immédiatement
    save();
}

bool HistoryManager::updateRating(uint32_t metadataHash, int rating) {
    // Limiter le rating entre 0 et 5
    if (rating < 0) rating = 0;
    if (rating > 5) rating = 5;
    
    // Trouver l'entrée correspondante
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
        [metadataHash](const HistoryEntry& e) {
            return e.metadataHash == metadataHash;
        });
    
    if (it != m_entries.end()) {
        it->rating = rating;
        save(); // Sauvegarder immédiatement
        return true;
    }
    
    return false; // Entrée non trouvée
}

int HistoryManager::getRating(uint32_t metadataHash) const {
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
        [metadataHash](const HistoryEntry& e) {
            return e.metadataHash == metadataHash;
        });
    
    if (it != m_entries.end()) {
        return it->rating;
    }
    
    return 0; // Pas de rating par défaut
}

