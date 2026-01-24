#ifdef ENABLE_CLOUD_SAVE

#include "CloudSyncManager.h"
#include "HTTPClient.h"
#include "RatingManager.h"
#include "HistoryManager.h"
#include "Logger.h"
#include "Utils.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

CloudSyncManager& CloudSyncManager::getInstance() {
    static CloudSyncManager instance;
    return instance;
}

CloudSyncManager::CloudSyncManager()
    : m_ratingManager(nullptr)
    , m_historyManager(nullptr)
    , m_enabled(false)
    , m_ratingStatus(SyncStatus::Disabled)
    , m_historyStatus(SyncStatus::Disabled)
    , m_running(false)
{
}

CloudSyncManager::~CloudSyncManager() {
    shutdown();
}

bool CloudSyncManager::initialize(RatingManager* ratingManager, HistoryManager* historyManager) {
    if (m_running) {
        return true;
    }
    
    m_ratingManager = ratingManager;
    m_historyManager = historyManager;
    
    m_httpClient = std::make_unique<HTTPClient>();
    if (!m_httpClient->initialize()) {
        LOG_ERROR("Failed to initialize HTTPClient for cloud sync");
        return false;
    }
    
    // Charger le Root CA (si disponible)
    // Chercher d'abord dans le répertoire source du projet, puis dans le répertoire de config
    fs::path caPath;
    
    // Essayer plusieurs emplacements possibles
    std::vector<fs::path> possiblePaths;
    
    // 1. Répertoire source du projet (depuis __FILE__ qui est dans src/)
    fs::path sourceDir = fs::path(__FILE__).parent_path().parent_path(); // Remonte à la racine du projet
    // Essayer d'abord le bundle (inclut Root R1 + WE1)
    possiblePaths.push_back(sourceDir / "certs" / "google_trust_services_bundle.pem");
    // Puis le certificat racine seul
    possiblePaths.push_back(sourceDir / "certs" / "google_trust_services_root_ca.pem");
    
    // 2. Répertoire parent du répertoire d'exécution (si exécuté depuis build/)
    fs::path execDir = fs::current_path();
    if (execDir.filename() == "build" || execDir.filename() == "bin") {
        possiblePaths.push_back(execDir.parent_path() / "certs" / "google_trust_services_root_ca.pem");
    }
    
    // 3. Répertoire de config utilisateur
    possiblePaths.push_back(getConfigDir() / "certs" / "google_trust_services_root_ca.pem");
    
    // 4. Répertoire courant (fallback)
    possiblePaths.push_back(fs::current_path() / "certs" / "google_trust_services_root_ca.pem");
    
    for (const auto& path : possiblePaths) {
        try {
            if (fs::exists(path)) {
                caPath = fs::canonical(path);
                break;
            }
        } catch (const std::exception& e) {
            // Ignorer les erreurs de canonical et continuer
            continue;
        }
    }
    
    if (fs::exists(caPath)) {
        if (!m_httpClient->loadRootCA(caPath.string())) {
            LOG_WARNING("Failed to load Root CA, cloud sync will use insecure mode");
        } else {
            LOG_INFO("Root CA loaded successfully from: {}", caPath.string());
        }
    } else {
        LOG_WARNING("Root CA not found in any of the searched locations, cloud sync will use insecure mode");
        LOG_DEBUG("Searched paths:");
        for (const auto& path : possiblePaths) {
            LOG_DEBUG("  - {}", path.string());
        }
    }
    
    m_running = true;
    m_syncThread = std::thread(&CloudSyncManager::syncWorker, this);
    
    LOG_INFO("CloudSyncManager initialized");
    return true;
}

void CloudSyncManager::shutdown() {
    if (!m_running) {
        return;
    }
    
    m_running = false;
    m_queueCondition.notify_all();
    
    if (m_syncThread.joinable()) {
        m_syncThread.join();
    }
    
    if (m_httpClient) {
        m_httpClient->cleanup();
        m_httpClient.reset();
    }
    
    LOG_INFO("CloudSyncManager shutdown");
}

void CloudSyncManager::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (enabled && !m_running) {
        initialize();
    }
    if (!enabled) {
        m_ratingStatus = SyncStatus::Disabled;
        m_historyStatus = SyncStatus::Disabled;
    }
}

void CloudSyncManager::setRatingEndpoint(const std::string& endpoint) {
    m_ratingEndpoint = endpoint;
    if (!endpoint.empty() && m_enabled) {
        m_ratingStatus = SyncStatus::Idle;
    } else {
        m_ratingStatus = SyncStatus::Disabled;
    }
}

void CloudSyncManager::setHistoryEndpoint(const std::string& endpoint) {
    m_historyEndpoint = endpoint;
    if (!endpoint.empty() && m_enabled) {
        m_historyStatus = SyncStatus::Idle;
    } else {
        m_historyStatus = SyncStatus::Disabled;
    }
}

void CloudSyncManager::queueRatingSync() {
    if (!isEnabled() || m_ratingEndpoint.empty()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_queueMutex);
    SyncTask task;
    task.type = SyncTaskType::Rating;
    m_syncQueue.push(task);
    m_queueCondition.notify_one();
}

void CloudSyncManager::queueHistorySync() {
    if (!isEnabled() || m_historyEndpoint.empty()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_queueMutex);
    SyncTask task;
    task.type = SyncTaskType::History;
    m_syncQueue.push(task);
    m_queueCondition.notify_one();
}

void CloudSyncManager::syncAll() {
    queueRatingSync();
    queueHistorySync();
}

std::string CloudSyncManager::getLastError() const {
    std::lock_guard<std::mutex> lock(m_errorMutex);
    return m_lastError;
}

void CloudSyncManager::setOnStatusChanged(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_onStatusChanged = callback;
}

std::string CloudSyncManager::buildNpointURL(const std::string& endpoint) {
    if (endpoint.empty()) {
        return "";
    }
    // Si l'endpoint est déjà une URL complète, l'utiliser tel quel
    if (endpoint.find("https://") == 0 || endpoint.find("http://") == 0) {
        return endpoint;
    }
    // Sinon, construire l'URL complète avec l'ID
    return "https://api.npoint.io/" + endpoint;
}

std::string CloudSyncManager::readLocalFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return "";
    }
    
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool CloudSyncManager::writeLocalFile(const std::string& filepath, const std::string& content) {
    fs::path path(filepath);
    fs::create_directories(path.parent_path());
    
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }
    
    file << content;
    return true;
}

void CloudSyncManager::syncWorker() {
    while (m_running) {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        
        // Attendre qu'une tâche soit disponible ou qu'on doive s'arrêter
        m_queueCondition.wait(lock, [this]() { return !m_syncQueue.empty() || !m_running; });
        
        if (!m_running && m_syncQueue.empty()) {
            break;
        }
        
        if (m_syncQueue.empty()) {
            continue;
        }
        
        SyncTask task = m_syncQueue.front();
        m_syncQueue.pop();
        lock.unlock();
        
        // Exécuter la tâche
        bool success = false;
        if (task.type == SyncTaskType::Rating) {
            m_ratingStatus = SyncStatus::Syncing;
            success = uploadRatings();
            m_ratingStatus = success ? SyncStatus::Success : SyncStatus::Error;
        } else if (task.type == SyncTaskType::History) {
            m_historyStatus = SyncStatus::Syncing;
            success = uploadHistory();
            m_historyStatus = success ? SyncStatus::Success : SyncStatus::Error;
        }
        
        // Notifier le callback UI
        {
            std::lock_guard<std::mutex> cbLock(m_callbackMutex);
            if (m_onStatusChanged) {
                m_onStatusChanged();
            }
        }
        
        // Attendre un peu avant de passer à la tâche suivante
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool CloudSyncManager::uploadRatings() {
    if (m_ratingEndpoint.empty() || !m_httpClient) {
        return false;
    }
    
    // Lire le fichier rating.json local
    RatingManager ratingManager;
    fs::path configDir = getConfigDir();
    std::string ratingPath = (configDir / "rating.json").string();
    
    std::string localJson = readLocalFile(ratingPath);
    if (localJson.empty()) {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "Failed to read local rating.json";
        return false;
    }
    
    // Upload vers npoint.io
    std::string url = buildNpointURL(m_ratingEndpoint);
    auto response = m_httpClient->post(url, localJson);
    
    if (response.statusCode == 200 || response.statusCode == 201) {
        LOG_INFO("Ratings uploaded successfully to cloud");
        return true;
    } else {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "HTTP " + std::to_string(response.statusCode) + ": " + response.body;
        LOG_ERROR("Failed to upload ratings: {}", m_lastError);
        return false;
    }
}

bool CloudSyncManager::uploadHistory() {
    if (m_historyEndpoint.empty() || !m_httpClient) {
        return false;
    }
    
    // Lire le fichier history.json local
    fs::path configDir = getConfigDir();
    std::string historyPath = (configDir / "history.json").string();
    
    std::string localJson = readLocalFile(historyPath);
    if (localJson.empty()) {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "Failed to read local history.json";
        return false;
    }
    
    // Upload vers npoint.io
    std::string url = buildNpointURL(m_historyEndpoint);
    auto response = m_httpClient->post(url, localJson);
    
    if (response.statusCode == 200 || response.statusCode == 201) {
        LOG_INFO("History uploaded successfully to cloud");
        return true;
    } else {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "HTTP " + std::to_string(response.statusCode) + ": " + response.body;
        LOG_ERROR("Failed to upload history: {}", m_lastError);
        return false;
    }
}

bool CloudSyncManager::downloadRatings() {
    if (m_ratingEndpoint.empty() || !m_httpClient) {
        return false;
    }
    
    // Télécharger depuis npoint.io
    std::string url = buildNpointURL(m_ratingEndpoint);
    auto response = m_httpClient->get(url);
    
    if (response.statusCode != 200) {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "HTTP " + std::to_string(response.statusCode) + ": " + response.body;
        return false;
    }
    
    // Lire le fichier local
    fs::path configDir = getConfigDir();
    std::string ratingPath = (configDir / "rating.json").string();
    std::string localJson = readLocalFile(ratingPath);
    
    // Fusionner (cloud écrase local)
    if (!localJson.empty()) {
        // TODO: Implémenter la fusion des ratings
        // Pour l'instant, on écrase simplement avec le cloud
    }
    
    // Sauvegarder le résultat fusionné
    if (!writeLocalFile(ratingPath, response.body)) {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "Failed to write merged rating.json";
        return false;
    }
    
    // Recharger RatingManager (utiliser l'instance existante)
    if (m_ratingManager) {
        m_ratingManager->load();
    }
    
    LOG_INFO("Ratings downloaded and merged from cloud");
    return true;
}

bool CloudSyncManager::downloadHistory() {
    if (m_historyEndpoint.empty() || !m_httpClient) {
        return false;
    }
    
    // Télécharger depuis npoint.io
    std::string url = buildNpointURL(m_historyEndpoint);
    auto response = m_httpClient->get(url);
    
    if (response.statusCode != 200) {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "HTTP " + std::to_string(response.statusCode) + ": " + response.body;
        return false;
    }
    
    // Lire le fichier local
    fs::path configDir = getConfigDir();
    std::string historyPath = (configDir / "history.json").string();
    std::string localJson = readLocalFile(historyPath);
    
    // Fusionner (cloud écrase local pour les doublons)
    if (!localJson.empty()) {
        // TODO: Implémenter la fusion de l'history
        // Pour l'instant, on écrase simplement avec le cloud
    }
    
    // Sauvegarder le résultat fusionné
    if (!writeLocalFile(historyPath, response.body)) {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "Failed to write merged history.json";
        return false;
    }
    
    // Recharger HistoryManager (utiliser l'instance existante)
    if (m_historyManager) {
        m_historyManager->load();
    }
    
    LOG_INFO("History downloaded and merged from cloud");
    return true;
}

bool CloudSyncManager::mergeRatings(const std::string& cloudJson, const std::string& localJson) {
    // TODO: Implémenter la fusion des ratings
    // Le rating cloud écrase le rating local si présent
    // Les ratings locaux non présents dans le cloud sont conservés
    
    // Pour l'instant, on retourne simplement le cloud (écrase tout)
    // L'implémentation complète sera faite avec Glaze pour parser/fusionner les JSON
    return true;
}

// Actions manuelles Push/Pull
bool CloudSyncManager::pushRatings() {
    if (m_ratingEndpoint.empty()) {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "Rating endpoint not configured";
        return false;
    }
    
    m_ratingStatus.store(SyncStatus::Syncing);
    bool success = uploadRatings();
    m_ratingStatus.store(success ? SyncStatus::Success : SyncStatus::Error);
    
    if (m_onStatusChanged) {
        std::lock_guard<std::mutex> cbLock(m_callbackMutex);
        m_onStatusChanged();
    }
    
    return success;
}

bool CloudSyncManager::pushHistory() {
    if (m_historyEndpoint.empty()) {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "History endpoint not configured";
        return false;
    }
    
    m_historyStatus.store(SyncStatus::Syncing);
    bool success = uploadHistory();
    m_historyStatus.store(success ? SyncStatus::Success : SyncStatus::Error);
    
    if (m_onStatusChanged) {
        std::lock_guard<std::mutex> cbLock(m_callbackMutex);
        m_onStatusChanged();
    }
    
    return success;
}

bool CloudSyncManager::pullRatings() {
    if (m_ratingEndpoint.empty()) {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "Rating endpoint not configured";
        return false;
    }
    
    m_ratingStatus.store(SyncStatus::Syncing);
    bool success = downloadRatings();
    m_ratingStatus.store(success ? SyncStatus::Success : SyncStatus::Error);
    
    if (m_onStatusChanged) {
        std::lock_guard<std::mutex> cbLock(m_callbackMutex);
        m_onStatusChanged();
    }
    
    return success;
}

bool CloudSyncManager::pullHistory() {
    if (m_historyEndpoint.empty()) {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "History endpoint not configured";
        return false;
    }
    
    m_historyStatus.store(SyncStatus::Syncing);
    bool success = downloadHistory();
    m_historyStatus.store(success ? SyncStatus::Success : SyncStatus::Error);
    
    if (m_onStatusChanged) {
        std::lock_guard<std::mutex> cbLock(m_callbackMutex);
        m_onStatusChanged();
    }
    
    return success;
}

std::string CloudSyncManager::createNpointEndpoint(const std::string& initialData) {
    if (!m_httpClient) {
        LOG_ERROR("HTTPClient not initialized, cannot create npoint.io endpoint");
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "HTTPClient not initialized";
        return "";
    }
    
    // Vérifier que HTTPClient est initialisé
    if (!m_httpClient->getLastError().empty() && m_httpClient->getLastError() != "No error") {
        LOG_WARNING("HTTPClient has previous error: {}", m_httpClient->getLastError());
    }
    
    // npoint.io: POST vers https://api.npoint.io/ avec le JSON initial
    // La réponse contient l'ID de l'endpoint (documentId)
    
    std::string createUrl = "https://api.npoint.io/";
    LOG_DEBUG("Creating npoint.io endpoint with URL: {}", createUrl);
    auto response = m_httpClient->post(createUrl, initialData);
    
    // Vérifier les erreurs HTTPClient
    std::string httpError = m_httpClient->getLastError();
    if (!httpError.empty() && httpError != "No error") {
        LOG_ERROR("HTTPClient error: {}", httpError);
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "HTTPClient error: " + httpError;
        return "";
    }
    
    LOG_DEBUG("npoint.io response: status={}, body={}", response.statusCode, response.body);
    
    if (response.statusCode == 200 || response.statusCode == 201) {
        // La réponse peut être:
        // 1. Un documentId direct (string): "abc123def456"
        // 2. Un JSON avec "name": {"name": "abc123def456"}
        // 3. Une URL complète: "https://api.npoint.io/abc123def456"
        
        std::string endpointId;
        std::string body = response.body;
        
        // Nettoyer les espaces et retours à la ligne
        body.erase(0, body.find_first_not_of(" \t\n\r"));
        body.erase(body.find_last_not_of(" \t\n\r") + 1);
        
        // Chercher l'URL complète d'abord
        size_t urlPos = body.find("https://api.npoint.io/");
        if (urlPos != std::string::npos) {
            size_t urlStart = urlPos + 22; // longueur de "https://api.npoint.io/"
            size_t urlEnd = body.find_first_of("\"}\n\r", urlStart);
            if (urlEnd != std::string::npos) {
                endpointId = body.substr(urlStart, urlEnd - urlStart);
            } else {
                endpointId = body.substr(urlStart);
            }
        } else {
            // Chercher "name" dans un JSON
            size_t namePos = body.find("\"name\"");
            if (namePos != std::string::npos) {
                size_t colonPos = body.find(":", namePos);
                if (colonPos != std::string::npos) {
                    size_t quoteStart = body.find("\"", colonPos);
                    if (quoteStart != std::string::npos) {
                        size_t quoteEnd = body.find("\"", quoteStart + 1);
                        if (quoteEnd != std::string::npos) {
                            endpointId = body.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                        }
                    }
                }
            } else {
                // Prendre tout le body comme ID (nettoyer les guillemets)
                endpointId = body;
                endpointId.erase(std::remove_if(endpointId.begin(), endpointId.end(), 
                    [](char c) { return c == '"' || c == '{' || c == '}'; }), 
                    endpointId.end());
            }
        }
        
        if (!endpointId.empty()) {
            std::string endpointUrl = "https://api.npoint.io/" + endpointId;
            LOG_INFO("Created npoint.io endpoint: {}", endpointUrl);
            return endpointUrl;
        } else {
            std::lock_guard<std::mutex> lock(m_errorMutex);
            m_lastError = "Could not extract endpoint ID from npoint.io response. Response body: " + response.body;
            LOG_ERROR("{}", m_lastError);
            return "";
        }
    } else {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "Failed to create npoint.io endpoint: HTTP " + std::to_string(response.statusCode) + " - " + response.body;
        LOG_ERROR("{}", m_lastError);
        return "";
    }
}

#endif // ENABLE_CLOUD_SAVE
