#ifndef SUPABASE_CLIENT_H
#define SUPABASE_CLIENT_H

#ifdef ENABLE_CLOUD_SAVE

#include <string>
#include <map>
#include <vector>
#include <memory>
#include "HTTPClient.h"

// Structure pour représenter un rating communautaire
struct CommunityRating {
    std::string id;           // UUID (généré par Supabase)
    std::string user_id;      // UUID de l'utilisateur
    std::string file_hash;    // MD5 du fichier
    int rating;               // 1-5
    std::string created_at;   // Timestamp ISO
    std::string updated_at;   // Timestamp ISO
};

// Structure pour les statistiques de rating
struct RatingStatistics {
    std::string file_hash;
    double average_rating;     // 0.0 - 5.0
    int total_ratings;        // Nombre total de votes
    std::map<int, int> distribution;  // {1: count, 2: count, ...}
};

// Structure pour la réponse d'authentification
struct AuthResponse {
    bool success;
    std::string access_token;  // JWT token
    std::string refresh_token; // Refresh token (optionnel)
    std::string user_id;       // UUID de l'utilisateur
    std::string error_message; // Message d'erreur si success = false
};

class SupabaseClient {
public:
    SupabaseClient();
    ~SupabaseClient();
    
    // Initialisation avec URL et clé API
    bool initialize(const std::string& projectUrl, const std::string& anonKey);
    
    // Authentification
    AuthResponse signInAnonymously();
    AuthResponse signUpWithEmail(const std::string& email, const std::string& password);
    AuthResponse signInWithEmail(const std::string& email, const std::string& password);
    bool signOut();
    
    // Gestion du token
    void setAccessToken(const std::string& token);
    std::string getAccessToken() const { return m_accessToken; }
    bool isAuthenticated() const { return !m_accessToken.empty(); }
    std::string getUserId() const { return m_userId; }
    
    // Refresh token pour renouveler l'access_token expiré
    AuthResponse refreshSession();
    bool isTokenExpired() const;  // Vérifie si le JWT est expiré (basique)
    
    // Gestion automatique du renouvellement de token
    bool handleTokenExpiration(HTTPClient::Response& response);  // Vérifie et renouvelle si expiré
    
    // Gestion du profil utilisateur
    bool updateUsername(const std::string& username);  // Met à jour le username dans user_metadata
    std::string getUsername() const;  // Récupère le username depuis user_metadata
    bool isUsernameAvailable(const std::string& username);  // Vérifie si un username est disponible
    
    // Système de récupération de compte avec code de secours
    struct RecoveryCodeResponse {
        bool success;
        std::string recovery_code;  // Code de 6-8 chiffres
        std::string error_message;
    };
    RecoveryCodeResponse generateRecoveryCode();  // Génère un code de récupération
    AuthResponse recoverAccountWithCode(const std::string& recoveryCode);  // Récupère le compte avec le code
    
    // Ratings communautaires
    bool upsertRating(const std::string& fileHash, int rating);
    bool getMyRating(const std::string& fileHash, CommunityRating& rating);
    bool getAllMyRatings(std::vector<CommunityRating>& ratings);
    bool deleteRating(const std::string& fileHash);
    
    // Statistiques communautaires
    bool getRatingStatistics(const std::string& fileHash, RatingStatistics& stats);
    bool getRatingStatisticsBatch(const std::vector<std::string>& fileHashes, 
                                  std::map<std::string, RatingStatistics>& stats);
    
    // Synchronisation
    bool syncRatingsToCloud(const std::vector<CommunityRating>& ratings);  // Liste des ratings à envoyer
    bool syncRatingsFromCloud(std::map<std::string, int>& localRatings);       // fileHash -> rating
    
    // Gestion d'erreurs
    std::string getLastError() const { return m_lastError; }
    void clearError() { m_lastError.clear(); }
    
private:
    // Requêtes HTTP vers Supabase REST API
    HTTPClient::Response get(const std::string& endpoint, const std::map<std::string, std::string>& queryParams = {}, bool includeAuth = true);
    HTTPClient::Response post(const std::string& endpoint, const std::string& jsonBody);
    HTTPClient::Response patch(const std::string& endpoint, const std::string& jsonBody);
    HTTPClient::Response deleteRequest(const std::string& endpoint);
    
    // Helpers pour construire les URLs et headers
    std::string buildUrl(const std::string& endpoint) const;
    std::map<std::string, std::string> buildHeaders(bool includeAuth = true) const;
    std::string buildQueryString(const std::map<std::string, std::string>& params) const;
    
    // Parsing JSON (basique, on utilisera peut-être Glaze plus tard)
    bool parseAuthResponse(const std::string& json, AuthResponse& response);
    bool parseRating(const std::string& json, CommunityRating& rating);
    bool parseRatingsArray(const std::string& json, std::vector<CommunityRating>& ratings);
    bool parseStatistics(const std::string& json, RatingStatistics& stats);
    
    // Configuration
    std::string m_projectUrl;
    std::string m_anonKey;
    std::string m_accessToken;  // JWT token après authentification
    std::string m_refreshToken; // Refresh token pour renouveler l'access_token
    std::string m_userId;       // UUID de l'utilisateur authentifié
    
    // HTTP Client
    std::unique_ptr<HTTPClient> m_httpClient;
    
    // État
    bool m_initialized;
    std::string m_lastError;
    mutable std::string m_username;  // Cache du username
    mutable bool m_usernameFetched;  // Flag pour éviter les requêtes répétées
    
    mutable std::mutex m_clientMutex; // Protection pour les accès concurrents aux tokens
};

#endif // ENABLE_CLOUD_SAVE

#endif // SUPABASE_CLIENT_H
