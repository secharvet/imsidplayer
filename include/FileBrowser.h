#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

class FileBrowser {
public:
    FileBrowser();
    
    // Afficher le dialog de sélection de fichier
    void render(bool& showDialog, std::string& selectedFilePath);
    
    // Mettre à jour la liste des fichiers du répertoire courant
    void updateDirectoryList();
    
    // Navigation
    void goToParent();
    void goToHome();
    void goToRoot();
    
    // Getters
    fs::path getCurrentDirectory() const { return m_currentDirectory; }
    void setCurrentDirectory(const fs::path& dir) { m_currentDirectory = dir; }
    
private:
    fs::path m_currentDirectory;
    std::vector<std::string> m_directoryEntries;
    std::string m_filterExtension;
    
    void refreshDirectoryList();
};

#endif // FILE_BROWSER_H









