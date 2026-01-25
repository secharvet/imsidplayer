#ifndef CLOUD_SYNC_MANAGER_H
#define CLOUD_SYNC_MANAGER_H

#ifdef ENABLE_CLOUD_SAVE

#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <functional>
#include <memory>

class HTTPClient;
class RatingManager;
class HistoryManager;

class CloudSyncManager {
public:
    enum class SyncStatus {
        Idle,
        Syncing,
        Success,
        Error,
        Disabled
    };
    
    static CloudSyncManager& getInstance();
    
    // Initialisation et nettoyage
    bool initialize(RatingManager* ratingManager = nullptr, HistoryManager* historyManager = nullptr);
    void shutdown();
    
    // Configuration
    bool isEnabled() const { return m_enabled && !m_ratingEndpoint.empty(); }
    void setEnabled(bool enabled);
    void setRatingEndpoint(const std::string& endpoint);
    void setHistoryEndpoint(const std::string& endpoint);
    std::string getRatingEndpoint() const { return m_ratingEndpoint; }
    std::string getHistoryEndpoint() const { return m_historyEndpoint; }
    
    // Synchronisation
    void queueRatingSync();
    void queueHistorySync();
    void syncAll();  // Synchroniser ratings et history
    
    // Actions manuelles Push/Pull
    bool pushRatings();  // Upload ratings (retourne true si succès)
    bool pushHistory();  // Upload history (retourne true si succès)
    bool pullRatings();  // Download ratings (retourne true si succès)
    bool pullHistory();  // Download history (retourne true si succès)
    
    // Créer un endpoint npoint.io si vide
    std::string createNpointEndpoint(const std::string& initialData = "{}");
    
    // État
    SyncStatus getRatingStatus() const { return m_ratingStatus; }
    SyncStatus getHistoryStatus() const { return m_historyStatus; }
    std::string getLastError() const;
    
    // Callbacks pour UI
    void setOnStatusChanged(std::function<void()> callback);
    
private:
    CloudSyncManager();
    ~CloudSyncManager();
    CloudSyncManager(const CloudSyncManager&) = delete;
    CloudSyncManager& operator=(const CloudSyncManager&) = delete;
    
    enum class SyncTaskType {
        Rating,
        History
    };
    
    struct SyncTask {
        SyncTaskType type;
        std::string data;  // JSON data to upload
    };
    
    // Thread de synchronisation
    void syncWorker();
    
    // Opérations de synchronisation
    bool uploadRatings();
    bool uploadHistory();
    bool downloadRatings();
    bool downloadHistory();
    
    // Fusion des ratings (cloud écrase local)
    bool mergeRatings(const std::string& cloudJson, const std::string& localJson);
    
    // Helpers
    std::string buildNpointURL(const std::string& endpoint);
    std::string readLocalFile(const std::string& filepath);
    bool writeLocalFile(const std::string& filepath, const std::string& content);
    
    // Configuration
    bool m_enabled;
    std::string m_ratingEndpoint;
    std::string m_historyEndpoint;
    
    // État de synchronisation
    std::atomic<SyncStatus> m_ratingStatus;
    std::atomic<SyncStatus> m_historyStatus;
    mutable std::mutex m_errorMutex;
    std::string m_lastError;
    
    // Références aux managers
    RatingManager* m_ratingManager;
    HistoryManager* m_historyManager;
    
    // Queue de synchronisation
    std::queue<SyncTask> m_syncQueue;
    std::mutex m_queueMutex;
    mutable std::condition_variable m_queueCondition;
    
    // Thread
    std::thread m_syncThread;
    std::atomic<bool> m_running;
    
    // HTTP Client
    std::unique_ptr<HTTPClient> m_httpClient;
    mutable std::mutex m_httpMutex;
    
    // Callback UI
    std::function<void()> m_onStatusChanged;
    std::mutex m_callbackMutex;
};

#endif // ENABLE_CLOUD_SAVE

#endif // CLOUD_SYNC_MANAGER_H
