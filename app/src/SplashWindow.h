#pragma once
#include <QElapsedTimer>
#include <QImage>
#include <QRasterWindow>
#include <QString>

namespace graphvis {

// The startup splash.
//
// It has to be a window of its own, not a QML overlay, because the time it
// exists to cover is spent before any QML exists: loading the 318-entry graph
// catalogue, opening the native Rust core, probing for the science add-on and
// reopening the last project all happen while AppController is being
// constructed. An overlay inside the main window could only appear after all of
// that was already done, which would make the progress bar a decoration.
//
// QRasterWindow rather than QSplashScreen: the latter lives in QtWidgets, and
// pulling the whole widget stack into a Qt Quick application to draw one
// picture is not a trade worth making.
class SplashWindow : public QRasterWindow {
public:
    SplashWindow();

    // Repaints and pumps the event loop, so the bar actually moves during a
    // long stage rather than jumping at the end.
    void setStage(const QString& text,double fraction);
    void finish();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage logo_;
    QString stage_;
    double fraction_=0.0;
    QElapsedTimer shown_;
};

} // namespace graphvis
