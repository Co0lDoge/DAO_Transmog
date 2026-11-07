cmake_minimum_required(VERSION 3.21)

project(DAOTransmog VERSION 1.0.0 LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(
        FATAL_ERROR
        "DAOTransmog must be built for 32-bit x86 (the Dragon Age: Origins architecture). "
        "Configure with a preset from CMakePresets.json (e.g. 'release' or 'debug')."
    )
endif()

# ---------------------------------------------------------------------------
# Dependencies & DAOLib Setup
# ---------------------------------------------------------------------------
if(DEFINED ENV{DAOLIB_DIR} AND NOT DEFINED DAOLIB_DIR)
    set(DAOLIB_DIR "$ENV{DAOLIB_DIR}" CACHE PATH "Path to DAOLib source tree")
elseif(NOT DEFINED DAOLIB_DIR)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/DAOLib/CMakeLists.txt")
        set(DAOLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/DAOLib" CACHE PATH "Path to DAOLib source tree")
    else()
        set(DAOLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../DAOLib-master" CACHE PATH "Path to DAOLib source tree")
    endif()
endif()
if(NOT EXISTS "${DAOLIB_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "DAOLib not found at '${DAOLIB_DIR}'. Please point DAOLIB_DIR to DAOLib.")
endif()

# Build DAOLib without its own probe plugins/examples
set(DAO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DAO_BUILD_PLUGINS OFF CACHE BOOL "" FORCE)
set(DAO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${DAOLIB_DIR}" "${CMAKE_BINARY_DIR}/daolib" EXCLUDE_FROM_ALL)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Plugin Target: DAOTransmog
# ---------------------------------------------------------------------------
set(PLUGIN_SOURCES
    src/Main.cpp
    src/Strings.h
    src/Strings.cpp
    src/SafeMode.h
    src/SafeMode.cpp
    src/Config.h
    src/Config.cpp
    src/Catalog.h
    src/Catalog.cpp
    src/State.h
    src/State.cpp
    src/ContextRules.h
    src/ContextRules.cpp
    src/AppearanceHook.h
    src/AppearanceHook.cpp
    src/WeaponHook.h
    src/WeaponHook.cpp
    src/PortraitHook.h
    src/PortraitHook.cpp
    src/GuiBridge.h
    src/GuiBridge.cpp
    src/TestBridge.h
    src/TestBridge.cpp
)

set(PLUGIN_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/tlk/DAOTransmog_en-us.tlk"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/tlk/DAOTransmog_ru-ru.tlk"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/daotransmog_ui.swf"
)

add_daolib_plugin(
    DAOTransmog
    SOURCES ${PLUGIN_SOURCES}
    FILES ${PLUGIN_FILES}
)

target_include_directories(DAOTransmog PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(DAOTransmog PRIVATE nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(DAOTransmog PRIVATE /W4 /utf-8 /permissive-)
endif()
target_compile_features(DAOTransmog PRIVATE cxx_std_20)

# ---------------------------------------------------------------------------
# Unit Tests Target: transmog_tests
# ---------------------------------------------------------------------------
enable_testing()

add_executable(
    transmog_tests
    tests/TestCatalog.cpp
    tests/TestStateAndCoSave.cpp
    tests/TestContextRules.cpp
    src/Catalog.h
    src/Catalog.cpp
    src/State.h
    src/State.cpp
    src/Strings.h
    src/Strings.cpp
    src/Config.h
    src/Config.cpp
    src/SafeMode.h
    src/SafeMode.cpp
    src/ContextRules.h
    src/ContextRules.cpp
)

target_include_directories(transmog_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(
    transmog_tests
    PRIVATE
        DAOLib::daolib
        Catch2::Catch2WithMain
        nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_options(transmog_tests PRIVATE /W4 /utf-8 /permissive-)
endif()
target_compile_features(transmog_tests PRIVATE cxx_std_20)

add_test(NAME TransmogUnitTests COMMAND transmog_tests)

# ---------------------------------------------------------------------------
# Automatic Post-Build Deployment to DAO_GAME_DIR
# ---------------------------------------------------------------------------
set(DEFAULT_GAME_DIR "$ENV{DAO_GAME_DIR}")
if(NOT DEFAULT_GAME_DIR AND EXISTS "D:/Entertainment/BlackFlag/Dragon Age Origins/bin_ship/DAOrigins.exe")
    set(DEFAULT_GAME_DIR "D:/Entertainment/BlackFlag/Dragon Age Origins")
endif()

set(DAO_GAME_DIR "${DEFAULT_GAME_DIR}" CACHE PATH "Path to Dragon Age: Origins installation or bin_ship")

if(DAO_GAME_DIR)
    file(TO_CMAKE_PATH "${DAO_GAME_DIR}" GAME_DIR_NORMALIZED)
    if(EXISTS "${GAME_DIR_NORMALIZED}/DAOrigins.exe")
        set(BIN_SHIP_DIR "${GAME_DIR_NORMALIZED}")
    elseif(EXISTS "${GAME_DIR_NORMALIZED}/bin_ship/DAOrigins.exe")
        set(BIN_SHIP_DIR "${GAME_DIR_NORMALIZED}/bin_ship")
    else()
        set(BIN_SHIP_DIR "${GAME_DIR_NORMALIZED}/bin_ship")
    endif()

    set(DEPLOY_PLUGIN_DIR "${BIN_SHIP_DIR}/daolib/plugins/DAOTransmog")
    message(STATUS "Configured auto-deployment to: ${DEPLOY_PLUGIN_DIR}")

    add_custom_command(
        TARGET DAOTransmog POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_PLUGIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:DAOTransmog>" "${DEPLOY_PLUGIN_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_PDB_FILE:DAOTransmog>" "${DEPLOY_PLUGIN_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/tlk/DAOTransmog_en-us.tlk" "${DEPLOY_PLUGIN_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/tlk/DAOTransmog_ru-ru.tlk" "${DEPLOY_PLUGIN_DIR}/"
        COMMENT "Deploying DAOTransmog to ${DEPLOY_PLUGIN_DIR}..."
    )
endif()
