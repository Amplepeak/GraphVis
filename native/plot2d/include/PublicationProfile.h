#pragma once
#include <QJsonObject>
#include <QString>
#include <QVector>
#include "PlotSpec.h"

namespace graphvis {

// Publication profiles - a port of GraphVis 17's core/publication.py.
//
// A journal figure is not a screen figure with a different file extension. It
// has a fixed column width in millimetres, a text size range measured in points
// at final size, a minimum line weight, and a resolution floor that differs for
// line art and halftones. Getting those wrong is the usual reason a figure comes
// back from a copy editor.
//
// The notes on each profile are carried through to the interface deliberately.
// They record what the baseline is and, more importantly, that journal-specific
// instructions take precedence over any of it - these are sensible starting
// points, not a promise about what a particular editor will accept.
struct PublicationProfile {
    QString name;
    int dpi = 600;
    QString fontFamily = QStringLiteral("Arial");
    double baseFontSize = 8.0;
    double titleSize = 9.0;
    double axisLabelSize = 8.0;
    double tickSize = 7.0;
    double legendSize = 7.0;
    double lineWidth = 1.2;
    double markerSize = 4.0;
    double marginPadding = 0.08;
    QString colorSpace = QStringLiteral("RGB");
    double figureWidthIn = 3.5;
    double figureHeightIn = 2.7;
    bool gridVisible = false;
    QString notes;

    // Clamps to what is physically sensible, the same bounds v17 applied.
    PublicationProfile normalised() const;
    // Returns a copy of `spec` with this profile's typography and geometry
    // applied. Series data is untouched: a profile changes how a figure looks,
    // never what it says.
    PlotSpec applyTo(const PlotSpec& spec) const;

    QJsonObject toJson() const;
    static PublicationProfile fromJson(const QJsonObject& json);
};

// The built-in set, in the order the interface should offer them.
QVector<PublicationProfile> builtinProfiles();
PublicationProfile profileByName(const QString& name);

} // namespace graphvis
