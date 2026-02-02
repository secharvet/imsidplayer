cmake_minimum_required(VERSION 3.15)

# Détecter la version depuis Git
# CMake exécute cette commande au moment de la configuration (cmake ..)
find_package(Git QUIET)
if(GIT_FOUND)
    # Obtenir le dernier tag (ex: v0.0.24)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    if(GIT_TAG)
        # Enlever le "v" du tag (ex: v0.0.24 -> 0.0.24)
        string(REGEX REPLACE "^v" "" VERSION_FROM_GIT "${GIT_TAG}")
        
        # Vérifier si on est exactement sur un tag ou après
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match HEAD
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_EXACT_TAG
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        
        # Vérifier si le working directory est "dirty" (modifications non commitées)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} diff --quiet HEAD
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            RESULT_VARIABLE GIT_DIRTY_RESULT
            ERROR_QUIET
        )
        
        # Vérifier si on est exactement sur un tag ET que le working directory est propre
        if(NOT GIT_EXACT_TAG OR GIT_DIRTY_RESULT)
            # On n'est pas exactement sur un tag, ou il y a des modifications non commitées
            # On garde la version de base pour project() mais on stocke le suffixe
            set(VERSION_SUFFIX "-dev")
            if(GIT_DIRTY_RESULT)
                message(STATUS "Version détectée depuis Git: ${VERSION_FROM_GIT}${VERSION_SUFFIX} (working directory dirty)")
            else()
                message(STATUS "Version détectée depuis Git: ${VERSION_FROM_GIT}${VERSION_SUFFIX} (après tag ${GIT_TAG})")
            endif()
        else()
            set(VERSION_SUFFIX "")
            message(STATUS "Version détectée depuis Git tag: ${VERSION_FROM_GIT} (tag: ${GIT_TAG})")
        endif()
    else()
        set(VERSION_FROM_GIT "0.0.0")
        set(VERSION_SUFFIX "-dev")
        message(STATUS "Aucun tag Git trouvé, utilisation de la version par défaut: ${VERSION_FROM_GIT}${VERSION_SUFFIX}")
    endif()
else()
    set(VERSION_FROM_GIT "0.0.0")
    set(VERSION_SUFFIX "-dev")
    message(STATUS "Git non trouvé, utilisation de la version par défaut: ${VERSION_FROM_GIT}${VERSION_SUFFIX}")
endif()

# Utiliser la version détectée (sans suffixe pour project(), CMake n'accepte que X.Y.Z)
project(imSidPlayer VERSION ${VERSION_FROM_GIT} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Générer compile_commands.json pour le LSP
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options de build
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# Dossiers
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Générer Version.h depuis le template
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/Version.h.in"
    "${CMAKE_BINARY_DIR}/include/Version.h"
    @ONLY
)

# SDL2 pour l'audio (doit être trouvé AVANT ImGui)
# Sur Windows avec MinGW, utiliser pkg-config comme sur Linux
# Sur Windows avec MSVC, utiliser la détection manuelle
find_package(PkgConfig QUIET)
if(WIN32 AND NOT PkgConfig_FOUND)
    # Windows avec MSVC: détection manuelle
    if(DEFINED ENV{CMAKE_PREFIX_PATH})
        string(REPLACE " " ";" CMAKE_PREFIX_PATH_LIST "$ENV{CMAKE_PREFIX_PATH}")
        list(GET CMAKE_PREFIX_PATH_LIST 0 SDL2_ROOT)
        message(STATUS "CMAKE_PREFIX_PATH from environment: $ENV{CMAKE_PREFIX_PATH}")
        message(STATUS "Using SDL2_ROOT: ${SDL2_ROOT}")
    elseif(DEFINED ENV{SDL2_DIR})
        string(REPLACE " " ";" SDL2_DIR_LIST "$ENV{SDL2_DIR}")
        list(GET SDL2_DIR_LIST 0 SDL2_DIR_FIRST)
        get_filename_component(SDL2_ROOT "${SDL2_DIR_FIRST}" DIRECTORY)
        message(STATUS "SDL2_DIR from environment: $ENV{SDL2_DIR}, using root: ${SDL2_ROOT}")
    else()
        set(SDL2_ROOT "")
        message(WARNING "Neither CMAKE_PREFIX_PATH nor SDL2_DIR set, SDL2 detection may fail")
    endif()
    
    message(STATUS "Searching SDL2 manually in: ${SDL2_ROOT}")
    
    find_path(SDL2_INCLUDE_DIR SDL.h
        PATHS
        ${SDL2_ROOT}
        PATH_SUFFIXES include include/SDL2
    )
    
    find_library(SDL2_LIBRARY
        NAMES SDL2
        PATHS
        ${SDL2_ROOT}
        PATH_SUFFIXES lib lib/x64
    )
    
    if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
        set(SDL2_FOUND TRUE)
        set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})
        set(SDL2_LIBRARIES ${SDL2_LIBRARY})
        set(SDL2_LIBRARY_DIRS "")
        set(SDL2_CFLAGS_OTHER "")
        message(STATUS "SDL2 found manually:")
        message(STATUS "  Include: ${SDL2_INCLUDE_DIRS}")
        message(STATUS "  Library: ${SDL2_LIBRARIES}")
    else()
        message(FATAL_ERROR "SDL2 not found. SDL2_ROOT: ${SDL2_ROOT}, Include: ${SDL2_INCLUDE_DIR}, Library: ${SDL2_LIBRARY}")
    endif()
else()
    # Linux/macOS ou Windows avec MinGW: utiliser pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
endif()

# ImGui (architecture propre : toujours créer une lib statique)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
    message(FATAL_ERROR "ImGui submodule missing. Run: git submodule update --init --recursive")
endif()

message(STATUS "ImGui found: creating static library")

# Créer une lib imgui statique avec tous les fichiers nécessaires
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)

# Include directories pour ImGui (core + backends + SDL2)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${SDL2_INCLUDE_DIRS}
)

# Linker SDL2 pour ImGui
target_link_libraries(imgui PUBLIC
    ${SDL2_LIBRARIES}
)

# SDL2_image pour charger des images (PNG, JPEG, etc.)
if(WIN32 AND NOT PkgConfig_FOUND)
    # Windows avec MSVC: détection manuelle
    find_package(SDL2_image QUIET)
    if(SDL2_image_FOUND)
        set(SDL2_IMAGE_FOUND TRUE)
        set(SDL2_IMAGE_LIBRARIES ${SDL2_image_LIBRARIES})
        set(SDL2_IMAGE_INCLUDE_DIRS ${SDL2_image_INCLUDE_DIRS})
        add_definitions(-DHAS_SDL2_IMAGE=1)
        message(STATUS "SDL2_image trouvé: support des images de fond activé")
    else()
        set(SDL2_IMAGE_FOUND FALSE)
        add_definitions(-DHAS_SDL2_IMAGE=0)
        message(WARNING "SDL2_image not found on Windows. Background image support will be disabled.")
    endif()
else()
    # Linux/macOS ou Windows avec MinGW: utiliser pkg-config
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2_IMAGE QUIET SDL2_image)
        if(SDL2_IMAGE_FOUND)
            add_definitions(-DHAS_SDL2_IMAGE=1)
            message(STATUS "SDL2_image trouvé: support des images de fond activé")
        else()
            set(SDL2_IMAGE_FOUND FALSE)
            add_definitions(-DHAS_SDL2_IMAGE=0)
            message(WARNING "SDL2_image not found. Background image support will be disabled.")
        endif()
    else()
        # Fallback: détection manuelle
        find_library(SDL2_IMAGE_LIB SDL2_image PATHS /usr/lib/x86_64-linux-gnu)
        find_path(SDL2_IMAGE_INCLUDE_DIR SDL2/SDL_image.h PATHS /usr/include)
        if(SDL2_IMAGE_LIB AND SDL2_IMAGE_INCLUDE_DIR)
            set(SDL2_IMAGE_FOUND TRUE)
            set(SDL2_IMAGE_LIBRARIES ${SDL2_IMAGE_LIB})
            set(SDL2_IMAGE_INCLUDE_DIRS ${SDL2_IMAGE_INCLUDE_DIR})
            add_definitions(-DHAS_SDL2_IMAGE=1)
            message(STATUS "SDL2_image trouvé: support des images de fond activé")
        else()
            set(SDL2_IMAGE_FOUND FALSE)
            add_definitions(-DHAS_SDL2_IMAGE=0)
            message(WARNING "SDL2_image not found. Background image support will be disabled. Install with: sudo apt-get install libsdl2-image-dev")
        endif()
    endif()
endif()

# sidplayfp
if(WIN32 AND NOT PkgConfig_FOUND)
    # Windows avec MSVC: détection manuelle
    if(DEFINED ENV{SIDPLAYFP_ROOT})
        string(REPLACE " " ";" SIDPLAYFP_ROOT_LIST "$ENV{SIDPLAYFP_ROOT}")
        list(GET SIDPLAYFP_ROOT_LIST 0 SIDPLAYFP_ROOT)
        message(STATUS "SIDPLAYFP_ROOT from environment: $ENV{SIDPLAYFP_ROOT}")
        message(STATUS "Using SIDPLAYFP_ROOT: ${SIDPLAYFP_ROOT}")
    else()
        set(SIDPLAYFP_ROOT "")
        message(WARNING "SIDPLAYFP_ROOT not set, sidplayfp detection may fail")
    endif()
    
    message(STATUS "Searching sidplayfp manually in: ${SIDPLAYFP_ROOT}")
    
    find_library(SIDPLAYFP_LIB
        NAMES sidplayfp libsidplayfp
        PATHS
        ${SIDPLAYFP_ROOT}
        PATH_SUFFIXES lib lib/x64 lib/x86_64-w64-mingw32/lib
    )
    
    find_path(SIDPLAYFP_INCLUDE_DIR sidplayfp/sidplayfp.h
        PATHS
        ${SIDPLAYFP_ROOT}
        PATH_SUFFIXES include include/x86_64-w64-mingw32
    )
    
    if(SIDPLAYFP_INCLUDE_DIR AND SIDPLAYFP_LIB)
        message(STATUS "sidplayfp found manually:")
        message(STATUS "  Include: ${SIDPLAYFP_INCLUDE_DIR}")
        message(STATUS "  Library: ${SIDPLAYFP_LIB}")
    else()
        message(FATAL_ERROR "sidplayfp not found. SIDPLAYFP_ROOT: ${SIDPLAYFP_ROOT}, Include: ${SIDPLAYFP_INCLUDE_DIR}, Library: ${SIDPLAYFP_LIB}")
    endif()
else()
    # Linux/macOS ou Windows avec MinGW: utiliser pkg-config ou find_library
    if(PkgConfig_FOUND)
        pkg_check_modules(SIDPLAYFP QUIET libsidplayfp)
        if(SIDPLAYFP_FOUND)
            set(SIDPLAYFP_INCLUDE_DIR ${SIDPLAYFP_INCLUDE_DIRS})
            # Convertir SIDPLAYFP_LIBRARIES en SIDPLAYFP_LIB pour compatibilité
            if(SIDPLAYFP_LIBRARIES)
                list(GET SIDPLAYFP_LIBRARIES 0 SIDPLAYFP_LIB)
            else()
                set(SIDPLAYFP_LIB ${SIDPLAYFP_LIBRARIES})
            endif()
            message(STATUS "sidplayfp found via pkg-config:")
            message(STATUS "  Include: ${SIDPLAYFP_INCLUDE_DIR}")
            message(STATUS "  Library: ${SIDPLAYFP_LIB}")
        else()
            # Fallback to find_library
            find_library(SIDPLAYFP_LIB sidplayfp)
            find_path(SIDPLAYFP_INCLUDE_DIR sidplayfp/sidplayfp.h)
            if(SIDPLAYFP_INCLUDE_DIR AND SIDPLAYFP_LIB)
                message(STATUS "sidplayfp found via find_library:")
                message(STATUS "  Include: ${SIDPLAYFP_INCLUDE_DIR}")
                message(STATUS "  Library: ${SIDPLAYFP_LIB}")
            else()
                message(FATAL_ERROR "sidplayfp not found. Install it with: sudo apt-get install libsidplayfp-dev")
            endif()
        endif()
    else()
        # Fallback: find_library standard
        find_library(SIDPLAYFP_LIB sidplayfp)
        if(NOT SIDPLAYFP_LIB)
            message(FATAL_ERROR "sidplayfp library not found. Install it with: sudo apt-get install libsidplayfp-dev")
        endif()

        find_path(SIDPLAYFP_INCLUDE_DIR sidplayfp/sidplayfp.h)
        if(NOT SIDPLAYFP_INCLUDE_DIR)
            message(FATAL_ERROR "sidplayfp headers not found. Install it with: sudo apt-get install libsidplayfp-dev")
        endif()
        message(STATUS "sidplayfp found:")
        message(STATUS "  Include: ${SIDPLAYFP_INCLUDE_DIR}")
        message(STATUS "  Library: ${SIDPLAYFP_LIB}")
    endif()
endif()

# Glaze (header-only JSON library)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glaze/CMakeLists.txt")
    add_subdirectory(third_party/glaze)
    # Glaze crée une cible glaze::glaze
else()
    message(WARNING "Glaze not found. Run: git clone https://github.com/stephenberry/glaze.git third_party/glaze")
endif()

# Quill - Bibliothèque de logging asynchrone
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/quill/CMakeLists.txt")
    add_subdirectory(third_party/quill)
    # Quill crée une cible quill::quill
else()
    message(WARNING "Quill not found. Run: git submodule add https://github.com/odygrd/quill.git third_party/quill")
endif()


# MBed TLS - Pour le cloud save (HTTPS)
option(ENABLE_CLOUD_SAVE "Enable cloud save functionality" ON)
option(ENABLE_UI_LOGS "Enable UI Manager logs (very verbose)" OFF)

if(ENABLE_CLOUD_SAVE)
    set(MBEDTLS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mbedtls)
    
    if(EXISTS "${MBEDTLS_DIR}/CMakeLists.txt")
        # Options de compilation MBed TLS (avant add_subdirectory)
        set(ENABLE_PROGRAMS OFF CACHE BOOL "Build programs" FORCE)
        set(ENABLE_TESTING OFF CACHE BOOL "Build tests" FORCE)
        set(INSTALL_MBEDTLS_HEADERS OFF CACHE BOOL "Install MBed TLS headers" FORCE)
        # Activer la génération de fichiers (nécessaire pour error.c, ssl_debug_helpers_generated.c, etc.)
        set(GEN_FILES ON CACHE BOOL "Generate auto-generated files" FORCE)
        
        # Désactiver les avertissements fatals pour éviter les erreurs de format printf sur MinGW/Windows
        set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "Treat warnings as errors" FORCE)
        
        # Les modules nécessaires pour npoint.io sont activés par défaut dans MBed TLS
        # On n'a pas besoin de les redéfinir, ils sont déjà dans mbedtls_config.h
        # Si besoin de désactiver RSA/DHM, on peut le faire via un fichier config personnalisé
        
        # Désactiver les warnings d'attributs non supportés par Clang (gnu_printf)
        # et les erreurs de format dans MBed TLS (pour MinGW)
        if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-ignored-attributes -Wno-format -Wno-error=format" CACHE STRING "" FORCE)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-ignored-attributes -Wno-format -Wno-error=format" CACHE STRING "" FORCE)
        endif()
        
        add_subdirectory(${MBEDTLS_DIR})
        
        # Ajouter les flags après add_subdirectory pour les cibles MBed TLS
        if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND TARGET mbedtls)
            target_compile_options(mbedtls PRIVATE -Wno-format -Wno-error=format)
            target_compile_options(mbedx509 PRIVATE -Wno-format -Wno-error=format)
            if(TARGET tfpsacrypto)
                target_compile_options(tfpsacrypto PRIVATE -Wno-format -Wno-error=format)
            endif()
        endif()
               
               message(STATUS "Cloud save enabled - MBed TLS will be compiled from sources")
    else()
        message(WARNING "MBed TLS not found at ${MBEDTLS_DIR}. Cloud save disabled.")
        message(STATUS "Please run: git submodule add https://github.com/Mbed-TLS/mbedtls.git third_party/mbedtls")
        set(ENABLE_CLOUD_SAVE OFF)
    endif()
else()
    message(STATUS "Cloud save disabled (use -DENABLE_CLOUD_SAVE=ON to enable)")
endif()


# Support OpenMP (requis par libsidplayfp)
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found: linking with OpenMP support")
endif()

# Sources (ImGui est maintenant une lib séparée, plus besoin de l'inclure ici)
set(SOURCES
    src/main.cpp
    src/SidPlayer.cpp
    src/Config.cpp
    src/Utils.cpp
    src/BackgroundManager.cpp
    src/PlaylistManager.cpp
    src/FileBrowser.cpp
    src/UIManager.cpp
    src/Application.cpp
    src/SidMetadata.cpp
    src/DatabaseManager.cpp
    src/FilterWidget.cpp
    src/HistoryManager.cpp
    src/RatingManager.cpp
    src/Logger.cpp
    src/MD5.cpp
    src/SongLengthDB.cpp
)

if(ENABLE_CLOUD_SAVE)
    list(APPEND SOURCES
        src/HTTPClient.cpp
        src/CloudSyncManager.cpp
        src/UpdateChecker.cpp
        src/UpdateInstaller.cpp
    )
endif()

set(HEADERS
    include/SidPlayer.h
    include/Config.h
    include/Utils.h
    include/BackgroundManager.h
    include/PlaylistManager.h
    include/FileBrowser.h
    include/UIManager.h
    include/Application.h
    include/SidMetadata.h
    include/DatabaseManager.h
    include/FilterWidget.h
    include/HistoryManager.h
    include/RatingManager.h
    include/Logger.h
    include/MD5.h
    include/SongLengthDB.h
)

if(ENABLE_CLOUD_SAVE)
    list(APPEND HEADERS
        include/HTTPClient.h
        include/CloudSyncManager.h
        include/UpdateChecker.h
        include/UpdateInstaller.h
    )
endif()

# Exécutable principal
# Sur Windows, utiliser WIN32 pour créer une application GUI sans console
if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})
endif()

# Exécutable de test MD5
add_executable(md5_test 
    tests/md5_test.cpp
    src/MD5.cpp
    src/Logger.cpp
    src/Utils.cpp
)
target_link_libraries(md5_test PRIVATE quill::quill)
target_include_directories(md5_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Exécutable de test HTTP (MBed TLS) - seulement si cloud save est activé
if(ENABLE_CLOUD_SAVE)
    add_executable(http_test
        tests/http_test.cpp
        src/HTTPClient.cpp
        src/Logger.cpp
        src/Utils.cpp
        src/MD5.cpp
    )
    target_link_libraries(http_test PRIVATE 
        quill::quill
        mbedtls
        mbedx509
    )
    target_include_directories(http_test PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${MBEDTLS_DIR}/include
    )
    target_compile_definitions(http_test PRIVATE ENABLE_CLOUD_SAVE=1)
    message(STATUS "HTTP test executable enabled: http_test")
endif()

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_BINARY_DIR}/include  # Pour Version.h généré
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
    ${SIDPLAYFP_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/quill/include
)

# MBed TLS include directories
if(ENABLE_CLOUD_SAVE AND TARGET mbedtls)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${MBEDTLS_DIR}/include
    )
endif()
if(SDL2_IMAGE_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${SDL2_IMAGE_INCLUDE_DIRS})
endif()
# ImGui include directories sont héritées via target_link_libraries(imgui)

# Libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    imgui
    ${SDL2_LIBRARIES}
    ${SIDPLAYFP_LIB}
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenMP::OpenMP_CXX)
endif()

# Glaze (header-only, link interface)
if(TARGET glaze::glaze)
    target_link_libraries(${PROJECT_NAME} PRIVATE glaze::glaze)
endif()

# Quill (logging library)
if(TARGET quill::quill)
    target_link_libraries(${PROJECT_NAME} PRIVATE quill::quill)
endif()

# MBed TLS (cloud save)
if(ENABLE_CLOUD_SAVE AND TARGET mbedtls)
    # Dans MBed TLS 4.0, mbedtls inclut déjà les fonctionnalités cryptographiques
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        mbedtls
        mbedx509
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_CLOUD_SAVE=1)
endif()

if(SDL2_IMAGE_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL2_IMAGE_LIBRARIES})
endif()

# Flags de compilation
target_compile_options(${PROJECT_NAME} PRIVATE
    ${SDL2_CFLAGS_OTHER}
)

# Flags de debug (symboles de debug pour gdb)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR NOT CMAKE_BUILD_TYPE)
    target_compile_options(${PROJECT_NAME} PRIVATE -g -O0)
    # Forcer les logs DEBUG en mode Debug
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_DEBUG_LOGS=1)
    message(STATUS "Build type: Debug - symbols enabled, DEBUG logs enabled")
else()
    # En mode Release, définir NDEBUG pour désactiver les logs DEBUG
    target_compile_definitions(${PROJECT_NAME} PRIVATE NDEBUG=1)
    message(STATUS "Build type: ${CMAKE_BUILD_TYPE} - DEBUG logs disabled (NDEBUG defined)")
endif()

# Logs UI (optionnel, désactivé par défaut car très verbeux)
if(ENABLE_UI_LOGS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_UI_LOGS=1)
    message(STATUS "UI Manager logs enabled (very verbose)")
else()
    message(STATUS "UI Manager logs disabled (use -DENABLE_UI_LOGS=ON to enable)")
endif()

# Flags de linkage
target_link_directories(${PROJECT_NAME} PRIVATE
    ${SDL2_LIBRARY_DIRS}
)

# Sur Windows MinGW, linkage par défaut (standard)
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(MINGW)
        # Forcer la compilation en 64 bits explicitement
        target_compile_options(${PROJECT_NAME} PRIVATE -m64)
        target_link_options(${PROJECT_NAME} PRIVATE -m64)
        # target_link_options(${PROJECT_NAME} PRIVATE -static-libgcc -static-libstdc++ -static)
        # S'assurer que les dépendances réseau sont bien linkées
        target_link_libraries(${PROJECT_NAME} PRIVATE iphlpapi ws2_32 crypt32)
    endif()
endif()

# --- Installation et Bundling ---

if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Dossier de destination pour le bundle (relatif à CMAKE_INSTALL_PREFIX)
    set(BUNDLE_DEST ".")

    # 1. Exécutable
    install(TARGETS ${PROJECT_NAME} DESTINATION ${BUNDLE_DEST})

    # 2. Polices
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/fonts")
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/fonts" DESTINATION ${BUNDLE_DEST})
    endif()

    # 3. Certificats (pour le Cloud Save)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/certs")
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/certs" DESTINATION ${BUNDLE_DEST})
    endif()

    # 4. DLLs de runtime (Nécessaires même avec -static pour certaines libs comme OpenMP)
    if(CMAKE_CROSSCOMPILING OR MINGW)
        # Liste des DLLs de runtime essentielles
        set(WIN_RUNTIME_DLLS
            "libgomp-1.dll"
            "libgcc_s_seh-1.dll"
            "libwinpthread-1.dll"
            "libstdc++-6.dll"
        )

        # Chemins de recherche pour les DLLs MinGW (Ubuntu 24.04 / 22.04)
        set(MINGW_SEARCH_PATHS
            "/usr/lib/gcc/x86_64-w64-mingw32/13-posix"
            "/usr/lib/gcc/x86_64-w64-mingw32/10-posix"
            "/usr/x86_64-w64-mingw32/bin"
            "/usr/x86_64-w64-mingw32/lib"
        )
        
        foreach(dll_name ${WIN_RUNTIME_DLLS})
            set(DLL_FOUND_PATH "DLL_FOUND_PATH-NOTFOUND")
            find_file(DLL_FOUND_PATH_${dll_name} NAMES ${dll_name} PATHS ${MINGW_SEARCH_PATHS} NO_DEFAULT_PATH)
            if(DLL_FOUND_PATH_${dll_name})
                install(FILES "${DLL_FOUND_PATH_${dll_name}}" DESTINATION ${BUNDLE_DEST})
                message(STATUS "Bundle: ${dll_name} inclus depuis ${DLL_FOUND_PATH_${dll_name}}")
            endif()
        endforeach()
    endif()
endif()