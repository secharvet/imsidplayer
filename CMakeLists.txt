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

# ImGui (intégration directe)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
set(IMGUI_FOUND FALSE)
set(IMGUI_HAS_CMAKE FALSE)
if(EXISTS "${IMGUI_DIR}/imgui.h" AND EXISTS "${IMGUI_DIR}/CMakeLists.txt")
    message(STATUS "ImGui found: adding include path and library targets")
    add_subdirectory(${IMGUI_DIR})
    set(IMGUI_FOUND TRUE)
    set(IMGUI_HAS_CMAKE TRUE)
elseif(EXISTS "${IMGUI_DIR}/imgui.h")
    message(STATUS "ImGui found: adding include path (no CMakeLists.txt, using direct include)")
    set(IMGUI_FOUND TRUE)
    set(IMGUI_HAS_CMAKE FALSE)
elseif(EXISTS "${IMGUI_DIR}")
    message(WARNING "third_party/imgui exists but imgui.h not found.")
    message(FATAL_ERROR "ImGui submodule is incomplete. Please run: git submodule update --init --recursive")
else()
    message(WARNING "third_party/imgui not found. If you want ImGui, run: git submodule update --init --recursive")
    message(FATAL_ERROR "ImGui submodule is required. Please run: git submodule update --init --recursive")
endif()

# SDL2 pour l'audio
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# SDL2_image pour charger des images (PNG, JPEG, etc.)
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

# sidplayfp
find_library(SIDPLAYFP_LIB sidplayfp)
if(NOT SIDPLAYFP_LIB)
    message(FATAL_ERROR "sidplayfp library not found. Install it with: sudo apt-get install libsidplayfp-dev")
endif()

find_path(SIDPLAYFP_INCLUDE_DIR sidplayfp/sidplayfp.h)
if(NOT SIDPLAYFP_INCLUDE_DIR)
    message(FATAL_ERROR "sidplayfp headers not found. Install it with: sudo apt-get install libsidplayfp-dev")
endif()


# Sources
set(SOURCES
    src/main.cpp
    src/SidPlayer.cpp
    src/Config.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)

# Ajouter les fichiers ImGui si pas de CMakeLists.txt (compilation directe)
if(IMGUI_FOUND AND NOT IMGUI_HAS_CMAKE)
    list(APPEND SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
    )
endif()

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

# Ajouter les include directories pour ImGui (toujours nécessaire)
if(IMGUI_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE "${IMGUI_DIR}")
endif()

# Libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${SDL2_LIBRARIES}
    ${SIDPLAYFP_LIB}
)

# Linker avec imgui seulement si le target existe (quand CMakeLists.txt est présent)
if(IMGUI_HAS_CMAKE AND TARGET imgui)
    target_link_libraries(${PROJECT_NAME} PRIVATE imgui)
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
    message(STATUS "Build type: Debug - symbols enabled")
else()
    message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
endif()

# Flags de linkage
target_link_directories(${PROJECT_NAME} PRIVATE
    ${SDL2_LIBRARY_DIRS}
)

