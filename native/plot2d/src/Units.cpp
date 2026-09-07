#include "Units.h"

#include <QRegularExpression>

#include <utility>

namespace Units {
namespace {

// Trailing [unit] or (unit). Anchored at the end so "Flow (corrected) [L/min]"
// yields "L/min" and not "corrected".
const QRegularExpression& unitPattern(){
    static const QRegularExpression re(QStringLiteral(R"((?:\[([^\]]+)\]|\(([^()]+)\))\s*$)"));
    return re;
}

// value_target = value_source * factor + offset. Taken from v17's table
// unchanged, including the two temperature entries, which are the only pairs
// with a non-zero offset.
const QVector<Conversion>& table(){
    static const QVector<Conversion> conversions{
        {QStringLiteral("mV"), QStringLiteral("V"),    1e-3,  0.0},
        {QStringLiteral("V"),  QStringLiteral("mV"),   1e3,   0.0},
        {QStringLiteral("uV"), QStringLiteral("V"),    1e-6,  0.0},
        {QStringLiteral("V"),  QStringLiteral("uV"),   1e6,   0.0},
        {QStringLiteral("kV"), QStringLiteral("V"),    1e3,   0.0},
        {QStringLiteral("V"),  QStringLiteral("kV"),   1e-3,  0.0},
        {QStringLiteral("mA"), QStringLiteral("A"),    1e-3,  0.0},
        {QStringLiteral("A"),  QStringLiteral("mA"),   1e3,   0.0},
        {QStringLiteral("uA"), QStringLiteral("A"),    1e-6,  0.0},
        {QStringLiteral("A"),  QStringLiteral("uA"),   1e6,   0.0},
        {QStringLiteral("ms"), QStringLiteral("s"),    1e-3,  0.0},
        {QStringLiteral("s"),  QStringLiteral("ms"),   1e3,   0.0},
        {QStringLiteral("min"),QStringLiteral("s"),    60.0,  0.0},
        {QStringLiteral("s"),  QStringLiteral("min"),  1.0/60.0, 0.0},
        {QStringLiteral("h"),  QStringLiteral("s"),    3600.0,0.0},
        {QStringLiteral("s"),  QStringLiteral("h"),    1.0/3600.0, 0.0},
        {QStringLiteral("mm"), QStringLiteral("m"),    1e-3,  0.0},
        {QStringLiteral("m"),  QStringLiteral("mm"),   1e3,   0.0},
        {QStringLiteral("um"), QStringLiteral("m"),    1e-6,  0.0},
        {QStringLiteral("m"),  QStringLiteral("um"),   1e6,   0.0},
        {QStringLiteral("nm"), QStringLiteral("m"),    1e-9,  0.0},
        {QStringLiteral("m"),  QStringLiteral("nm"),   1e9,   0.0},
        {QStringLiteral("kPa"),QStringLiteral("Pa"),   1e3,   0.0},
        {QStringLiteral("Pa"), QStringLiteral("kPa"),  1e-3,  0.0},
        {QStringLiteral("MPa"),QStringLiteral("Pa"),   1e6,   0.0},
        {QStringLiteral("Pa"), QStringLiteral("MPa"),  1e-6,  0.0},
        {QStringLiteral("bar"),QStringLiteral("Pa"),   1e5,   0.0},
        {QStringLiteral("Pa"), QStringLiteral("bar"),  1e-5,  0.0},
        {QStringLiteral("C"),  QStringLiteral("K"),    1.0,   273.15},
        {QStringLiteral("degC"),QStringLiteral("K"),   1.0,   273.15},
        {QStringLiteral("°C"), QStringLiteral("K"),    1.0,   273.15},
        {QStringLiteral("K"),  QStringLiteral("°C"),   1.0,  -273.15},
        {QStringLiteral("g/L"),QStringLiteral("mg/L"), 1000.0,0.0},
        {QStringLiteral("mg/L"),QStringLiteral("g/L"), 1e-3,  0.0},
    };
    return conversions;
}

}  // namespace

QString normaliseUnit(const QString& unit){
    QString u = unit.trimmed();
    u.replace(QChar(0x03BC), QLatin1Char('u'));  // Greek small letter mu
    u.replace(QChar(0x00B5), QLatin1Char('u'));  // micro sign
    return u;
}

QString extractUnit(const QString& label){
    const QRegularExpressionMatch m = unitPattern().match(label);
    if(!m.hasMatch()) return QString();
    QString unit = m.captured(1);
    if(unit.isEmpty()) unit = m.captured(2);
    return unit.trimmed();
}

QString stripUnit(const QString& label){
    QString out = label;
    out.remove(unitPattern());
    return out.trimmed();
}

QString displayName(const QString& raw){
    QString base = stripUnit(raw);
    if(base.startsWith(QLatin1String("lhs_"))) base.remove(0, 4);
    base.replace(QLatin1Char('_'), QLatin1Char(' '));
    base = base.simplified();
    if(base.isEmpty()) return raw;
    // Title case, but only where a word starts: "flow rate" -> "Flow Rate",
    // while "pH" and "CO2" keep the capitals the source already gave them.
    QString out;
    out.reserve(base.size());
    bool atWordStart = true;
    for(const QChar c : std::as_const(base)){
        out.append(atWordStart ? c.toUpper() : c);
        atWordStart = c.isSpace();
    }
    return out;
}

QString axisLabel(const QString& raw){
    const QString unit = extractUnit(raw);
    const QString name = displayName(raw);
    return unit.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(name, unit);
}

QVector<Conversion> conversionsFor(const QString& unit){
    const QString wanted = normaliseUnit(unit);
    if(wanted.isEmpty()) return {};
    QVector<Conversion> out;
    for(const Conversion& c : table())
        if(normaliseUnit(c.source).compare(wanted, Qt::CaseInsensitive) == 0)
            out.append(c);
    return out;
}

QString replaceUnit(const QString& label, const QString& newUnit){
    const QString base = stripUnit(label);
    if(newUnit.isEmpty()) return base;
    return QStringLiteral("%1 [%2]").arg(base, newUnit);
}

}  // namespace Units
