#include "NativeViewportWindow.h"
#include "NativeApi.h"
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

NativeViewportWindow::NativeViewportWindow(QObject* parent):QWindow(){ setParent(qobject_cast<QWindow*>(parent)); setTitle(QStringLiteral("GraphVis Native WGPU Viewport")); }
NativeViewportWindow::~NativeViewportWindow(){ if(renderer_&&NativeApi::instance().rendererFree) NativeApi::instance().rendererFree(renderer_); }

void NativeViewportWindow::ensureRenderer(){
    if(renderer_||!isExposed()) return;
    auto& api=NativeApi::instance(); if(!api.available()) return;
#ifdef Q_OS_WIN
    const auto hwnd=static_cast<qintptr>(winId()); const auto hinst=reinterpret_cast<qintptr>(GetModuleHandleW(nullptr));
    renderer_=api.rendererNew(hwnd,hinst,qMax(2,width()),qMax(2,height()));
#else
    renderer_=nullptr;
#endif
    if(renderer_) { pushCamera(); emit nativeStatusChanged(QStringLiteral("Native WGPU direct presentation active")); }
    else { lastError_=QStringLiteral("Unable to create native WGPU surface"); emit nativeStatusChanged(lastError_); }
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
bool NativeViewportWindow::event(QEvent* e){if(e->type()==QEvent::UpdateRequest){requestNativeFrame();return true;}return QWindow::event(e);}
