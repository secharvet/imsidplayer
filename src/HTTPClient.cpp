#ifdef ENABLE_CLOUD_SAVE

#include "HTTPClient.h"
#include "Logger.h"
#include "Utils.h"
#include <cstring>
#include <sstream>
#include <algorithm>
#include <cctype>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET socket_t;
    #define SOCKET_ERROR_CODE WSAGetLastError()
    #define CLOSE_SOCKET closesocket
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define SOCKET_ERROR_VALUE SOCKET_ERROR
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    #include <errno.h>
    typedef int socket_t;
    #define SOCKET_ERROR_CODE errno
    #define CLOSE_SOCKET close
    #define INVALID_SOCKET_VALUE -1
    #define SOCKET_ERROR_VALUE -1
#endif

// Variable statique pour l'initialisation des sockets
bool HTTPClient::s_socketsInitialized = false;

HTTPClient::HTTPClient()
    : m_initialized(false)
    , m_rootCALoaded(false)
    , m_connected(false)
{
    mbedtls_ssl_init(&m_ssl);
    mbedtls_ssl_config_init(&m_conf);
    mbedtls_net_init(&m_serverFd);
    mbedtls_x509_crt_init(&m_cacert);
    m_psaInitialized = false;
}

HTTPClient::~HTTPClient() {
    cleanup();
    mbedtls_x509_crt_free(&m_cacert);
    mbedtls_net_free(&m_serverFd);
    mbedtls_ssl_config_free(&m_conf);
    mbedtls_ssl_free(&m_ssl);
    
    // PSA Crypto n'a pas de fonction de nettoyage explicite
    // Il sera nettoyé automatiquement à la fin du programme
    m_psaInitialized = false;
}

bool HTTPClient::initializeSockets() {
    if (s_socketsInitialized) {
        return true;
    }
    
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        return false;
    }
#endif
    
    s_socketsInitialized = true;
    return true;
}

void HTTPClient::cleanupSockets() {
    if (!s_socketsInitialized) {
        return;
    }
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    s_socketsInitialized = false;
}

bool HTTPClient::initialize() {
    if (m_initialized) {
        return true;
    }
    
    if (!initializeSockets()) {
        m_lastError = "Failed to initialize sockets";
        return false;
    }
    
    // Initialiser PSA Crypto (MBed TLS 4.0 utilise PSA pour le RNG)
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        m_lastError = "Failed to initialize PSA Crypto";
        return false;
    }
    m_psaInitialized = true;
    
    // Configurer SSL/TLS
    int ret = mbedtls_ssl_config_defaults(&m_conf,
                                          MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        char error_buf[256];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        m_lastError = "Failed to configure SSL defaults: " + std::string(error_buf);
        return false;
    }
    
    // Configurer la validation des certificats (sera activée après chargement du Root CA)
    mbedtls_ssl_conf_authmode(&m_conf, MBEDTLS_SSL_VERIFY_NONE); // Temporaire, sera changé après loadRootCA
    
    // MBed TLS 4.0 utilise PSA pour le RNG automatiquement
    // Ne pas limiter les ciphersuites - laisser MBed TLS choisir les meilleurs
    // mbedtls_ssl_conf_ciphersuites(&m_conf, mbedtls_ssl_list_ciphersuites()); // Commenté - laisser les defaults
    
    // Configurer les timeouts pour éviter les blocages
    mbedtls_ssl_conf_read_timeout(&m_conf, 5000); // 5 secondes de timeout pour la lecture
    
    // Initialiser le contexte SSL
    ret = mbedtls_ssl_setup(&m_ssl, &m_conf);
    if (ret != 0) {
        char error_buf[256];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        m_lastError = "Failed to setup SSL context: " + std::string(error_buf);
        return false;
    }
    
    m_initialized = true;
    LOG_INFO("HTTPClient initialized");
    return true;
}

void HTTPClient::cleanup() {
    if (m_connected) {
        disconnect();
    }
    
    if (m_initialized) {
        m_initialized = false;
        LOG_DEBUG("HTTPClient cleaned up");
    }
}

bool HTTPClient::loadRootCA(const std::string& caFilePath) {
    if (!m_initialized) {
        m_lastError = "HTTPClient not initialized";
        return false;
    }
    
    FILE* f = fopen(caFilePath.c_str(), "r");
    if (!f) {
        m_lastError = "Failed to open Root CA file: " + caFilePath;
        return false;
    }
    
    // mbedtls_x509_crt_parse_file peut parser plusieurs certificats dans un fichier PEM
    // Cela permet de charger un bundle avec Root CA + certificats intermédiaires
    int ret = mbedtls_x509_crt_parse_file(&m_cacert, caFilePath.c_str());
    fclose(f);
    
    if (ret != 0) {
        char error_buf[256];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        m_lastError = "Failed to parse Root CA: " + std::string(error_buf);
        return false;
    }
    
    // Configurer MBed TLS pour utiliser ce Root CA (et les certificats intermédiaires)
    // m_cacert contient maintenant toute la chaîne de certificats
    mbedtls_ssl_conf_ca_chain(&m_conf, &m_cacert, NULL);
    mbedtls_ssl_conf_authmode(&m_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    
    m_rootCALoaded = true;
    LOG_INFO("Root CA (and intermediates) loaded successfully from: {}", caFilePath);
    return true;
}

bool HTTPClient::resolveHostname(const std::string& hostname, std::string& ipAddress) {
    struct addrinfo hints, *result = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;  // IPv4
    hints.ai_socktype = SOCK_STREAM;
    
    int ret = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);
    if (ret != 0) {
        m_lastError = "Failed to resolve hostname: " + hostname;
        return false;
    }
    
    char ip_str[INET_ADDRSTRLEN];
    struct sockaddr_in* addr = (struct sockaddr_in*)result->ai_addr;
    inet_ntop(AF_INET, &addr->sin_addr, ip_str, INET_ADDRSTRLEN);
    ipAddress = ip_str;
    
    freeaddrinfo(result);
    return true;
}

bool HTTPClient::connectSSL(const std::string& host, int port) {
    if (m_connected) {
        disconnect();
    }
    
    if (!m_initialized) {
        m_lastError = "HTTPClient not initialized";
        return false;
    }
    
    // Connecter au serveur avec MBed TLS
    // Utiliser le hostname directement pour que SNI fonctionne correctement
    // mbedtls_net_connect peut prendre soit l'IP soit le hostname
    std::string portStr = std::to_string(port);
    LOG_DEBUG("Connecting to {}:{}", host, port);
    int ret = mbedtls_net_connect(&m_serverFd, host.c_str(), portStr.c_str(), MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) {
        char error_buf[256];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        m_lastError = "Failed to connect to " + host + ":" + portStr + ": " + std::string(error_buf);
        LOG_ERROR("Connection failed: {}", m_lastError);
        return false;
    }
    LOG_DEBUG("TCP connection established");
    
    // Configurer le socket pour le contexte SSL
    mbedtls_ssl_set_bio(&m_ssl, &m_serverFd, mbedtls_net_send, mbedtls_net_recv, nullptr);
    
    // Configurer le hostname pour la validation du certificat
    ret = mbedtls_ssl_set_hostname(&m_ssl, host.c_str());
    if (ret != 0) {
        char error_buf[256];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        m_lastError = "Failed to set hostname: " + std::string(error_buf);
        mbedtls_net_free(&m_serverFd);
        return false;
    }
    
    // Effectuer le handshake SSL/TLS
    LOG_DEBUG("Starting SSL/TLS handshake with {}:{}", host, port);
    int handshakeAttempts = 0;
    while ((ret = mbedtls_ssl_handshake(&m_ssl)) != 0) {
        handshakeAttempts++;
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            char error_buf[256];
            mbedtls_strerror(ret, error_buf, sizeof(error_buf));
            m_lastError = "SSL handshake failed: " + std::string(error_buf);
            LOG_ERROR("SSL handshake failed after {} attempts: {}", handshakeAttempts, m_lastError);
            mbedtls_net_free(&m_serverFd);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    LOG_DEBUG("SSL/TLS handshake completed successfully after {} attempts", handshakeAttempts);
    
    // Vérifier le certificat si Root CA chargé
    if (m_rootCALoaded) {
        ret = verifyCertificate(host);
        if (ret != 0) {
            disconnect();
            return false;
        }
    }
    
    m_connected = true;
    LOG_DEBUG("Connected to {}:{} via SSL/TLS", host, port);
    return true;
}

void HTTPClient::disconnect() {
    if (m_connected) {
        // Essayer de fermer proprement la connexion SSL
        // Mais ne pas bloquer si ça échoue
        int ret = mbedtls_ssl_close_notify(&m_ssl);
        if (ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            // Si la fermeture échoue, on continue quand même
            LOG_DEBUG("SSL close_notify returned error: {}", ret);
        }
        mbedtls_net_free(&m_serverFd);
        mbedtls_net_init(&m_serverFd);
        // Réinitialiser le contexte SSL pour la prochaine connexion
        mbedtls_ssl_session_reset(&m_ssl);
        m_connected = false;
    }
}

int HTTPClient::verifyCertificate(const std::string& hostname) {
    uint32_t flags = mbedtls_ssl_get_verify_result(&m_ssl);
    
    if (flags != 0) {
        char vrfy_buf[512];
        mbedtls_x509_crt_verify_info(vrfy_buf, sizeof(vrfy_buf), "  ! ", flags);
        m_lastError = "Certificate verification failed: " + std::string(vrfy_buf);
        LOG_ERROR("Certificate verification failed for {}: {}", hostname, vrfy_buf);
        return -1;
    }
    
    LOG_DEBUG("Certificate verified successfully for {}", hostname);
    return 0;
}

std::string HTTPClient::buildHTTPRequest(const std::string& method, 
                                         const std::string& path,
                                         const std::string& host,
                                         const std::string& body) {
    std::ostringstream request;
    
    request << method << " " << path << " HTTP/1.1\r\n";
    request << "Host: " << host << "\r\n";
    request << "User-Agent: imSidPlayer/1.0\r\n";
    request << "Accept: application/json\r\n";
    request << "Connection: close\r\n";
    
    if (!body.empty()) {
        request << "Content-Type: application/json; charset=utf-8\r\n";
        size_t bodySize = body.length();
        request << "Content-Length: " << bodySize << "\r\n";
        LOG_INFO("buildHTTPRequest: Body size={} bytes, Content-Length={}", bodySize, bodySize);
    }
    
    request << "\r\n";
    
    if (!body.empty()) {
        request << body;
    }
    
    std::string requestStr = request.str();
    LOG_INFO("buildHTTPRequest: Total request size={} bytes (headers + body)", requestStr.length());
    return requestStr;
}

int HTTPClient::sendRequest(const std::string& request) {
    LOG_INFO("sendRequest: Starting to send HTTP request ({} bytes)", request.length());
    if (request.length() > 500) {
        LOG_DEBUG("sendRequest: Request preview (first 500 chars): {}", request.substr(0, 500));
    } else {
        LOG_DEBUG("sendRequest: Full request: {}", request);
    }
    
    size_t written = 0;
    size_t len = request.length();
    const unsigned char* buf = (const unsigned char*)request.c_str();
    
    while (written < len) {
        int ret = mbedtls_ssl_write(&m_ssl, buf + written, len - written);
        if (ret < 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                char error_buf[256];
                mbedtls_strerror(ret, error_buf, sizeof(error_buf));
                m_lastError = "Failed to send request: " + std::string(error_buf);
                LOG_ERROR("sendRequest: Failed to send request: {} (ret={})", m_lastError, ret);
                return -1;
            }
            // WANT_READ ou WANT_WRITE : continuer
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            written += ret;
            LOG_DEBUG("sendRequest: Sent {} bytes (total: {}/{})", ret, written, len);
        }
    }
    
    LOG_INFO("sendRequest: Request sent completely ({} bytes total)", written);
    
    // Pour POST/PUT/PATCH, s'assurer que toutes les données sont bien envoyées
    // En forçant un flush SSL si nécessaire
    // Pas besoin d'attendre - on va lire immédiatement après
    
    return 0;
}

HTTPClient::Response HTTPClient::parseResponse() {
    LOG_DEBUG("parseResponse: Starting to read HTTP response");
    Response response;
    std::string responseData;
    char buffer[4096];
    size_t contentLength = 0;
    bool headersComplete = false;
    int consecutiveEmptyReads = 0;
    const int maxEmptyReads = 10; // Maximum de lectures vides avant d'abandonner
    int totalReads = 0;
    
    // Lire la réponse par chunks jusqu'à ce qu'on ait tout reçu
    while (true) {
        totalReads++;
        LOG_DEBUG("parseResponse: Read attempt #{}", totalReads);
        int ret = mbedtls_ssl_read(&m_ssl, (unsigned char*)buffer, sizeof(buffer) - 1);
        LOG_DEBUG("parseResponse: mbedtls_ssl_read returned: {} ({} bytes)", ret, ret > 0 ? ret : 0);
        
        if (ret > 0) {
            consecutiveEmptyReads = 0; // Reset le compteur si on a reçu des données
            buffer[ret] = '\0';
            responseData += std::string(buffer, ret);
            LOG_DEBUG("parseResponse: Received {} bytes, total so far: {} bytes", ret, responseData.length());
            
            // Vérifier si les headers sont complets
            if (!headersComplete) {
                size_t headerEnd = responseData.find("\r\n\r\n");
                if (headerEnd != std::string::npos) {
                    headersComplete = true;
                    LOG_DEBUG("parseResponse: Headers complete at position {}", headerEnd);
                    // Extraire Content-Length si présent
                    std::string headers = responseData.substr(0, headerEnd);
                    size_t clPos = headers.find("Content-Length:");
                    if (clPos != std::string::npos) {
                        size_t clValueStart = headers.find(' ', clPos) + 1;
                        size_t clValueEnd = headers.find("\r\n", clValueStart);
                    if (clValueEnd != std::string::npos) {
                        try {
                            contentLength = std::stoul(headers.substr(clValueStart, clValueEnd - clValueStart));
                            LOG_DEBUG("parseResponse: Content-Length found: {} bytes", contentLength);
                        } catch (...) {
                            contentLength = 0;
                            LOG_DEBUG("parseResponse: Failed to parse Content-Length");
                        }
                    } else {
                        LOG_DEBUG("parseResponse: No Content-Length header found");
                    }
                }
            }
            }
            
            // Si on a Content-Length, vérifier si on a tout lu
            if (headersComplete && contentLength > 0) {
                size_t bodyStart = responseData.find("\r\n\r\n");
                if (bodyStart != std::string::npos) {
                    bodyStart += 4;
                    if (responseData.length() >= bodyStart + contentLength) {
                        break; // On a tout lu
                    }
                }
            }
        } else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            // Continuer à attendre - c'est normal, le serveur n'a pas encore envoyé de données
            LOG_DEBUG("parseResponse: SSL_WANT_READ/WRITE, waiting...");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        } else if (ret == -26880 || ret == -31488) {
            // Ces codes sont des messages TLS post-handshake (NewSessionTicket) - les ignorer et continuer
            LOG_DEBUG("parseResponse: TLS post-handshake message (code {}), ignoring", ret);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        } else if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            // Connexion fermée normalement par le serveur
            LOG_DEBUG("parseResponse: SSL_PEER_CLOSE_NOTIFY received, connection closed by server");
            break;
        } else if (ret == 0) {
            // EOF (fin de fichier) - connexion fermée
            consecutiveEmptyReads++;
            LOG_DEBUG("parseResponse: EOF (ret=0), consecutive empty reads: {}/{}", consecutiveEmptyReads, maxEmptyReads);
            if (consecutiveEmptyReads >= maxEmptyReads) {
                LOG_DEBUG("parseResponse: Max empty reads reached, stopping");
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            // Erreur de lecture - vérifier le code d'erreur
            char error_buf[256];
            mbedtls_strerror(ret, error_buf, sizeof(error_buf));
            LOG_DEBUG("parseResponse: Error code {} ({})", ret, error_buf);
            
            // -29184 (invalid SSL record) peut arriver si le serveur a déjà commencé à répondre
            // ou si la connexion se ferme. Essayer de continuer à lire plusieurs fois.
            if (ret == -29184) {
                LOG_DEBUG("parseResponse: Invalid SSL record detected, this might be normal. Waiting and retrying...");
                // Attendre un peu et réessayer plusieurs fois (jusqu'à 10 fois)
                for (int retryCount = 0; retryCount < 10; retryCount++) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    int retry = mbedtls_ssl_read(&m_ssl, (unsigned char*)buffer, sizeof(buffer) - 1);
                    LOG_DEBUG("parseResponse: Retry #{} returned: {}", retryCount + 1, retry);
                    if (retry > 0) {
                        LOG_DEBUG("parseResponse: Retry successful! Got {} bytes", retry);
                        buffer[retry] = '\0';
                        responseData += std::string(buffer, retry);
                        // Continuer la boucle principale pour lire plus
                        break;
                    } else if (retry == MBEDTLS_ERR_SSL_WANT_READ || retry == MBEDTLS_ERR_SSL_WANT_WRITE) {
                        LOG_DEBUG("parseResponse: Retry wants read/write, continuing...");
                        continue; // Continuer la boucle de retry
                    } else if (retry == -26880 || retry == -31488) {
                        LOG_DEBUG("parseResponse: Retry got post-handshake message, continuing...");
                        continue; // Continuer la boucle de retry
                    } else if (retry == 0 || retry == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                        LOG_DEBUG("parseResponse: Connection closed during retry");
                        break; // Sortir de la boucle de retry
                    } else if (retry == -29184) {
                        // Encore une erreur invalid SSL record, continuer à essayer
                        continue;
                    }
                }
                // Si on a reçu des données, continuer la boucle principale
                if (!responseData.empty()) {
                    continue;
                }
            }
            
            // Pour les autres erreurs, essayer d'attendre un peu et de relire une fois
            if (responseData.empty() && ret != -26880 && ret != -31488 && ret != -29184) {
                LOG_DEBUG("parseResponse: Error detected, waiting 100ms and retrying once...");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                int retry = mbedtls_ssl_read(&m_ssl, (unsigned char*)buffer, sizeof(buffer) - 1);
                LOG_DEBUG("parseResponse: Retry returned: {}", retry);
                if (retry > 0) {
                    LOG_DEBUG("parseResponse: Retry successful! Got {} bytes", retry);
                    buffer[retry] = '\0';
                    responseData += std::string(buffer, retry);
                    continue; // Continuer pour lire plus
                } else if (retry == MBEDTLS_ERR_SSL_WANT_READ || retry == MBEDTLS_ERR_SSL_WANT_WRITE || retry == -26880 || retry == -31488) {
                    LOG_DEBUG("parseResponse: Retry wants read/write or post-handshake, continuing...");
                    continue;
                }
            }
            
            if (responseData.empty()) {
                m_lastError = "Failed to read response: " + std::string(error_buf);
                LOG_ERROR("parseResponse error: {} (ret={})", m_lastError, ret);
            } else {
                // On a déjà reçu des données, on peut continuer
                LOG_DEBUG("parseResponse: Received error {} ({}) but already have {} bytes, stopping", ret, error_buf, responseData.length());
            }
            break;
        }
    }
    
    if (responseData.empty()) {
        m_lastError = "Empty response from server";
        LOG_ERROR("parseResponse: Empty response data - no data received from server (total reads: {})", totalReads);
        return response;
    }
    
    LOG_DEBUG("parseResponse: Received {} bytes of data", responseData.length());
    if (responseData.length() > 0) {
        size_t previewLen = std::min(responseData.length(), size_t(300));
        LOG_DEBUG("parseResponse: First {} chars: {}", previewLen, responseData.substr(0, previewLen));
    }
    
    // Parser la réponse HTTP
    size_t headerEnd = responseData.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        m_lastError = "Invalid HTTP response format";
        return response;
    }
    
    std::string headers = responseData.substr(0, headerEnd);
    std::string rawBody = responseData.substr(headerEnd + 4);
    
    // Extraire Content-Length depuis les headers si pas déjà fait dans la boucle de lecture
    if (contentLength == 0) {
        size_t clPos = headers.find("Content-Length:");
        if (clPos != std::string::npos) {
            size_t clValueStart = headers.find(' ', clPos) + 1;
            size_t clValueEnd = headers.find("\r\n", clValueStart);
            if (clValueEnd != std::string::npos) {
                try {
                    contentLength = std::stoul(headers.substr(clValueStart, clValueEnd - clValueStart));
                    LOG_DEBUG("parseResponse: Content-Length from headers: {} bytes", contentLength);
                } catch (...) {
                    contentLength = 0;
                }
            }
        }
    }
    
    // Vérifier si Transfer-Encoding: chunked
    bool isChunked = false;
    size_t tePos = headers.find("Transfer-Encoding:");
    if (tePos != std::string::npos) {
        size_t teValueStart = headers.find(':', tePos) + 1;
        size_t teValueEnd = headers.find("\r\n", teValueStart);
        if (teValueEnd != std::string::npos) {
            std::string teValue = headers.substr(teValueStart, teValueEnd - teValueStart);
            // Trim whitespace
            teValue.erase(0, teValue.find_first_not_of(" \t"));
            teValue.erase(teValue.find_last_not_of(" \t") + 1);
            if (teValue == "chunked") {
                isChunked = true;
                LOG_DEBUG("parseResponse: Response uses Transfer-Encoding: chunked");
            }
        }
    }
    
    // Décoder le body si chunked
    if (isChunked) {
        response.body = decodeChunkedBody(rawBody);
        LOG_DEBUG("parseResponse: Decoded chunked body: {} bytes -> {} bytes", rawBody.length(), response.body.length());
    } else {
        // Si on a Content-Length, limiter le body à cette taille
        // (il peut y avoir des données supplémentaires après le body)
        if (contentLength > 0 && rawBody.length() > contentLength) {
            response.body = rawBody.substr(0, contentLength);
            LOG_DEBUG("parseResponse: Limited body to Content-Length: {} bytes (raw body was {} bytes)", 
                     contentLength, rawBody.length());
        } else {
            response.body = rawBody;
        }
    }
    
    // Parser la ligne de statut
    size_t firstLineEnd = headers.find("\r\n");
    if (firstLineEnd != std::string::npos) {
        std::string statusLine = headers.substr(0, firstLineEnd);
        LOG_DEBUG("parseResponse: Status line: {}", statusLine);
        // Format: HTTP/1.1 200 OK ou HTTP/2 200
        size_t firstSpace = statusLine.find(' ');
        if (firstSpace != std::string::npos) {
            size_t secondSpace = statusLine.find(' ', firstSpace + 1);
            if (secondSpace != std::string::npos) {
                try {
                    response.statusCode = std::stoi(statusLine.substr(firstSpace + 1, secondSpace - firstSpace - 1));
                } catch (...) {
                    LOG_ERROR("parseResponse: Failed to parse status code from: {}", statusLine);
                    response.statusCode = 0;
                }
            } else {
                // Format HTTP/2 sans message (HTTP/2 200)
                try {
                    response.statusCode = std::stoi(statusLine.substr(firstSpace + 1));
                } catch (...) {
                    LOG_ERROR("parseResponse: Failed to parse status code from: {}", statusLine);
                    response.statusCode = 0;
                }
            }
        }
    } else {
        LOG_ERROR("parseResponse: No status line found in headers");
    }
    
    LOG_DEBUG("parseResponse: Parsed status code: {}", response.statusCode);
    
    // Parser les headers
    size_t pos = firstLineEnd + 2;
    while (pos < headers.length()) {
        size_t lineEnd = headers.find("\r\n", pos);
        if (lineEnd == std::string::npos) {
            lineEnd = headers.length();
        }
        
        std::string headerLine = headers.substr(pos, lineEnd - pos);
        size_t colonPos = headerLine.find(':');
        if (colonPos != std::string::npos) {
            std::string key = headerLine.substr(0, colonPos);
            std::string value = headerLine.substr(colonPos + 1);
            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            response.headers[key] = value;
        }
        
        pos = lineEnd + 2;
    }
    
    return response;
}

HTTPClient::Response HTTPClient::get(const std::string& url) {
    Response response;
    m_lastError.clear();
    
    // Suivre les redirections (max 5 pour éviter les boucles)
    std::string currentUrl = url;
    int redirectCount = 0;
    const int maxRedirects = 5;
    
    while (redirectCount < maxRedirects) {
        // Parser l'URL
        if (currentUrl.find("https://") != 0) {
            m_lastError = "Only HTTPS URLs are supported";
            return response;
        }
        
        std::string host;
        int port = 443;
        
        // Extraire le host et le path de l'URL
        size_t pathStart = currentUrl.find("https://");
        if (pathStart != std::string::npos) {
            pathStart += 8; // "https://"
            size_t pathEnd = currentUrl.find('/', pathStart);
            if (pathEnd == std::string::npos) {
                pathEnd = currentUrl.length();
            }
            host = currentUrl.substr(pathStart, pathEnd - pathStart);
            
            std::string path = "/";
            if (pathEnd < currentUrl.length()) {
                path = currentUrl.substr(pathEnd);
            }
            
            if (!connectSSL(host, port)) {
                return response;
            }
            
            std::string request = buildHTTPRequest("GET", path, host);
            if (sendRequest(request) != 0) {
                disconnect();
                return response;
            }
            
            response = parseResponse();
            disconnect();
            
            // Si c'est une redirection (301, 302, 303, 307, 308), suivre la redirection
            if (response.statusCode == 301 || response.statusCode == 302 || 
                response.statusCode == 303 || response.statusCode == 307 || 
                response.statusCode == 308) {
                auto locationIt = response.headers.find("Location");
                if (locationIt != response.headers.end()) {
                    std::string newUrl = locationIt->second;
                    LOG_DEBUG("Following redirect {} -> {}", currentUrl, newUrl);
                    
                    // Si l'URL de redirection est relative, la convertir en absolue
                    if (newUrl.find("http://") == 0 || newUrl.find("https://") == 0) {
                        currentUrl = newUrl;
                    } else {
                        // URL relative : construire l'URL complète
                        size_t lastSlash = currentUrl.find_last_of('/');
                        if (lastSlash != std::string::npos) {
                            currentUrl = currentUrl.substr(0, lastSlash + 1) + newUrl;
                        } else {
                            currentUrl = currentUrl + "/" + newUrl;
                        }
                    }
                    redirectCount++;
                    continue; // Refaire la requête avec la nouvelle URL
                } else {
                    LOG_WARNING("Redirect {} but no Location header found", response.statusCode);
                    break; // Pas de Location header, arrêter
                }
            } else {
                // Pas de redirection, retourner la réponse
                return response;
            }
        } else {
            m_lastError = "Invalid URL format";
            return response;
        }
    }
    
    if (redirectCount >= maxRedirects) {
        m_lastError = "Too many redirects (max " + std::to_string(maxRedirects) + ")";
        LOG_ERROR("Too many redirects, stopping at: {}", currentUrl);
    }
    
    return response;
}

HTTPClient::Response HTTPClient::post(const std::string& url, const std::string& jsonData) {
    Response response;
    m_lastError.clear();
    
    LOG_INFO("POST: URL={}, body size={} bytes", url, jsonData.length());
    
    // Parser l'URL (similaire à get)
    if (url.find("https://") != 0) {
        m_lastError = "Only HTTPS URLs are supported";
        return response;
    }
    
    std::string host = "api.npoint.io";
    int port = 443;
    
    size_t pathStart = url.find("https://");
    if (pathStart != std::string::npos) {
        pathStart += 8;
        size_t pathEnd = url.find('/', pathStart);
        if (pathEnd == std::string::npos) {
            pathEnd = url.length();
        }
        host = url.substr(pathStart, pathEnd - pathStart);
        
        std::string path = "/";
        if (pathEnd < url.length()) {
            path = url.substr(pathEnd);
        }
        
        LOG_INFO("POST: host={}, path={}, port={}", host, path, port);
        
        if (!connectSSL(host, port)) {
            LOG_ERROR("POST: Failed to connect SSL");
            return response;
        }
        
        std::string request = buildHTTPRequest("POST", path, host, jsonData);
        LOG_INFO("POST: Request built, total size={} bytes", request.length());
        
        if (sendRequest(request) != 0) {
            LOG_ERROR("POST: Failed to send request");
            disconnect();
            return response;
        }
        
        LOG_INFO("POST: Request sent successfully, waiting for response...");
        response = parseResponse();
        LOG_INFO("POST: Response received: status={}, body size={} bytes", response.statusCode, response.body.length());
        disconnect();
    }
    
    return response;
}

HTTPClient::Response HTTPClient::put(const std::string& url, const std::string& jsonData) {
    Response response;
    m_lastError.clear();
    
    // Similaire à post, mais avec PUT
    if (url.find("https://") != 0) {
        m_lastError = "Only HTTPS URLs are supported";
        return response;
    }
    
    std::string host = "api.npoint.io";
    int port = 443;
    
    size_t pathStart = url.find("https://");
    if (pathStart != std::string::npos) {
        pathStart += 8;
        size_t pathEnd = url.find('/', pathStart);
        if (pathEnd == std::string::npos) {
            pathEnd = url.length();
        }
        host = url.substr(pathStart, pathEnd - pathStart);
        
        std::string path = "/";
        if (pathEnd < url.length()) {
            path = url.substr(pathEnd);
        }
        
        if (!connectSSL(host, port)) {
            return response;
        }
        
        std::string request = buildHTTPRequest("PUT", path, host, jsonData);
        if (sendRequest(request) != 0) {
            disconnect();
            return response;
        }
        
        response = parseResponse();
        disconnect();
    }
    
    return response;
}

HTTPClient::Response HTTPClient::patch(const std::string& url, const std::string& jsonData) {
    Response response;
    m_lastError.clear();
    
    // Similaire à post, mais avec PATCH
    if (url.find("https://") != 0) {
        m_lastError = "Only HTTPS URLs are supported";
        return response;
    }
    
    std::string host = "api.npoint.io";
    int port = 443;
    
    size_t pathStart = url.find("https://");
    if (pathStart != std::string::npos) {
        pathStart += 8;
        size_t pathEnd = url.find('/', pathStart);
        if (pathEnd == std::string::npos) {
            pathEnd = url.length();
        }
        host = url.substr(pathStart, pathEnd - pathStart);
        
        std::string path = "/";
        if (pathEnd < url.length()) {
            path = url.substr(pathEnd);
        }
        
        if (!connectSSL(host, port)) {
            return response;
        }
        
        std::string request = buildHTTPRequest("PATCH", path, host, jsonData);
        if (sendRequest(request) != 0) {
            disconnect();
            return response;
        }
        
        response = parseResponse();
        disconnect();
    }
    
    return response;
}

std::string HTTPClient::decodeChunkedBody(const std::string& chunkedData) {
    std::string decoded;
    size_t pos = 0;
    
    while (pos < chunkedData.length()) {
        // Trouver la fin de la ligne de taille (en hexadécimal)
        size_t lineEnd = chunkedData.find("\r\n", pos);
        if (lineEnd == std::string::npos) {
            // Pas de fin de ligne trouvée, prendre le reste
            break;
        }
        
        // Lire la taille du chunk (en hexadécimal)
        std::string chunkSizeStr = chunkedData.substr(pos, lineEnd - pos);
        // Supprimer les espaces et extensions (si présentes, format: size;extension)
        size_t semicolonPos = chunkSizeStr.find(';');
        if (semicolonPos != std::string::npos) {
            chunkSizeStr = chunkSizeStr.substr(0, semicolonPos);
        }
        
        // Trim whitespace
        chunkSizeStr.erase(0, chunkSizeStr.find_first_not_of(" \t"));
        chunkSizeStr.erase(chunkSizeStr.find_last_not_of(" \t") + 1);
        
        // Convertir de hexadécimal en décimal
        size_t chunkSize = 0;
        try {
            chunkSize = std::stoul(chunkSizeStr, nullptr, 16);
        } catch (...) {
            LOG_ERROR("decodeChunkedBody: Failed to parse chunk size: {}", chunkSizeStr);
            break;
        }
        
        // Si la taille est 0, c'est le dernier chunk
        if (chunkSize == 0) {
            break;
        }
        
        // Avancer après le \r\n
        pos = lineEnd + 2;
        
        // Vérifier qu'on a assez de données
        if (pos + chunkSize > chunkedData.length()) {
            LOG_WARNING("decodeChunkedBody: Chunk size {} exceeds remaining data", chunkSize);
            break;
        }
        
        // Ajouter les données du chunk
        decoded += chunkedData.substr(pos, chunkSize);
        pos += chunkSize;
        
        // Sauter le \r\n après les données du chunk
        if (pos + 1 < chunkedData.length() && chunkedData.substr(pos, 2) == "\r\n") {
            pos += 2;
        } else {
            // Pas de \r\n, peut-être la fin des données
            break;
        }
    }
    
    return decoded;
}

#endif // ENABLE_CLOUD_SAVE
