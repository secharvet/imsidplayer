#include "Config.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <functional>

Config& Config::getInstance() {
    static Config instance;
    return instance;
}

bool Config::load(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Ignorer les lignes vides et les commentaires
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Extraire la clé et la valeur
        size_t colonPos = line.find(':');
        if (colonPos == std::string::npos) {
            continue;
        }
        
        std::string key = line.substr(0, colonPos);
        std::string value = line.substr(colonPos + 1);
        
        // Supprimer les espaces en début/fin
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        // Parser les différentes clés
        if (key == "current_file") {
            m_currentFile = value;
        } else if (key == "background_index") {
            try { m_backgroundIndex = std::stoi(value); } catch (...) {}
        } else if (key == "background_filename") {
            m_backgroundFilename = value;
        } else if (key == "songlengths_path") {
            m_songlengthsPath = value;
        } else if (key == "background_shown") {
            m_backgroundShown = (value == "true" || value == "1");
        } else if (key == "progress_bar_animated") {
            m_progressBarAnimated = (value == "true" || value == "1");
        } else if (key == "star_rating_rainbow") {
            m_starRatingRainbow = (value == "true" || value == "1");
        } else if (key == "star_rating_rainbow_step") {
            try { m_starRatingRainbowStep = std::max(0, std::min(51, std::stoi(value))); } catch (...) {}
        } else if (key == "star_rating_rainbow_cycle_freq") {
            try { m_starRatingRainbowCycleFreq = std::max(0, std::min(20, std::stoi(value))); } catch (...) {}
        } else if (key == "star_rating_rainbow_offset") {
            try { m_starRatingRainbowOffset = std::max(0, std::min(255, std::stoi(value))); } catch (...) {}
        } else if (key == "loop_enabled") {
            m_loopEnabled = (value == "true" || value == "1");
        } else if (key == "background_alpha") {
            try { m_backgroundAlpha = std::stoi(value); } catch (...) {}
        } else if (key == "window_x") {
            try { m_windowX = std::stoi(value); } catch (...) {}
        } else if (key == "window_y") {
            try { m_windowY = std::stoi(value); } catch (...) {}
        } else if (key == "window_width") {
            try { m_windowWidth = std::stoi(value); } catch (...) {}
        } else if (key == "window_height") {
            try { m_windowHeight = std::stoi(value); } catch (...) {}
        } else if (key == "voice_0_active") {
            m_voiceActive[0] = (value == "true" || value == "1");
        } else if (key == "voice_1_active") {
            m_voiceActive[1] = (value == "true" || value == "1");
        } else if (key == "voice_2_active") {
            m_voiceActive[2] = (value == "true" || value == "1");
#ifdef ENABLE_CLOUD_SAVE
        } else if (key == "cloud_save_enabled") {
            m_cloudSaveEnabled = (value == "true" || value == "1");
        } else if (key == "cloud_rating_endpoint") {
            m_cloudRatingEndpoint = value;
        } else if (key == "cloud_history_endpoint") {
            m_cloudHistoryEndpoint = value;
#endif
        }
    }
    
    return true;
}

bool Config::save(const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    file << "# Configuration imSid Player\n";
    file << "# Format simple type YAML\n\n";
    
    file << "current_file: " << m_currentFile << "\n";
    file << "background_index: " << m_backgroundIndex << "\n";
    file << "background_filename: " << m_backgroundFilename << "\n";
    file << "songlengths_path: " << m_songlengthsPath << "\n";
    file << "background_shown: " << (m_backgroundShown ? "true" : "false") << "\n";
    file << "progress_bar_animated: " << (m_progressBarAnimated ? "true" : "false") << "\n";
    file << "star_rating_rainbow: " << (m_starRatingRainbow ? "true" : "false") << "\n";
    file << "star_rating_rainbow_step: " << m_starRatingRainbowStep << "\n";
    file << "star_rating_rainbow_cycle_freq: " << m_starRatingRainbowCycleFreq << "\n";
    file << "star_rating_rainbow_offset: " << m_starRatingRainbowOffset << "\n";
    file << "loop_enabled: " << (m_loopEnabled ? "true" : "false") << "\n";
    file << "background_alpha: " << m_backgroundAlpha << "\n";
    file << "window_x: " << m_windowX << "\n";
    file << "window_y: " << m_windowY << "\n";
    file << "window_width: " << m_windowWidth << "\n";
    file << "window_height: " << m_windowHeight << "\n";
    file << "voice_0_active: " << (m_voiceActive[0] ? "true" : "false") << "\n";
    file << "voice_1_active: " << (m_voiceActive[1] ? "true" : "false") << "\n";
    file << "voice_2_active: " << (m_voiceActive[2] ? "true" : "false") << "\n";
    
#ifdef ENABLE_CLOUD_SAVE
    file << "cloud_save_enabled: " << (m_cloudSaveEnabled ? "true" : "false") << "\n";
    file << "cloud_rating_endpoint: " << m_cloudRatingEndpoint << "\n";
    file << "cloud_history_endpoint: " << m_cloudHistoryEndpoint << "\n";
#endif
    
    return true;
}
