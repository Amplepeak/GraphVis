//! Stable C ABI used by the Qt/QML desktop shell.
//! Large arrays never cross this ABI.  Dataset imports become Arrow record
//! batches inside Rust and WGPU consumes normalized point buffers directly.

use graphvis_core::{AppState, VariableMapping};
use graphvis_data::{DatasetStore, QueryEngine, numeric_column};
use graphvis_render::{CameraState, NativeSurfaceRenderer, PointVertex, voxel_aggregate};
use graphvis_smart::{SmartProfile, analyze as analyze_smart, optimize_scatter};
use parking_lot::Mutex;
use std::{
    ffi::{CStr, CString, c_char, c_void},
    panic::{AssertUnwindSafe, catch_unwind},
    path::PathBuf,
    ptr,
};
use tokio::runtime::Runtime as TokioRuntime;
use uuid::Uuid;

/// Everything behind one lock.
///
/// The Qt shell calls into this from two threads at once: imports and SQL
/// queries run on QtConcurrent workers while the GUI thread asks for state,
/// undo/redo and smart plans. `rt()` used to hand out an unbounded-lifetime
/// `&mut GraphVisRuntime` from the raw pointer with no synchronisation at all.
/// `AppState` is internally locked, but these `Option` FIELDS were not - so an
/// import and a query starting together both saw `tokio.is_none()`, both built
/// a Runtime, and the second assignment dropped the one the first thread was
/// still `block_on`-ing. That is a use-after-free, and it presented as a random
/// crash when a query was run during an import.
pub struct GraphVisRuntime {
    inner: Mutex<RuntimeInner>,
}

struct RuntimeInner {
    state: AppState,
    datasets: Option<DatasetStore>,
    query: Option<QueryEngine>,
    tokio: Option<TokioRuntime>,
    cache_dir: PathBuf,
}

/// Runs `f` and turns a panic into an error string instead of letting it
/// unwind out of `extern "C"`, which is undefined behaviour.
///
/// This is not theoretical: every import runs Arrow/DataFusion parsing over a
/// file the user chose, so one out-of-bounds index or `unwrap` in that stack
/// took the whole application down with no dialog and no log line. The crate
/// builds with `panic = "unwind"` for exactly this reason - under `abort` these
/// guards cannot catch anything.
fn guarded<F>(f: F) -> *mut c_char
where
    F: FnOnce() -> Result<String, String>,
{
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(Ok(s)) => cstring(s),
        Ok(Err(e)) => error_json(e),
        Err(_) => error_json("the native core panicked - the file may be malformed or unsupported"),
    }
}

fn guarded_bool<F>(f: F) -> bool
where
    F: FnOnce() -> bool,
{
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(false)
}

fn cstr(p: *const c_char) -> Result<String, String> {
    if p.is_null() {
        return Err("null string".into());
    };
    unsafe { CStr::from_ptr(p) }
        .to_str()
        .map(|s| s.to_string())
        .map_err(|e| e.to_string())
}
fn cstring(s: String) -> *mut c_char {
    CString::new(s.replace('\0', " ")).unwrap().into_raw()
}
fn error_json(message: impl ToString) -> *mut c_char {
    cstring(serde_json::json!({"ok":false,"error":message.to_string()}).to_string())
}

#[unsafe(no_mangle)]
pub extern "C" fn gv_version() -> *mut c_char {
    cstring(env!("CARGO_PKG_VERSION").into())
}
/// Free a string returned by any other gv_* function.
///
/// # Safety
///
/// `p` must be null, or a pointer this library returned and that has not
/// already been freed. Passing anything else - a C string this library did not
/// allocate, or one freed twice - is undefined behaviour.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gv_string_free(p: *mut c_char) {
    if !p.is_null() {
        unsafe { drop(CString::from_raw(p)) };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn gv_runtime_new(cache_dir: *const c_char) -> *mut c_void {
    let Ok(cache) = cstr(cache_dir) else {
        return ptr::null_mut();
    };
    // An unwritable cache directory used to be swallowed here and resurfaced
    // much later as an unrelated import failure.
    if std::fs::create_dir_all(&cache).is_err() {
        return ptr::null_mut();
    }
    // Keep application startup lightweight: the Tokio/DataFusion/data stack is
    // created lazily on first import/query instead of spawning worker threads
    // during every GraphVis launch.
    Box::into_raw(Box::new(GraphVisRuntime {
        inner: Mutex::new(RuntimeInner {
            state: AppState::default(),
            datasets: None,
            query: None,
            tokio: None,
            cache_dir: PathBuf::from(cache),
        }),
    })) as *mut c_void
}

// Guarded because dropping a Tokio Runtime can itself panic (notably if it is
// dropped from inside an async context), and this runs during application
// shutdown where an abort looks to the user like a crash on exit.
#[unsafe(no_mangle)]
pub extern "C" fn gv_runtime_free(p: *mut c_void) {
    if !p.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
            drop(Box::from_raw(p as *mut GraphVisRuntime));
        }));
    }
}

/// A SHARED reference - the interior `Mutex` is what makes concurrent calls
/// safe. Never hand out `&mut` from a pointer the Qt side uses from several
/// threads.
fn rt<'a>(p: *mut c_void) -> Result<&'a GraphVisRuntime, String> {
    if p.is_null() {
        Err("runtime is null".into())
    } else {
        Ok(unsafe { &*(p as *const GraphVisRuntime) })
    }
}

fn ensure_data_runtime(inner: &mut RuntimeInner) -> Result<(), String> {
    if inner.tokio.is_none() {
        inner.tokio = Some(TokioRuntime::new().map_err(|e| e.to_string())?);
    }
    if inner.datasets.is_none() {
        inner.datasets = Some(DatasetStore::default());
    }
    if inner.query.is_none() {
        inner.query = Some(QueryEngine::default());
    }
    Ok(())
}

#[unsafe(no_mangle)]
pub extern "C" fn gv_state_json(p: *mut c_void) -> *mut c_char {
    guarded(|| {
        let g = rt(p)?.inner.lock();
        serde_json::to_string(&g.state.snapshot()).map_err(|e| e.to_string())
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn gv_import_dataset(p: *mut c_void, path: *const c_char) -> *mut c_char {
    guarded(|| -> Result<String, String> {
        let runtime = rt(p)?;
        let mut guard = runtime.inner.lock();
        // Reborrow once so the field borrows below are seen as disjoint.
        let inner = &mut *guard;
        ensure_data_runtime(inner)?;
        let path = PathBuf::from(cstr(path)?);
        let ext = path
            .extension()
            .and_then(|x| x.to_str())
            .unwrap_or("")
            .to_ascii_lowercase();
        let cache_dir = inner.cache_dir.clone();
        let ds=match ext.as_str(){
            "arrow"|"ipc"=>inner.datasets.as_mut().ok_or("dataset runtime unavailable")?.import_arrow_ipc(&path,&cache_dir),
            "csv"|"tsv"=>{
                let tokio=inner.tokio.as_ref().ok_or("async runtime unavailable")?;
                let datasets=inner.datasets.as_mut().ok_or("dataset runtime unavailable")?;
                tokio.block_on(datasets.import_csv(&path,&cache_dir))
            }
            "parquet"=>{
                let tokio=inner.tokio.as_ref().ok_or("async runtime unavailable")?;
                let datasets=inner.datasets.as_mut().ok_or("dataset runtime unavailable")?;
                tokio.block_on(datasets.import_parquet(&path,&cache_dir))
            }
            _=>return Err(format!("native core accepts Arrow IPC, CSV/TSV and Parquet directly; '{}' should be imported through an optional scientific IO plugin that emits Arrow IPC",ext)),
        }.map_err(|e|e.to_string())?;
        inner
            .query
            .as_mut()
            .ok_or("query runtime unavailable")?
            .register_dataset(&sanitize_name(&ds.meta.name), &ds)
            .map_err(|e| e.to_string())?;
        inner.state.add_dataset(ds.meta.clone());
        serde_json::to_string(&ds.meta).map_err(|e| e.to_string())
    })
}

// A table name DataFusion will accept. Two independent rules, not an
// `else if`: an empty name becomes "dataset", and a name that then starts with
// a digit gets a letter in front. Packed onto one line this read as broken
// control flow to clippy, and to a reader.
fn sanitize_name(s: &str) -> String {
    let mut out: String = s
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() { c } else { '_' })
        .collect();
    if out.is_empty() {
        out = "dataset".into();
    }
    // Safe to unwrap: the branch above guarantees at least one character.
    if out.chars().next().unwrap().is_ascii_digit() {
        out.insert(0, 'd');
    }
    out
}

#[unsafe(no_mangle)]
pub extern "C" fn gv_query_to_ipc(
    p: *mut c_void,
    sql: *const c_char,
    output: *const c_char,
) -> *mut c_char {
    guarded(|| -> Result<String, String> {
        let runtime = rt(p)?;
        let mut guard = runtime.inner.lock();
        let inner = &mut *guard;
        ensure_data_runtime(inner)?;
        let query = cstr(sql)?;
        let output = PathBuf::from(cstr(output)?);
        let tokio = inner.tokio.as_ref().ok_or("async runtime unavailable")?;
        let engine = inner.query.as_ref().ok_or("query runtime unavailable")?;
        let (batches, rows) = tokio
            .block_on(engine.sql_to_ipc(&query, &output))
            .map_err(|e| e.to_string())?;
        Ok(serde_json::json!({"ok":true,"path":output,"batches":batches,"rows":rows,"streamed":true}).to_string())
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn gv_undo(p: *mut c_void) -> bool {
    guarded_bool(|| rt(p).map(|r| r.inner.lock().state.undo()).unwrap_or(false))
}
#[unsafe(no_mangle)]
pub extern "C" fn gv_redo(p: *mut c_void) -> bool {
    guarded_bool(|| rt(p).map(|r| r.inner.lock().state.redo()).unwrap_or(false))
}

#[unsafe(no_mangle)]
pub extern "C" fn gv_renderer_new_win32(
    hwnd: isize,
    hinstance: isize,
    width: u32,
    height: u32,
) -> *mut c_void {
    // wgpu adapter/surface creation runs graphics-driver code; a panic there
    // must not take the application with it.
    catch_unwind(AssertUnwindSafe(|| {
        match unsafe { NativeSurfaceRenderer::from_win32(hwnd, hinstance, width, height) } {
            Ok(r) => Box::into_raw(Box::new(r)) as *mut c_void,
            Err(_) => ptr::null_mut(),
        }
    }))
    .unwrap_or(ptr::null_mut())
}
#[unsafe(no_mangle)]
pub extern "C" fn gv_renderer_free(p: *mut c_void) {
    if !p.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
            drop(Box::from_raw(p as *mut NativeSurfaceRenderer));
        }));
    }
}
fn renderer<'a>(p: *mut c_void) -> Option<&'a mut NativeSurfaceRenderer> {
    if p.is_null() {
        None
    } else {
        Some(unsafe { &mut *(p as *mut NativeSurfaceRenderer) })
    }
}
#[unsafe(no_mangle)]
pub extern "C" fn gv_renderer_resize(p: *mut c_void, w: u32, h: u32) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if let Some(r) = renderer(p) {
            r.resize(w, h)
        }
    }));
}
#[unsafe(no_mangle)]
pub extern "C" fn gv_renderer_render(p: *mut c_void) -> bool {
    guarded_bool(|| renderer(p).map(|r| r.render().is_ok()).unwrap_or(false))
}
#[unsafe(no_mangle)]
pub extern "C" fn gv_renderer_camera(
    p: *mut c_void,
    az: f32,
    el: f32,
    pan_x: f32,
    pan_y: f32,
    zoom: f32,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if let Some(r) = renderer(p) {
            r.set_camera(CameraState {
                azimuth_deg: az,
                elevation_deg: el,
                pan_x,
                pan_y,
                zoom,
            })
        }
    }));
}
#[unsafe(no_mangle)]
pub extern "C" fn gv_renderer_style(
    p: *mut c_void,
    point_size: f32,
    opacity: f32,
    invert_opacity: bool,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if let Some(r) = renderer(p) {
            r.set_style(point_size, opacity, invert_opacity)
        }
    }));
}

#[unsafe(no_mangle)]
pub extern "C" fn gv_smart_plan(
    p: *mut c_void,
    dataset_id: *const c_char,
    color: *const c_char,
    profile_code: u32,
) -> *mut c_char {
    guarded(|| -> Result<String, String> {
        let runtime = rt(p)?;
        let mut guard = runtime.inner.lock();
        let inner = &mut *guard;
        ensure_data_runtime(inner)?;
        let id = Uuid::parse_str(&cstr(dataset_id)?).map_err(|e| e.to_string())?;
        let ds = inner
            .datasets
            .as_ref()
            .ok_or("dataset runtime unavailable")?
            .get(id)
            .ok_or("dataset not found")?;
        let cn = cstr(color)?;
        let cv = numeric_column(&ds, &cn).map_err(|e| e.to_string())?;
        let profile = SmartProfile::from_code(profile_code)
            .ok_or("smart profile must be 1=balanced, 2=clarity or 3=performance")?;
        let plan = analyze_smart(&cv, cv.len(), profile)
            .ok_or("response column contains no finite data")?;
        serde_json::to_string(&plan).map_err(|e| e.to_string())
    })
}

fn smart_colormap_id(name: &str) -> u32 {
    match name {
        "Viridis" => 1,
        "Plasma" => 2,
        "CoolWarm" => 3,
        _ => 0,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn gv_renderer_set_dataset(
    renderer_ptr: *mut c_void,
    runtime_ptr: *mut c_void,
    dataset_id: *const c_char,
    x: *const c_char,
    y: *const c_char,
    z: *const c_char,
    color: *const c_char,
    point_size: f32,
    opacity: f32,
    invert_opacity: bool,
    voxel_bins: u32,
    smart_profile: u32,
) -> *mut c_char {
    guarded(|| -> Result<String, String> {
        let r = renderer(renderer_ptr).ok_or("renderer unavailable")?;
        let runtime = rt(runtime_ptr)?;
        let mut guard = runtime.inner.lock();
        let inner = &mut *guard;
        ensure_data_runtime(inner)?;
        let id = Uuid::parse_str(&cstr(dataset_id)?).map_err(|e| e.to_string())?;
        let ds = inner
            .datasets
            .as_ref()
            .ok_or("dataset runtime unavailable")?
            .get(id)
            .ok_or("dataset not found")?;
        let xn = cstr(x)?;
        let yn = cstr(y)?;
        let zn = cstr(z)?;
        let cn = cstr(color)?;
        let xv = numeric_column(&ds, &xn).map_err(|e| e.to_string())?;
        let yv = numeric_column(&ds, &yn).map_err(|e| e.to_string())?;
        let zv = numeric_column(&ds, &zn).map_err(|e| e.to_string())?;
        let cv = numeric_column(&ds, &cn).map_err(|e| e.to_string())?;
        let n = xv.len().min(yv.len()).min(zv.len()).min(cv.len());
        let extent = |v: &[f64]| {
            let mut lo = f64::INFINITY;
            let mut hi = f64::NEG_INFINITY;
            for &q in v {
                if q.is_finite() {
                    lo = lo.min(q);
                    hi = hi.max(q)
                }
            }
            (lo, hi)
        };
        let (xl, xh) = extent(&xv[..n]);
        let (yl, yh) = extent(&yv[..n]);
        let (zl, zh) = extent(&zv[..n]);
        let (cl, ch) = extent(&cv[..n]);
        if ![xl, xh, yl, yh, zl, zh, cl, ch]
            .iter()
            .all(|q| q.is_finite())
        {
            return Err("mapped columns do not contain finite extents".into());
        }
        let norm = |q: f64, lo: f64, hi: f64| -> f32 {
            (((q - lo) / (hi - lo).max(f64::EPSILON)) * 2.0 - 1.0) as f32
        };

        let (points, aggregation, applied_size, applied_opacity, smart_json) =
            if let Some(profile) = SmartProfile::from_code(smart_profile) {
                let plan = analyze_smart(&cv[..n], n, profile)
                    .ok_or("Smart Render could not analyze the mapped response")?;
                let optimized = optimize_scatter(&xv[..n], &yv[..n], &zv[..n], &cv[..n], &plan);
                let pts = optimized
                    .into_iter()
                    .map(|p| PointVertex {
                        position: [norm(p.x, xl, xh), norm(p.y, yl, yh), norm(p.z, zl, zh)],
                        response: p.mapped_response,
                        size: p.size,
                        alpha: p.alpha,
                    })
                    .collect::<Vec<_>>();
                r.set_smart_style(
                    plan.point_size,
                    plan.base_opacity,
                    invert_opacity,
                    plan.minimum_alpha,
                    smart_colormap_id(&plan.colormap),
                );
                let json = serde_json::to_value(&plan).map_err(|e| e.to_string())?;
                (
                    pts,
                    format!("smart:{:?}:lod={}", profile, plan.target_points),
                    plan.point_size,
                    plan.base_opacity,
                    Some(json),
                )
            } else if voxel_bins >= 2 {
                let bins = voxel_bins.clamp(2, 128) as usize;
                let cells =
                    voxel_aggregate(&xv[..n], &yv[..n], &zv[..n], &cv[..n], [bins, bins, bins]);
                let pts = cells
                    .into_iter()
                    .map(|cell| PointVertex {
                        position: [
                            norm(cell.center[0], xl, xh),
                            norm(cell.center[1], yl, yh),
                            norm(cell.center[2], zl, zh),
                        ],
                        response: ((cell.mean - cl) / (ch - cl).max(f64::EPSILON)).clamp(0.0, 1.0)
                            as f32,
                        size: (1.0 + (cell.count as f32).ln_1p() * 0.15).min(4.0),
                        alpha: 1.0,
                    })
                    .collect::<Vec<_>>();
                r.set_style(point_size, opacity, invert_opacity);
                (pts, format!("voxel:{}^3", bins), point_size, opacity, None)
            } else {
                let mut pts = Vec::with_capacity(n);
                for i in 0..n {
                    if [xv[i], yv[i], zv[i], cv[i]].iter().all(|q| q.is_finite()) {
                        pts.push(PointVertex {
                            position: [
                                norm(xv[i], xl, xh),
                                norm(yv[i], yl, yh),
                                norm(zv[i], zl, zh),
                            ],
                            response: ((cv[i] - cl) / (ch - cl).max(f64::EPSILON)).clamp(0.0, 1.0)
                                as f32,
                            size: 1.0,
                            alpha: 1.0,
                        });
                    }
                }
                r.set_style(point_size, opacity, invert_opacity);
                (pts, "raw".to_string(), point_size, opacity, None)
            };
        r.set_points(&points);
        let mapping = VariableMapping {
            x: Some(xn),
            y: Some(yn),
            z: Some(zn),
            color: Some(cn),
            fifth: None,
            fifth_role: None,
            invert_opacity,
        };
        let plot_id = inner.state.set_mapping(
            id,
            mapping,
            applied_size,
            applied_opacity,
            aggregation.clone(),
        );
        Ok(serde_json::json!({"ok":true,"points":points.len(),"source_points":n,"dataset":ds.meta.name,"plot_id":plot_id,"aggregation":aggregation,"smart_plan":smart_json}).to_string())
    })
}
