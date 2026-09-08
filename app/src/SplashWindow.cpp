#include "SplashWindow.h"

#include "ReefScene.h"

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QScreen>
#include <QThread>

namespace graphvis {
namespace {
constexpr int kWidth=520;
constexpr int kHeight=330;
// Long enough that the splash reads as deliberate rather than as a flicker,
// short enough that a fast start is still a fast start.
constexpr qint64 kMinimumVisibleMs=650;
}

SplashWindow::SplashWindow(){
    setFlags(Qt::SplashScreen|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);
    resize(kWidth,kHeight);

    logo_.load(QStringLiteral(":/qt/qml/GraphVis/assets/graphvis_icon.png"));

    // One of a hundred reefs, drawn from a seed rather than loaded from a
    // hundred image files. The otter is untouched - it is the logo, and it is
    // drawn over the reef exactly as it always was.
    reefSeed_=QRandomGenerator::global()->bounded(int(kReefSceneCount));

    // Centred on the screen under the pointer, for the same reason the main
    // window is: centring on the virtual desktop straddles the bezel.
    QScreen* screen=QGuiApplication::screenAt(QCursor::pos());
    if(!screen) screen=QGuiApplication::primaryScreen();
    if(screen){
        const QRect area=screen->availableGeometry();
        setPosition(area.x()+(area.width()-kWidth)/2,area.y()+(area.height()-kHeight)/2);
    }
    shown_.start();
}

void SplashWindow::setStage(const QString& text,double fraction){
    stage_=text;
    fraction_=qBound(0.0,fraction,1.0);
    requestUpdate();
    // The startup path is a straight line of blocking calls, so without pumping
    // here the window would never paint at all.
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void SplashWindow::finish(){
    while(shown_.elapsed()<kMinimumVisibleMs){
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QThread::msleep(16);
    }
    close();
}

void SplashWindow::paintEvent(QPaintEvent*){
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing,true);
    p.setRenderHint(QPainter::SmoothPixmapTransform,true);

    const QRectF box(0,0,width(),height());

    // Deep water behind the otter, so the artwork sits on something rather than
    // floating on a grey rectangle.
    QPainterPath rounded;
    rounded.addRoundedRect(box,14,14);
    p.save();
    p.setClipPath(rounded);
    drawReefScene(p,box,reefSeed_);
    p.restore();

    // A scrim under the writing. The reef's floor is pale sand and the text is
    // pale blue: without this the progress bar and the percentage sit on a
    // light background in the colours chosen for a dark one, which is the one
    // way a splash screen can be genuinely unreadable.
    QLinearGradient scrim(QPointF(0,box.height()*0.44),QPointF(0,box.bottom()));
    scrim.setColorAt(0.0,QColor(0x05,0x12,0x1a,0));
    scrim.setColorAt(0.45,QColor(0x05,0x12,0x1a,190));
    scrim.setColorAt(1.0,QColor(0x05,0x12,0x1a,225));
    p.save();
    p.setClipPath(rounded);
    p.fillRect(box,scrim);
    p.restore();

    p.setPen(QPen(QColor(0x27,0x63,0x72),1.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(rounded);

    if(!logo_.isNull()){
        const int side=152;
        const QRect target(int(box.center().x()-side/2.0),34,side,side);
        p.drawImage(target,logo_);
    }

    p.setPen(QColor(0xe3,0xf2,0xf5));
    QFont title=p.font();
    title.setPointSizeF(19.0);
    title.setBold(true);
    p.setFont(title);
    p.drawText(QRectF(0,196,width(),30),Qt::AlignHCenter|Qt::AlignVCenter,
               QStringLiteral("GraphVis 18"));

    p.setPen(QColor(0xa9,0xc9,0xd1));
    QFont small=p.font();
    small.setPointSizeF(9.5);
    small.setBold(false);
    p.setFont(small);
    p.drawText(QRectF(0,224,width(),20),Qt::AlignHCenter|Qt::AlignVCenter,
               stage_.isEmpty()?QStringLiteral("Starting"):stage_);

    const QRectF track(60,266,width()-120,7);
    QPainterPath trackPath;
    trackPath.addRoundedRect(track,3.5,3.5);
    p.fillPath(trackPath,QColor(0x1b,0x44,0x50));

    if(fraction_>0.0){
        QRectF fill=track;
        fill.setWidth(qMax(7.0,track.width()*fraction_));
        QPainterPath fillPath;
        fillPath.addRoundedRect(fill,3.5,3.5);
        QLinearGradient bar(fill.topLeft(),fill.topRight());
        bar.setColorAt(0.0,QColor(0x3f,0xbf,0xae));
        bar.setColorAt(1.0,QColor(0x4f,0x9d,0xf7));
        p.fillPath(fillPath,bar);
    }

    p.setPen(QColor(0x7b,0xa3,0xad));
    small.setPointSizeF(8.5);
    p.setFont(small);
    p.drawText(QRectF(0,288,width(),18),Qt::AlignHCenter|Qt::AlignVCenter,
               QStringLiteral("%1%").arg(int(fraction_*100.0)));
}

} // namespace graphvis
