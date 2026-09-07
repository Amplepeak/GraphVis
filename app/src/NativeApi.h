#pragma once
#include <QLibrary>
#include <QString>
#include <memory>

class NativeApi final {
public:
    static NativeApi& instance();
    bool load();
    bool available() const { return loaded_; }
    QString error() const { return error_; }
    // The version the loaded library reports. A graphvis_ffi built from a
    // different source tree than the executable next to it is a real Windows
    // failure - an old DLL left in the install folder - and it presents as
    // inexplicable behaviour rather than as a load error, because every symbol
    // still resolves. Asking is cheap; guessing is not.
    QString nativeVersion() const;
    bool versionMatches() const;

    using FnVersion = char*(*)();
    using FnStringFree = void(*)(char*);
    using FnRuntimeNew = void*(*)(const char*);
    using FnRuntimeFree = void(*)(void*);
    using FnStateJson = char*(*)(void*);
    using FnImport = char*(*)(void*, const char*);
    using FnQuery = char*(*)(void*, const char*, const char*);
    using FnUndoRedo = bool(*)(void*);
    using FnRendererNew = void*(*)(qintptr,qintptr,unsigned,unsigned);
    using FnRendererFree = void(*)(void*);
    using FnRendererResize = void(*)(void*,unsigned,unsigned);
    using FnRendererRender = bool(*)(void*);
    using FnRendererCamera = void(*)(void*,float,float,float,float,float);
    using FnRendererStyle = void(*)(void*,float,float,bool);
    using FnSmartPlan = char*(*)(void*,const char*,const char*,unsigned);
    using FnRendererDataset = char*(*)(void*,void*,const char*,const char*,const char*,const char*,const char*,float,float,bool,unsigned,unsigned);

    FnVersion version{};
    FnStringFree stringFree{}; FnRuntimeNew runtimeNew{}; FnRuntimeFree runtimeFree{};
    FnStateJson stateJson{}; FnImport importDataset{}; FnQuery queryToIpc{}; FnUndoRedo undo{}; FnUndoRedo redo{};
    FnRendererNew rendererNew{}; FnRendererFree rendererFree{}; FnRendererResize rendererResize{};
    FnRendererRender rendererRender{}; FnRendererCamera rendererCamera{}; FnRendererStyle rendererStyle{}; FnSmartPlan smartPlan{}; FnRendererDataset rendererDataset{};

    QString takeString(char* raw) const;

private:
    NativeApi() = default;
    template<class T> bool resolve(T& out, const char* symbol) {
        out = reinterpret_cast<T>(library_.resolve(symbol));
        if (!out) { error_ = QStringLiteral("Missing native symbol: %1").arg(QString::fromLatin1(symbol)); return false; }
        return true;
    }
    QLibrary library_;
    bool loaded_ = false;
    QString error_;
};
