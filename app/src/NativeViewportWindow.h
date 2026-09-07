#pragma once
#include <QWindow>
#include <QElapsedTimer>
#include <QPointF>
#include <QVariantMap>

class NativeViewportWindow final : public QWindow {
    Q_OBJECT
public:
    explicit NativeViewportWindow(QObject* parent=nullptr);
    ~NativeViewportWindow() override;
    void setRuntime(void* runtime) { runtime_=runtime; }
    Q_INVOKABLE bool mapDataset(const QString& datasetId,const QString& x,const QString& y,const QString& z,const QString& color,double pointSize,double opacity,bool invertOpacity,unsigned voxelBins=0,unsigned smartProfile=0);
    Q_INVOKABLE void resetCamera();
    Q_INVOKABLE void setPointStyle(double pointSize,double opacity,bool invertOpacity);
    Q_INVOKABLE void requestNativeFrame();
    QString lastError() const { return lastError_; }
    QVariantMap lastMappingResult() const { return lastMappingResult_; }
signals:
    void nativeStatusChanged(const QString& status);
protected:
    bool event(QEvent* e) override;
    void exposeEvent(QExposeEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    // Without this dragButton_ is never cleared, and because QWindow delivers
    // move events with no button held, merely moving the pointer across the
    // viewport kept rotating the camera long after the drag had ended - each
    // move doing a pushCamera() and a full re-render.
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    void ensureRenderer();
    void pushCamera();
    void* runtime_{}; void* renderer_{};
    float az_=-40.f, el_=25.f, panX_=0.f, panY_=0.f, zoom_=1.f;
    QPointF lastMouse_; Qt::MouseButton dragButton_=Qt::NoButton; QString lastError_; QVariantMap lastMappingResult_;
};
