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

    qsizetype longest=0;
    for(int bi=0;bi<reader->num_record_batches();++bi){
        auto br=reader->ReadRecordBatch(bi);
        if(!br.ok()) continue;
        auto batch=*br;
        const qint64 batchRows=batch->num_rows();

        // Reserve each column ONCE, from the first batch, for the whole file.
        //
        // This used to be reserve(out.size() + batchRows) inside the column
        // loop below - an exact capacity request per batch, so every batch
        // reallocated and copied the whole accumulated column. Quadratic in
        // the number of batches: a 481 MB file written in 300 batches took
        // 13.8 s to load, against 76 ms for the same data in one batch.
        // Isolated, the two patterns are 723 ms and 11 ms for 1.5M appends.
        //
        // The estimate is first-batch rows x batch count. It costs nothing -
        // batch 0 is already in hand - where CountRows() reads every batch's
        // metadata and measured 20-73 ms on these files, which is most of the
        // load time for a single-batch one. An estimate that falls short just
        // hands the rest to QVector's geometric growth.
        if(bi==0&&batchRows>0){
            qsizetype guess=qsizetype(batchRows)*qsizetype(reader->num_record_batches());
            if(maxRows>0) guess=qMin(guess,qsizetype(maxRows));
            for(const QString& key:std::as_const(fieldKeys))
                values_[key].reserve(guess);
        }

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
                longest=qMax(longest,out.size());
                continue;
            }
            auto arr=std::static_pointer_cast<arrow::DoubleArray>(cast->make_array());
            // qsizetype, not int: Array::length() is int64_t and narrowing it
            // could reserve a negative size.
            //
            // reserve-and-append, NOT resize-and-memcpy. The memcpy version
            // looks faster and is not: QVector::resize value-initialises, so
            // the bulk copy makes two passes over the data where the append
            // loop makes one. Measured on a 120 MB, 3M-row file: append 36 ms,
            // resize+memcpy 65 ms. Arrow's Cast is free here - it is zero-copy
            // for a column that is already float64 - and the real cost is this
            // one pass, so there is nothing further to win.
            // No reserve here. The capacity was taken once above; asking for
            // an exact size per batch is what made this quadratic, and if
            // CountRows failed then QVector's own geometric growth is still
            // sixty times better than an exact reserve per batch.
            for(int64_t r=0;r<arr->length();++r){
                if(maxRows>0&&out.size()>=maxRows) break;
                // A null is a gap, not a zero. NaN carries that through to the
                // engine, which decides whether to break the line.
                out.append(arr->IsNull(r)?std::numeric_limits<double>::quiet_NaN():arr->Value(r));
            }
            // Tracked as the batches go by rather than rescanning every column
            // after each one, which was O(batches x columns) for a number that
            // only ever grows.
            longest=qMax(longest,out.size());
        }
        // Count what was actually stored, not what the batch claimed: with
        // maxRows set, the columns stop before the batch does.
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
