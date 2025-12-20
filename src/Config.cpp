#include "Config.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

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
    std::vector<PlaylistItem> currentPlaylistStack;
    int currentIndent = 0;
    
    while (std::getline(file, line)) {
        // Ignorer les lignes vides et les commentaires
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Compter l'indentation (tabs)
        int indent = 0;
        size_t pos = 0;
        while (pos < line.length() && line[pos] == '\t') {
            indent++;
            pos++;
        }
        
        // Extraire la clé et la valeur
        std::string content = line.substr(pos);
        size_t colonPos = content.find(':');
        
        if (colonPos == std::string::npos) {
            continue;
        }
        
        std::string key = content.substr(0, colonPos);
        std::string value = content.substr(colonPos + 1);
        
        // Supprimer les espaces en début/fin
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        // Parser les différentes clés
        if (key == "current_file") {
            m_currentFile = value;
        } else if (key == "background_index") {
            m_backgroundIndex = std::stoi(value);
        } else if (key == "background_filename") {
            m_backgroundFilename = value;
        } else if (key == "background_shown") {
            m_backgroundShown = (value == "true" || value == "1");
        } else if (key == "background_alpha") {
            m_backgroundAlpha = std::stoi(value);
        } else if (key == "window_x") {
            m_windowX = std::stoi(value);
        } else if (key == "window_y") {
            m_windowY = std::stoi(value);
        } else if (key == "window_width") {
            m_windowWidth = std::stoi(value);
        } else if (key == "window_height") {
            m_windowHeight = std::stoi(value);
        } else if (key == "voice_0_active") {
            m_voiceActive[0] = (value == "true" || value == "1");
        } else if (key == "voice_1_active") {
            m_voiceActive[1] = (value == "true" || value == "1");
        } else if (key == "voice_2_active") {
            m_voiceActive[2] = (value == "true" || value == "1");
        } else if (key == "audio_engine") {
            m_audioEngine = std::stoi(value);
        } else if (key == "playlist") {
            // Le début de la playlist
            m_playlist.clear();
            currentPlaylistStack.clear();
            currentIndent = indent;
        } else if (indent > currentIndent) {
            // Item de playlist avec indentation
            if (key == "item") {
                PlaylistItem item;
                item.name = value;
                item.path = ""; // Par défaut
                item.isFolder = false; // Par défaut
                
                // Chercher les propriétés suivantes (path, folder)
                std::string nextLine;
                std::streampos pos = file.tellg();
                while (std::getline(file, nextLine)) {
                    if (nextLine.empty() || nextLine[0] == '#') {
                        pos = file.tellg();
                        continue;
                    }
                    
                    int nextIndent = 0;
                    size_t nextPos = 0;
                    while (nextPos < nextLine.length() && nextLine[nextPos] == '\t') {
                        nextIndent++;
                        nextPos++;
                    }
                    
                    // Si on revient au même niveau ou moins, on a fini cet item
                    if (nextIndent <= indent) {
                        file.seekg(pos);
                        break;
                    }
                    
                    // Si c'est un item enfant (niveau suivant), on l'ignore ici (sera géré récursivement)
                    if (nextIndent > indent + 1) {
                        pos = file.tellg();
                        continue;
                    }
                    
                    std::string nextContent = nextLine.substr(nextPos);
                    size_t nextColonPos = nextContent.find(':');
                    if (nextColonPos == std::string::npos) {
                        pos = file.tellg();
                        continue;
                    }
                    
                    std::string nextKey = nextContent.substr(0, nextColonPos);
                    std::string nextValue = nextContent.substr(nextColonPos + 1);
                    nextKey.erase(0, nextKey.find_first_not_of(" \t"));
                    nextKey.erase(nextKey.find_last_not_of(" \t") + 1);
                    nextValue.erase(0, nextValue.find_first_not_of(" \t"));
                    nextValue.erase(nextValue.find_last_not_of(" \t") + 1);
                    
                    if (nextKey == "path") {
                        item.path = nextValue;
                    } else if (nextKey == "folder") {
                        item.isFolder = (nextValue == "true" || nextValue == "1");
                    }
                    
                    pos = file.tellg();
                }
                
                // Ajouter l'item au bon niveau (premier niveau après "playlist:")
                if (indent == currentIndent + 1) {
                    m_playlist.push_back(item);
                }
            }
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
    file << "background_shown: " << (m_backgroundShown ? "true" : "false") << "\n";
    file << "background_alpha: " << m_backgroundAlpha << "\n";
    file << "window_x: " << m_windowX << "\n";
    file << "window_y: " << m_windowY << "\n";
    file << "window_width: " << m_windowWidth << "\n";
    file << "window_height: " << m_windowHeight << "\n";
    file << "voice_0_active: " << (m_voiceActive[0] ? "true" : "false") << "\n";
    file << "voice_1_active: " << (m_voiceActive[1] ? "true" : "false") << "\n";
    file << "voice_2_active: " << (m_voiceActive[2] ? "true" : "false") << "\n";
    file << "audio_engine: " << m_audioEngine << "\n";
    
    file << "\nplaylist:\n";
    writePlaylist(file, m_playlist, 1);
    
    return true;
}

void Config::writePlaylist(std::ofstream& file, const std::vector<PlaylistItem>& items, int indent) {
    for (const auto& item : items) {
        for (int i = 0; i < indent; i++) {
            file << "\t";
        }
        file << "item: " << item.name << "\n";
        
        for (int i = 0; i < indent; i++) {
            file << "\t";
        }
        file << "\tpath: " << item.path << "\n";
        
        for (int i = 0; i < indent; i++) {
            file << "\t";
        }
        file << "\tfolder: " << (item.isFolder ? "true" : "false") << "\n";
        
        if (!item.children.empty()) {
            writePlaylist(file, item.children, indent + 1);
        }
    }
}

