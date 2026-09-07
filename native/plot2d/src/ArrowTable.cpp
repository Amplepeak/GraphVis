// Arrow must be included before any Qt header in this translation unit.
// arrow/util/cancel.h declares
//     Status RegisterCancellingSignalHandler(const std::vector<int>& signals);
// and Qt defines `signals` as a macro (`#define signals public`), so a Qt-first
// include order turns that parameter into `public` and the header fails to
// compile. native/vtk_backend/src/GraphVisVtkItem.cpp orders its includes the
// same way for the same reason.
#include <arrow/array.h>
#include <arrow/compute/api.h>
#include <arrow/io/file.h>
#include <arrow/ipc/reader.h>
#include <arrow/record_batch.h>
#include <arrow/table.h>

#include "ArrowTable.h"

#include <QHash>
#include <cmath>
#include <limits>

namespace graphvis {

bool ArrowTable::load(const QString& path,int maxRows){
    valid_=false; error_.clear(); rowCount_=0; columns_.clear(); values_.clear();
    if(path.isEmpty()){ error_=QStringLiteral("No dataset is active"); return false; }

    auto fileResult=arrow::io::ReadableFile::Open(path.toStdString());
    if(!fileResult.ok()){ error_=QStringLiteral("Cannot open %1").arg(path); return false; }
    auto readerResult=arrow::ipc::RecordBatchFileReader::Open(*fileResult);
    if(!readerResult.ok()){ error_=QStringLiteral("Not a readable Arrow IPC file: %1").arg(path); return false; }
    auto reader=*readerResult;

    const auto schema=reader->schema();
    // Duplicate field names are legal in Arrow and common in a file written
    // from a join. Keyed by bare name, two such columns appended into the SAME
    // vector and the user got a zig-zag of two unrelated series under one name.
    QVector<QString> fieldKeys;
    fieldKeys.reserve(schema->num_fields());
    for(int i=0;i<schema->num_fields();++i){
        QString key=QString::fromStdString(schema->field(i)->name());
        if(key.isEmpty()) key=QStringLiteral("column_%1").arg(i+1);
        if(fieldKeys.contains(key)){
            int n=2;
            while(fieldKeys.contains(QStringLiteral("%1_%2").arg(key).arg(n))) ++n;
            key=QStringLiteral("%1_%2").arg(key).arg(n);
        }
        fieldKeys.append(key);
        columns_.append(key);
    }

    for(int bi=0;bi<reader->num_record_batches();++bi){
        auto br=reader->ReadRecordBatch(bi);
        if(!br.ok()) continue;
        auto batch=*br;
        const qint64 batchRows=batch->num_rows();
        for(int ci=0;ci<batch->num_columns()&&ci<fieldKeys.size();++ci){
            const QString& name=fieldKeys.at(ci);
            QVector<double>& out=values_[name];
            const auto room=[&]{ return maxRows>0 ? qMax(qsizetype(0),qsizetype(maxRows)-out.size()) : batchRows; };

            auto cast=arrow::compute::Cast(batch->column(ci),arrow::float64());
            if(!cast.ok()){
                // Cast failure is per batch and depends on CONTENT, so one
                // non-numeric cell in batch 0 used to skip that batch for this
                // column alone - leaving it short against every other column,
                // and buildPlotSeries then just takes qMin() of the two sizes.
                // Every y was plotted against the wrong x, with no warning.
                // Pad instead: alignment is not negotiable, and a column that
                // is NaN throughout is dropped below.
                const qsizetype pad=qMin(qsizetype(batchRows),room());
                if(pad>0) out.insert(out.end(),pad,std::numeric_limits<double>::quiet_NaN());
                continue;
            }
            auto arr=std::static_pointer_cast<arrow::DoubleArray>(cast->make_array());
            // qsizetype, not int: Array::length() is int64_t and narrowing it
            // could reserve a negative size.
            out.reserve(out.size()+qsizetype(arr->length()));
            for(int64_t r=0;r<arr->length();++r){
                if(maxRows>0&&out.size()>=maxRows) break;
                // A null is a gap, not a zero. NaN carries that through to the
                // engine, which decides whether to break the line.
                out.append(arr->IsNull(r)?std::numeric_limits<double>::quiet_NaN():arr->Value(r));
            }
        }
        // Count what was actually stored, not what the batch claimed: with
        // maxRows set, the columns stop before the batch does.
        qsizetype longest=0;
        for(auto it=values_.constBegin();it!=values_.constEnd();++it) longest=qMax(longest,it.value().size());
        rowCount_=int(qMin<qsizetype>(longest,std::numeric_limits<int>::max()));
        if(maxRows>0&&rowCount_>=maxRows) break;
    }

    // Keep only columns that carry at least one real number. A text column
    // fails every cast and is now all-NaN rather than absent, so emptiness -
    // not absence - is what marks it as unplottable.
    QStringList numeric;
    for(const QString& c:std::as_const(columns_)){
        const auto it=values_.constFind(c);
        if(it==values_.constEnd()) continue;
        bool anyFinite=false;
        for(double v:it.value()) if(v==v&&!std::isinf(v)){ anyFinite=true; break; }
        if(anyFinite) numeric.append(c); else values_.remove(c);
    }
    columns_=numeric;

    valid_=!columns_.isEmpty();
    if(!valid_) error_=QStringLiteral("No numeric columns in %1").arg(path);
    return valid_;
}

} // namespace graphvis
