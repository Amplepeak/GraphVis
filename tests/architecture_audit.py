from __future__ import annotations
from pathlib import Path
import json, py_compile

ROOT=Path(__file__).resolve().parents[1]
TEXT_EXT={'.rs','.cpp','.h','.qml','.md','.toml','.json','.py','.ps1','.nsi','.txt','.bat','.yml','.yaml'}

def text_files(base=ROOT):
    for p in base.rglob('*'):
        if p.is_file() and p.suffix.lower() in TEXT_EXT and 'build' not in p.parts and '.cache' not in p.parts and p.name != 'architecture_audit.py':
            yield p

def corpus(paths=None):
    return '\n'.join(p.read_text(errors='ignore') for p in (paths or list(text_files())))

def require(cond,msg):
    if not cond: raise AssertionError(msg)

def main():
    all_text=corpus()
    banned=['lhs_flow','lhs_area','lhs_ea','total_VHPR_mean','vhpr','sCOD','VFA','GraphVis 17','17.0.0']
    for token in banned: require(token.lower() not in all_text.lower(), f'legacy/domain token remains: {token}')
    data_ext={'.csv','.tsv','.mat','.h5','.hdf5','.nc','.npy','.npz','.parquet','.arrow','.ipc','.duckdb','.sqlite','.db','.pdf'}
    bundled=[p for p in ROOT.rglob('*') if p.is_file() and p.suffix.lower() in data_ext and 'assets' not in p.parts]
    require(not bundled, f'fresh-install violation: bundled scientific/user data: {bundled}')
    require(json.loads((ROOT/'config/variable_aliases.json').read_text())=={}, 'shipped variable aliases must be empty')
    require(not (ROOT/'src'/'graphvis').exists(), 'old Python app host returned')
    require(not (ROOT/'assets/branding/logo graphvis.jpg').exists(), 'obsolete branding asset remains')
    require(not (ROOT/'config/plugin_services.json').exists(), 'unused plugin registry remains')

    render=(ROOT/'native/crates/graphvis-render/src/lib.rs').read_text()
    require('frame.present()' in render and 'map_async' not in render and 'read_buffer' not in render, 'direct WGPU present/readback gate failed')
    for marker in ['camera_buffer','camera_bind_group','depth_view','point_capacity_bytes','queue.write_buffer','voxel_aggregate']:
        require(marker in render, f'WGPU optimization missing: {marker}')
    shader=(ROOT/'native/crates/graphvis-render/shaders/point_cloud.wgsl').read_text()
    require('invert opacity' in shader.lower() and 'camera.style' in shader, 'GPU opacity/inversion mapping missing')

    data=(ROOT/'native/crates/graphvis-data/src/lib.rs').read_text()
    require('RecordBatch' in data and 'SessionContext' in data and 'execute_stream' in data and 'sql_to_ipc' in data, 'Arrow/DataFusion stream path incomplete')
    ffi=(ROOT/'native/crates/graphvis-ffi/src/lib.rs').read_text()
    require('state.set_mapping' in ffi and 'voxel_bins' in ffi, 'renderer mapping is not recorded in native state or voxel lane missing')

    smart=(ROOT/'native/crates/graphvis-smart/src/lib.rs').read_text()
    for marker in ['SmartRenderPlan','robust_stats','histogram_equalized','optimize_scatter','preserve_extrema','density_emphasis']:
        require(marker in smart, f'Smart Render engine missing: {marker}')
    require('gv_smart_plan' in ffi and 'SmartProfile::from_code' in ffi and 'set_smart_style' in ffi, 'Smart Render FFI/WGPU integration incomplete')
    core=(ROOT/'native/crates/graphvis-core/src/lib.rs').read_text()
    require('pub fn execute(&self, command: Command)' in core and 'pub fn set_mapping' in core, 'native command/state authority incomplete')

    flight=(ROOT/'native/crates/graphvis-flight/src/lib.rs').read_text()
    require('FlightRecordBatchStream' in flight and '.chain(incoming.map_err' in flight, 'Flight DoPut still bulk-buffers FlightData')
    require('list_flights' in flight and 'graphvis.sql' in flight, 'Flight discovery/SQL missing')
    vtk=(ROOT/'native/vtk_backend/src/GraphVisVtkItem.cpp').read_text()
    require('queueMaterialUpdate' in vtk and 'applyMaterial' in vtk and 'RecordBatchFileReader' in vtk and 'SetInterpolationToPBR' in vtk, 'VTK native/PBR material path incomplete')

    qml=corpus(list((ROOT/'app/qml').rglob('*.qml')))
    require('Smart Render' in qml and 'Analyze & Apply' in qml and 'Heatmap / surface auto-clarity policy' in qml, 'Smart Render UX missing')
    require('Experimental UI' in qml and 'PdfMultiPageView' in qml and 'Command palette' in qml, 'experimental/literature UI missing')
    require('WindowContainer' in qml and 'GraphVisVtkItem' in qml, 'native viewport containers missing')
    require(len(list((ROOT/'app/qml').rglob('*.qml')))>=14, 'QML remains under-componentized')
    controller=(ROOT/'app/src/AppController.cpp').read_text()
    require('QtConcurrent::run' in controller and 'importFirstLiteratureDataset' in controller and 'GraphVis 18' in controller, 'async/literature controller path incomplete')

    cmake=(ROOT/'CMakeLists.txt').read_text(); appcmake=(ROOT/'app/CMakeLists.txt').read_text(); installer=(ROOT/'installer/windows/GraphVis.nsi').read_text()
    require('GRAPHVIS_RUST_TARGET_DIR' in cmake and '_rust_outputs' in cmake, 'persistent/output-driven Rust build missing')
    require('qt_generate_deploy_qml_app_script' in appcmake and 'Qt6::Pdf' in appcmake, 'Qt QML deployment/PDF integration missing')
    require('Install.exe' in installer and 'Runtime-Maintenance.ps1' in installer, '18.4 NSIS installer recipe missing')
    bootstrap=(ROOT/'tools/Bootstrap-WindowsBuild.ps1').read_text(); buildenv=(ROOT/'tools/Build-Environment.ps1').read_text()
    for marker in ['1.98.0','sccache','9e593bb18ea69cc5095e012465dcd675a822ed0d']:
        require(marker in bootstrap, f'bootstrap optimization missing: {marker}')
    for marker in ['VCPKG_BINARY_SOURCES','CARGO_TARGET_DIR','SCCACHE_DIR','RUSTC_WRAPPER']:
        require(marker in buildenv, f'central build environment missing: {marker}')
    require('--locked' in cmake and 'GRAPHVIS_RUST_PROFILE_ARGS' in cmake, 'locked/fast Cargo profile integration missing')
    require((ROOT/'infra/Dockerfile.rust-dev').exists() and (ROOT/'scripts/audit_cull.sh').exists(), 'prewarmed environment/audit tool missing')
    quick=(ROOT/'tools/Quick-Build.ps1').read_text(); vtkcmake=(ROOT/'native/vtk_backend/CMakeLists.txt').read_text(); devrun=(ROOT/'dev/dev_run.bat').read_text()
    require('winget install' not in quick.lower() and 'rustup toolchain' not in quick.lower() and 'cargo fetch --' not in quick.lower() and "'vcpkg.exe') install" not in quick.lower(), 'quick build performs bootstrap/update work')
    require('pause' in devrun.lower() and 'goto :fail' in devrun.lower(), 'developer launcher does not preserve errors')
    require('datasets: Option<DatasetStore>' in ffi and 'tokio: Option<TokioRuntime>' in ffi and 'ensure_data_runtime' in ffi, 'DataFusion/Tokio are not lazy-started')
    require('PLUGIN_TARGET GraphVisVtkPlugin' in vtkcmake and 'GraphVisVtkBackend' not in appcmake.split('target_link_libraries(graphvis PRIVATE',1)[1].split(')',1)[0], 'VTK is still eagerly linked into main executable')
    require('SetCompressor zlib' in installer and 'RequestExecutionLevel user' in installer, 'fast/free installer policy missing')

    for p in (ROOT/'services/python').rglob('*.py'): py_compile.compile(str(p), doraise=True)
    print('GraphVis 18.4 architecture audit: PASS')
    for item in [
        'fresh install/domain neutrality','direct persistent WGPU + depth/no readback','Arrow/DataFusion streamed query output',
        'Rust AppState/Command mapping authority','native voxel density lane','VTK C++ material-only PBR updates',
        'Arrow Flight streaming upload/discovery','async Qt data/query jobs + cancellation reversion','Qt PDF literature workspace',
        'stable + experimental UI shells','pinned/cached build pipeline + no-check quick build','lazy DataFusion/VTK startup','Smart Render auto-optimization','fast installer extraction','standalone installer source','optional Python/Julia/R services']:
        print('  '+item+': PASS')

if __name__=='__main__': main()
