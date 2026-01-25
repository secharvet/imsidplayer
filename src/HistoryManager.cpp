#include "HistoryManager.h"
#include "Utils.h"
#include "Logger.h"
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

// Structure temporaire pour la migration de l'ancien format JSON
struct OldHistoryEntry {
    std::string timestamp;
    std::string title;
    std::string author;
    uint32_t metadataHash;
    uint32_t playCount;
    int rating;
};

template <>
struct glz::meta<OldHistoryEntry> {
    using T = OldHistoryEntry;
    static constexpr auto value = glz::object(
        "timestamp", &T::timestamp,
        "title", &T::title,
        "author", &T::author,
        "metadataHash", &T::metadataHash,
        "playCount", &T::playCount,
        "rating", &T::rating
    );
};

HistoryManager::HistoryManager() {
    m_filepath = getHistoryFilePath();
    load();
}

std::string HistoryManager::getHistoryFilePath() const {
    fs::path configDir = getConfigDir();
    return (configDir / "history.txt").string(); // Changement d'extension en .txt
}

bool HistoryManager::load(const std::string& filepath) {
    m_filepath = filepath.empty() ? getHistoryFilePath() : filepath;
    
    // Essayer aussi l'ancien fichier .json pour migration si le .txt n'existe pas
    if (!fs::exists(m_filepath)) {
        std::string oldJsonPath = (fs::path(m_filepath).parent_path() / "history.json").string();
        if (fs::exists(oldJsonPath)) {
            LOG_INFO("Migrating history from JSON to TXT format...");
            std::ifstream file(oldJsonPath);
            std::string buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();
            
            std::vector<OldHistoryEntry> oldEntries;
            auto error = glz::read_json(oldEntries, buffer);
            if (!error) {
                for (const auto& e : oldEntries) {
                    HistoryEntry entry;
                    entry.timestamp = e.timestamp;
                    entry.title = e.title;
                    entry.author = e.author;
                    entry.metadataHash = e.metadataHash;
                    m_entries.push_back(std::move(entry));
                }
                save(); // Sauvegarder au nouveau format TXT
                fs::remove(oldJsonPath); // Supprimer l'ancien JSON
            } else {
                LOG_ERROR("Failed to parse old history.json for migration: {}", glz::format_error(error, buffer));
            }
        }
    }

    if (!fs::exists(m_filepath)) {
        m_entries.clear();
        return true;
    }
    
    try {
        std::ifstream file(m_filepath);
        if (!file.is_open()) return false;
        
        m_entries.clear();
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty()) {
                m_entries.push_back(lineToEntry(line));
            }
        }
        
        // Les entrées sont lues dans l'ordre du fichier (chronologique)
        // On les inverse pour avoir les plus récents en premier en mémoire
        std::reverse(m_entries.begin(), m_entries.end());
        
        LOG_INFO("History loaded: {} entries", m_entries.size());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while loading history: {}", e.what());
        return false;
    }
}

bool HistoryManager::save(const std::string& filepath) {
    std::string savePath = filepath.empty() ? m_filepath : filepath;
    
    try {
        fs::path dir = fs::path(savePath).parent_path();
        if (!dir.empty() && !fs::exists(dir)) {
            fs::create_directories(dir);
        }
        
        std::ofstream file(savePath, std::ios::trunc);
        if (!file.is_open()) return false;
        
        // Sauvegarder dans l'ordre chronologique (le plus ancien en premier dans le fichier)
        // pour que l'append reste cohérent avec l'ordre chronologique
        std::vector<HistoryEntry> sorted = m_entries;
        std::reverse(sorted.begin(), sorted.end());
        
        for (const auto& entry : sorted) {
            file << entryToLine(entry) << "\n";
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while saving history: {}", e.what());
        return false;
    }
}

void HistoryManager::addEntry(const std::string& title, const std::string& author, uint32_t metadataHash) {
    HistoryEntry entry(title, author, metadataHash);
    
    // 1. Ajouter en mémoire (en haut de la pile)
    m_entries.insert(m_entries.begin(), entry);
    
    // 2. Append rapide au fichier
    try {
        std::ofstream file(m_filepath, std::ios::app);
        if (file.is_open()) {
            file << entryToLine(entry) << "\n";
        }
    } catch (...) {}
    
    // 3. Rotation si nécessaire
    if (m_entries.size() > MAX_ENTRIES) {
        m_entries.resize(MAX_ENTRIES);
        save(); // On réécrit tout seulement lors de la rotation
    }
}

void HistoryManager::setEntries(std::vector<HistoryEntry>&& entries) {
    m_entries = std::move(entries);
    save();
}

std::string HistoryManager::entryToLine(const HistoryEntry& entry) const {
    std::stringstream ss;
    // Format: timestamp|title|author|metadataHash
    // On utilise '|' car moins commun que ',' dans les titres de SID
    ss << entry.timestamp << "|" << entry.title << "|" << entry.author << "|" << entry.metadataHash;
    return ss.str();
}

HistoryEntry HistoryManager::lineToEntry(const std::string& line) const {
    HistoryEntry entry;
    std::stringstream ss(line);
    std::string item;
    
    if (std::getline(ss, item, '|')) entry.timestamp = item;
    if (std::getline(ss, item, '|')) entry.title = item;
    if (std::getline(ss, item, '|')) entry.author = item;
    if (std::getline(ss, item, '|')) {
        try {
            entry.metadataHash = std::stoul(item);
        } catch (...) {
            entry.metadataHash = 0;
        }
    }
    
    return entry;
}
