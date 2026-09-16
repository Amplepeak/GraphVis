#include <arrow/array.h>
#include <arrow/compute/api.h>
#include <arrow/io/file.h>
#include <arrow/ipc/reader.h>
#include "GraphVisVtkItem.h"
#include <vtkActor.h>
#include <vtkDelaunay2D.h>
#include <vtkFloatArray.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#include <vtkVertexGlyphFilter.h>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
struct SceneSettings { QString path,x,y,z,color,mode; double roughness,metallic,specular,pointSize,scalarMin,scalarMax; };

class SceneData final : public vtkObject {
public:
    static SceneData* New();
    vtkTypeMacro(SceneData, vtkObject);
    vtkSmartPointer<vtkRenderer> renderer;
    vtkSmartPointer<vtkActor> actor;
    vtkSmartPointer<vtkPolyDataMapper> mapper;
};
vtkStandardNewMacro(SceneData);

std::shared_ptr<arrow::Array> numericColumn(const std::shared_ptr<arrow::RecordBatch>& batch,const QString& name){
    int idx=batch->schema()->GetFieldIndex(name.toStdString()); if(idx<0) return {};
    auto result=arrow::compute::Cast(batch->column(idx),arrow::float64()); if(!result.ok()) return {};
    return result->make_array();
}

void applyMaterial(SceneData* data,const SceneSettings&s){
    if(!data || !data->actor) return;
    auto* property=data->actor->GetProperty();
    if(s.mode.compare(QStringLiteral("points"),Qt::CaseInsensitive)==0){ property->SetPointSize(std::clamp(s.pointSize,1.0,30.0)); }
    if(s.mode.compare(QStringLiteral("points"),Qt::CaseInsensitive)!=0) property->SetInterpolationToPBR();
    property->SetRoughness(std::clamp(s.roughness,0.0,1.0));
    property->SetMetallic(std::clamp(s.metallic,0.0,1.0));
    property->SetSpecular(std::clamp(s.specular,0.0,1.0));
    if(data->mapper && std::isfinite(s.scalarMin)&&std::isfinite(s.scalarMax)&&s.scalarMin<s.scalarMax) data->mapper->SetScalarRange(s.scalarMin,s.scalarMax);
}

void rebuildGeometry(SceneData* data,const SceneSettings&s){
    if(!data || !data->renderer) return;
    auto* renderer=data->renderer.GetPointer();
    renderer->RemoveAllViewProps(); renderer->SetBackground(0.027,0.035,0.047); data->actor=nullptr;
    if(s.path.isEmpty()||s.x.isEmpty()||s.y.isEmpty()||s.z.isEmpty()) return;
    auto fileResult=arrow::io::ReadableFile::Open(s.path.toStdString()); if(!fileResult.ok()) return;
    auto readerResult=arrow::ipc::RecordBatchFileReader::Open(*fileResult); if(!readerResult.ok()) return; auto reader=*readerResult;
    vtkNew<vtkPoints> points; vtkNew<vtkFloatArray> scalars; scalars->SetName("GraphVis response");
    for(int bi=0;bi<reader->num_record_batches();++bi){
        auto br=reader->ReadRecordBatch(bi); if(!br.ok()) continue; auto batch=*br;
        auto xa=numericColumn(batch,s.x), ya=numericColumn(batch,s.y), za=numericColumn(batch,s.z), ca=numericColumn(batch,s.color.isEmpty()?s.z:s.color);
        if(!xa||!ya||!za||!ca) continue;
        auto X=std::static_pointer_cast<arrow::DoubleArray>(xa),Y=std::static_pointer_cast<arrow::DoubleArray>(ya),Z=std::static_pointer_cast<arrow::DoubleArray>(za),C=std::static_pointer_cast<arrow::DoubleArray>(ca);
        const int64_t n=std::min({X->length(),Y->length(),Z->length(),C->length()});
        for(int64_t i=0;i<n;++i){
            if(X->IsNull(i)||Y->IsNull(i)||Z->IsNull(i)||C->IsNull(i)) continue;
            const double x=X->Value(i),y=Y->Value(i),z=Z->Value(i),c=C->Value(i);
            if(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)&&std::isfinite(c)){ points->InsertNextPoint(x,y,z); scalars->InsertNextValue(float(c)); }
        }
    }
    vtkNew<vtkPolyData> cloud; cloud->SetPoints(points); cloud->GetPointData()->SetScalars(scalars);
    vtkSmartPointer<vtkPolyData> output;
    if(s.mode.compare(QStringLiteral("points"),Qt::CaseInsensitive)==0){
        vtkNew<vtkVertexGlyphFilter> glyph; glyph->SetInputData(cloud); glyph->Update(); output=vtkSmartPointer<vtkPolyData>::New(); output->ShallowCopy(glyph->GetOutput());
    }else{
        vtkNew<vtkDelaunay2D> surface; surface->SetInputData(cloud); surface->SetTolerance(0.001); surface->Update(); output=vtkSmartPointer<vtkPolyData>::New(); output->ShallowCopy(surface->GetOutput());
    }
    vtkNew<vtkPolyDataMapper> mapper; mapper->SetInputData(output); mapper->ScalarVisibilityOn(); mapper->SetColorModeToMapScalars();
    if(std::isfinite(s.scalarMin)&&std::isfinite(s.scalarMax)&&s.scalarMin<s.scalarMax) mapper->SetScalarRange(s.scalarMin,s.scalarMax);
    data->mapper=mapper; auto actor=vtkSmartPointer<vtkActor>::New(); actor->SetMapper(mapper); data->actor=actor; applyMaterial(data,s);
    renderer->AddActor(actor); renderer->ResetCamera();
}
}

GraphVisVtkItem::GraphVisVtkItem(QQuickItem*parent):QQuickVTKItem(parent){}
QQuickVTKItem::vtkUserData GraphVisVtkItem::initializeVTK(vtkRenderWindow*rw){
    auto data=vtkSmartPointer<SceneData>::New(); data->renderer=vtkSmartPointer<vtkRenderer>::New(); rw->AddRenderer(data->renderer);
    rebuildGeometry(data,SceneSettings{arrowPath_,x_,y_,z_,color_,mode_,roughness_,metallic_,specular_,smartPointSize_,scalarMin_,scalarMax_}); return data;
}
void GraphVisVtkItem::queueGeometryRebuild(){
    const SceneSettings s{arrowPath_,x_,y_,z_,color_,mode_,roughness_,metallic_,specular_,smartPointSize_,scalarMin_,scalarMax_};
    dispatch_async([s](vtkRenderWindow*,vtkUserData user){auto*data=SceneData::SafeDownCast(user); if(data)rebuildGeometry(data,s);}); scheduleRender(); emit sceneChanged();
}
void GraphVisVtkItem::queueMaterialUpdate(){
    const SceneSettings s{arrowPath_,x_,y_,z_,color_,mode_,roughness_,metallic_,specular_,smartPointSize_,scalarMin_,scalarMax_};
    dispatch_async([s](vtkRenderWindow*,vtkUserData user){auto*data=SceneData::SafeDownCast(user); if(data)applyMaterial(data,s);}); scheduleRender(); emit sceneChanged();
}
void GraphVisVtkItem::reload(){queueGeometryRebuild();}
#define GEOMETRY_SETTER(name,field,type) void GraphVisVtkItem::name(const type&v){if(field==v)return;field=v;queueGeometryRebuild();}
GEOMETRY_SETTER(setArrowPath,arrowPath_,QString) GEOMETRY_SETTER(setXColumn,x_,QString) GEOMETRY_SETTER(setYColumn,y_,QString) GEOMETRY_SETTER(setZColumn,z_,QString) GEOMETRY_SETTER(setColorColumn,color_,QString) GEOMETRY_SETTER(setMode,mode_,QString)
#undef GEOMETRY_SETTER
void GraphVisVtkItem::setRoughness(double v){v=std::clamp(v,0.0,1.0);if(roughness_==v)return;roughness_=v;queueMaterialUpdate();}
void GraphVisVtkItem::setMetallic(double v){v=std::clamp(v,0.0,1.0);if(metallic_==v)return;metallic_=v;queueMaterialUpdate();}
void GraphVisVtkItem::setSpecular(double v){v=std::clamp(v,0.0,1.0);if(specular_==v)return;specular_=v;queueMaterialUpdate();}

void GraphVisVtkItem::setSmartPointSize(double v){v=std::clamp(v,1.0,30.0);if(smartPointSize_==v)return;smartPointSize_=v;queueMaterialUpdate();}
void GraphVisVtkItem::setScalarMin(double v){if((std::isnan(scalarMin_)&&std::isnan(v))||scalarMin_==v)return;scalarMin_=v;queueMaterialUpdate();}
void GraphVisVtkItem::setScalarMax(double v){if((std::isnan(scalarMax_)&&std::isnan(v))||scalarMax_==v)return;scalarMax_=v;queueMaterialUpdate();}
