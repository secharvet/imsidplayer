#include "BackgroundManager.h"
#include "Utils.h"
#include "Logger.h"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

#ifdef HAS_SDL2_IMAGE
#include <SDL2/SDL_image.h>
#endif

BackgroundManager::BackgroundManager(SDL_Renderer* renderer)
    : m_renderer(renderer), m_currentIndex(-1), m_showBackground(false), m_backgroundAlpha(255) {
}

BackgroundManager::~BackgroundManager() {
    clearImages();
}

void BackgroundManager::clearImages() {
    for (auto& img : m_images) {
        unloadImageTexture(img);
    }
    m_images.clear();
}

void BackgroundManager::loadImageTexture(BackgroundImage& img) {
    if (img.texture) {
        return;  // Déjà chargé
    }
    
#ifdef HAS_SDL2_IMAGE
    SDL_Surface* bgSurface = IMG_Load(img.fullPath.c_str());
    if (bgSurface) {
        img.texture = SDL_CreateTextureFromSurface(m_renderer, bgSurface);
        SDL_FreeSurface(bgSurface);
        if (!img.texture) {
            LOG_ERROR("Error creating texture for {}", img.filename);
        }
    } else {
        LOG_ERROR("Error loading image {}: {}", img.filename, IMG_GetError());
    }
#endif
}

void BackgroundManager::unloadImageTexture(BackgroundImage& img) {
    if (img.texture) {
        SDL_DestroyTexture(img.texture);
        img.texture = nullptr;
    }
}

void BackgroundManager::loadImages() {
    // Décharger toutes les textures chargées
    for (auto& img : m_images) {
        unloadImageTexture(img);
    }
    m_images.clear();
    
#ifdef HAS_SDL2_IMAGE
    fs::path configDir = getConfigDir();
    fs::path backgroundPath = configDir / "background";
    
    if (!fs::exists(backgroundPath)) {
        try {
            fs::create_directories(backgroundPath);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to create background directory: {}", e.what());
            return;
        }
    }
    
    std::vector<std::string> imageExtensions = {".png", ".jpg", ".jpeg", ".bmp", ".gif"};
    try {
        for (const auto& entry : fs::directory_iterator(backgroundPath)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                
                if (std::find(imageExtensions.begin(), imageExtensions.end(), ext) != imageExtensions.end()) {
                    std::string filename = entry.path().filename().string();
                    std::string fullPath = entry.path().string();
                    if (filename[0] != '.') {
                        // Charger seulement les métadonnées (pas la texture)
                        SDL_Surface* bgSurface = IMG_Load(fullPath.c_str());
                        if (bgSurface) {
                            BackgroundImage bgImg;
                            bgImg.texture = nullptr;  // Non chargé pour l'instant
                            bgImg.filename = filename;
                            bgImg.fullPath = fullPath;
                            bgImg.width = bgSurface->w;
                            bgImg.height = bgSurface->h;
                            m_images.push_back(bgImg);
                            SDL_FreeSurface(bgSurface);
                        }
                    }
                }
            }
        }
        
        if (!m_images.empty() && m_currentIndex >= (int)m_images.size()) {
            m_currentIndex = 0;
        } else if (m_images.empty()) {
            m_currentIndex = -1;
        }
        
        // Charger la texture de l'image courante si elle existe
        if (m_currentIndex >= 0 && m_currentIndex < (int)m_images.size()) {
            loadImageTexture(m_images[m_currentIndex]);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error loading images: {}", e.what());
    }
#endif
}

void BackgroundManager::reloadImages() {
    loadImages();
}

bool BackgroundManager::addImageFromFile(const std::string& filepath) {
#ifdef HAS_SDL2_IMAGE
    try {
        fs::path path(filepath);
        fs::path configDir = getConfigDir();
        fs::path backgroundPath = configDir / "background";
        
        if (!fs::exists(backgroundPath)) {
            fs::create_directories(backgroundPath);
        }
        
        fs::path destPath = backgroundPath / path.filename();
        
        // Gérer les doublons
        int counter = 1;
        std::string baseName = path.stem().string();
        std::string extension = path.extension().string();
        while (fs::exists(destPath)) {
            std::string newName = baseName + "_" + std::to_string(counter) + extension;
            destPath = backgroundPath / newName;
            counter++;
        }
        
        fs::copy_file(path, destPath, fs::copy_options::overwrite_existing);
        reloadImages();
        
        // Sélectionner la nouvelle image
        for (size_t i = 0; i < m_images.size(); i++) {
            if (m_images[i].filename == destPath.filename().string()) {
                setCurrentIndex(i);  // Utiliser setCurrentIndex pour charger la texture
                return true;
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error copying image: {}", e.what());
        return false;
    }
#endif
    return false;
}

void BackgroundManager::setCurrentIndex(int index) {
    int oldIndex = m_currentIndex;
    
    if (index >= 0 && index < (int)m_images.size()) {
        m_currentIndex = index;
    } else if (m_images.empty()) {
        m_currentIndex = -1;
    } else {
        m_currentIndex = 0;
    }
    
    // Décharger l'ancienne texture et charger la nouvelle
    if (oldIndex >= 0 && oldIndex < (int)m_images.size() && oldIndex != m_currentIndex) {
        unloadImageTexture(m_images[oldIndex]);
    }
    
    if (m_currentIndex >= 0 && m_currentIndex < (int)m_images.size()) {
        loadImageTexture(m_images[m_currentIndex]);
    }
}

BackgroundImage* BackgroundManager::getCurrentImage() {
    if (m_currentIndex >= 0 && m_currentIndex < (int)m_images.size()) {
        return &m_images[m_currentIndex];
    }
    return nullptr;
}

void BackgroundManager::render(int windowWidth, int windowHeight) {
    if (!m_showBackground || m_currentIndex < 0 || m_currentIndex >= (int)m_images.size()) {
        return;
    }
    
    BackgroundImage* img = getCurrentImage();
    if (!img) {
        return;
    }
    
    // Charger la texture si elle n'est pas déjà chargée
    if (!img->texture) {
        loadImageTexture(*img);
        if (!img->texture) {
            return;  // Échec du chargement
        }
    }
    
    SDL_Rect destRect = {0, 0, windowWidth, windowHeight};
    
    if (m_backgroundAlpha >= 255) {
        SDL_SetTextureBlendMode(img->texture, SDL_BLENDMODE_NONE);
        SDL_SetTextureAlphaMod(img->texture, 255);
    } else {
        SDL_SetTextureBlendMode(img->texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(img->texture, m_backgroundAlpha);
    }
    
    SDL_RenderCopy(m_renderer, img->texture, nullptr, &destRect);
}


