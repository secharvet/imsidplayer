#ifndef BACKGROUND_MANAGER_H
#define BACKGROUND_MANAGER_H

#include <string>
#include <vector>
#include <SDL2/SDL.h>

struct BackgroundImage {
    SDL_Texture* texture = nullptr;  // Chargé à la volée, nullptr si non chargé
    std::string filename;
    std::string fullPath;  // Chemin complet pour chargement à la volée
    int width = 0;
    int height = 0;
};

class BackgroundManager {
public:
    BackgroundManager(SDL_Renderer* renderer);
    ~BackgroundManager();
    
    // Charger les images depuis le répertoire de configuration
    void loadImages();
    
    // Recharger les images (utile après drag & drop)
    void reloadImages();
    
    // Ajouter une image depuis un fichier (drag & drop)
    bool addImageFromFile(const std::string& filepath);
    
    // Getters/Setters
    int getCurrentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);
    
    bool isShown() const { return m_showBackground; }
    void setShown(bool shown) { m_showBackground = shown; }
    
    int getAlpha() const { return m_backgroundAlpha; }
    void setAlpha(int alpha) { m_backgroundAlpha = alpha; }
    
    const std::vector<BackgroundImage>& getImages() const { return m_images; }
    BackgroundImage* getCurrentImage();
    
    // Rendu de l'image de fond
    void render(int windowWidth, int windowHeight);
    
private:
    SDL_Renderer* m_renderer;
    std::vector<BackgroundImage> m_images;
    int m_currentIndex;
    bool m_showBackground;
    int m_backgroundAlpha; // 0-255
    
    void clearImages();
    void loadImageTexture(BackgroundImage& img);
    void unloadImageTexture(BackgroundImage& img);
};

#endif // BACKGROUND_MANAGER_H


