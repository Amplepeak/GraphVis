qt_add_executable(graphvis
  src/main.cpp
  src/NativeApi.cpp
  src/NativeApi.h
  src/AppController.cpp
  src/AppController.h
  src/UiLayouts.cpp
  src/UiLayouts.h
  src/NativeViewportWindow.cpp
  src/NativeViewportWindow.h
  src/ProjectWorkspace.cpp
  src/ProjectWorkspace.h
  src/ReefScene.cpp
  src/ReefScene.h
  src/SplashWindow.cpp
  src/SplashWindow.h
)

# The icon Windows draws for the program itself.
#
# setWindowIcon covers the window. Everything OUTSIDE it - the taskbar button,
# the Explorer file icon, the Start menu and desktop shortcuts the installer
# creates, the Alt+Tab card - comes from an icon RESOURCE compiled into the
# executable, and there was none. The installer points its shortcuts at
# graphvis.exe and takes whatever icon the exe carries, so a missing resource
# is a generic placeholder in every one of those places at once.
if(WIN32)
  target_sources(graphvis PRIVATE graphvis.rc)
endif()

# --------------------------------------------------------------------------
# 2-D plot renderer (native/plot2d)
#
# Unlike the VTK backend, this is linked straight into the executable rather
# than being a lazily-loaded QML plugin: 2-D plotting is the core of a graph
# editor, not a specialist view, so the indirection would buy nothing.
# See docs/PORT-PLAN.md for the renderer adapter design.
# --------------------------------------------------------------------------
set(GRAPHVIS_PLOT2D_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../native/plot2d)
set(GRAPHVIS_PLOT2D_SOURCES
  ${GRAPHVIS_PLOT2D_DIR}/src/QtPlotBackend.cpp
  # The engine-rewrite chain, split across six translation units.
  #
  # It was one 19,600-line function in a 1.78 MB file that took 2 minutes 10
  # seconds to compile on its own - single-threaded, on every build, because a
  # translation unit cannot be divided across cores. As seven units the same
  # work measures 48 seconds in total and 15 seconds of wall clock on six
  # cores, and a change to one engine recompiles about 6 seconds of it.
  #
  # The total dropped as well as the wall clock, which is the compiler's cost
  # being superlinear in function size rather than anything clever here.
  ${GRAPHVIS_PLOT2D_DIR}/src/QtPlotBackendEngines1.cpp
  ${GRAPHVIS_PLOT2D_DIR}/src/QtPlotBackendEngines2.cpp
  ${GRAPHVIS_PLOT2D_DIR}/src/QtPlotBackendEngines3.cpp
  ${GRAPHVIS_PLOT2D_DIR}/src/QtPlotBackendEngines4.cpp
  ${GRAPHVIS_PLOT2D_DIR}/src/QtPlotBackendEngines5.cpp
  ${GRAPHVIS_PLOT2D_DIR}/src/QtPlotBackendEngines6.cpp
  ${GRAPHVIS_PLOT2D_DIR}/src/QtPlotBackendShared.h
  ${GRAPHVIS_PLOT2D_DIR}/src/Expression.cpp
  ${GRAPHVIS_PLOT2D_DIR}/src/PublicationProfile.cpp
  ${GRAPHVIS_PLOT2D_DIR}/src/ArrowTable.cpp
  ${GRAPHVIS_PLOT2D_DIR}/src/PlotCanvas.cpp
  ${GRAPHVIS_PLOT2D_DIR}/src/PlotSelfTest.cpp
  ${GRAPHVIS_PLOT2D_DIR}/src/Units.cpp
  ${GRAPHVIS_PLOT2D_DIR}/src/SurfaceEstimators.cpp
  ${GRAPHVIS_PLOT2D_DIR}/include/ColourVision.h
  ${GRAPHVIS_PLOT2D_DIR}/include/Expression.h
  ${GRAPHVIS_PLOT2D_DIR}/include/PublicationProfile.h
  ${GRAPHVIS_PLOT2D_DIR}/include/PlotSpec.h
  ${GRAPHVIS_PLOT2D_DIR}/include/PlotBackend.h
  ${GRAPHVIS_PLOT2D_DIR}/include/QtPlotBackend.h
  ${GRAPHVIS_PLOT2D_DIR}/include/ArrowTable.h
  ${GRAPHVIS_PLOT2D_DIR}/include/PlotCanvas.h
  ${GRAPHVIS_PLOT2D_DIR}/include/ColourMaps.h
  ${GRAPHVIS_PLOT2D_DIR}/include/TouchGesture.h
  ${GRAPHVIS_PLOT2D_DIR}/include/PlotSelfTest.h
  ${GRAPHVIS_PLOT2D_DIR}/include/Units.h
)

# VtkViewport.qml is a RESOURCE, not a QML_FILE, and that is deliberate: listing
# it under QML_FILES would make `import GraphVis.VTK` a build-time dependency of
# the main application module and defeat the whole point of loading VTK lazily.
#
# The cost is that nothing - not qmllint, not qmlcachegen - ever looks at this
# file, so a plain syntax error in it is invisible until a user selects VTK/PBR
# and gets nothing but a Loader.Error. It shipped for months with two property
# names that started with a capital letter, which QML does not allow. If you
# edit that file, read it carefully; the build will not help you.
set_source_files_properties(
  qml/lazy/VtkViewport.qml
  PROPERTIES
    QT_RESOURCE_ALIAS lazy/VtkViewport.qml
)

set_source_files_properties(
  ../assets/branding/graphvis_icon.png
  PROPERTIES
    QT_RESOURCE_ALIAS assets/graphvis_icon.png
)

# Theme is a QML singleton: one source of colour/typography for every panel,
# and the mechanism behind the theme catalogue.
set_source_files_properties(
  qml/Theme.qml
  PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

# The GraphVis 17 graph catalogue (318 entries / 30 categories), generated from
# src/graphvis/rendering/graph_library.py. Embedded so AppController can read it
# at :/qt/qml/GraphVis/catalogue/graph_catalogue.json with no path lookup.
set_source_files_properties(
  ../config/graph_catalogue.json
  PROPERTIES
    QT_RESOURCE_ALIAS catalogue/graph_catalogue.json
)

# The in-application help, embedded the same way and for the same reason:
# help that depends on where the program was installed is help that is missing
# on somebody's machine.
set_source_files_properties(
  ../config/help_topics.json
  PROPERTIES
    QT_RESOURCE_ALIAS help/help_topics.json
)

# The citation styles, generated by tools/make_citation_styles.py from the one
# table in graphvis_science/citation_styles.py. Embedded so the picker and the
# formatter cannot come to offer different sets - which is exactly what a
# hand-kept list in the QML produced: a style argument the service accepted and
# ignored.
set_source_files_properties(
  ../config/citation_styles.json
  PROPERTIES
    QT_RESOURCE_ALIAS citations/citation_styles.json
)

# Every QML file in the module, listed once.
#
# It used to be written out twice - once for the resource-alias loop below
# and once for qt_add_qml_module - so adding a component meant editing two
# lists, and forgetting the second gave a file that built but could not be
# imported. Same list, one place.
set(GRAPHVIS_QML_FILES
  qml/Main.qml
  qml/Theme.qml
  qml/components/TopBar.qml
  qml/components/WorkspaceRail.qml
  qml/components/ThemePicker.qml
  qml/components/ColourVisionBar.qml
  qml/components/ProjectPanel.qml
  qml/components/RenderProgressBadge.qml
  qml/components/PreviewReadyNotice.qml
  qml/components/ControlSidebar.qml
  qml/components/DataWorkspace.qml
  qml/components/MappingPanel.qml
  qml/components/AnalysisPanel.qml
  qml/components/LiteraturePanel.qml
  qml/components/PublicationPanel.qml
  qml/components/StatusStrip.qml
  qml/components/GraphLibrary.qml
  qml/components/GvGroupBox.qml
  qml/components/GvSplitHandle.qml
  qml/components/LimitRow.qml
  qml/components/RangePanel.qml
  qml/components/ColourStops.qml
  qml/components/FigureLab.qml
  qml/components/BusyOverlay.qml
  qml/components/ExpressionBox.qml
  qml/components/LatexExport.qml
  qml/components/StatusPill.qml
  qml/components/ActiveDatasetBar.qml
  qml/components/AnnotationLayer.qml
  qml/components/ColourMapSelector.qml
  qml/components/FigureStyleBar.qml
  qml/components/FigureBackgroundPicker.qml
  qml/components/FigureAppearanceBar.qml
  qml/components/ExportDialog.qml
  qml/components/UnitSelector.qml
  qml/components/SolverPanel.qml
  qml/components/AddOnsPanel.qml
  qml/components/PanelScroll.qml
  qml/components/PlotNotice.qml
  qml/components/DockPanel.qml
  qml/components/MainMenuBar.qml
  qml/components/HelpBrowser.qml
  qml/components/LayoutPicker.qml
  qml/components/CommandPalette.qml
  qml/components/RibbonBar.qml
  qml/components/AxisScaleBar.qml
  qml/components/IconRail.qml
  qml/components/ShelfBar.qml
  qml/components/PageBar.qml
  qml/components/FigureCell.qml
  # The single figure, lifted out of VisualizeWorkspace so that more than one
  # can exist, and the strip that switches between them.
  qml/components/FigureCanvas.qml
  qml/components/FigureTabBar.qml
  qml/components/FigureWindow.qml
  qml/workspaces/NotebookCanvas.qml
  qml/components/ScanPanel.qml
  qml/workspaces/ClassicShell.qml
  qml/workspaces/HomeWorkspace.qml
  qml/workspaces/LiteratureWorkspace.qml
  qml/workspaces/VisualizeWorkspace.qml
  qml/experimental/ExperimentalShell.qml
)

foreach(_qml_file IN LISTS GRAPHVIS_QML_FILES)
  string(REGEX REPLACE "^qml/" "" _alias "${_qml_file}")

  set_source_files_properties(
    ${_qml_file}
    PROPERTIES
      QT_RESOURCE_ALIAS "${_alias}"
  )
endforeach()

qt_add_qml_module(graphvis
  URI GraphVis
  VERSION 18.4

  # C++ QML types. PlotCanvas carries QML_ELEMENT and becomes GraphVis.PlotCanvas.
  SOURCES
    ${GRAPHVIS_PLOT2D_SOURCES}

  # Pin the prefix. Without it the prefix depends on Qt policy QTP0001, and if
  # that resolves to OLD the module lands at qrc:/GraphVis/ instead of
  # qrc:/qt/qml/GraphVis/. The engine only searches qrc:/qt/qml, so the module
  # then fails to load with: Module "GraphVis" contains no type named "Main".
  # main.cpp also refers to qrc:/qt/qml/GraphVis/assets/graphvis_icon.png.
  RESOURCE_PREFIX "/qt/qml"

  QML_FILES
    ${GRAPHVIS_QML_FILES}

  RESOURCES
    ../assets/branding/graphvis_icon.png
    qml/lazy/VtkViewport.qml
    ../config/graph_catalogue.json
    ../config/help_topics.json
    ../config/citation_styles.json
)

target_include_directories(graphvis PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${GRAPHVIS_PLOT2D_DIR}/include
)

if(TARGET Arrow::arrow_shared)
  set(GRAPHVIS_APP_ARROW_TARGET Arrow::arrow_shared)
elseif(TARGET Arrow::arrow_static)
  set(GRAPHVIS_APP_ARROW_TARGET Arrow::arrow_static)
else()
  message(FATAL_ERROR "Apache Arrow C++ target not found for the GraphVis executable")
endif()

target_link_libraries(graphvis PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Quick
  Qt6::Qml
  Qt6::QuickControls2
  Qt6::Concurrent
  Qt6::Svg
  ${GRAPHVIS_APP_ARROW_TARGET}
)

if(TARGET graphvis_rust)
  add_dependencies(graphvis graphvis_rust)
endif()

if(WIN32)
  target_compile_definitions(graphvis PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )

  target_link_libraries(graphvis PRIVATE
    user32
  )

  set_target_properties(graphvis PROPERTIES
    WIN32_EXECUTABLE TRUE
  )

  # --------------------------------------------------------------------------
  # Windows application manifest
  # --------------------------------------------------------------------------

  set(GRAPHVIS_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/graphvis.manifest"
  )

  set(GRAPHVIS_MANIFEST_RC
    "${CMAKE_CURRENT_BINARY_DIR}/graphvis_manifest.rc"
  )

  file(WRITE
    "${GRAPHVIS_MANIFEST_RC}"
    "1 24 \"${GRAPHVIS_MANIFEST}\"\n"
  )

  set_property(
    SOURCE "${GRAPHVIS_MANIFEST_RC}"
    PROPERTY OBJECT_DEPENDS "${GRAPHVIS_MANIFEST}"
  )

  target_sources(graphvis PRIVATE
    "${GRAPHVIS_MANIFEST_RC}"
  )

  # 8 MB of stack, reserved, instead of the 1 MB MSVC default.
  #
  # This is not precautionary. The engine sweep died with 0xC00000FD, a stack
  # overflow, on the 57th engine - Beeswarm - and the two engines either side
  # of it in the same source branch, Dot Plot and Strip Plot, rendered fine.
  # The only difference between them is depth: Dot Plot and Strip Plot rewrite
  # to "4D / 5D Scatter" and return, one call into prepareSpecCore; Beeswarm
  # rewrites to "Swarm" and hands the spec back through prepareSpec, so it
  # enters prepareSpecCore a second time.
  #
  # Two frames, and the stack is gone. `g++ -fstack-usage` puts that function
  # at 47 KB unoptimised and 11.7 KB at -O1; MSVC's must be far larger, because
  # nothing else in the path is big enough to account for 1 MB in two calls.
  # The function has a branch per catalogue engine - 434 of them - and carries
  # the locals of all of them in one frame, so it grew with the catalogue.
  # A sweep of 201 engines passed in September; the same sweep of 434 dies.
  #
  # Raising the reserve is the honest short-term fix rather than the whole one.
  # The real problem is one function with a frame that scales with the
  # catalogue, and the answer to that is to split it - a refactor, not a
  # one-line change. Until then this buys the headroom the recursion needs.
  #
  # It covers the interactive path too, which matters more than the self-test:
  # PlotCanvas runs the full render on a QtConcurrent pool thread, and a
  # Windows thread created with a zero stack size takes its reserve from this
  # same PE header field. Beeswarm from the Graph Library would have taken the
  # whole application down with no message.
  target_link_options(graphvis PRIVATE
    "/MANIFEST:NO"
    "/MANIFESTUAC:NO"
    "/STACK:8388608"
  )
endif()

# --------------------------------------------------------------------------
# Install executable
# --------------------------------------------------------------------------

install(TARGETS graphvis
  RUNTIME DESTINATION .
  BUNDLE DESTINATION .
)

# --------------------------------------------------------------------------
# Windows runtime deployment
#
# These DLLs are produced DURING the build: vcpkg's applocal deploy copies Qt,
# VTK and their dependencies next to the freshly linked executable, and the Qt
# platform/imageformat plugins into subdirectories of the build root.
#
# A configure-time file(GLOB) therefore sees an EMPTY directory on a clean tree.
# That is invisible in build/windows-fast, which was already populated when
# CMake last reconfigured, but on a clean build/windows-release - the tree the
# end-user installer is built from - it silently ships no qwindows.dll, and the
# installed app dies with "no Qt platform plugin could be initialized".
#
# install(CODE) defers the glob to install time, when the files actually exist.
# --------------------------------------------------------------------------

if(WIN32)
  # vcpkg's own bin directory is populated at configure time, so a normal
  # install(DIRECTORY) is correct and cheapest here.
  install(DIRECTORY
    "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin/"
    DESTINATION .
    FILES_MATCHING PATTERN "*.dll"
  )

  # --------------------------------------------------------------------------
  # Qt's own QML modules.
  #
  # The application's GraphVis module is compiled into the resource system and
  # needs nothing here. Every module it IMPORTS - QtQuick, QtQuick.Controls,
  # QtQuick.Layouts, QtQuick.Window, QtQuick.Dialogs and what those pull in -
  # is loaded from disk at runtime and has to be beside the executable.
  #
  # Nothing installed them. The staged tree had exactly one folder under qml/,
  # the application's own, and the app died at startup with:
  #
  #     The GraphVis QML interface failed to load. This is commonly caused by
  #     a missing Qt/QML runtime module or an incomplete deployed plugin
  #     directory.
  #
  # It was both, in fact: the same build also shipped no platform plugin. Both
  # were left to vcpkg's applocal deploy step, which did not run, and both were
  # then collected by globbing the directory that step was supposed to fill.
  #
  # vcpkg_installed is populated at configure time and cannot be empty, so a
  # plain install(DIRECTORY) is correct and is not a deferred glob.
  #
  # QtTest, QmlTime and the tooling/designer trees are development aids the
  # application never imports; .pdb files are debug symbols for someone else's
  # libraries. Everything else is kept, including the Controls styles the app
  # does not currently select, because a style is chosen at RUN time and a
  # missing one fails the same way this bug did.
  # --------------------------------------------------------------------------
  install(DIRECTORY
    "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/Qt6/qml/"
    DESTINATION qml
    PATTERN "*.pdb"    EXCLUDE
    PATTERN "QtTest"   EXCLUDE
    PATTERN "QmlTime"  EXCLUDE
    PATTERN "tooling"  EXCLUDE
    PATTERN "designer" EXCLUDE
  )

  install(CODE "
    # Runtime DLLs copied beside the executable by the applocal deploy step.
    file(GLOB _gv_root_dlls \"${CMAKE_BINARY_DIR}/*.dll\")
    if(_gv_root_dlls)
      file(INSTALL DESTINATION \"\${CMAKE_INSTALL_PREFIX}\" TYPE SHARED_LIBRARY FILES \${_gv_root_dlls})
    endif()

    # Qt plugin directories.
    #
    # TWO sources, and the order matters. The build root is where vcpkg's
    # applocal deploy is SUPPOSED to put them, and where they are when it runs.
    # vcpkg_installed/.../Qt6/plugins is where they actually live, is populated
    # at configure time, and cannot be empty.
    #
    # Globbing only the build root is what shipped an application that could
    # not open a window:
    #
    #     This application failed to start because no Qt platform plugin
    #     could be initialized.
    #
    # The deploy step had not run on that build, the glob matched nothing, the
    # install reported success, and the missing file was qwindows.dll - which
    # was sitting in vcpkg_installed the whole time. An install rule must not
    # depend on another step having happened when it can read the real source
    # directly. The build-root pass is kept because a fast-preset tree may
    # carry a plugin the vcpkg tree does not, and file(INSTALL) skips a file
    # that is already there and identical.
    foreach(_gv_plugin_dir platforms imageformats iconengines styles tls networkinformation generic)
      # unset, not set to \"\": list(APPEND) onto an empty STRING leaves a
      # leading empty element, and file(INSTALL) on an empty path errors.
      unset(_gv_plugin_dlls)
      file(GLOB _gv_vcpkg_plugins
           \"${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/\${_gv_plugin_dir}/*.dll\")
      file(GLOB _gv_build_plugins \"${CMAKE_BINARY_DIR}/\${_gv_plugin_dir}/*.dll\")
      if(_gv_vcpkg_plugins)
        list(APPEND _gv_plugin_dlls \${_gv_vcpkg_plugins})
      endif()
      if(_gv_build_plugins)
        list(APPEND _gv_plugin_dlls \${_gv_build_plugins})
      endif()
      if(_gv_plugin_dlls)
        list(REMOVE_DUPLICATES _gv_plugin_dlls)
      endif()
      if(_gv_plugin_dlls)
        file(INSTALL DESTINATION \"\${CMAKE_INSTALL_PREFIX}/\${_gv_plugin_dir}\" TYPE SHARED_LIBRARY FILES \${_gv_plugin_dlls})
      endif()
    endforeach()

    # The platform plugin is not optional and its absence is not a warning:
    # without it the application starts, fails, and shows a dialog that tells
    # the user to reinstall - which does not help, because the installer is
    # what is missing it. Better to fail the install than to package that.
    if(NOT EXISTS \"\${CMAKE_INSTALL_PREFIX}/platforms/qwindows.dll\")
      message(FATAL_ERROR
        \"No Qt platform plugin was installed: platforms/qwindows.dll is missing \"
        \"from \${CMAKE_INSTALL_PREFIX}. The application would start and immediately \"
        \"fail with 'no Qt platform plugin could be initialized'. Looked in \"
        \"${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms \"
        \"and ${CMAKE_BINARY_DIR}/platforms.\")
    endif()

    # And every QML module the interface imports. Checked one by one rather
    # than by testing that qml/ merely exists: it DID exist, holding only the
    # application's own module, and that is exactly the state that shipped.
    foreach(_gv_qml_module QtQuick QtQuick/Controls QtQuick/Layouts QtQuick/Window QtQuick/Dialogs QtQml)
      if(NOT EXISTS \"\${CMAKE_INSTALL_PREFIX}/qml/\${_gv_qml_module}/qmldir\")
        message(FATAL_ERROR
          \"QML module \${_gv_qml_module} was not installed: \"
          \"qml/\${_gv_qml_module}/qmldir is missing from \${CMAKE_INSTALL_PREFIX}. \"
          \"The application would start and immediately fail with 'The GraphVis \"
          \"QML interface failed to load'. Source: \"
          \"${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/Qt6/qml.\")
      endif()
    endforeach()
  ")
endif()

# Sources added: ProjectWorkspace (project folders), SplashWindow (startup).
# LOD preview overlays added.
