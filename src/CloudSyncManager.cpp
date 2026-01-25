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
    std::vector<fs::path> searchDirs;
    
    // 1. Répertoire courant
    searchDirs.push_back(fs::current_path());
    
    // 2. Répertoire parent et grand-parent (si exécuté depuis build/bin)
    searchDirs.push_back(fs::current_path().parent_path());
    searchDirs.push_back(fs::current_path().parent_path().parent_path());
    
    // 3. Répertoire de config utilisateur
    searchDirs.push_back(getConfigDir());
    
    // 4. Emplacement standard Linux
    searchDirs.push_back("/usr/local/share/imsidplayer");
    searchDirs.push_back("/usr/share/imsidplayer");
    
    std::vector<std::string> certFiles = {
        "google_trust_services_bundle.pem",
        "google_trust_services_root_ca.pem"
    };
    
    for (const auto& dir : searchDirs) {
        for (const auto& file : certFiles) {
            fs::path path = dir / "certs" / file;
            try {
                if (fs::exists(path)) {
                    caPath = fs::canonical(path);
                    LOG_DEBUG("Found Root CA at: {}", caPath.string());
                    goto found_ca;
                }
            } catch (...) {}
            
            // Essayer aussi sans le sous-dossier "certs"
            path = dir / file;
            try {
                if (fs::exists(path)) {
                    caPath = fs::canonical(path);
                    LOG_DEBUG("Found Root CA at: {}", caPath.string());
                    goto found_ca;
                }
            } catch (...) {}
        }
    }
    
found_ca:
    if (!caPath.empty() && fs::exists(caPath)) {
        if (!m_httpClient->loadRootCA(caPath.string())) {
            LOG_WARNING("Failed to load Root CA, cloud sync will use insecure mode");
        } else {
            LOG_INFO("Root CA loaded successfully from: {}", caPath.string());
        }
    } else {
        LOG_WARNING("Root CA not found in any of the searched locations, cloud sync will use insecure mode");
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
    std::lock_guard<std::mutex> httpLock(m_httpMutex);
    if (m_ratingEndpoint.empty() || !m_httpClient || !m_ratingManager) {
        LOG_ERROR("Cannot upload ratings: endpoint empty, httpClient null or manager null");
        return false;
    }
    
    // Convertir les données en mémoire en JSON
    RatingData data;
    const auto& ratings = m_ratingManager->getAllData();
    for (const auto& [hash, info] : ratings) {
        data.ratings.push_back(RatingEntry(hash, info.rating, info.playCount));
    }
    
    auto result = glz::write_json(data);
    if (!result.has_value()) {
        LOG_ERROR("Failed to serialize ratings for upload");
        return false;
    }
    
    std::string localJson = result.value();
    
    // Upload vers npoint.io
    std::string url = buildNpointURL(m_ratingEndpoint);
    LOG_DEBUG("Uploading ratings to {}", url);
    auto response = m_httpClient->post(url, localJson);
    
    if (response.statusCode == 200 || response.statusCode == 201) {
        LOG_INFO("Ratings uploaded successfully to cloud ({} bytes)", localJson.length());
        return true;
    } else {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "HTTP " + std::to_string(response.statusCode) + " during upload: " + response.body;
        if (response.statusCode == 0) m_lastError += " (Network error: " + m_httpClient->getLastError() + ")";
        LOG_ERROR("Failed to upload ratings: {}", m_lastError);
        return false;
    }
}

bool CloudSyncManager::uploadHistory() {
    std::lock_guard<std::mutex> httpLock(m_httpMutex);
    if (m_historyEndpoint.empty() || !m_httpClient || !m_historyManager) {
        LOG_ERROR("Cannot upload history: endpoint empty, httpClient null or manager null");
        return false;
    }
    
    // Convertir les données en mémoire en JSON
    const auto& entries = m_historyManager->getEntries();
    auto result = glz::write_json(entries);
    if (!result.has_value()) {
        LOG_ERROR("Failed to serialize history for upload");
        return false;
    }
    
    std::string localJson = result.value();
    
    // Upload vers npoint.io
    std::string url = buildNpointURL(m_historyEndpoint);
    LOG_DEBUG("Uploading history to {}", url);
    auto response = m_httpClient->post(url, localJson);
    
    if (response.statusCode == 200 || response.statusCode == 201) {
        LOG_INFO("History uploaded successfully to cloud ({} bytes)", localJson.length());
        return true;
    } else {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "HTTP " + std::to_string(response.statusCode) + " during upload: " + response.body;
        if (response.statusCode == 0) m_lastError += " (Network error: " + m_httpClient->getLastError() + ")";
        LOG_ERROR("Failed to upload history: {}", m_lastError);
        return false;
    }
}

bool CloudSyncManager::mergeRatings(const std::string& cloudJson, const std::string& localJson) {
    RatingData cloudData;
    auto cloudError = glz::read_json(cloudData, cloudJson);
    if (cloudError) {
        LOG_ERROR("Failed to parse cloud ratings for merge: {}", glz::format_error(cloudError, cloudJson));
        return false;
    }
    
    // Si pas de ratings locaux, le cloud écrase tout
    if (localJson.empty() || localJson == "{}" || localJson == "{\"ratings\":[]}") {
        return writeLocalFile((getConfigDir() / "rating.json").string(), cloudJson);
    }
    
    RatingData localData;
    auto localError = glz::read_json(localData, localJson);
    if (localError) {
        LOG_ERROR("Failed to parse local ratings for merge: {}", glz::format_error(localError, localJson));
        // En cas d'erreur locale, on préfère le cloud par sécurité
        return writeLocalFile((getConfigDir() / "rating.json").string(), cloudJson);
    }
    
    // Créer une map pour la fusion
    std::map<uint32_t, RatingEntry> mergedMap;
    
    // Charger d'abord les locaux
    for (const auto& entry : localData.ratings) {
        mergedMap[entry.metadataHash] = entry;
    }
    
    // Le cloud écrase les locaux si présent
    for (const auto& entry : cloudData.ratings) {
        auto& existing = mergedMap[entry.metadataHash];
        existing.metadataHash = entry.metadataHash;
        existing.rating = entry.rating;
        // Cumuler ou prendre le max pour le playCount ? 
        // Prenons le max pour éviter de gonfler artificiellement en cas de multiples pulls
        existing.playCount = std::max(existing.playCount, entry.playCount);
    }
    
    // Reconvertir en RatingData
    RatingData mergedData;
    for (const auto& [hash, entry] : mergedMap) {
        mergedData.ratings.push_back(entry);
    }
    
    // Trier
    std::sort(mergedData.ratings.begin(), mergedData.ratings.end(),
        [](const RatingEntry& a, const RatingEntry& b) {
            return a.metadataHash < b.metadataHash;
        });
        
    // Sérialiser et sauvegarder
    std::string mergedJson;
    auto result = glz::write_json(mergedData);
    if (result.has_value()) {
        mergedJson = result.value();
        return writeLocalFile((getConfigDir() / "rating.json").string(), mergedJson);
    }
    
    return false;
}

bool CloudSyncManager::downloadRatings() {
    std::lock_guard<std::mutex> httpLock(m_httpMutex);
    if (m_ratingEndpoint.empty() || !m_httpClient) {
        LOG_ERROR("Cannot download ratings: endpoint empty or httpClient null");
        return false;
    }
    
    // Télécharger depuis npoint.io
    std::string url = buildNpointURL(m_ratingEndpoint);
    LOG_DEBUG("Downloading ratings from {}", url);
    auto response = m_httpClient->get(url);
    
    if (response.statusCode != 200) {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "HTTP " + std::to_string(response.statusCode) + " during download: " + response.body;
        if (response.statusCode == 0) m_lastError += " (Network error: " + m_httpClient->getLastError() + ")";
        LOG_ERROR("Failed to download ratings: {}", m_lastError);
        return false;
    }
    
    if (response.body.empty() || response.body == "null") {
        LOG_INFO("Cloud ratings are empty, nothing to download");
        return true;
    }
    
    // Lire le fichier local
    fs::path configDir = getConfigDir();
    std::string ratingPath = (configDir / "rating.json").string();
    std::string localJson = readLocalFile(ratingPath);
    
    // Fusionner
    if (!mergeRatings(response.body, localJson)) {
        LOG_ERROR("Failed to merge ratings");
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
    std::lock_guard<std::mutex> httpLock(m_httpMutex);
    if (m_historyEndpoint.empty() || !m_httpClient || !m_historyManager) {
        LOG_ERROR("Cannot download history: endpoint empty, httpClient null or manager null");
        return false;
    }
    
    // Télécharger depuis npoint.io
    std::string url = buildNpointURL(m_historyEndpoint);
    LOG_DEBUG("Downloading history from {}", url);
    auto response = m_httpClient->get(url);
    
    if (response.statusCode != 200) {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "HTTP " + std::to_string(response.statusCode) + " during download: " + response.body;
        if (response.statusCode == 0) m_lastError += " (Network error: " + m_httpClient->getLastError() + ")";
        LOG_ERROR("Failed to download history: {}", m_lastError);
        return false;
    }
    
    if (response.body.empty() || response.body == "null" || response.body == "[]") {
        LOG_INFO("Cloud history is empty, nothing to download");
        return true;
    }
    
    // Parser le JSON du cloud
    std::vector<HistoryEntry> cloudEntries;
    auto cloudError = glz::read_json(cloudEntries, response.body);
    if (cloudError) {
        LOG_ERROR("Failed to parse cloud history: {}", glz::format_error(cloudError, response.body));
        return false;
    }

    // Fusionner avec l'historique local en mémoire
    const auto& localEntries = m_historyManager->getEntries();
    
    // On utilise une map pour dédoublonner par (timestamp + hash) ou juste hash ?
    // Pour l'historique, le timestamp est important.
    std::map<std::string, HistoryEntry> mergedMap;
    
    // Charger les locaux
    for (const auto& entry : localEntries) {
        mergedMap[entry.timestamp + std::to_string(entry.metadataHash)] = entry;
    }
    
    // Ajouter les cloud (écrase si même timestamp+hash, ce qui est normal)
    for (const auto& entry : cloudEntries) {
        mergedMap[entry.timestamp + std::to_string(entry.metadataHash)] = entry;
    }
    
    // Reconvertir en vector et trier par timestamp décroissant
    std::vector<HistoryEntry> mergedEntries;
    for (auto& [key, entry] : mergedMap) {
        mergedEntries.push_back(std::move(entry));
    }
    
    std::sort(mergedEntries.begin(), mergedEntries.end(),
        [](const HistoryEntry& a, const HistoryEntry& b) {
            return a.timestamp > b.timestamp;
        });
        
    // Limiter la taille
    if (mergedEntries.size() > 10000) {
        mergedEntries.resize(10000);
    }
    
    // Mettre à jour le manager (qui sauvegardera au format .txt)
    m_historyManager->setEntries(std::move(mergedEntries));
    
    LOG_INFO("History downloaded and merged from cloud");
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
    std::lock_guard<std::mutex> httpLock(m_httpMutex);
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
