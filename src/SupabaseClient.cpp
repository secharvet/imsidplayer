#ifdef ENABLE_CLOUD_SAVE

#include "SupabaseClient.h"
#include "Logger.h"
#include "Config.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <random>
#include <glaze/glaze.hpp>

SupabaseClient::SupabaseClient()
    : m_initialized(false), m_usernameFetched(false)
{
    m_httpClient = std::make_unique<HTTPClient>();
}

SupabaseClient::~SupabaseClient() {
    // HTTPClient sera détruit automatiquement
}

bool SupabaseClient::initialize(const std::string& projectUrl, const std::string& anonKey) {
    if (m_initialized) {
        return true;
    }
    
    if (projectUrl.empty() || anonKey.empty()) {
        m_lastError = "Project URL and anon key are required";
        return false;
    }
    
    // Nettoyer l'URL (enlever le slash final si présent)
    m_projectUrl = projectUrl;
    if (m_projectUrl.back() == '/') {
        m_projectUrl.pop_back();
    }
    
    m_anonKey = anonKey;
    
    if (!m_httpClient->initialize()) {
        m_lastError = "Failed to initialize HTTPClient: " + m_httpClient->getLastError();
        return false;
    }
    
    m_initialized = true;
    
    // Essayer de charger le JWT depuis la config pour réutiliser l'authentification
    Config& config = Config::getInstance();
    std::string savedToken = config.getSupabaseAccessToken();
    std::string savedRefreshToken = config.getSupabaseRefreshToken();
    std::string savedUserId = config.getSupabaseUserId();
    
    if (!savedToken.empty() && !savedUserId.empty()) {
        m_accessToken = savedToken;
        m_refreshToken = savedRefreshToken;
        m_userId = savedUserId;
        
        // Vérifier si le token est expiré et le rafraîchir si nécessaire
        if (isTokenExpired() && !m_refreshToken.empty()) {
            LOG_INFO("Access token expired, refreshing with refresh_token...");
            AuthResponse refreshResponse = refreshSession();
            if (refreshResponse.success) {
                LOG_INFO("Token refreshed successfully");
            } else {
                LOG_WARNING("Failed to refresh token: {}, will need to re-authenticate", refreshResponse.error_message);
                // Le token sera réinitialisé, l'utilisateur devra se ré-authentifier
                m_accessToken.clear();
                m_refreshToken.clear();
                m_userId.clear();
            }
        } else {
            LOG_INFO("Reusing saved authentication token for user_id: {}", m_userId);
        }
    }
    
    LOG_INFO("SupabaseClient initialized with project URL: {}", m_projectUrl);
    return true;
}

AuthResponse SupabaseClient::signInAnonymously() {
    AuthResponse response;
    response.success = false;
    
    if (!m_initialized) {
        response.error_message = "SupabaseClient not initialized";
        return response;
    }
    
    // Authentification anonyme : POST vers /auth/v1/signup avec data vide
    std::string url = m_projectUrl + "/auth/v1/signup";
    
    // Body pour l'authentification anonyme (data vide)
    std::string jsonBody = "{\"data\":{}}";
    
    // Headers pour l'authentification anonyme (pas besoin d'Authorization)
    std::map<std::string, std::string> headers = buildHeaders(false);
    LOG_DEBUG("Signing in anonymously to: {}", url);
    HTTPClient::Response httpResponse = m_httpClient->post(url, jsonBody, headers);
    
    LOG_DEBUG("Auth response: status={}, body length={}", httpResponse.statusCode, httpResponse.body.length());
    if (httpResponse.body.length() > 0 && httpResponse.body.length() < 500) {
        LOG_DEBUG("Auth response body: {}", httpResponse.body);
    }
    
    if (httpResponse.statusCode == 200 || httpResponse.statusCode == 201) {
        if (parseAuthResponse(httpResponse.body, response)) {
            response.success = true;
            m_accessToken = response.access_token;
            m_refreshToken = response.refresh_token;  // Stocker le refresh_token
            m_userId = response.user_id;  // Stocker le user_id
            
            // Sauvegarder le JWT et refresh_token dans la config pour réutilisation
            Config& config = Config::getInstance();
            config.setSupabaseAccessToken(m_accessToken);
            config.setSupabaseRefreshToken(m_refreshToken);
            config.setSupabaseUserId(m_userId);
            config.save();  // Sauvegarder immédiatement
            
            LOG_INFO("Anonymous sign-in successful, user_id: {} (saved to config)", response.user_id);
        } else {
            response.error_message = "Failed to parse auth response. Body: " + httpResponse.body.substr(0, 200);
            m_lastError = response.error_message;
            LOG_ERROR("Failed to parse auth response. Body preview: {}", httpResponse.body.substr(0, 200));
        }
    } else {
        response.error_message = "HTTP " + std::to_string(httpResponse.statusCode) + ": " + httpResponse.body;
        m_lastError = response.error_message;
        LOG_ERROR("Anonymous sign-in failed: {}", response.error_message);
    }
    
    return response;
}

AuthResponse SupabaseClient::signUpWithEmail(const std::string& email, const std::string& password) {
    AuthResponse response;
    response.success = false;
    
    if (!m_initialized) {
        response.error_message = "SupabaseClient not initialized";
        return response;
    }
    
    // POST vers /auth/v1/signup
    std::string url = m_projectUrl + "/auth/v1/signup";
    
    // Construire le JSON body
    std::ostringstream json;
    json << "{\"email\":\"" << email << "\",\"password\":\"" << password << "\"}";
    
    // Headers pour l'authentification (pas besoin d'Authorization pour signup)
    std::map<std::string, std::string> headers = buildHeaders(false);
    HTTPClient::Response httpResponse = m_httpClient->post(url, json.str(), headers);
    
    if (httpResponse.statusCode == 200 || httpResponse.statusCode == 201) {
        if (parseAuthResponse(httpResponse.body, response)) {
            response.success = true;
            m_accessToken = response.access_token;
            m_refreshToken = response.refresh_token;  // Stocker le refresh_token
            m_userId = response.user_id;  // Stocker le user_id
            
            // Sauvegarder le JWT et refresh_token dans la config pour réutilisation
            Config& config = Config::getInstance();
            config.setSupabaseAccessToken(m_accessToken);
            config.setSupabaseRefreshToken(m_refreshToken);
            config.setSupabaseUserId(m_userId);
            config.save();
            
            LOG_INFO("Email sign-up successful, user_id: {} (saved to config)", response.user_id);
        } else {
            response.error_message = "Failed to parse auth response";
            m_lastError = response.error_message;
        }
    } else {
        response.error_message = "HTTP " + std::to_string(httpResponse.statusCode) + ": " + httpResponse.body;
        m_lastError = response.error_message;
        LOG_ERROR("Email sign-up failed: {}", response.error_message);
    }
    
    return response;
}

AuthResponse SupabaseClient::signInWithEmail(const std::string& email, const std::string& password) {
    AuthResponse response;
    response.success = false;
    
    if (!m_initialized) {
        response.error_message = "SupabaseClient not initialized";
        return response;
    }
    
    // POST vers /auth/v1/token?grant_type=password
    std::string url = m_projectUrl + "/auth/v1/token?grant_type=password";
    
    // Construire le JSON body
    std::ostringstream json;
    json << "{\"email\":\"" << email << "\",\"password\":\"" << password << "\"}";
    
    // Headers pour l'authentification (pas besoin d'Authorization pour signin)
    std::map<std::string, std::string> headers = buildHeaders(false);
    HTTPClient::Response httpResponse = m_httpClient->post(url, json.str(), headers);
    
    if (httpResponse.statusCode == 200 || httpResponse.statusCode == 201) {
        if (parseAuthResponse(httpResponse.body, response)) {
            response.success = true;
            m_accessToken = response.access_token;
            m_refreshToken = response.refresh_token;  // Stocker le refresh_token
            m_userId = response.user_id;  // Stocker le user_id
            
            // Sauvegarder le JWT et refresh_token dans la config pour réutilisation
            Config& config = Config::getInstance();
            config.setSupabaseAccessToken(m_accessToken);
            config.setSupabaseRefreshToken(m_refreshToken);
            config.setSupabaseUserId(m_userId);
            config.save();
            
            LOG_INFO("Email sign-in successful, user_id: {} (saved to config)", response.user_id);
        } else {
            response.error_message = "Failed to parse auth response";
            m_lastError = response.error_message;
        }
    } else {
        response.error_message = "HTTP " + std::to_string(httpResponse.statusCode) + ": " + httpResponse.body;
        m_lastError = response.error_message;
        LOG_ERROR("Email sign-in failed: {}", response.error_message);
    }
    
    return response;
}

bool SupabaseClient::signOut() {
    // 1. Tenter de supprimer l'entrée account_transfer avant de perdre l'authentification
    // On le fait sans le lock pour éviter les deadlocks avec refreshSession (qui peut être appelé par deleteRequest),
    // mais on prend une copie locale des infos nécessaires sous lock.
    std::string usernameToCheck;
    std::string currentToken;
    
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        usernameToCheck = m_username;
        currentToken = m_accessToken;
    }
    
    // Fallback: essayer de récupérer le username depuis la config si non présent en cache
    if (usernameToCheck.empty()) {
        Config& config = Config::getInstance();
        usernameToCheck = config.getCommunityRatingsUsername();
    }
    
    if (!usernameToCheck.empty() && !currentToken.empty()) {
        LOG_INFO("Deleting account transfer entry for username: {}", usernameToCheck);
        
        // Construire l'URL de suppression
        std::map<std::string, std::string> queryParams;
        queryParams["username"] = "eq." + usernameToCheck;
        
        std::string queryString = buildQueryString(queryParams);
        std::string deleteEndpoint = "/rest/v1/account_transfer?" + queryString;
        
        // Appel de suppression (peut déclencher un refresh qui prendra le lock brièvement)
        deleteRequest(deleteEndpoint);
    }

    std::lock_guard<std::mutex> lock(m_clientMutex); // Protection contre refreshSession concurrent pour le nettoyage
    
    m_accessToken.clear();
    m_refreshToken.clear();
    m_userId.clear();
    m_username.clear(); // Vider le cache username
    m_usernameFetched = false; // Reset le flag
    
    // Effacer le JWT et refresh_token de la config
    Config& config = Config::getInstance();
    config.setSupabaseAccessToken("");
    config.setSupabaseRefreshToken("");
    config.setSupabaseUserId("");
    config.save();
    
    LOG_INFO("Signed out (tokens cleared from config)");
    return true;
}

void SupabaseClient::setAccessToken(const std::string& token) {
    m_accessToken = token;
}

bool SupabaseClient::upsertRating(const std::string& fileHash, int rating) {
    if (!isAuthenticated()) {
        m_lastError = "Not authenticated";
        return false;
    }
    
    if (rating < 1 || rating > 5) {
        m_lastError = "Rating must be between 1 and 5";
        return false;
    }
    
    // POST vers /rest/v1/community_ratings avec upsert (ON CONFLICT)
    // Supabase utilise le header Prefer: resolution=merge-duplicates pour upsert
    std::string url = buildUrl("/rest/v1/community_ratings");
    
    // Construire le JSON body
    std::string userId = getUserId();
    std::ostringstream json;
    json << "{\"user_id\":\"" << userId << "\","
         << "\"file_hash\":\"" << fileHash 
         << "\",\"rating\":" << rating << "}";
    
    // Pour l'upsert, on utilise POST avec le header Prefer
    // Supabase nécessite le header Prefer: resolution=merge-duplicates pour upsert
    // et on doit spécifier les colonnes dans l'URL
    url += "?on_conflict=user_id,file_hash";
    
    std::map<std::string, std::string> headers = buildHeaders(true);
    headers["Prefer"] = "resolution=merge-duplicates";
    HTTPClient::Response response = m_httpClient->post(url, json.str(), headers);
    
    // Vérifier si le token a expiré et renouveler si nécessaire
    if (handleTokenExpiration(response)) {
        // Relancer la requête avec le nouveau token
        headers = buildHeaders(true);
        headers["Prefer"] = "resolution=merge-duplicates";
        response = m_httpClient->post(url, json.str(), headers);
    }
    
    if (response.statusCode == 200 || response.statusCode == 201) {
        LOG_INFO("Rating upserted successfully: file_hash={}, rating={}", fileHash, rating);
        return true;
    } else {
        m_lastError = "HTTP " + std::to_string(response.statusCode) + ": " + response.body;
        LOG_ERROR("Failed to upsert rating: status={}, body={}, user_id={}", 
                  response.statusCode, response.body.substr(0, 200), m_userId);
        return false;
    }
}

bool SupabaseClient::getMyRating(const std::string& fileHash, CommunityRating& rating) {
    if (!isAuthenticated()) {
        m_lastError = "Not authenticated";
        return false;
    }
    
    if (m_userId.empty()) {
        m_lastError = "User ID not available";
        LOG_ERROR("getMyRating: User ID is empty");
        return false;
    }
    
    // GET vers /rest/v1/community_ratings?file_hash=eq.{fileHash}&user_id=eq.{userId}&select=*
    // On filtre explicitement par user_id pour plus de sécurité (en plus de RLS)
    std::map<std::string, std::string> queryParams;
    queryParams["file_hash"] = "eq." + fileHash;
    queryParams["user_id"] = "eq." + m_userId;
    queryParams["select"] = "*";
    
    LOG_DEBUG("getMyRating: file_hash={}, user_id={}, queryParams: file_hash={}, user_id={}, select={}", 
              fileHash, m_userId, queryParams["file_hash"], queryParams["user_id"], queryParams["select"]);
    HTTPClient::Response response = get("/rest/v1/community_ratings", queryParams);
    LOG_DEBUG("getMyRating response: status={}, body length={}", response.statusCode, response.body.length());
    
    if (response.statusCode == 200) {
        // Parser le JSON (tableau avec un élément)
        LOG_DEBUG("Parsing rating response. Body length: {}", response.body.length());
        if (response.body.length() < 500) {
            LOG_DEBUG("Response body: {}", response.body);
        }
        
        std::vector<CommunityRating> ratings;
        if (parseRatingsArray(response.body, ratings)) {
            LOG_DEBUG("Parsed {} ratings", ratings.size());
            if (!ratings.empty()) {
                rating = ratings[0];
                if (!rating.id.empty()) {
                    LOG_DEBUG("Rating found: id={}, file_hash={}, rating={}", rating.id, rating.file_hash, rating.rating);
                    return true;
                } else {
                    LOG_WARNING("Rating parsed but id is empty");
                    m_lastError = "Rating parsed but id is empty";
                    return false;
                }
            } else {
                // Pas de rating trouvé - c'est normal si l'utilisateur n'a pas encore voté
                LOG_DEBUG("No ratings found in response (empty array) - user hasn't rated this file yet");
                m_lastError = "Rating not found for this file";
                return false;
            }
        } else {
            // Erreur de parsing JSON
            LOG_WARNING("Failed to parse ratings array. Response body: {}", response.body.substr(0, 500));
            m_lastError = "Failed to parse response: " + response.body.substr(0, 200);
            return false;
        }
    } else {
        m_lastError = "HTTP " + std::to_string(response.statusCode) + ": " + response.body;
        LOG_ERROR("Failed to get rating: status={}, body length={}, body={}", 
                  response.statusCode, response.body.length(), 
                  response.body.length() > 0 ? response.body.substr(0, 500) : "(empty)");
        // Si le body est vide, c'est suspect - logger aussi les headers
        if (response.body.empty()) {
            LOG_ERROR("Response body is empty for status {}", response.statusCode);
        }
        return false;
    }
}

bool SupabaseClient::getAllMyRatings(std::vector<CommunityRating>& ratings) {
    if (!isAuthenticated()) {
        m_lastError = "Not authenticated";
        return false;
    }
    
    // GET vers /rest/v1/community_ratings?select=*
    // RLS garantit qu'on ne récupère que nos propres ratings
    std::map<std::string, std::string> queryParams;
    queryParams["select"] = "*";
    
    HTTPClient::Response response = get("/rest/v1/community_ratings", queryParams);
    
    if (response.statusCode == 200) {
        return parseRatingsArray(response.body, ratings);
    } else {
        m_lastError = "HTTP " + std::to_string(response.statusCode) + ": " + response.body;
        return false;
    }
}

bool SupabaseClient::deleteRating(const std::string& fileHash) {
    if (!isAuthenticated()) {
        m_lastError = "Not authenticated";
        return false;
    }
    
    // DELETE vers /rest/v1/community_ratings?file_hash=eq.{fileHash}
    // Construire les paramètres de requête
    std::map<std::string, std::string> queryParams;
    queryParams["file_hash"] = "eq." + fileHash;
    
    // Utiliser get() avec la méthode DELETE (ou créer une méthode dédiée)
    // Pour l'instant, on construit l'URL manuellement mais on passe seulement l'endpoint à deleteRequest
    std::string endpoint = "/rest/v1/community_ratings";
    if (!queryParams.empty()) {
        endpoint += "?" + buildQueryString(queryParams);
    }
    
    HTTPClient::Response response = deleteRequest(endpoint);
    
    if (response.statusCode == 200 || response.statusCode == 204) {
        LOG_DEBUG("Rating deleted successfully: file_hash={}", fileHash);
        return true;
    } else {
        m_lastError = "HTTP " + std::to_string(response.statusCode) + ": " + response.body;
        return false;
    }
}

bool SupabaseClient::getRatingStatistics(const std::string& fileHash, RatingStatistics& stats) {
    // GET vers /rest/v1/rating_statistics?file_hash=eq.{fileHash}
    std::map<std::string, std::string> queryParams;
    queryParams["file_hash"] = "eq." + fileHash;
    
    HTTPClient::Response response = get("/rest/v1/rating_statistics", queryParams);
    
    if (response.statusCode == 200) {
        return parseStatistics(response.body, stats);
    } else {
        m_lastError = "HTTP " + std::to_string(response.statusCode) + ": " + response.body;
        return false;
    }
}

bool SupabaseClient::getRatingStatisticsBatch(const std::vector<std::string>& fileHashes, 
                                               std::map<std::string, RatingStatistics>& stats) {
    // Pour un batch, on peut utiliser in() dans PostgREST
    // GET vers /rest/v1/rating_statistics?file_hash=in.(hash1,hash2,...)
    if (fileHashes.empty()) {
        return true;
    }
    
    std::ostringstream inClause;
    inClause << "in.(";
    for (size_t i = 0; i < fileHashes.size(); ++i) {
        if (i > 0) inClause << ",";
        inClause << fileHashes[i];
    }
    inClause << ")";
    
    std::map<std::string, std::string> queryParams;
    queryParams["file_hash"] = inClause.str();
    
    HTTPClient::Response response = get("/rest/v1/rating_statistics", queryParams);
    
    if (response.statusCode == 200) {
        // Parser le tableau de statistiques
        // Pour l'instant, on fait un parsing basique
        // TODO: Améliorer avec Glaze
        LOG_DEBUG("Batch statistics response: {}", response.body);
        // TODO: Implémenter le parsing du batch
        return false; // Pas encore implémenté
    } else {
        m_lastError = "HTTP " + std::to_string(response.statusCode) + ": " + response.body;
        return false;
    }
}

// Structure locale pour l'upsert
struct RatingUpsert {
    std::string user_id;
    std::string file_hash;
    int rating;
};

template <>
struct glz::meta<RatingUpsert> {
    using T = RatingUpsert;
    static constexpr auto value = glz::object(
        "user_id", &T::user_id,
        "file_hash", &T::file_hash,
        "rating", &T::rating
    );
};

bool SupabaseClient::syncRatingsToCloud(const std::vector<CommunityRating>& ratings) {
    if (!isAuthenticated()) {
        m_lastError = "Not authenticated";
        return false;
    }
    
    if (ratings.empty()) {
        return true;
    }
    
    LOG_INFO("Syncing {} ratings to cloud...", ratings.size());
    
    // Batch size (Supabase peut gérer plus, mais 50 est sûr pour les timeouts et buffers)
    const size_t BATCH_SIZE = 50;
    size_t processed = 0;
    
    for (size_t i = 0; i < ratings.size(); i += BATCH_SIZE) {
        size_t end = std::min(i + BATCH_SIZE, ratings.size());
        
        // Préparer le batch
        std::vector<RatingUpsert> batch;
        batch.reserve(end - i);
        std::string userId = getUserId();
        for (size_t j = i; j < end; ++j) {
            batch.push_back({userId, ratings[j].file_hash, ratings[j].rating});
        }
        
        // Sérialiser en JSON avec Glaze
        std::string jsonBody;
        auto err = glz::write<glz::opts{}>(batch, jsonBody);
        if (err.ec != glz::error_code::none) {
             LOG_ERROR("Failed to serialize batch to JSON");
             return false;
        }
        
        // POST vers /rest/v1/community_ratings avec upsert
        std::string url = buildUrl("/rest/v1/community_ratings");
        url += "?on_conflict=user_id,file_hash";
        
        std::map<std::string, std::string> headers = buildHeaders(true);
        headers["Prefer"] = "resolution=merge-duplicates";
        
        HTTPClient::Response response = m_httpClient->post(url, jsonBody, headers);
        
        // Vérifier si le token a expiré et renouveler si nécessaire
        if (handleTokenExpiration(response)) {
            // Relancer la requête avec le nouveau token
            headers = buildHeaders(true);
            headers["Prefer"] = "resolution=merge-duplicates";
            response = m_httpClient->post(url, jsonBody, headers);
        }
        
        if (response.statusCode >= 200 && response.statusCode < 300) {
            processed += batch.size();
            LOG_INFO("Synced batch {}/{}: {} ratings upserted.", (i/BATCH_SIZE)+1, (ratings.size()+BATCH_SIZE-1)/BATCH_SIZE, batch.size());
        } else {
            m_lastError = "HTTP " + std::to_string(response.statusCode) + ": " + response.body;
            LOG_ERROR("Failed to sync batch: {}", m_lastError);
            return false;
        }
    }
    
    LOG_INFO("Successfully synced {} ratings to cloud.", processed);
    return true;
}

bool SupabaseClient::syncRatingsFromCloud(std::map<std::string, int>& localRatings) {
    // TODO: Implémenter la synchronisation depuis le cloud
    return false;
}

// Méthodes privées

HTTPClient::Response SupabaseClient::get(const std::string& endpoint, const std::map<std::string, std::string>& queryParams, bool includeAuth) {
    // buildUrl() retourne déjà une URL complète (avec m_projectUrl)
    std::string url = buildUrl(endpoint);
    if (!queryParams.empty()) {
        url += "?" + buildQueryString(queryParams);
    }
    
    // Vérifier que l'URL n'est pas dupliquée (debug)
    if (url.find(m_projectUrl + m_projectUrl) != std::string::npos) {
        LOG_ERROR("URL duplication detected! URL: {}", url);
        // Corriger en enlevant la duplication
        size_t dupPos = url.find(m_projectUrl + m_projectUrl);
        if (dupPos != std::string::npos) {
            url = m_projectUrl + url.substr(dupPos + m_projectUrl.length());
        }
    }
    LOG_DEBUG("GET request URL: {}", url);
    std::map<std::string, std::string> headers = buildHeaders(includeAuth);
    
    // Debug détaillé via logger uniquement (pas de std::cout)
    LOG_DEBUG("GET request headers:");
    for (const auto& [key, value] : headers) {
        LOG_DEBUG("  {}: {}", key, (value.length() > 50 ? value.substr(0, 50) + "..." : value));
    }
    
    HTTPClient::Response response = m_httpClient->get(url, headers);
    
    LOG_DEBUG("GET response: status={}, body length={}", response.statusCode, response.body.length());
    if (response.statusCode != 200 && response.body.length() < 500) {
        LOG_DEBUG("GET response body: {}", response.body);
    }
    
    // Vérifier si le token a expiré et renouveler si nécessaire (sauf si requête sans auth)
    if (includeAuth && handleTokenExpiration(response)) {
        // Relancer la requête avec le nouveau token
        headers = buildHeaders(true);
        response = m_httpClient->get(url, headers);
    }
    
    return response;
}

HTTPClient::Response SupabaseClient::post(const std::string& endpoint, const std::string& jsonBody) {
    std::string url = buildUrl(endpoint);
    std::map<std::string, std::string> headers = buildHeaders(true);
    HTTPClient::Response response = m_httpClient->post(url, jsonBody, headers);
    
    // Vérifier si le token a expiré et renouveler si nécessaire
    if (handleTokenExpiration(response)) {
        // Relancer la requête avec le nouveau token
        headers = buildHeaders(true);
        response = m_httpClient->post(url, jsonBody, headers);
    }
    
    return response;
}

HTTPClient::Response SupabaseClient::patch(const std::string& endpoint, const std::string& jsonBody) {
    std::string url = buildUrl(endpoint);
    std::map<std::string, std::string> headers = buildHeaders(true);
    return m_httpClient->patch(url, jsonBody, headers);
}

HTTPClient::Response SupabaseClient::deleteRequest(const std::string& endpoint) {
    std::string url = buildUrl(endpoint);
    std::map<std::string, std::string> headers = buildHeaders(true);
    HTTPClient::Response response = m_httpClient->deleteRequest(url, headers);
    
    // Vérifier si le token a expiré et renouveler si nécessaire
    if (handleTokenExpiration(response)) {
        // Relancer la requête avec le nouveau token
        headers = buildHeaders(true);
        response = m_httpClient->deleteRequest(url, headers);
    }
    
    return response;
}

std::string SupabaseClient::buildUrl(const std::string& endpoint) const {
    std::string url = m_projectUrl;
    if (endpoint[0] != '/') {
        url += "/";
    }
    url += endpoint;
    return url;
}

std::map<std::string, std::string> SupabaseClient::buildHeaders(bool includeAuth) const {
    std::map<std::string, std::string> headers;
    headers["apikey"] = m_anonKey;
    headers["Content-Type"] = "application/json";
    if (includeAuth && !m_accessToken.empty()) {
        headers["Authorization"] = "Bearer " + m_accessToken;
    }
    return headers;
}

std::string SupabaseClient::buildQueryString(const std::map<std::string, std::string>& params) const {
    std::ostringstream query;
    bool first = true;
    for (const auto& [key, value] : params) {
        if (!first) query << "&";
        // Encoder les caractères spéciaux dans la valeur (basique)
        std::string encodedValue = value;
        // Remplacer les espaces par %20 (encodage URL basique)
        size_t pos = 0;
        while ((pos = encodedValue.find(' ', pos)) != std::string::npos) {
            encodedValue.replace(pos, 1, "%20");
            pos += 3;
        }
        query << key << "=" << encodedValue;
        first = false;
    }
    return query.str();
}

bool SupabaseClient::parseAuthResponse(const std::string& json, AuthResponse& response) {
    // Parsing JSON basique (on pourra améliorer avec Glaze plus tard)
    // Format attendu: {"access_token":"...","user":{"id":"..."}}
    // ou {"access_token":"...","token":"...","user":{"id":"..."}}
    
    size_t tokenPos = json.find("\"access_token\"");
    if (tokenPos == std::string::npos) {
        tokenPos = json.find("\"token\"");
    }
    if (tokenPos != std::string::npos) {
        size_t colonPos = json.find(':', tokenPos);
        if (colonPos != std::string::npos) {
            size_t quoteStart = json.find('"', colonPos);
            if (quoteStart != std::string::npos) {
                size_t quoteEnd = json.find('"', quoteStart + 1);
                if (quoteEnd != std::string::npos) {
                    response.access_token = json.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                }
            }
        }
    }
    
    // Extraire refresh_token
    size_t refreshPos = json.find("\"refresh_token\"");
    if (refreshPos != std::string::npos) {
        size_t colonPos = json.find(':', refreshPos);
        if (colonPos != std::string::npos) {
            size_t quoteStart = json.find('"', colonPos);
            if (quoteStart != std::string::npos) {
                size_t quoteEnd = json.find('"', quoteStart + 1);
                if (quoteEnd != std::string::npos) {
                    response.refresh_token = json.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                }
            }
        }
    }
    
    // Extraire user_id
    size_t userPos = json.find("\"user\"");
    if (userPos != std::string::npos) {
        size_t idPos = json.find("\"id\"", userPos);
        if (idPos != std::string::npos) {
            size_t colonPos = json.find(':', idPos);
            if (colonPos != std::string::npos) {
                size_t quoteStart = json.find('"', colonPos);
                if (quoteStart != std::string::npos) {
                    size_t quoteEnd = json.find('"', quoteStart + 1);
                    if (quoteEnd != std::string::npos) {
                        response.user_id = json.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                    }
                }
            }
        }
    }
    
    return !response.access_token.empty();
}

bool SupabaseClient::parseRating(const std::string& json, CommunityRating& rating) {
    // Parsing JSON basique pour un objet rating
    // Format: {"id":"...","user_id":"...","file_hash":"...","filepath":"...","rating":5,"created_at":"...","updated_at":"..."}
    
    // Extraire id
    size_t idPos = json.find("\"id\"");
    if (idPos != std::string::npos) {
        size_t colonPos = json.find(':', idPos);
        if (colonPos != std::string::npos) {
            size_t quoteStart = json.find('"', colonPos);
            if (quoteStart != std::string::npos) {
                size_t quoteEnd = json.find('"', quoteStart + 1);
                if (quoteEnd != std::string::npos) {
                    rating.id = json.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                }
            }
        }
    }
    
    // Extraire user_id
    size_t userIdPos = json.find("\"user_id\"");
    if (userIdPos != std::string::npos) {
        size_t colonPos = json.find(':', userIdPos);
        if (colonPos != std::string::npos) {
            size_t quoteStart = json.find('"', colonPos);
            if (quoteStart != std::string::npos) {
                size_t quoteEnd = json.find('"', quoteStart + 1);
                if (quoteEnd != std::string::npos) {
                    rating.user_id = json.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                }
            }
        }
    }
    
    // Extraire file_hash
    size_t hashPos = json.find("\"file_hash\"");
    if (hashPos != std::string::npos) {
        size_t colonPos = json.find(':', hashPos);
        if (colonPos != std::string::npos) {
            size_t quoteStart = json.find('"', colonPos);
            if (quoteStart != std::string::npos) {
                size_t quoteEnd = json.find('"', quoteStart + 1);
                if (quoteEnd != std::string::npos) {
                    rating.file_hash = json.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                }
            }
        }
    }
    
    // Extraire rating (nombre)
    size_t ratingPos = json.find("\"rating\"");
    if (ratingPos != std::string::npos) {
        size_t colonPos = json.find(':', ratingPos);
        if (colonPos != std::string::npos) {
            size_t numStart = json.find_first_of("0123456789", colonPos);
            if (numStart != std::string::npos) {
                size_t numEnd = json.find_first_not_of("0123456789", numStart);
                if (numEnd == std::string::npos) {
                    numEnd = json.length();
                }
                std::string ratingStr = json.substr(numStart, numEnd - numStart);
                try {
                    rating.rating = std::stoi(ratingStr);
                } catch (...) {
                    return false;
                }
            }
        }
    }
    
    // Extraire created_at
    size_t createdPos = json.find("\"created_at\"");
    if (createdPos != std::string::npos) {
        size_t colonPos = json.find(':', createdPos);
        if (colonPos != std::string::npos) {
            size_t quoteStart = json.find('"', colonPos);
            if (quoteStart != std::string::npos) {
                size_t quoteEnd = json.find('"', quoteStart + 1);
                if (quoteEnd != std::string::npos) {
                    rating.created_at = json.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                }
            }
        }
    }
    
    // Extraire updated_at
    size_t updatedPos = json.find("\"updated_at\"");
    if (updatedPos != std::string::npos) {
        size_t colonPos = json.find(':', updatedPos);
        if (colonPos != std::string::npos) {
            size_t quoteStart = json.find('"', colonPos);
            if (quoteStart != std::string::npos) {
                size_t quoteEnd = json.find('"', quoteStart + 1);
                if (quoteEnd != std::string::npos) {
                    rating.updated_at = json.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                }
            }
        }
    }
    
    return !rating.id.empty() && !rating.file_hash.empty();
}

bool SupabaseClient::parseRatingsArray(const std::string& json, std::vector<CommunityRating>& ratings) {
    // Parsing JSON basique pour un tableau de ratings
    // Format: [{"id":"...","user_id":"...",...}, {"id":"...","user_id":"...",...}]
    
    ratings.clear();
    
    // Trouver le début du tableau
    size_t arrayStart = json.find('[');
    if (arrayStart == std::string::npos) {
        return false;
    }
    
    // Parser chaque objet dans le tableau
    size_t pos = arrayStart + 1;
    while (pos < json.length()) {
        // Trouver le début d'un objet
        size_t objStart = json.find('{', pos);
        if (objStart == std::string::npos) {
            break;
        }
        
        // Trouver la fin de l'objet (chercher le } correspondant)
        int braceCount = 0;
        size_t objEnd = objStart;
        for (size_t i = objStart; i < json.length(); ++i) {
            if (json[i] == '{') {
                braceCount++;
            } else if (json[i] == '}') {
                braceCount--;
                if (braceCount == 0) {
                    objEnd = i + 1;
                    break;
                }
            }
        }
        
        if (objEnd > objStart) {
            // Extraire l'objet JSON
            std::string objJson = json.substr(objStart, objEnd - objStart);
            
            // Parser l'objet
            CommunityRating rating;
            if (parseRating(objJson, rating)) {
                ratings.push_back(rating);
            }
        }
        
        // Passer à l'objet suivant
        pos = objEnd;
        
        // Chercher la virgule ou la fin du tableau
        size_t nextComma = json.find(',', pos);
        size_t arrayEnd = json.find(']', pos);
        
        if (arrayEnd != std::string::npos && (nextComma == std::string::npos || arrayEnd < nextComma)) {
            break;  // Fin du tableau
        }
        
        if (nextComma != std::string::npos) {
            pos = nextComma + 1;
        } else {
            break;
        }
    }
    
    return !ratings.empty();
}

bool SupabaseClient::parseStatistics(const std::string& json, RatingStatistics& stats) {
    // Parsing JSON basique
    // TODO: Améliorer avec Glaze
    return false; // Pas encore implémenté
}

AuthResponse SupabaseClient::refreshSession() {
    std::lock_guard<std::mutex> lock(m_clientMutex); // Protection contre signOut concurrent
    
    AuthResponse response;
    response.success = false;
    
    if (!m_initialized) {
        response.error_message = "SupabaseClient not initialized";
        return response;
    }
    
    if (m_refreshToken.empty()) {
        response.error_message = "No refresh token available";
        m_lastError = response.error_message;
        return response;
    }
    
    // POST vers /auth/v1/token?grant_type=refresh_token
    std::string url = m_projectUrl + "/auth/v1/token?grant_type=refresh_token";
    
    // Body avec refresh_token
    std::ostringstream json;
    json << "{\"refresh_token\":\"" << m_refreshToken << "\"}";
    
    // Headers (pas besoin d'Authorization pour refresh)
    std::map<std::string, std::string> headers = buildHeaders(false);
    HTTPClient::Response httpResponse = m_httpClient->post(url, json.str(), headers);
    
    if (httpResponse.statusCode == 200 || httpResponse.statusCode == 201) {
        if (parseAuthResponse(httpResponse.body, response)) {
            response.success = true;
            m_accessToken = response.access_token;
            // Le refresh_token peut être renouvelé aussi
            if (!response.refresh_token.empty()) {
                m_refreshToken = response.refresh_token;
            }
            m_userId = response.user_id;  // S'assurer que user_id est toujours présent
            
            // Sauvegarder les nouveaux tokens dans la config
            // Note: Config est thread-safe, pas besoin de lock ici pour Config
            Config& config = Config::getInstance();
            config.setSupabaseAccessToken(m_accessToken);
            config.setSupabaseRefreshToken(m_refreshToken);
            config.setSupabaseUserId(m_userId);
            config.save();
            
            LOG_INFO("Session refreshed successfully, user_id: {}", m_userId);

            // Mettre à jour le refresh_token dans la table account_transfer pour maintenir la validité du code de récupération
            // car Supabase utilise la rotation de refresh tokens (l'ancien est invalidé).
            std::string usernameToUpdate = m_username;
            if (usernameToUpdate.empty()) {
                Config& c = Config::getInstance();
                usernameToUpdate = c.getCommunityRatingsUsername();
            }

            if (!usernameToUpdate.empty()) {
                LOG_INFO("Updating refresh token in account_transfer for user: {}", usernameToUpdate);
                
                // PATCH /rest/v1/account_transfer?username=eq.{username}
                // On construit l'URL manuellement pour éviter d'appeler buildUrl/buildHeaders qui pourraient être complexes ici
                std::string url = m_projectUrl + "/rest/v1/account_transfer?username=eq." + usernameToUpdate;
                
                std::ostringstream jsonUpdate;
                jsonUpdate << "{\"refresh_token\":\"" << m_refreshToken << "\"}";
                
                std::map<std::string, std::string> headers;
                headers["apikey"] = m_anonKey;
                headers["Content-Type"] = "application/json";
                headers["Authorization"] = "Bearer " + m_accessToken;
                headers["Prefer"] = "return=minimal";
                
                // On utilise le client HTTP directement (on a déjà le lock)
                // On ignore le résultat, c'est du "best effort"
                m_httpClient->patch(url, jsonUpdate.str(), headers);
            }
        } else {
            response.error_message = "Failed to parse refresh response";
            m_lastError = response.error_message;
        }
    } else {
        response.error_message = "HTTP " + std::to_string(httpResponse.statusCode) + ": " + httpResponse.body;
        m_lastError = response.error_message;
        LOG_ERROR("Failed to refresh session: {}", response.error_message);
    }
    
    return response;
}

bool SupabaseClient::isTokenExpired() const {
    if (m_accessToken.empty()) {
        return true;
    }
    
    // Décoder le JWT pour vérifier l'expiration
    // Format JWT: header.payload.signature
    // On extrait le payload (base64url)
    size_t firstDot = m_accessToken.find('.');
    if (firstDot == std::string::npos) {
        return true;  // Token invalide
    }
    
    size_t secondDot = m_accessToken.find('.', firstDot + 1);
    if (secondDot == std::string::npos) {
        return true;  // Token invalide
    }
    
    std::string payload = m_accessToken.substr(firstDot + 1, secondDot - firstDot - 1);
    
    // Ajouter le padding si nécessaire pour base64url
    while (payload.length() % 4 != 0) {
        payload += "=";
    }
    
    // Décoder base64url (remplacer - par + et _ par /)
    std::replace(payload.begin(), payload.end(), '-', '+');
    std::replace(payload.begin(), payload.end(), '_', '/');
    
    // Chercher "exp" dans le payload (qui est en JSON)
    size_t expPos = payload.find("exp");
    if (expPos == std::string::npos) {
        // Pas de champ exp, on considère comme valide (peut être un token sans expiration)
        return false;
    }
    
    // Extraire la valeur de exp (nombre Unix timestamp)
    size_t colonPos = payload.find(':', expPos);
    if (colonPos == std::string::npos) {
        return false;
    }
    
    // Trouver le nombre après ":"
    size_t numStart = payload.find_first_of("0123456789", colonPos);
    if (numStart == std::string::npos) {
        return false;
    }
    
    size_t numEnd = payload.find_first_not_of("0123456789", numStart);
    if (numEnd == std::string::npos) {
        numEnd = payload.length();
    }
    
    std::string expStr = payload.substr(numStart, numEnd - numStart);
    try {
        long long expTimestamp = std::stoll(expStr);
        
        // Obtenir le timestamp actuel (Unix time)
        auto now = std::chrono::system_clock::now();
        auto nowTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        
        // Vérifier si expiré (avec une marge de 60 secondes pour éviter les problèmes de timing)
        return expTimestamp < (nowTimestamp + 60);
    } catch (...) {
        return false;  // Erreur de parsing, on considère comme valide
    }
}

bool SupabaseClient::handleTokenExpiration(HTTPClient::Response& response) {
    // Vérifier si la réponse indique un token expiré
    // Supabase retourne généralement 401 avec un message d'erreur
    if (response.statusCode == 401) {
        // Vérifier si c'est une erreur de token expiré
        std::string bodyLower = response.body;
        std::transform(bodyLower.begin(), bodyLower.end(), bodyLower.begin(), ::tolower);
        
        if ((bodyLower.find("token") != std::string::npos && 
            (bodyLower.find("expired") != std::string::npos || 
             bodyLower.find("invalid") != std::string::npos)) ||
             bodyLower.find("jwt expired") != std::string::npos) {
            
            LOG_WARNING("Token expired or invalid detected in API response, attempting refresh...");
            
            // Tenter de renouveler le token
            if (!m_refreshToken.empty()) {
                AuthResponse refreshResponse = refreshSession();
                if (refreshResponse.success) {
                    LOG_INFO("Token refreshed successfully, retrying original request");
                    return true;  // Token renouvelé, la requête doit être relancée
                } else {
                    LOG_ERROR("Failed to refresh token: {}", refreshResponse.error_message);
                    m_lastError = "Token expired and refresh failed: " + refreshResponse.error_message;
                    return false;  // Échec du refresh
                }
            } else {
                LOG_ERROR("Token expired but no refresh_token available");
                m_lastError = "Token expired and no refresh_token available";
                return false;  // Pas de refresh_token
            }
        }
    }
    
    return false;  // Pas une erreur de token expiré
}

bool SupabaseClient::updateUsername(const std::string& username) {
    if (!isAuthenticated()) {
        m_lastError = "Not authenticated";
        return false;
    }
    
    // PUT vers /auth/v1/user pour mettre à jour user_metadata
    std::string url = m_projectUrl + "/auth/v1/user";
    
    // Construire le JSON body avec user_metadata
    std::ostringstream json;
    json << "{\"user_metadata\":{\"username\":\"" << username << "\"}}";
    
    std::map<std::string, std::string> headers = buildHeaders(true);
    HTTPClient::Response response = m_httpClient->put(url, json.str(), headers);
    
    if (response.statusCode == 200 || response.statusCode == 201) {
        LOG_INFO("Username updated successfully: {}", username);
        m_username = username; // Mettre à jour le cache
        m_usernameFetched = true; // Cache valide
        return true;
    } else {
        m_lastError = "HTTP " + std::to_string(response.statusCode) + ": " + response.body;
        LOG_ERROR("Failed to update username: {}", m_lastError);
        return false;
    }
}

std::string SupabaseClient::getUsername() const {
    if (!isAuthenticated()) {
        return "";
    }
    
    // Utiliser le cache si disponible
    if (!m_username.empty()) {
        return m_username;
    }
    
    // Si on a déjà tenté de récupérer le username (même si échec), ne pas insister
    // pour éviter de spammer l'API à chaque frame
    if (m_usernameFetched) {
        return "";
    }
    
    m_usernameFetched = true; // Marquer comme tenté
    
    // GET vers /auth/v1/user pour récupérer les métadonnées
    std::string url = m_projectUrl + "/auth/v1/user";
    
    std::map<std::string, std::string> headers = buildHeaders(true);
    HTTPClient::Response response = m_httpClient->get(url, headers);
    
    if (response.statusCode == 200) {
        // Parser le JSON pour extraire user_metadata.username
        // Format: {"user_metadata":{"username":"..."}}
        size_t usernamePos = response.body.find("\"username\"");
        if (usernamePos != std::string::npos) {
            size_t colonPos = response.body.find(':', usernamePos);
            if (colonPos != std::string::npos) {
                size_t quoteStart = response.body.find('"', colonPos);
                if (quoteStart != std::string::npos) {
                    size_t quoteEnd = response.body.find('"', quoteStart + 1);
                    if (quoteEnd != std::string::npos) {
                        m_username = response.body.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                        return m_username;
                    }
                }
            }
        }
    }
    
    return "";
}

bool SupabaseClient::isUsernameAvailable(const std::string& username) {
    if (!m_initialized) {
        m_lastError = "SupabaseClient not initialized";
        return false;
    }
    
    if (username.empty()) {
        m_lastError = "Username is empty";
        return false;
    }
    
    // GET vers /rest/v1/account_transfer?username=eq.{username}
    // Si le résultat est vide ([]), le username est disponible
    std::map<std::string, std::string> queryParams;
    queryParams["username"] = "eq." + username;
    queryParams["select"] = "username";
    
    HTTPClient::Response response = get("/rest/v1/account_transfer", queryParams);
    
    if (response.statusCode == 200) {
        // Si le résultat est un tableau vide [], le username est disponible
        // Si le résultat contient un élément, le username est déjà pris
        if (response.body == "[]" || response.body.empty()) {
            LOG_DEBUG("Username '{}' is available", username);
            return true;
        } else {
            LOG_DEBUG("Username '{}' is already taken", username);
            m_lastError = "Username already taken";
            return false;
        }
    } else {
        m_lastError = "HTTP " + std::to_string(response.statusCode) + ": " + response.body;
        LOG_ERROR("Failed to check username availability: {}", m_lastError);
        return false;
    }
}

SupabaseClient::RecoveryCodeResponse SupabaseClient::generateRecoveryCode() {
    RecoveryCodeResponse response;
    response.success = false;
    
    if (!isAuthenticated()) {
        response.error_message = "Not authenticated";
        return response;
    }
    
    if (m_refreshToken.empty()) {
        response.error_message = "No refresh token available";
        return response;
    }
    
    // Générer un code aléatoire de 8 chiffres
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(10000000, 99999999);
    std::string code = std::to_string(dis(gen));
    
    // POST vers /rest/v1/account_transfer pour créer le code
    std::string url = buildUrl("/rest/v1/account_transfer");
    
    // Récupérer le username depuis user_metadata (ou config si pas encore synchronisé)
    std::string username = getUsername();
    if (username.empty()) {
        // Fallback: utiliser le username de la config locale
        Config& config = Config::getInstance();
        username = config.getCommunityRatingsUsername();
    }
    
    if (username.empty()) {
        response.error_message = "Username not set. Please set a username first.";
        return response;
    }
    
    // Nettoyer les anciens codes de cet utilisateur (pour éviter les conflits d'unicité)
    // Le username est UNIQUE dans la table account_transfer
    std::string deleteUrl = "/rest/v1/account_transfer?username=eq." + username;
    deleteRequest(deleteUrl);
    
    // Construire le JSON body avec code, refresh_token et username
    std::ostringstream json;
    json << "{\"code\":\"" << code << "\",\"refresh_token\":\"" << m_refreshToken 
         << "\",\"username\":\"" << username << "\"}";
    
    std::map<std::string, std::string> headers = buildHeaders(true);
    HTTPClient::Response httpResponse = m_httpClient->post(url, json.str(), headers);
    
    if (httpResponse.statusCode == 200 || httpResponse.statusCode == 201) {
        response.success = true;
        response.recovery_code = code;
        LOG_INFO("Recovery code generated successfully: {}", code);
    } else {
        response.error_message = "HTTP " + std::to_string(httpResponse.statusCode) + ": " + httpResponse.body;
        LOG_ERROR("Failed to generate recovery code: {}", response.error_message);
    }
    
    return response;
}

AuthResponse SupabaseClient::recoverAccountWithCode(const std::string& recoveryCode) {
    AuthResponse response;
    response.success = false;
    
    if (!m_initialized) {
        response.error_message = "SupabaseClient not initialized";
        return response;
    }
    
    if (recoveryCode.empty()) {
        response.error_message = "Recovery code is empty";
        return response;
    }
    
    // GET vers /rest/v1/account_transfer?code=eq.{recoveryCode}
    std::map<std::string, std::string> queryParams;
    queryParams["code"] = "eq." + recoveryCode;
    queryParams["select"] = "refresh_token,username";
    
    // Même requête que isUsernameAvailable : avec auth si session disponible (anon ou autre)
    // Sans auth (anon pur), le rôle anon peut ne pas voir les lignes selon RLS/GRANT.
    HTTPClient::Response httpResponse = get("/rest/v1/account_transfer", queryParams, true);
    
    if (httpResponse.statusCode == 200) {
        // Sans refresh_token dans la réponse = code inexistant ou non trouvé (RLS/GRANT, etc.)
        if (httpResponse.body.find("\"refresh_token\"") == std::string::npos) {
            response.error_message = "Code invalide ou non trouvé. Vérifie le code ou génère-en un nouveau sur l'autre appareil.";
            LOG_WARNING("Recovery code '{}' not found. Body length={}", recoveryCode, httpResponse.body.length());
            return response;
        }
        // Parser le JSON pour extraire refresh_token
        // Format: [{"refresh_token":"..."}]
        size_t refreshPos = httpResponse.body.find("\"refresh_token\"");
        if (refreshPos != std::string::npos) {
            size_t colonPos = httpResponse.body.find(':', refreshPos);
            if (colonPos != std::string::npos) {
                size_t quoteStart = httpResponse.body.find('"', colonPos);
                if (quoteStart != std::string::npos) {
                    size_t quoteEnd = httpResponse.body.find('"', quoteStart + 1);
                    if (quoteEnd != std::string::npos) {
                        std::string refreshToken = httpResponse.body.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                        
                        // Utiliser le refresh_token pour récupérer l'access_token
                        m_refreshToken = refreshToken;
                        AuthResponse refreshResponse = refreshSession();
                        
                        if (refreshResponse.success) {
                            response = refreshResponse;
                            
                            // Extraire le username depuis la réponse (si présent)
                            size_t usernamePos = httpResponse.body.find("\"username\"");
                            if (usernamePos != std::string::npos) {
                                size_t colonPos = httpResponse.body.find(':', usernamePos);
                                if (colonPos != std::string::npos) {
                                    size_t quoteStart = httpResponse.body.find('"', colonPos);
                                    if (quoteStart != std::string::npos) {
                                        size_t quoteEnd = httpResponse.body.find('"', quoteStart + 1);
                                        if (quoteEnd != std::string::npos) {
                                            std::string recoveredUsername = httpResponse.body.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                                            
                                            // Sauvegarder le username dans la config
                                            Config& config = Config::getInstance();
                                            config.setCommunityRatingsUsername(recoveredUsername);
                                            config.save();
                                            
                                            LOG_INFO("Username recovered: {}", recoveredUsername);
                                        }
                                    }
                                }
                            }
                            
                            // Mettre à jour le refresh_token dans account_transfer : Supabase fait une rotation,
                            // l'ancien est invalidé, le nouveau doit être stocké pour garder le code valide
                            std::string patchUrl = m_projectUrl + "/rest/v1/account_transfer?code=eq." + recoveryCode;
                            std::ostringstream jsonUpdate;
                            jsonUpdate << "{\"refresh_token\":\"" << m_refreshToken << "\"}";
                            std::map<std::string, std::string> patchHeaders = buildHeaders(true);
                            patchHeaders["Prefer"] = "return=minimal";
                            m_httpClient->patch(patchUrl, jsonUpdate.str(), patchHeaders);
                            LOG_INFO("Account recovered successfully with code: {}, refresh_token updated in account_transfer", recoveryCode);
                        } else {
                            response.error_message = "Failed to refresh session: " + refreshResponse.error_message;
                        }
                    }
                }
            }
        }
        
        if (!response.success && response.error_message.empty()) {
            response.error_message = "Failed to parse recovery code response";
        }
    } else {
        response.error_message = "HTTP " + std::to_string(httpResponse.statusCode) + ": " + httpResponse.body;
        if (httpResponse.statusCode == 404 || httpResponse.body.find("[]") != std::string::npos) {
            response.error_message = "Invalid or expired recovery code";
        }
        LOG_ERROR("Failed to recover account: {}", response.error_message);
    }
    
    return response;
}

#endif // ENABLE_CLOUD_SAVE
