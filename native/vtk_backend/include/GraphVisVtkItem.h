#pragma once
#include <QQuickVTKItem.h>
#include <QString>
#include <qqmlintegration.h>
#include <limits>

class GraphVisVtkItem : public QQuickVTKItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString arrowPath READ arrowPath WRITE setArrowPath NOTIFY sceneChanged)
    Q_PROPERTY(QString xColumn READ xColumn WRITE setXColumn NOTIFY sceneChanged)
    Q_PROPERTY(QString yColumn READ yColumn WRITE setYColumn NOTIFY sceneChanged)
    Q_PROPERTY(QString zColumn READ zColumn WRITE setZColumn NOTIFY sceneChanged)
    Q_PROPERTY(QString colorColumn READ colorColumn WRITE setColorColumn NOTIFY sceneChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY sceneChanged)
    Q_PROPERTY(double roughness READ roughness WRITE setRoughness NOTIFY sceneChanged)
    Q_PROPERTY(double metallic READ metallic WRITE setMetallic NOTIFY sceneChanged)
    Q_PROPERTY(double specular READ specular WRITE setSpecular NOTIFY sceneChanged)
    Q_PROPERTY(double smartPointSize READ smartPointSize WRITE setSmartPointSize NOTIFY sceneChanged)
    Q_PROPERTY(double scalarMin READ scalarMin WRITE setScalarMin NOTIFY sceneChanged)
    Q_PROPERTY(double scalarMax READ scalarMax WRITE setScalarMax NOTIFY sceneChanged)
public:
    explicit GraphVisVtkItem(QQuickItem* parent=nullptr);
    vtkUserData initializeVTK(vtkRenderWindow* renderWindow) override;
    QString arrowPath()const{return arrowPath_;} QString xColumn()const{return x_;} QString yColumn()const{return y_;} QString zColumn()const{return z_;} QString colorColumn()const{return color_;} QString mode()const{return mode_;}
    double roughness()const{return roughness_;} double metallic()const{return metallic_;} double specular()const{return specular_;}
    double smartPointSize()const{return smartPointSize_;} double scalarMin()const{return scalarMin_;} double scalarMax()const{return scalarMax_;}
    void setArrowPath(const QString&);void setXColumn(const QString&);void setYColumn(const QString&);void setZColumn(const QString&);void setColorColumn(const QString&);void setMode(const QString&);void setRoughness(double);void setMetallic(double);void setSpecular(double);void setSmartPointSize(double);void setScalarMin(double);void setScalarMax(double);
    Q_INVOKABLE void reload();
signals:void sceneChanged();
private:
    void queueGeometryRebuild();
    void queueMaterialUpdate();
    QString arrowPath_,x_,y_,z_,color_,mode_=QStringLiteral("surface");
    double roughness_=0.35,metallic_=0.0,specular_=0.45,smartPointSize_=4.0,scalarMin_=std::numeric_limits<double>::quiet_NaN(),scalarMax_=std::numeric_limits<double>::quiet_NaN();
};
