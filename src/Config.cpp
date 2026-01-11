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
    
    // Lire toutes les lignes dans un vecteur pour permettre la navigation
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    file.close();
    
    size_t lineIndex = 0;
    int playlistBaseIndent = -1;
    bool inPlaylist = false;
    
    // Fonction récursive pour parser un item de playlist et ses enfants
    std::function<PlaylistItem(size_t&, int)> parsePlaylistItem = [&](size_t& idx, int expectedIndent) -> PlaylistItem {
        PlaylistItem item;
        
        if (idx >= lines.size()) {
            return item;
        }
        
        // Lire la ligne "item:"
        std::string itemLine = lines[idx];
        int indent = 0;
        size_t pos = 0;
        while (pos < itemLine.length() && itemLine[pos] == '\t') {
            indent++;
            pos++;
        }
        
        if (indent != expectedIndent) {
            return item; // Mauvais niveau d'indentation
        }
        
        std::string content = itemLine.substr(pos);
        size_t colonPos = content.find(':');
        if (colonPos == std::string::npos || content.substr(0, colonPos) != "item") {
            return item;
        }
        
        item.name = content.substr(colonPos + 1);
        // Supprimer les espaces
        item.name.erase(0, item.name.find_first_not_of(" \t"));
        item.name.erase(item.name.find_last_not_of(" \t") + 1);
        
        idx++;
        
        // Lire les propriétés (path, folder) et les enfants
        while (idx < lines.size()) {
            std::string propLine = lines[idx];
            if (propLine.empty() || propLine[0] == '#') {
                idx++;
                continue;
            }
            
            int propIndent = 0;
            size_t propPos = 0;
            while (propPos < propLine.length() && propLine[propPos] == '\t') {
                propIndent++;
                propPos++;
            }
            
            // Si on revient au même niveau ou moins, on a fini cet item
            if (propIndent <= indent) {
                break;
            }
            
            std::string propContent = propLine.substr(propPos);
            size_t propColonPos = propContent.find(':');
            if (propColonPos == std::string::npos) {
                idx++;
                continue;
            }
            
            std::string propKey = propContent.substr(0, propColonPos);
            std::string propValue = propContent.substr(propColonPos + 1);
            propKey.erase(0, propKey.find_first_not_of(" \t"));
            propKey.erase(propKey.find_last_not_of(" \t") + 1);
            propValue.erase(0, propValue.find_first_not_of(" \t"));
            propValue.erase(propValue.find_last_not_of(" \t") + 1);
            
            if (propKey == "path") {
                item.path = propValue;
            } else if (propKey == "folder") {
                item.isFolder = (propValue == "true" || propValue == "1");
            } else if (propKey == "item" && propIndent == indent + 1) {
                // C'est un enfant, parser récursivement
                item.children.push_back(parsePlaylistItem(idx, indent + 1));
                continue; // Ne pas incrémenter idx car parsePlaylistItem l'a déjà fait
            }
            
            idx++;
        }
        
        return item;
    };
    
    // Parser le fichier ligne par ligne
    while (lineIndex < lines.size()) {
        std::string currentLine = lines[lineIndex];
        
        // Ignorer les lignes vides et les commentaires
        if (currentLine.empty() || currentLine[0] == '#') {
            lineIndex++;
            continue;
        }
        
        // Compter l'indentation
        int indent = 0;
        size_t pos = 0;
        while (pos < currentLine.length() && currentLine[pos] == '\t') {
            indent++;
            pos++;
        }
        
        // Extraire la clé et la valeur
        std::string content = currentLine.substr(pos);
        size_t colonPos = content.find(':');
        
        if (colonPos == std::string::npos) {
            lineIndex++;
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
        } else if (key == "songlengths_path") {
            m_songlengthsPath = value;
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
        } else if (key == "playlist") {
            // Le début de la playlist
            m_playlist.clear();
            inPlaylist = true;
            playlistBaseIndent = indent;
            lineIndex++;
            // Parser récursivement tous les items de la playlist
            while (lineIndex < lines.size()) {
                std::string nextLine = lines[lineIndex];
                if (nextLine.empty() || nextLine[0] == '#') {
                    lineIndex++;
                    continue;
                }
                
                int nextIndent = 0;
                size_t nextPos = 0;
                while (nextPos < nextLine.length() && nextLine[nextPos] == '\t') {
                    nextIndent++;
                    nextPos++;
                }
                
                // Si on revient au niveau de base ou moins, on a fini la playlist
                if (nextIndent <= playlistBaseIndent) {
                    break;
                }
                
                // Si c'est un item au premier niveau de la playlist
                if (nextIndent == playlistBaseIndent + 1) {
                    std::string nextContent = nextLine.substr(nextPos);
                    if (nextContent.find("item:") == 0) {
                        m_playlist.push_back(parsePlaylistItem(lineIndex, playlistBaseIndent + 1));
                        continue; // parsePlaylistItem a déjà incrémenté lineIndex
                    }
                }
                
                lineIndex++;
            }
            inPlaylist = false;
            continue;
        }
        
        lineIndex++;
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
    file << "background_alpha: " << m_backgroundAlpha << "\n";
    file << "window_x: " << m_windowX << "\n";
    file << "window_y: " << m_windowY << "\n";
    file << "window_width: " << m_windowWidth << "\n";
    file << "window_height: " << m_windowHeight << "\n";
    file << "voice_0_active: " << (m_voiceActive[0] ? "true" : "false") << "\n";
    file << "voice_1_active: " << (m_voiceActive[1] ? "true" : "false") << "\n";
    file << "voice_2_active: " << (m_voiceActive[2] ? "true" : "false") << "\n";
    
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

