#pragma once
// =========================================================================
// ArrowTable - reads numeric columns out of a dataset's Arrow IPC file.
//
// The Rust core owns the data path and writes Arrow IPC; AppController exposes
// the file as activeArrowPath. This reader turns named columns into plain
// double vectors for the plot engines, casting whatever numeric type the
// column actually has, exactly as native/vtk_backend does for the 3-D scene.
//
// Non-finite values are preserved rather than dropped: a failed solver point
// is data, and only the engine decides how a gap is drawn.
// =========================================================================
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace graphvis {

class ArrowTable {
public:
    // Opens the IPC file and reads every record batch. Returns false and sets
    // error() when the file cannot be read.
    bool load(const QString& path, int maxRows = 0);

    bool isValid() const { return valid_; }
    QString error() const { return error_; }
    int rowCount() const { return rowCount_; }
    QStringList columnNames() const { return columns_; }
    bool hasColumn(const QString& name) const { return values_.contains(name); }

    // Empty when the column is absent or not castable to float64.
    QVector<double> column(const QString& name) const { return values_.value(name); }

private:
    bool valid_ = false;
    QString error_;
    int rowCount_ = 0;
    QStringList columns_;
    QHash<QString, QVector<double>> values_;
};

} // namespace graphvis
