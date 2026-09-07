import QtQuick
import GraphVis.VTK 1.0

GraphVisVtkItem {
    id: root
    // This wrapper is intentionally loaded as a raw QML resource only when the
    // user selects the VTK/PBR renderer. Keeping the GraphVis.VTK import here
    // prevents the heavyweight VTK QML plugin from participating in normal
    // Home/Literature startup.
    //
    // Because it is a RESOURCE and not a QML_FILE, qmllint never sees it, and
    // nothing in the build catches a mistake in this file. It used to declare
    // `property var AppController` and `property var MappingSource`: QML
    // property names must begin with a lowercase letter, so the whole component
    // failed to compile and VTK/PBR could never load - the only symptom being
    // the Loader.Error notice. Keep every name here lowercase.
    property var controller: null
    property var mappingSource: null

    arrowPath: root.controller ? root.controller.activeArrowPath : ""
    xColumn: root.mappingSource ? root.mappingSource.mappingX : ""
    yColumn: root.mappingSource ? root.mappingSource.mappingY : ""
    zColumn: root.mappingSource ? root.mappingSource.mappingZ : ""
    colorColumn: root.mappingSource ? root.mappingSource.mappingColor : ""
    mode: root.mappingSource ? root.mappingSource.vtkMode : "surface"
    roughness: root.mappingSource ? root.mappingSource.roughness : 0.35
    metallic: root.mappingSource ? root.mappingSource.metallic : 0.0
    specular: root.mappingSource ? root.mappingSource.specular : 0.45

    readonly property var smartPlan: root.controller ? root.controller.smartRenderPlan : null
    smartPointSize: (root.smartPlan && root.smartPlan.point_size !== undefined)
                    ? root.smartPlan.point_size : 4.0
    scalarMin: (root.smartPlan && root.smartPlan.display_min !== undefined)
               ? root.smartPlan.display_min : Number.NaN
    scalarMax: (root.smartPlan && root.smartPlan.display_max !== undefined)
               ? root.smartPlan.display_max : Number.NaN
}
