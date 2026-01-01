#include "PlaylistManager.h"
#include "Config.h"
#include <algorithm>
#include <iostream>
#include <functional>

PlaylistManager::PlaylistManager()
    : m_root(std::make_unique<PlaylistNode>("Playlist", "", true)), m_currentNode(nullptr), m_shouldScrollToCurrent(false) {
}

void PlaylistManager::loadFromConfig(const Config& config) {
    m_root->children.clear();
    m_currentNode = nullptr;
    
    const auto& loadedPlaylist = config.getPlaylist();
    for (const auto& item : loadedPlaylist) {
        m_root->children.push_back(convertFromConfigItem(item, m_root.get()));
    }
    
    // Trouver le nœud correspondant au fichier courant
    if (!config.getCurrentFile().empty() && std::filesystem::exists(config.getCurrentFile())) {
        m_currentNode = findNodeByPath(config.getCurrentFile());
        if (m_currentNode) {
            m_shouldScrollToCurrent = true;
        }
    }
}

void PlaylistManager::saveToConfig(Config& config) const {
    std::vector<Config::PlaylistItem> configPlaylist;
    for (const auto& child : m_root->children) {
        configPlaylist.push_back(convertToConfigItem(child.get()));
    }
    config.setPlaylist(configPlaylist);
}

PlaylistNode* PlaylistManager::findNodeByPath(const std::string& filepath) {
    std::function<PlaylistNode*(PlaylistNode*, const std::string&)> findRecursive;
    findRecursive = [&](PlaylistNode* node, const std::string& targetPath) -> PlaylistNode* {
        if (!node) return nullptr;
        
        if (!node->isFolder && !node->filepath.empty()) {
            try {
                std::filesystem::path nodePath = std::filesystem::canonical(node->filepath);
                std::filesystem::path target = std::filesystem::canonical(targetPath);
                if (nodePath == target) {
                    return node;
                }
            } catch (...) {
                // Si canonical échoue, comparer directement
                if (node->filepath == targetPath) {
                    return node;
                }
            }
        }
        
        for (auto& child : node->children) {
            PlaylistNode* found = findRecursive(child.get(), targetPath);
            if (found) return found;
        }
        
        return nullptr;
    };
    
    return findRecursive(m_root.get(), filepath);
}

void PlaylistManager::addFile(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".sid") {
        auto fileNode = std::make_unique<PlaylistNode>(
            path.filename().string(), 
            path.string(), 
            false);
        fileNode->parent = m_root.get();
        m_root->children.push_back(std::move(fileNode));
        sortNode(m_root.get());
    }
}

void PlaylistManager::addDirectory(const std::filesystem::path& path) {
    auto rootFolderNode = std::make_unique<PlaylistNode>(
        path.filename().string(), "", true);
    rootFolderNode->parent = m_root.get();
    PlaylistNode* rootFolderPtr = rootFolderNode.get();
    
    addDirectoryRecursive(path, rootFolderPtr);
    
    m_root->children.push_back(std::move(rootFolderNode));
    sortNode(m_root.get());
}

std::vector<PlaylistNode*> PlaylistManager::getAllFiles() const {
    std::vector<PlaylistNode*> files;
    std::function<void(PlaylistNode*)> collect;
    collect = [&](PlaylistNode* node) {
        if (!node) return;
        if (!node->isFolder && !node->filepath.empty()) {
            files.push_back(node);
        }
        for (auto& child : node->children) {
            collect(child.get());
        }
    };
    collect(m_root.get());
    return files;
}

PlaylistNode* PlaylistManager::getPreviousFile() {
    auto allFiles = getAllFiles();
    if (allFiles.empty() || !m_currentNode) return nullptr;
    
    auto it = std::find(allFiles.begin(), allFiles.end(), m_currentNode);
    if (it != allFiles.end()) {
        if (it == allFiles.begin()) {
            it = allFiles.end() - 1;
        } else {
            --it;
        }
        return *it;
    }
    return allFiles.empty() ? nullptr : allFiles.back();
}

PlaylistNode* PlaylistManager::getNextFile() {
    auto allFiles = getAllFiles();
    if (allFiles.empty() || !m_currentNode) return nullptr;
    
    auto it = std::find(allFiles.begin(), allFiles.end(), m_currentNode);
    if (it != allFiles.end()) {
        ++it;
        if (it == allFiles.end()) {
            it = allFiles.begin();
        }
        return *it;
    }
    return allFiles.empty() ? nullptr : allFiles[0];
}

void PlaylistManager::clear() {
    m_root->children.clear();
    m_currentNode = nullptr;
}

std::unique_ptr<PlaylistNode> PlaylistManager::convertFromConfigItem(const Config::PlaylistItem& item, PlaylistNode* parent) {
    auto node = std::make_unique<PlaylistNode>(item.name, item.path, item.isFolder);
    node->parent = parent;
    for (const auto& child : item.children) {
        node->children.push_back(convertFromConfigItem(child, node.get()));
    }
    return node;
}

Config::PlaylistItem PlaylistManager::convertToConfigItem(const PlaylistNode* node) const {
    Config::PlaylistItem item;
    item.name = node->name;
    item.path = node->filepath;
    item.isFolder = node->isFolder;
    for (const auto& child : node->children) {
        item.children.push_back(convertToConfigItem(child.get()));
    }
    return item;
}

void PlaylistManager::sortNode(PlaylistNode* node) {
    std::sort(node->children.begin(), node->children.end(),
        [](const std::unique_ptr<PlaylistNode>& a, const std::unique_ptr<PlaylistNode>& b) {
            if (a->isFolder != b->isFolder) {
                return a->isFolder > b->isFolder;
            }
            return a->name < b->name;
        });
    for (auto& child : node->children) {
        if (child->isFolder) {
            sortNode(child.get());
        }
    }
}

void PlaylistManager::addDirectoryRecursive(const std::filesystem::path& dir, PlaylistNode* parent) {
    try {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_directory()) {
                auto folderNode = std::make_unique<PlaylistNode>(
                    entry.path().filename().string(), "", true);
                folderNode->parent = parent;
                PlaylistNode* folderPtr = folderNode.get();
                parent->children.push_back(std::move(folderNode));
                addDirectoryRecursive(entry.path(), folderPtr);
            } else if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".sid") {
                    auto fileNode = std::make_unique<PlaylistNode>(
                        entry.path().filename().string(), 
                        entry.path().string(), 
                        false);
                    fileNode->parent = parent;
                    parent->children.push_back(std::move(fileNode));
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Erreur lecture dossier: " << e.what() << std::endl;
    }
}

std::unique_ptr<PlaylistNode> PlaylistManager::createFilteredTree(
    PlaylistNode* sourceNode,
    std::function<bool(PlaylistNode*)> filterFunc) const {
    if (!sourceNode) return nullptr;
    
    // Pour les fichiers : créer une copie seulement s'ils matchent le filtre
    if (!sourceNode->isFolder) {
        if (filterFunc(sourceNode)) {
            auto filteredNode = std::make_unique<PlaylistNode>(
                sourceNode->name, 
                sourceNode->filepath, 
                false);
            filteredNode->parent = nullptr; // Sera défini par le parent
            return filteredNode;
        }
        return nullptr; // Fichier ne correspond pas au filtre
    }
    
    // Pour les dossiers : créer une copie seulement s'ils ont des enfants qui matchent
    std::unique_ptr<PlaylistNode> filteredFolder = nullptr;
    std::vector<std::unique_ptr<PlaylistNode>> filteredChildren;
    
    for (auto& child : sourceNode->children) {
        auto filteredChild = createFilteredTree(child.get(), filterFunc);
        if (filteredChild) {
            filteredChildren.push_back(std::move(filteredChild));
        }
    }
    
    // Si le dossier a des enfants filtrés, créer le nœud
    if (!filteredChildren.empty()) {
        filteredFolder = std::make_unique<PlaylistNode>(
            sourceNode->name,
            "",
            true);
        filteredFolder->children = std::move(filteredChildren);
        
        // Définir le parent pour tous les enfants
        for (auto& child : filteredFolder->children) {
            child->parent = filteredFolder.get();
        }
    }
    
    return filteredFolder;
}

PlaylistNode* PlaylistManager::findFirstMatchingNode(
    PlaylistNode* node,
    std::function<bool(PlaylistNode*)> filterFunc) const {
    if (!node) return nullptr;
    
    // Pour les fichiers : retourner s'ils matchent
    if (!node->isFolder) {
        if (filterFunc(node)) {
            return node;
        }
        return nullptr;
    }
    
    // Pour les dossiers : chercher récursivement dans les enfants
    for (auto& child : node->children) {
        PlaylistNode* found = findFirstMatchingNode(child.get(), filterFunc);
        if (found) {
            return found;
        }
    }
    
    return nullptr;
}

