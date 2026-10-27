cmake_minimum_required(VERSION 3.24)
project(LabyTerm VERSION 1.1.0 LANGUAGES C CXX)  # keep packaging/arch/PKGBUILD and the Omarchy manifest equal: tools/check_version.sh

# Optimized by default: the CPU frame renderer and CRT filter are ~20x slower without -O2.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Release by default)" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# --- Dependencies (nlohmann/json is fetched at configure time; SDL3 comes from the system) -----
find_package(SDL3 REQUIRED CONFIG)
find_package(Threads REQUIRED)

# A system nlohmann-json is preferred (distro packages must not touch the network while building);
# otherwise it is fetched.
find_package(nlohmann_json 3.11 QUIET CONFIG)
if(NOT nlohmann_json_FOUND)
  # The release tarball, checked against its SHA-256 (nlohmann's recommended FetchContent source): a moved tag cannot change it.
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

# --- Game logic library (no SDL: usable from tests and tools) ----------------------
add_library(labycore STATIC
  src/core/DataFile.cpp
  src/core/Tuning.cpp
  src/game/Level.cpp
  src/game/Dungeon.cpp
  src/game/WallView.cpp
  src/game/Monster.cpp
  src/game/Item.cpp
  src/game/Hero.cpp
  src/game/Combat.cpp
  src/game/Scrolls.cpp
  src/game/Store.cpp
  src/game/Fountain.cpp
  src/game/Ghost.cpp
  src/game/Persist.cpp
  src/core/Settings.cpp
  src/game/Walker.cpp)
target_include_directories(labycore PUBLIC src)
target_link_libraries(labycore PUBLIC nlohmann_json::nlohmann_json)
target_compile_options(labycore PRIVATE -Wall -Wextra)
# Default data directory: the source tree's data/ (override with LABYTERM_DATA at runtime). A distro package sets
# LABYTERM_SOURCE_DATA_DIR to its install path so no build directory ends up in the binary.
set(LABYTERM_SOURCE_DATA_DIR "${CMAKE_SOURCE_DIR}/data" CACHE PATH "Fallback data directory compiled into the binary")
target_compile_definitions(labycore PUBLIC LABYTERM_DATA_DIR="${LABYTERM_SOURCE_DATA_DIR}" LABYTERM_VERSION="${PROJECT_VERSION}")

# --- The renderer: glyph atlas, CPU frame renderer, CRT filter, art-to-text mosaics (no SDL) -----
add_library(labyrender STATIC
  src/render/CellGrid.cpp
  src/render/GlyphAtlas.cpp
  src/render/FrameRenderer.cpp
  src/render/Mosaic.cpp
  src/render/TermFrame.cpp
  src/render/TermCaps.cpp
  src/render/TextShapes.cpp
  src/render/Image8.cpp
  src/render/StbImpl.cpp
  src/platform/Theme.cpp
  src/ui/FontShowcase.cpp
  src/ui/ViewPainter.cpp
  src/ui/TownArt.cpp
  src/ui/ShaftArt.cpp
  src/ui/CorridorRenderer.cpp
  src/ui/MainScreen.cpp
  src/ui/OrbArt.cpp
  src/ui/ScrollArt.cpp
  src/ui/MapGlyphs.cpp
  src/ui/LayoutVariants.cpp
  src/ui/PanelKit.cpp
  src/ui/KeyMap.cpp
  src/ui/SystemMenu.cpp
  src/ui/TitleScreen.cpp
  src/ui/BossAura.cpp
  src/ui/TermScreen.cpp
  src/ui/WalkSession.cpp)
target_include_directories(labyrender PUBLIC src)
target_include_directories(labyrender SYSTEM PUBLIC third_party/stb)  # SYSTEM: the vendored stb headers' own warnings are not ours to fix
target_link_libraries(labyrender PUBLIC labycore Threads::Threads)
target_compile_options(labyrender PRIVATE -Wall -Wextra)

# DEBUG hotkeys (monster spawning toggle, ...), listed with the other key hints in the UI. They are for development: off by
# default, so a plain `cmake` build, a package and the plugin's build have none; on by default only for a Debug build type.
# tools/build.sh (a Release build for development) turns them on explicitly. Every hotkey must sit behind this switch.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(LABYTERM_DEBUG_KEYS_DEFAULT ON)
else()
  set(LABYTERM_DEBUG_KEYS_DEFAULT OFF)
endif()
option(LABYTERM_DEBUG_KEYS "Compile the DEBUG hotkeys in (development builds)" ${LABYTERM_DEBUG_KEYS_DEFAULT})
if(LABYTERM_DEBUG_KEYS)
  target_compile_definitions(labyrender PUBLIC LABYTERM_DEBUG_KEYS=1)
endif()

# --- GPU CRT filter: shaders compiled to SPIR-V at build time (needs glslc), embedded as C arrays ---------
find_program(GLSLC glslc)
if(GLSLC)
  set(SHADER_DIR ${CMAKE_SOURCE_DIR}/src/render/shaders)
  set(SHADER_INC ${CMAKE_BINARY_DIR}/shaders)
  file(MAKE_DIRECTORY ${SHADER_INC})
  set(SHADER_INCS)
  foreach(sh fullscreen.vert crt_bright.frag crt_down.frag crt_blur.frag crt_final.frag)
    string(REPLACE "." "_" var ${sh})
    add_custom_command(OUTPUT ${SHADER_INC}/${sh}.spv.inc
      COMMAND ${GLSLC} -I ${SHADER_DIR} ${SHADER_DIR}/${sh} -o ${SHADER_INC}/${sh}.spv
      COMMAND ${CMAKE_COMMAND} -DIN=${SHADER_INC}/${sh}.spv -DOUT=${SHADER_INC}/${sh}.spv.inc -DVAR=${var}_spv -P ${CMAKE_SOURCE_DIR}/cmake/embed_spv.cmake
      DEPENDS ${SHADER_DIR}/${sh} ${SHADER_DIR}/crt_common.glsl ${SHADER_DIR}/torch_common.glsl ${CMAKE_SOURCE_DIR}/cmake/embed_spv.cmake)
    list(APPEND SHADER_INCS ${SHADER_INC}/${sh}.spv.inc)
  endforeach()
  add_library(labygpu STATIC src/render/GpuCrt.cpp ${SHADER_INCS})
  target_include_directories(labygpu PUBLIC ${SHADER_INC})
  target_link_libraries(labygpu PUBLIC labyrender SDL3::SDL3)
  target_compile_options(labygpu PRIVATE -Wall -Wextra)
  target_compile_definitions(labygpu PUBLIC LABYTERM_GPU_CRT=1)
else()
  message(STATUS "glslc not found: building without the GPU CRT filter (CPU filter only)")
endif()

if(GLSLC)
  add_executable(gpu_crt_test tests/gpu_crt_test.cpp)
  target_link_libraries(gpu_crt_test PRIVATE labygpu)
  target_include_directories(gpu_crt_test PRIVATE tests)
  target_include_directories(gpu_crt_test SYSTEM PRIVATE third_party/stb)
endif()

# --- Sound: the mixer (no SDL) and the SDL3 player that loads data/audio/*.wav and plays the game's AudioEvents ---
add_library(labyaudio STATIC src/audio/Mixer.cpp src/audio/AudioPlayer.cpp)
target_include_directories(labyaudio PUBLIC src)
target_link_libraries(labyaudio PUBLIC labycore SDL3::SDL3)
target_compile_options(labyaudio PRIVATE -Wall -Wextra)

# --- The game -------------------------------------------------------------------------------
add_executable(labyterm
  src/main.cpp
  src/render/SdlBackend.cpp
  src/render/TerminalBackend.cpp
  src/render/BackendFactory.cpp
  src/ui/MapViewer.cpp
  src/ui/FontShowcaseApp.cpp
  src/ui/LayoutVariantsApp.cpp
  src/ui/WalkApp.cpp)
target_link_libraries(labyterm PRIVATE labyrender labyaudio SDL3::SDL3)
if(GLSLC)
  target_link_libraries(labyterm PRIVATE labygpu)
endif()
target_compile_options(labyterm PRIVATE -Wall -Wextra)

# --- The game in a real terminal (truecolor + Unicode octants), cells only ---------------------------------
add_executable(octant_probe tests/octant_probe.cpp)
target_include_directories(octant_probe PRIVATE src)
target_compile_options(octant_probe PRIVATE -Wall -Wextra)
add_executable(term_glyphs_test tests/term_glyphs_test.cpp)
target_include_directories(term_glyphs_test PRIVATE src)
target_link_libraries(term_glyphs_test PRIVATE labyrender)
add_test(NAME term_glyphs_test COMMAND term_glyphs_test)
add_test(NAME term_startup COMMAND python3 -I ${CMAKE_SOURCE_DIR}/tools/test_term_startup.py $<TARGET_FILE:labyterm>)

# Headless tools: render screens to PNG with the same renderer and filter the window uses
add_executable(atlas_dump tests/atlas_dump.cpp)
target_include_directories(atlas_dump PRIVATE tests)
target_link_libraries(atlas_dump PRIVATE labyrender)
add_executable(text_shapes_test tests/text_shapes_test.cpp)
target_link_libraries(text_shapes_test PRIVATE labyrender)
add_executable(session_sim tests/session_sim.cpp)
target_link_libraries(session_sim PRIVATE labyrender)
# Balance harness (no ctest on purpose: it plays whole runs and takes minutes; tools/balance_report.py drives it)
add_executable(balance_bot tests/balance_bot.cpp)
target_link_libraries(balance_bot PRIVATE labyrender)
add_executable(combat_sim tests/combat_sim.cpp)
target_link_libraries(combat_sim PRIVATE labycore)
add_executable(monster_action_test tests/monster_action_test.cpp)
target_link_libraries(monster_action_test PRIVATE labycore)
add_executable(balance_rules_test tests/balance_rules_test.cpp)
target_link_libraries(balance_rules_test PRIVATE labycore)
add_executable(loot_rules_test tests/loot_rules_test.cpp)
target_link_libraries(loot_rules_test PRIVATE labycore)
add_executable(store_test tests/store_test.cpp)
target_link_libraries(store_test PRIVATE labyrender)
add_executable(save_test tests/save_test.cpp)
add_executable(menu_test tests/menu_test.cpp)
target_link_libraries(save_test PRIVATE labyrender)
add_executable(launch_mode_test tests/launch_mode_test.cpp)
target_link_libraries(launch_mode_test PRIVATE labyrender)
add_executable(persist_fuzz_test tests/persist_fuzz_test.cpp)
target_link_libraries(persist_fuzz_test PRIVATE labyrender)
target_link_libraries(menu_test PRIVATE labyrender)
add_executable(run_test tests/run_test.cpp)
target_link_libraries(run_test PRIVATE labyrender)
add_executable(scroll_test tests/scroll_test.cpp)
target_link_libraries(scroll_test PRIVATE labycore)
add_executable(fountain_test tests/fountain_test.cpp)
target_link_libraries(fountain_test PRIVATE labycore)
add_executable(floor_persist_test tests/floor_persist_test.cpp)
target_link_libraries(floor_persist_test PRIVATE labyrender)
add_executable(secret_test tests/secret_test.cpp)
target_link_libraries(secret_test PRIVATE labyrender)
add_executable(corridor_test tests/corridor_test.cpp)
target_link_libraries(corridor_test PRIVATE labyrender)
add_executable(view_dump tests/view_dump.cpp)
target_include_directories(view_dump PRIVATE tests)
target_link_libraries(view_dump PRIVATE labyrender)
add_executable(art_sheet tests/art_sheet.cpp)
target_include_directories(art_sheet PRIVATE tests)
target_link_libraries(art_sheet PRIVATE labyrender)
add_executable(ride_sheet tests/ride_sheet.cpp)
target_include_directories(ride_sheet PRIVATE tests)
target_link_libraries(ride_sheet PRIVATE labyrender)
add_executable(walk_dump tests/walk_dump.cpp)
target_include_directories(walk_dump PRIVATE tests)
target_link_libraries(walk_dump PRIVATE labyrender)
add_executable(scroll_dump tests/scroll_dump.cpp)
target_include_directories(scroll_dump PRIVATE tests)
target_link_libraries(scroll_dump PRIVATE labyrender)
add_executable(variant_dump tests/variant_dump.cpp)
target_include_directories(variant_dump PRIVATE tests)
target_link_libraries(variant_dump PRIVATE labyrender)

# --- Tests ----------------------------------------------------------------------------------
add_executable(level_test tests/level_test.cpp)
target_link_libraries(level_test PRIVATE labycore)

enable_testing()
add_test(NAME text_shapes COMMAND text_shapes_test)
add_test(NAME text_glow COMMAND text_glow_test)
add_test(NAME level_test COMMAND level_test 300 12)
add_test(NAME corridor COMMAND corridor_test)
add_test(NAME combat_sim COMMAND combat_sim 3 500)
add_test(NAME monster_actions COMMAND monster_action_test)
add_test(NAME scroll_test COMMAND scroll_test)
add_test(NAME fountain_test COMMAND fountain_test)
add_test(NAME secret_test COMMAND secret_test)
add_test(NAME menu_test COMMAND menu_test)
add_test(NAME save_test COMMAND save_test)
add_test(NAME floor_persist_test COMMAND floor_persist_test)
add_test(NAME persist_fuzz COMMAND persist_fuzz_test)
add_test(NAME launch_mode_test COMMAND launch_mode_test)
add_test(NAME run_test COMMAND run_test)
add_test(NAME balance_rules COMMAND balance_rules_test)
add_test(NAME store_test COMMAND store_test)
add_test(NAME loot_rules COMMAND loot_rules_test)
add_test(NAME session_sim COMMAND session_sim 8 3000)
set_tests_properties(session_sim PROPERTIES ENVIRONMENT "LABYTERM_TUNING=${CMAKE_SOURCE_DIR}/tests/session_force.json")
add_executable(audio_test tests/audio_test.cpp)
target_link_libraries(audio_test PRIVATE labyrender labyaudio)
add_test(NAME audio COMMAND audio_test)
add_test(NAME check_data COMMAND labyterm --check-data)
add_test(NAME release_compile COMMAND ${CMAKE_SOURCE_DIR}/tools/test_release_compile.sh ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})
add_executable(minimap_dump tests/minimap_dump.cpp)
target_include_directories(minimap_dump PRIVATE tests)
target_link_libraries(minimap_dump PRIVATE labyrender)
add_executable(draw_bench tests/draw_bench.cpp)
target_link_libraries(draw_bench PRIVATE labyrender)
add_executable(text_charset_pick tests/text_charset_pick.cpp)
target_link_libraries(text_charset_pick PRIVATE labyrender)
add_executable(text_glow_test tests/text_glow_test.cpp)
target_link_libraries(text_glow_test PRIVATE labyrender)

# --- Install: `cmake --install build --prefix <dir>` puts the game in <dir>/bin, its data in <dir>/share/labyterm and its menu entry in <dir>/share/applications ---
# (the binary finds ../share/labyterm by itself, so the prefix can be anywhere and can be moved)
install(TARGETS labyterm RUNTIME DESTINATION bin)
install(DIRECTORY data/ DESTINATION share/labyterm)
install(FILES LICENSE LICENSE-ART.txt THIRD_PARTY.md DESTINATION share/doc/labyterm)
# stb is compiled in: its license text travels with the binary (the font licenses ship with the fonts, in the data directory)
install(FILES third_party/stb/LICENSE DESTINATION share/doc/labyterm RENAME LICENSE-stb.txt)
# Start-menu entry (Exec=labyterm, so the install prefix's bin must be on PATH; the Omarchy plugin ships its own entry)
install(FILES packaging/labyterm.desktop DESTINATION share/applications)
# Icons (packaging/icons, generated by tools/make_icons.py): the storm castle is the default, installed as "labyterm"
install(FILES packaging/icons/labyterm-castle-storm.svg DESTINATION share/icons/hicolor/scalable/apps RENAME labyterm.svg)
foreach(px 16 24 32 48 64 128 256 512)
  install(FILES packaging/icons/png/${px}x${px}/labyterm-castle-storm.png
          DESTINATION share/icons/hicolor/${px}x${px}/apps RENAME labyterm.png)
endforeach()
