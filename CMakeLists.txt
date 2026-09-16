cmake_minimum_required(VERSION 3.29)
project(GraphVis VERSION 18.4.0 LANGUAGES CXX)

# The Windows application manifest is embedded as an RT_MANIFEST resource
# (see app/CMakeLists.txt), which needs the RC language enabled.
if(WIN32)
  enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(GRAPHVIS_BUILD_RUST "Build Rust native core" ON)
# OFF by default. The Arrow Flight worker is a 79 MB binary that statically
# links its own copy of DataFusion, and nothing in the desktop app launches it -
# it exists for a future remote/multi-user mode. Every preset already turns it
# off; this makes a preset-less configure agree with them rather than quietly
# adding 79 MB to the staged app, which is how a stale copy ended up sitting in
# build/stage-fast for days.
option(GRAPHVIS_BUILD_FLIGHT "Build Arrow Flight worker (remote mode, not used by the desktop app)" OFF)
option(GRAPHVIS_FAST_DEV "Skip remote worker for fast UI development" OFF)
option(GRAPHVIS_FAST_LINKER "Use an available ABI-compatible fast native linker" ON)

if(GRAPHVIS_FAST_LINKER)
  if(WIN32)
    find_program(GRAPHVIS_LLD_LINK NAMES lld-link lld-link.exe)
    if(GRAPHVIS_LLD_LINK)
      set(CMAKE_LINKER_TYPE LLD)
      message(STATUS "GraphVis fast linker: LLD (${GRAPHVIS_LLD_LINK})")
    else()
      message(STATUS "GraphVis fast linker: MSVC default (lld-link not found)")
    endif()
  elseif(APPLE)
    find_program(GRAPHVIS_LLD NAMES ld64.lld ld.lld)
    if(GRAPHVIS_LLD)
      set(CMAKE_LINKER_TYPE LLD)
      message(STATUS "GraphVis fast linker: LLD (${GRAPHVIS_LLD})")
    else()
      message(STATUS "GraphVis fast linker: Apple system linker")
    endif()
  else()
    find_program(GRAPHVIS_MOLD NAMES mold)
    find_program(GRAPHVIS_LLD NAMES ld.lld)
    if(GRAPHVIS_MOLD)
      set(CMAKE_LINKER_TYPE MOLD)
      message(STATUS "GraphVis fast linker: mold (${GRAPHVIS_MOLD})")
    elseif(GRAPHVIS_LLD)
      set(CMAKE_LINKER_TYPE LLD)
      message(STATUS "GraphVis fast linker: LLD (${GRAPHVIS_LLD})")
    else()
      message(STATUS "GraphVis fast linker: system default")
    endif()
  endif()
endif()

if(GRAPHVIS_FAST_DEV)
  # Keep VTK available so the QML type graph is identical in fast and full builds.
  # The remote worker is the expensive optional component for UI-only iteration.
  set(GRAPHVIS_BUILD_FLIGHT OFF CACHE BOOL "" FORCE)
endif()

# Svg is for QSvgGenerator, which is how a figure exports as editable vector
# artwork - the format most journals ask for alongside the PDF.
find_package(Qt6 6.11.1 REQUIRED COMPONENTS Core Gui Quick Qml QuickControls2 Concurrent Svg)
qt_standard_project_setup()
find_package(Arrow CONFIG REQUIRED)
find_package(VTK REQUIRED COMPONENTS
  CommonCore CommonDataModel FiltersCore FiltersGeneral FiltersGeometry FiltersSources
  RenderingCore RenderingOpenGL2 InteractionStyle GUISupportQtQuick)

if(GRAPHVIS_BUILD_RUST)
  find_program(CARGO_EXECUTABLE cargo REQUIRED)
  file(GLOB_RECURSE GRAPHVIS_RUST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/native/crates/*.rs"
    "${CMAKE_SOURCE_DIR}/native/crates/*.wgsl"
    "${CMAKE_SOURCE_DIR}/native/Cargo.toml"
    "${CMAKE_SOURCE_DIR}/native/crates/*/Cargo.toml")
  if(DEFINED ENV{CARGO_TARGET_DIR})
    # The build environment exports this as a native Windows path. Left as-is it
    # is pasted verbatim into cmake_install.cmake, where "C:\src\GraphVis\.cache"
    # is an invalid escape and the whole install script fails to parse - which
    # silently left graphvis_ffi.dll out of the staged application.
    file(TO_CMAKE_PATH "$ENV{CARGO_TARGET_DIR}" GRAPHVIS_RUST_TARGET_DIR)
  else()
    set(GRAPHVIS_RUST_TARGET_DIR "${CMAKE_SOURCE_DIR}/.cache/cargo-target")
  endif()
  if(GRAPHVIS_FAST_DEV)
    set(GRAPHVIS_RUST_PROFILE "fast")
    set(GRAPHVIS_RUST_PROFILE_ARGS --profile fast)
  else()
    set(GRAPHVIS_RUST_PROFILE "release")
    set(GRAPHVIS_RUST_PROFILE_ARGS --release)
  endif()
  if(WIN32)
    set(GRAPHVIS_RUST_LIB "${GRAPHVIS_RUST_TARGET_DIR}/${GRAPHVIS_RUST_PROFILE}/graphvis_ffi.dll")
    set(GRAPHVIS_RUST_WORKER "${GRAPHVIS_RUST_TARGET_DIR}/${GRAPHVIS_RUST_PROFILE}/graphvis-worker.exe")
  elseif(APPLE)
    set(GRAPHVIS_RUST_LIB "${GRAPHVIS_RUST_TARGET_DIR}/${GRAPHVIS_RUST_PROFILE}/libgraphvis_ffi.dylib")
    set(GRAPHVIS_RUST_WORKER "${GRAPHVIS_RUST_TARGET_DIR}/${GRAPHVIS_RUST_PROFILE}/graphvis-worker")
  else()
    set(GRAPHVIS_RUST_LIB "${GRAPHVIS_RUST_TARGET_DIR}/${GRAPHVIS_RUST_PROFILE}/libgraphvis_ffi.so")
    set(GRAPHVIS_RUST_WORKER "${GRAPHVIS_RUST_TARGET_DIR}/${GRAPHVIS_RUST_PROFILE}/graphvis-worker")
  endif()
  set(_cargo_packages -p graphvis-ffi)
  if(GRAPHVIS_BUILD_FLIGHT)
    list(APPEND _cargo_packages -p graphvis-flight)
  endif()
  set(_rust_outputs "${GRAPHVIS_RUST_LIB}")
  if(GRAPHVIS_BUILD_FLIGHT)
    list(APPEND _rust_outputs "${GRAPHVIS_RUST_WORKER}")
  endif()
  add_custom_command(
    OUTPUT ${_rust_outputs}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GRAPHVIS_RUST_TARGET_DIR}"
    COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${GRAPHVIS_RUST_TARGET_DIR} ${CARGO_EXECUTABLE} build --locked --manifest-path ${CMAKE_SOURCE_DIR}/native/Cargo.toml ${GRAPHVIS_RUST_PROFILE_ARGS} ${_cargo_packages}
    DEPENDS ${GRAPHVIS_RUST_SOURCES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/native
    COMMENT "Building GraphVis Rust/WGPU/DataFusion core (incremental cached target)"
    VERBATIM)
  add_custom_target(graphvis_rust DEPENDS ${_rust_outputs})
endif()

add_subdirectory(native/vtk_backend)
add_subdirectory(app)

install(DIRECTORY assets/ DESTINATION share/graphvis/assets)
install(FILES VERSION README.md DESTINATION share/graphvis)

# --------------------------------------------------------------------------
# The science add-on, and the scripts that install it
#
# Without this, a release contains the native application and nothing else:
# the add-on is installed by pip from services/python, and that folder only
# ever existed in the source tree. So an end user with Install.exe could not
# install it - and everything behind it was unreachable. That is 149 dataset
# formats, all nine analysis operations, the seventeen surface estimators,
# literature extraction, .gvfig save and open, folder batching, and the MATLAB
# engine path.
#
# The layout mirrors the source tree on purpose. AppController looks for
# tools/Manage-OptionalComponents.ps1 beside the executable, and that script
# resolves the package as ../services/python, so both find what they expect
# with no path special-casing for an installed copy.
#
# Source only - a few hundred kilobytes. The packages themselves are still
# downloaded on demand into the user's own virtual environment, which is what
# keeps the release small and the add-on optional.
install(DIRECTORY services/python/graphvis_science
  DESTINATION services/python
  PATTERN "__pycache__" EXCLUDE
  PATTERN "*.pyc" EXCLUDE
)
install(FILES services/python/pyproject.toml DESTINATION services/python)

install(FILES
    tools/Manage-OptionalComponents.ps1
    tools/Install-OptionalPythonScience.ps1
    tools/optional-components.json
    tools/component_requirements.py
  DESTINATION tools
)
if(GRAPHVIS_BUILD_RUST)
  install(FILES "${GRAPHVIS_RUST_LIB}" DESTINATION . OPTIONAL)
  if(GRAPHVIS_BUILD_FLIGHT)
    install(PROGRAMS "${GRAPHVIS_RUST_WORKER}" DESTINATION . OPTIONAL)
  endif()
endif()