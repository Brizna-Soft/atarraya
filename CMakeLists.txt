cmake_minimum_required(VERSION 3.25)

# --- vcpkg: triplet propio (todo estático, solo Release) y puerto propio de libtorrent ---
set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_SOURCE_DIR}/triplets" CACHE STRING "")
set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vcpkg-overlays/ports" CACHE STRING "")
set(VCPKG_TARGET_TRIPLET "x64-windows-static-rel" CACHE STRING "")
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" _vcpkg)
    else()
        set(_vcpkg "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vcpkg")
    endif()
    set(CMAKE_TOOLCHAIN_FILE "${_vcpkg}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

# Torrents (libtorrent, con Boost y OpenSSL). Sin ellos compila mucho más rápido: sirve para probar lo demás.
option(CON_TORRENTS "Torrents y enlaces magnet (libtorrent)" ON)
if(CON_TORRENTS)
    set(VCPKG_MANIFEST_NO_DEFAULT_FEATURES OFF CACHE BOOL "" FORCE)
else()
    set(VCPKG_MANIFEST_NO_DEFAULT_FEATURES ON CACHE BOOL "" FORCE)
endif()

# CRT estático, igual que las bibliotecas de vcpkg. Solo Release: el triplet no compila versiones Debug.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "" FORCE)

file(STRINGS version.txt APP_VERSION LIMIT_COUNT 1)
project(Atarraya VERSION ${APP_VERSION} LANGUAGES C CXX RC)
if(NOT APP_VERSION_TEXTO)
    set(APP_VERSION_TEXTO "${APP_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(wxWidgets CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
if(CON_TORRENTS)
    find_package(LibtorrentRasterbar CONFIG REQUIRED)
endif()

configure_file(src/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generado/version.h" @ONLY)

# /external: sin avisos de los encabezados de las bibliotecas (los que se incluyen con <>)
set(OPCIONES_CXX /utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus /guard:cf /external:anglebrackets /external:W0)
# WIN32_LEAN_AND_MEAN: windows.h sin winsock.h (choca con el winsock2.h que usan wx y curl)
set(DEFINICIONES UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)

# --- El programa ---
file(GLOB FUENTES CONFIGURE_DEPENDS src/*.cpp src/*.hpp)
function(armar_programa objetivo)
    add_executable(${objetivo} WIN32 ${FUENTES} res/app.rc)
    target_include_directories(${objetivo} PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generado")
    target_compile_definitions(${objetivo} PRIVATE ${DEFINICIONES})
    target_compile_options(${objetivo} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${OPCIONES_CXX}>)
    target_link_options(${objetivo} PRIVATE /MANIFEST:NO /guard:cf)
    target_link_libraries(${objetivo} PRIVATE
        wx::core wx::base
        CURL::libcurl
        nlohmann_json::nlohmann_json
        bcrypt iphlpapi shlwapi winhttp ws2_32)
    if(CON_TORRENTS)
        target_compile_definitions(${objetivo} PRIVATE CON_TORRENTS=1)
        target_link_libraries(${objetivo} PRIVATE LibtorrentRasterbar::torrent-rasterbar crypt32 iphlpapi ws2_32 mswsock)
    endif()
endfunction()
armar_programa(Atarraya)

# --- El puente con los navegadores (native messaging) ---
function(armar_puente objetivo)
    add_executable(${objetivo} puente/puente.cpp res/puente.rc)
    target_include_directories(${objetivo} PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generado")
    target_compile_definitions(${objetivo} PRIVATE ${DEFINICIONES})
    target_compile_options(${objetivo} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${OPCIONES_CXX}>)
    target_link_options(${objetivo} PRIVATE /guard:cf)
    target_link_libraries(${objetivo} PRIVATE nlohmann_json::nlohmann_json)
endfunction()
armar_puente(atarraya-puente)

# --- Copia de prueba (no se publica): el mismo programa con ATARRAYA_PRUEBAS. Usa su propia carpeta de datos (nunca
#     la de verdad), su propio canal con su propio puente y solo anota las páginas que abriría en el navegador. Sirve
#     para probarlo en un escritorio aparte sin tocar la copia que la persona usa. ---
armar_programa(Atarraya-prueba)
target_compile_definitions(Atarraya-prueba PRIVATE ATARRAYA_PRUEBAS)
armar_puente(atarraya-puente-prueba)
target_compile_definitions(atarraya-puente-prueba PRIVATE ATARRAYA_PRUEBAS)

# --- Pruebas sin ventana (no se publican) ---
add_executable(prueba-motor pruebas/prueba-motor.cpp src/motor.cpp src/nombres.cpp src/util.cpp src/enlaces.cpp
    src/redlocal.cpp)
target_include_directories(prueba-motor PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generado")
target_compile_definitions(prueba-motor PRIVATE ${DEFINICIONES} SIN_WX)
target_compile_options(prueba-motor PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${OPCIONES_CXX}>)
target_link_libraries(prueba-motor PRIVATE CURL::libcurl nlohmann_json::nlohmann_json bcrypt iphlpapi shlwapi winhttp ws2_32)

add_executable(prueba-enlaces pruebas/prueba-enlaces.cpp src/enlaces.cpp src/motor.cpp src/nombres.cpp src/util.cpp
    src/clicknload.cpp src/recolector.cpp src/redlocal.cpp src/deunaenuna.cpp src/jicaro.cpp)
target_include_directories(prueba-enlaces PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generado")
# ATARRAYA_PRUEBAS: deja simular fallas de Windows y usa datos de prueba (solo en las pruebas; el programa no lo trae)
target_compile_definitions(prueba-enlaces PRIVATE ${DEFINICIONES} SIN_WX ATARRAYA_PRUEBAS)
target_compile_options(prueba-enlaces PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${OPCIONES_CXX}>)
target_link_libraries(prueba-enlaces PRIVATE CURL::libcurl nlohmann_json::nlohmann_json bcrypt iphlpapi shlwapi winhttp ws2_32)
