# VTK is deliberately a dynamically-loaded QML plugin. The GraphVis desktop
# executable does not link VTK/Arrow C++ directly, so Home/Literature startup
# avoids loading the large specialist 3D dependency graph.
qt_add_library(GraphVisVtkBackend SHARED)
qt_add_qml_module(GraphVisVtkBackend
  URI GraphVis.VTK
  VERSION 1.0
  PLUGIN_TARGET GraphVisVtkPlugin
  SOURCES
    src/GraphVisVtkItem.cpp
    include/GraphVisVtkItem.h
)
target_include_directories(GraphVisVtkBackend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(TARGET Arrow::arrow_shared)
  set(GRAPHVIS_ARROW_CPP_TARGET Arrow::arrow_shared)
elseif(TARGET Arrow::arrow_static)
  set(GRAPHVIS_ARROW_CPP_TARGET Arrow::arrow_static)
else()
  message(FATAL_ERROR "Apache Arrow C++ target not found")
endif()
target_link_libraries(GraphVisVtkBackend PRIVATE
  Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml
  ${GRAPHVIS_ARROW_CPP_TARGET}
  VTK::GUISupportQtQuick
  ${VTK_LIBRARIES}
)
vtk_module_autoinit(TARGETS GraphVisVtkBackend MODULES ${VTK_LIBRARIES})

# Explicitly deploy the backing DLL, runtime-loadable QML plugin, and qmldir.
# Keeping the plugin under qml/GraphVis/VTK lets the QML engine load VTK only
# when VisualizeWorkspace imports GraphVis.VTK.
qt_query_qml_module(GraphVisVtkBackend
  PLUGIN_TARGET GRAPHVIS_VTK_PLUGIN_TARGET
  TARGET_PATH GRAPHVIS_VTK_TARGET_PATH
  QMLDIR GRAPHVIS_VTK_QMLDIR
)
install(TARGETS GraphVisVtkBackend
  RUNTIME DESTINATION .
  LIBRARY DESTINATION .
  ARCHIVE DESTINATION lib
)
install(TARGETS ${GRAPHVIS_VTK_PLUGIN_TARGET}
  RUNTIME DESTINATION qml/${GRAPHVIS_VTK_TARGET_PATH}
  LIBRARY DESTINATION qml/${GRAPHVIS_VTK_TARGET_PATH}
)
install(FILES "${GRAPHVIS_VTK_QMLDIR}" DESTINATION qml/${GRAPHVIS_VTK_TARGET_PATH})
