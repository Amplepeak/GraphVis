#include "NativeViewportWindow.h"
#include "NativeApi.h"
// QPlatformSurfaceEvent: the event that says the HWND is going away.
#include <QPlatformSurfaceEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QResizeEvent>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

NativeViewportWindow::NativeViewportWindow(QObject* parent):QWindow(){ setParent(qobject_cast<QWindow*>(parent)); setTitle(QStringLiteral("GraphVis Native WGPU Viewport")); }
NativeViewportWindow::~NativeViewportWindow(){ releaseRenderer(); }

// Let go of the native surface.
//
// THE RENDERER IS TIED TO AN HWND, and that HWND does not live as long as this
// object does. A QWindow's platform surface is destroyed and recreated whenever
// the window is reparented or its frame style changes - putting this window
// into a WindowContainer, which is exactly what selecting the Native WGPU
// renderer does, is a reparent. Nothing here listened for that, so the surface
// created against the first HWND went on being presented to after that HWND had
// been destroyed. Qt's own rule for a QWindow that renders natively is that the
// graphics resources must be released before the platform surface goes away;
// this is that release.
//
// The attempt latch is cleared with it, so the next expose creates a surface
// against the new window rather than refusing because an earlier one succeeded.
void NativeViewportWindow::releaseRenderer(){
    if(renderer_&&NativeApi::instance().rendererFree)
        NativeApi::instance().rendererFree(renderer_);
    renderer_=nullptr;
    attemptedRenderer_=false;
}

void NativeViewportWindow::ensureRenderer(){
    if(renderer_||!isExposed()) return;
    // ONE ATTEMPT. Not one per expose, resize and frame.
    //
    // This is called from exposeEvent, resizeEvent, requestNativeFrame and
    // mapDataset, and it returned without recording that it had already tried
    // - so a surface that cannot be created was re-attempted on every one of
    // those, for the life of the window. If the attempt is slow, that is a
    // stall on every frame; if it dies inside the driver, it dies again on
    // every frame, and a fault that might have been survivable once becomes an
    // unrecoverable loop.
    if(attemptedRenderer_) return;
    attemptedRenderer_=true;

    auto& api=NativeApi::instance();
    if(!api.available()){
        lastError_=QStringLiteral("The native library is not loaded: %1").arg(api.error());
        emit nativeStatusChanged(lastError_);
        return;
    }
    // Checked rather than assumed. NativeApi::load only reports success when
    // every symbol resolved, so this should never fire - but calling through a
    // null function pointer is not a failure that can be diagnosed afterwards,
    // and setPointStyle a few lines below has always checked its own pointer.
    // Whatever is true of one of them is true of all of them.
    if(!api.rendererNew||!api.rendererCamera||!api.rendererRender||!api.rendererResize){
        lastError_=QStringLiteral("The native library is missing a renderer entry point");
        emit nativeStatusChanged(lastError_);
        return;
    }
#ifdef Q_OS_WIN
    const auto hwnd=static_cast<qintptr>(winId()); const auto hinst=reinterpret_cast<qintptr>(GetModuleHandleW(nullptr));
    // A BREADCRUMB IMMEDIATELY BEFORE THE DANGEROUS CALL.
    //
    // gv_renderer_new_win32 goes into Rust and then into a graphics driver. A
    // panic crossing an FFI boundary, or a driver that takes the process down,
    // leaves nothing behind that says where it happened. This line is written
    // through the Qt message handler into startup.log, which the next launch
    // now preserves as startup.previous.log - so a session that dies in here
    // ends its log on this sentence instead of ending it in silence.
    qInfo("GraphVis: creating the native WGPU surface (%dx%d)",
          qMax(2,width()),qMax(2,height()));
    renderer_=api.rendererNew(hwnd,hinst,unsigned(qMax(2,width())),unsigned(qMax(2,height())));
    qInfo("GraphVis: native WGPU surface %s",renderer_?"created":"refused");
#else
    renderer_=nullptr;
    lastError_=QStringLiteral("The native WGPU viewport is implemented for Windows only");
#endif
    if(renderer_) { pushCamera(); emit nativeStatusChanged(QStringLiteral("Native WGPU direct presentation active")); }
    else {
        if(lastError_.isEmpty())
            lastError_=QStringLiteral("Unable to create a native WGPU surface on this display");
        emit nativeStatusChanged(lastError_);
    }
}
void NativeViewportWindow::exposeEvent(QExposeEvent*){ensureRenderer(); if(isExposed())requestNativeFrame();}
void NativeViewportWindow::resizeEvent(QResizeEvent* e){ensureRenderer();if(renderer_)NativeApi::instance().rendererResize(renderer_,qMax(2,e->size().width()),qMax(2,e->size().height()));requestNativeFrame();}
void NativeViewportWindow::requestNativeFrame(){ensureRenderer();if(renderer_&&isExposed())NativeApi::instance().rendererRender(renderer_);}
void NativeViewportWindow::pushCamera(){if(renderer_)NativeApi::instance().rendererCamera(renderer_,az_,el_,panX_,panY_,zoom_);}
void NativeViewportWindow::resetCamera(){az_=-40;el_=25;panX_=panY_=0;zoom_=1;pushCamera();requestNativeFrame();}
void NativeViewportWindow::setPointStyle(double pointSize,double opacity,bool invertOpacity){ if(renderer_&&NativeApi::instance().rendererStyle){ NativeApi::instance().rendererStyle(renderer_,float(pointSize),float(opacity),invertOpacity); requestNativeFrame(); } }

bool NativeViewportWindow::mapDataset(const QString& id,const QString& x,const QString& y,const QString& z,const QString& color,double size,double opacity,bool invert,unsigned voxelBins,unsigned smartProfile){
    ensureRenderer(); if(!renderer_||!runtime_) return false; auto& api=NativeApi::instance();
    const QByteArray a=id.toUtf8(),b=x.toUtf8(),c=y.toUtf8(),d=z.toUtf8(),f=color.toUtf8();
    const QString result=api.takeString(api.rendererDataset(renderer_,runtime_,a.constData(),b.constData(),c.constData(),d.constData(),f.constData(),float(size),float(opacity),invert,voxelBins,smartProfile));
    const auto doc=QJsonDocument::fromJson(result.toUtf8()); lastMappingResult_=doc.isObject()?doc.object().toVariantMap():QVariantMap{};
    if(result.contains(QStringLiteral("\"ok\":false"))){lastError_=result;emit nativeStatusChanged(result);return false;} requestNativeFrame();return true;
}

void NativeViewportWindow::mousePressEvent(QMouseEvent* e){lastMouse_=e->position();dragButton_=e->button();e->accept();}
void NativeViewportWindow::mouseReleaseEvent(QMouseEvent* e){dragButton_=Qt::NoButton;e->accept();}
void NativeViewportWindow::mouseMoveEvent(QMouseEvent* e){const QPointF delta=e->position()-lastMouse_;lastMouse_=e->position();const bool pan=(dragButton_==Qt::MiddleButton)||((e->modifiers()&Qt::ShiftModifier)&&dragButton_==Qt::LeftButton);if(pan){panX_+=float(delta.x()/qMax(200,width()))*2.f;panY_-=float(delta.y()/qMax(200,height()))*2.f;}else if(dragButton_==Qt::LeftButton){az_+=float(delta.x())*.45f;el_=qBound(-89.f,el_+float(delta.y())*.35f,89.f);}pushCamera();requestNativeFrame();e->accept();}
void NativeViewportWindow::wheelEvent(QWheelEvent* e){zoom_=qBound(.15f,zoom_*std::pow(1.0015f,float(e->angleDelta().y())),12.f);pushCamera();requestNativeFrame();e->accept();}
// ---------------------------------------------------------------------- touch
//
// Qt synthesises a mouse press and drag from a touch nobody accepted, which is
// why a single finger already rotated the camera. It synthesises nothing for a
// second finger, and this window's zoom is bound to wheelEvent and its pan to a
// middle button or Shift - so on a touchscreen two thirds of the camera was
// unreachable. Handling the touch directly costs one function and gives all
// three gestures.
bool NativeViewportWindow::handleTouch(QTouchEvent* e){
    if(e->type()==QEvent::TouchEnd||e->type()==QEvent::TouchCancel){
        touch_.reset(); return true;
    }
    const graphvis::TouchGesture::Step step=
        touch_.update(e->points(),e->type()==QEvent::TouchBegin);
    if(!step.usable) return true;

    if(step.fingers==1){
        az_+=float(step.movement.x())*.45f;
        el_=qBound(-89.f,el_+float(step.movement.y())*.35f,89.f);
    }else{
        zoom_=qBound(.15f,zoom_*float(step.scale),12.f);
        panX_+=float(step.movement.x()/qMax(200,width()))*2.f;
        panY_-=float(step.movement.y()/qMax(200,height()))*2.f;
    }
    pushCamera(); requestNativeFrame();
    return true;
}

// A precision touchpad, and a touchscreen on the Windows versions that report
// a pinch as a gesture rather than as touch points. Same camera, same limits.
bool NativeViewportWindow::handleNativeGesture(QNativeGestureEvent* e){
    switch(e->gestureType()){
    case Qt::ZoomNativeGesture:
        zoom_=qBound(.15f,zoom_*float(1.0+e->value()),12.f);
        break;
    case Qt::RotateNativeGesture:
        az_+=float(e->value());
        break;
    case Qt::PanNativeGesture:
        panX_+=float(e->delta().x()/qMax(200,width()))*2.f;
        panY_-=float(e->delta().y()/qMax(200,height()))*2.f;
        break;
    case Qt::SmartZoomNativeGesture:
        resetCamera();
        return true;
    default:
        return false;
    }
    pushCamera(); requestNativeFrame();
    return true;
}

bool NativeViewportWindow::event(QEvent* e){
    switch(e->type()){
    case QEvent::UpdateRequest:
        requestNativeFrame();
        return true;
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
        // Accepting the touch is also what stops Qt synthesising a mouse drag
        // from the same fingers, which would rotate the camera a second time.
        e->accept();
        return handleTouch(static_cast<QTouchEvent*>(e));
    case QEvent::NativeGesture:
        if(handleNativeGesture(static_cast<QNativeGestureEvent*>(e))){ e->accept(); return true; }
        break;
    case QEvent::PlatformSurface:
        // The one event that says the HWND underneath is going away. See
        // releaseRenderer: without this the native surface outlived the window
        // it was created against.
        if(static_cast<QPlatformSurfaceEvent*>(e)->surfaceEventType()
           ==QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed){
            qInfo("GraphVis: native viewport surface about to be destroyed; "
                  "releasing the WGPU renderer");
            releaseRenderer();
        }
        break;
    default:
        break;
    }
    return QWindow::event(e);
}
