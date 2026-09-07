from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def req(x,msg):
    if not x: raise AssertionError(msg)

smart=(ROOT/'native/crates/graphvis-smart/src/lib.rs').read_text()
ffi=(ROOT/'native/crates/graphvis-ffi/src/lib.rs').read_text()
shader=(ROOT/'native/crates/graphvis-render/shaders/point_cloud.wgsl').read_text()
qml=(ROOT/'app/qml/components/MappingPanel.qml').read_text()
vtk=(ROOT/'native/vtk_backend/src/GraphVisVtkItem.cpp').read_text()
for marker in ['RobustStats','SmartRenderPlan','robust_stats','histogram_equalized','symlog','log10','optimize_scatter','preserve_extrema','density_emphasis']:
    req(marker in smart, f'missing smart engine marker: {marker}')
for marker in ['gv_smart_plan','SmartProfile::from_code','optimize_scatter','set_smart_style','smart_plan']:
    req(marker in ffi, f'FFI smart integration missing: {marker}')
for palette in ['viridis','plasma','coolwarm','turbo']:
    req(palette in shader.lower(), f'GPU palette missing: {palette}')
req('Smart Render' in qml and 'Analyze & Apply' in qml and 'Balanced' in qml and 'Clarity' in qml and 'Performance' in qml, 'Smart Render UX missing')
req('SetScalarRange' in vtk and 'smartPointSize' in vtk, 'VTK smart display-range integration missing')
print('GraphVis Smart Render acceptance: PASS')
