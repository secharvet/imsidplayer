cmake_minimum_required(VERSION 3.15)
project(imSidPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Générer compile_commands.json pour le LSP
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options de build
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# Dossiers
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

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
    src/Logger.cpp
    src/MD5.cpp
    src/SongLengthDB.cpp
)

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
    include/Logger.h
    include/MD5.h
    include/SongLengthDB.h
)

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

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
    ${SIDPLAYFP_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/quill/include
)
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

# Glaze (header-only, link interface)
if(TARGET glaze::glaze)
    target_link_libraries(${PROJECT_NAME} PRIVATE glaze::glaze)
endif()

# Quill (logging library)
if(TARGET quill::quill)
    target_link_libraries(${PROJECT_NAME} PRIVATE quill::quill)
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
    message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
endif()

# Flags de linkage
target_link_directories(${PROJECT_NAME} PRIVATE
    ${SDL2_LIBRARY_DIRS}
)

# Copier les fichiers FontAwesome (police et header) dans le répertoire de build
# La police doit être accessible depuis l'exécutable (dans bin/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/fonts DESTINATION ${CMAKE_BINARY_DIR}/bin)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/fonts DESTINATION ${CMAKE_BINARY_DIR})

