cmake_minimum_required(VERSION 3.16)
project(MudBuddies CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Platform-independent simulation core (physics, terrain, CAD kernel, audio DSP, netcode).
set(MB_CORE_SOURCES
  src/terrain/terrain.cpp
  src/physics/specs.cpp
  src/physics/vehicle.cpp
  src/physics/collide.cpp
  src/game/ai.cpp
  src/cad/cad.cpp
  src/audio/synth.cpp
  src/audio/audio.cpp
  src/net/net.cpp
  src/game/world.cpp
  src/game/particles.cpp
  src/game/modes.cpp
  src/game/recovery.cpp
)
add_library(mbcore STATIC ${MB_CORE_SOURCES})
target_include_directories(mbcore PUBLIC src)

if(EMSCRIPTEN)
  # ---- Dear ImGui (developer / tuning panel, toggled in-game with F1 or `) ----
  option(MB_DEVTOOLS "Build the Dear ImGui developer panel into the web build" ON)
  if(MB_DEVTOOLS)
    include(FetchContent)
    FetchContent_Declare(imgui
      GIT_REPOSITORY https://github.com/ocornut/imgui.git
      GIT_TAG v1.91.9
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(imgui)
    add_library(imgui STATIC
      ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
      ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3 IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
  endif()

  # ---- Browser build: WebGL2 + Web Audio + Gamepad API, packaged as WASM ----
  set(MB_WEB_SOURCES src/devtools/devui.cpp src/main.cpp src/game/game.cpp src/render/renderer.cpp
      src/render/gl.cpp src/input/input.cpp)
  set(MB_WEB_LINK
    -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8,HEAPF32,HEAPU8
    -sEXPORTED_FUNCTIONS=_main,_malloc,_free
    -sENVIRONMENT=web
    --embed-file ${CMAKE_SOURCE_DIR}/cad/parts@/cad)
  foreach(T mudbuddies mudbuddies_single)
    add_executable(${T} ${MB_WEB_SOURCES})
    target_link_libraries(${T} mbcore)
    if(MB_DEVTOOLS)
      target_link_libraries(${T} imgui)
      target_compile_definitions(${T} PRIVATE MB_DEVTOOLS=1)
    endif()
    target_link_options(${T} PRIVATE ${MB_WEB_LINK})
  endforeach()
  set_target_properties(mudbuddies PROPERTIES SUFFIX ".js" RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/web/build)
  # Same game with the .wasm inlined as base64, for the standalone single-file HTML
  # (tools/make_standalone.py -> dist/mudbuddies.html).
  set_target_properties(mudbuddies_single PROPERTIES SUFFIX ".js" RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  target_link_options(mudbuddies_single PRIVATE -sSINGLE_FILE=1)
else()
  # ---- Native: headless tests + CAD command-line tool + audio forensics probe ----
  add_executable(mb_tests tests/test_main.cpp)
  target_link_libraries(mb_tests mbcore)
  add_executable(mbcad tools/mbcad.cpp)
  target_link_libraries(mbcad mbcore)
  add_executable(audio_probe tools/audio_probe.cpp)
  add_executable(render_wav tools/render_wav.cpp)
  target_link_libraries(render_wav mbcore)
  enable_testing()
  add_test(NAME physics_and_core COMMAND mb_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
