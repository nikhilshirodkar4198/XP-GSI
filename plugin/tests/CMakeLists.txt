# Unit tests — pure-logic only, no XPLM link required.

# ── Phase 1 test executable ───────────────────────────────────────────────────
add_executable(xpgsi_tests
    AptDatTest.cpp
    CslScannerTest.cpp
    ModelSelectorTest.cpp
    ConflictEngineTest.cpp
    SettingsTest.cpp
    ObjGeometryTest.cpp
    AirportDataTest.cpp
    PlannerTest.cpp
    UiLogicTest.cpp
    StandLayoutTest.cpp
    ObjLightPatchTest.cpp
    GateOccupancyTest.cpp
    FollowUserTest.cpp
    ReservationsTest.cpp
    GateSuggestTest.cpp
    WikiFallbackTest.cpp
    TerminalZonesTest.cpp
    LiveryRulesTest.cpp
    LiveTestFixesTest.cpp

    # Sources under test (no XPLM calls in these files)
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/scenery/AptDat.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/scenery/Fingerprint.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/scenery/SceneryPacks.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/core/Log.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/csl/CslScanner.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/csl/ModelSelector.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/csl/ObjGeometry.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/csl/ObjLightPatch.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/data/AirportData.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/data/WikiFallback.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/data/RegionalBrands.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/data/StandOverrides.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/csl/RelatedTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine/TerminalZones.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine/ConflictEngine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine/Planner.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/core/Settings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/data/AirportTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine/AutoStart.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine/Density.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine/StandLayout.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine/StandEvidence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine/GateOccupancy.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine/Reservations.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine/GateSuggest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/core/CduAdapters.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/UiText.cpp"
)

target_include_directories(xpgsi_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

# Inject the fixtures directory as an absolute path so tests don't depend on CWD.
target_compile_definitions(xpgsi_tests PRIVATE
    "FIXTURES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures\""
)

target_link_libraries(xpgsi_tests PRIVATE
    nlohmann_json::nlohmann_json
    doctest::doctest
    Threads::Threads
)

# doctest_SOURCE_DIR is set by the parent CMakeLists.txt.
include("${doctest_SOURCE_DIR}/cmake/doctest.cmake")
doctest_discover_tests(xpgsi_tests)

# Local diagnostics against a real X-Plane install (Zz*.cpp, skipped unless their
# environment variables are set); optional, not part of the public source.
file(GLOB XPGSI_DIAG_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/Zz*.cpp")
target_sources(xpgsi_tests PRIVATE ${XPGSI_DIAG_TESTS})
