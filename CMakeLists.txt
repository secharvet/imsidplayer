cmake_minimum_required(VERSION 3.15)
project(imSidPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Générer compile_commands.json pour le LSP
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options de build
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# Dossiers
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# SDL2 pour l'audio (doit être trouvé AVANT ImGui)
# Sur Windows, utiliser find_package(SDL2), sur Linux/macOS utiliser pkg-config
if(WIN32)
    # Sur Windows, utiliser uniquement la détection manuelle (find_package ne fonctionne pas toujours)
    # SDL2_DIR pointe vers cmake/, CMAKE_PREFIX_PATH pointe vers la racine de SDL2
    if(DEFINED ENV{CMAKE_PREFIX_PATH})
        # Prendre seulement le premier chemin si plusieurs sont présents (séparés par des espaces)
        string(REPLACE " " ";" CMAKE_PREFIX_PATH_LIST "$ENV{CMAKE_PREFIX_PATH}")
        list(GET CMAKE_PREFIX_PATH_LIST 0 SDL2_ROOT)
        message(STATUS "CMAKE_PREFIX_PATH from environment: $ENV{CMAKE_PREFIX_PATH}")
        message(STATUS "Using SDL2_ROOT: ${SDL2_ROOT}")
    elseif(DEFINED ENV{SDL2_DIR})
        # Si SDL2_DIR pointe vers cmake/, remonter d'un niveau pour obtenir la racine
        # Prendre seulement le premier chemin
        string(REPLACE " " ";" SDL2_DIR_LIST "$ENV{SDL2_DIR}")
        list(GET SDL2_DIR_LIST 0 SDL2_DIR_FIRST)
        get_filename_component(SDL2_ROOT "${SDL2_DIR_FIRST}" DIRECTORY)
        message(STATUS "SDL2_DIR from environment: $ENV{SDL2_DIR}, using root: ${SDL2_ROOT}")
    else()
        set(SDL2_ROOT "")
        message(WARNING "Neither CMAKE_PREFIX_PATH nor SDL2_DIR set, SDL2 detection may fail")
    endif()
    
    # Détection manuelle de SDL2 sur Windows (pas de find_package pour éviter les erreurs)
    message(STATUS "Searching SDL2 manually in: ${SDL2_ROOT}")
    
    # Sur Windows, SDL2 est généralement dans: SDL2-2.x.x/include et SDL2-2.x.x/lib/x64
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
if(WIN32)
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

# sidplayfp
if(WIN32)
    # Sur Windows, utiliser la détection manuelle
    if(DEFINED ENV{SIDPLAYFP_ROOT})
        # Prendre seulement le premier chemin si plusieurs sont présents
        string(REPLACE " " ";" SIDPLAYFP_ROOT_LIST "$ENV{SIDPLAYFP_ROOT}")
        list(GET SIDPLAYFP_ROOT_LIST 0 SIDPLAYFP_ROOT)
        message(STATUS "SIDPLAYFP_ROOT from environment: $ENV{SIDPLAYFP_ROOT}")
        message(STATUS "Using SIDPLAYFP_ROOT: ${SIDPLAYFP_ROOT}")
    else()
        set(SIDPLAYFP_ROOT "")
        message(WARNING "SIDPLAYFP_ROOT not set, sidplayfp detection may fail")
    endif()
    
    # Détection manuelle de sidplayfp sur Windows
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
    # Linux/macOS: utiliser find_library standard
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


# Sources (ImGui est maintenant une lib séparée, plus besoin de l'inclure ici)
set(SOURCES
    src/main.cpp
    src/SidPlayer.cpp
    src/Config.cpp
)

set(HEADERS
    include/SidPlayer.h
    include/Config.h
)

# Exécutable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
    ${SIDPLAYFP_INCLUDE_DIR}
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
    message(STATUS "Build type: Debug - symbols enabled")
else()
    message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
endif()

# Flags de linkage
target_link_directories(${PROJECT_NAME} PRIVATE
    ${SDL2_LIBRARY_DIRS}
)

