#include "NativeApi.h"
#include <QCoreApplication>
#include <QDir>

NativeApi& NativeApi::instance(){ static NativeApi api; return api; }

bool NativeApi::load(){
    if (loaded_) return true;
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("graphvis_ffi.dll");
#elif defined(Q_OS_MACOS)
    const QString name = QStringLiteral("libgraphvis_ffi.dylib");
#else
    const QString name = QStringLiteral("libgraphvis_ffi.so");
#endif
    QStringList candidates{QCoreApplication::applicationDirPath()+QDir::separator()+name,
                           QCoreApplication::applicationDirPath()+QStringLiteral("/native/")+name,
                           name};
    for (const auto& path : candidates) { library_.setFileName(path); if (library_.load()) break; }
    if (!library_.isLoaded()) { error_ = library_.errorString(); return false; }
    bool ok=true;
#define GV_RESOLVE(member,symbol) ok = resolve(member,symbol) && ok
    // Optional, and deliberately not folded into `ok`: a library that predates
    // this symbol still works, and refusing to load one over a version string
    // would be a worse failure than the mismatch it is meant to warn about.
    version=reinterpret_cast<FnVersion>(library_.resolve("gv_version"));
    GV_RESOLVE(stringFree,"gv_string_free"); GV_RESOLVE(runtimeNew,"gv_runtime_new"); GV_RESOLVE(runtimeFree,"gv_runtime_free");
    GV_RESOLVE(stateJson,"gv_state_json"); GV_RESOLVE(importDataset,"gv_import_dataset"); GV_RESOLVE(queryToIpc,"gv_query_to_ipc");
    GV_RESOLVE(undo,"gv_undo"); GV_RESOLVE(redo,"gv_redo"); GV_RESOLVE(rendererNew,"gv_renderer_new_win32");
    GV_RESOLVE(rendererFree,"gv_renderer_free"); GV_RESOLVE(rendererResize,"gv_renderer_resize"); GV_RESOLVE(rendererRender,"gv_renderer_render");
    GV_RESOLVE(rendererCamera,"gv_renderer_camera"); GV_RESOLVE(rendererStyle,"gv_renderer_style"); GV_RESOLVE(smartPlan,"gv_smart_plan"); GV_RESOLVE(rendererDataset,"gv_renderer_set_dataset");
#undef GV_RESOLVE
    loaded_=ok; return ok;
}

QString NativeApi::nativeVersion() const {
    if (!version) return {};
    return takeString(version());
}

bool NativeApi::versionMatches() const {
    const QString reported=nativeVersion();
    // No answer is not a mismatch: an older library that predates gv_version
    // is a question this cannot answer, and claiming a mismatch would be worse
    // than saying nothing.
    if (reported.isEmpty()) return true;
    return reported==QCoreApplication::applicationVersion();
}

QString NativeApi::takeString(char* raw) const {
    if (!raw) return {};
    const QString s=QString::fromUtf8(raw);
    if (stringFree) stringFree(raw);
    return s;
}
