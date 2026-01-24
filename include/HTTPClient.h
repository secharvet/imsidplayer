#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#ifdef ENABLE_CLOUD_SAVE

#include <string>
#include <map>
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>
#include <psa/crypto.h>

class HTTPClient {
public:
    struct Response {
        int statusCode = 0;
        std::string body;
        std::map<std::string, std::string> headers;
    };
    
    HTTPClient();
    ~HTTPClient();
    
    // Initialisation et nettoyage
    bool initialize();
    void cleanup();
    
    // Charger le Root CA pour validation des certificats
    bool loadRootCA(const std::string& caFilePath);
    
    // Requêtes HTTP
    Response get(const std::string& url);
    Response post(const std::string& url, const std::string& jsonData);
    Response put(const std::string& url, const std::string& jsonData);
    Response patch(const std::string& url, const std::string& jsonData);
    
    // Obtenir le dernier message d'erreur
           std::string getLastError() const { return m_lastError; }
           
       private:
           // Helper pour décoder Transfer-Encoding: chunked
           std::string decodeChunkedBody(const std::string& chunkedData);
    // Connexion SSL/TLS
    bool connectSSL(const std::string& host, int port);
    void disconnect();
    
    // Construction et envoi de requêtes HTTP
    std::string buildHTTPRequest(const std::string& method, 
                                 const std::string& path,
                                 const std::string& body = "");
    int sendRequest(const std::string& request);
    Response parseResponse();
    
    // Validation du certificat
    int verifyCertificate(const std::string& hostname);
    
    // Résolution DNS et connexion
    bool resolveHostname(const std::string& hostname, std::string& ipAddress);
    
    // Contextes MBed TLS
    mbedtls_ssl_context m_ssl;
    mbedtls_ssl_config m_conf;
    mbedtls_net_context m_serverFd;
    mbedtls_x509_crt m_cacert;  // Root CA pour validation
    bool m_psaInitialized;  // PSA Crypto initialisé
    
    // État
    bool m_initialized;
    bool m_rootCALoaded;
    bool m_connected;
    std::string m_lastError;
    
    // Initialisation des sockets (Windows vs POSIX)
    static bool initializeSockets();
    static void cleanupSockets();
    static bool s_socketsInitialized;
};

#endif // ENABLE_CLOUD_SAVE

#endif // HTTP_CLIENT_H
