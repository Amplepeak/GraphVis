// Variable-name context: units, display names and conversions.
//
// A port of GraphVis 17's data/variable_context.py. Instruments and export
// scripts name their columns "Pressure [kPa]", "T (degC)", "flow_rate_lpm";
// v17 read the unit out of the label, showed a tidy axis title and offered the
// conversions that unit has. v18 had none of it and drew the raw column name.
//
// This lives in C++ rather than in the science service on purpose: an axis
// label is drawn on every frame, and a machine with no add-on installed still
// deserves a readable axis.
#pragma once

#include <QString>
#include <QVector>

namespace Units {

// A linear conversion. value_target = value_source * factor + offset, which is
// enough for every unit pair v17 shipped, temperature included.
struct Conversion {
    QString source;
    QString target;
    double factor = 1.0;
    double offset = 0.0;
    double apply(double value) const { return value * factor + offset; }
};

// The unit in a trailing [..] or (..), or an empty string. "Pressure [kPa]" -> "kPa".
QString extractUnit(const QString& label);

// The label with that trailing unit removed. "Pressure [kPa]" -> "Pressure".
QString stripUnit(const QString& label);

// A human name for a raw column: underscores become spaces, the v17 "lhs_"
// prefix is dropped, and the result is title-cased. "lhs_flow_rate" -> "Flow Rate".
QString displayName(const QString& raw);

// What an axis should say. "Pressure [kPa]" -> "Pressure (kPa)"; a column with
// no unit is just its display name, so this is safe to apply unconditionally.
QString axisLabel(const QString& raw);

// Every conversion that starts from this unit. Empty for an unknown unit, which
// is the common case and not an error.
QVector<Conversion> conversionsFor(const QString& unit);

// The same label carrying a different unit. "Pressure [kPa]", "Pa" -> "Pressure [Pa]".
QString replaceUnit(const QString& label, const QString& newUnit);

// Micro sign and Greek mu both spell "u", and comparison is case-insensitive,
// so "uV", "µV" and "μV" are one unit rather than three.
QString normaliseUnit(const QString& unit);

}  // namespace Units
