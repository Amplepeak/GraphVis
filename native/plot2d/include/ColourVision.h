#pragma once
#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>

namespace graphvis {

// Series colour cycles for colour vision deficiency.
//
// This is a plotting setting, deliberately separate from the UI theme: someone
// who needs a deuteranopia-safe figure needs it whatever the interface looks
// like, and needs it to stay put. It is persisted by AppController and applies
// to every engine, on screen and in exported PDFs alike.
//
// The palettes are not asserted to be safe, they were measured. Each candidate
// was simulated through the Vienot / Brettel-Mollon dichromat projection for
// the deficiency in question and every pair compared in CIELAB; a palette was
// only accepted when its worst-separated pair still cleared a comfortable
// margin. The worst-pair figures below are that measurement, in dE76 - higher
// is better, and anything under about 10 is a pair a viewer would struggle to
// tell apart. tools/theme-generator carries the simulation.
//
// For reference, the palette this replaced - which carried the comment "a
// distinguishable, colour-blind-safe rotation" - measured 3.6 under protanopia.
// It was not safe. That is why these are generated and checked rather than
// chosen by eye.
enum class ColourVision {
    Standard = 0,      // normal 39.0  protan 24.8  deutan 24.1  tritan 25.4
    Protanopia = 1,    // protan 34.7
    Deuteranopia = 2,  // deutan 32.1
    Tritanopia = 3,    // tritan 30.7
    Monochrome = 4,    // greyscale, worst pair 13.0, backed by dash patterns
};

inline ColourVision colourVisionFromInt(int value){
    return (value >= 0 && value <= 4) ? static_cast<ColourVision>(value) : ColourVision::Standard;
}

inline QStringList colourVisionNames(){
    return {
        QStringLiteral("Standard (safe for all types)"),
        QStringLiteral("Protanopia (red-blind)"),
        QStringLiteral("Deuteranopia (green-blind)"),
        QStringLiteral("Tritanopia (blue-blind)"),
        QStringLiteral("Monochrome (no colour)"),
    };
}

inline QVector<QColor> seriesPalette(ColourVision mode){
    switch(mode){
    case ColourVision::Protanopia:
        return {QColor("#a34c9b"),QColor("#a9cf08"),QColor("#c90027"),QColor("#444ee7"),
                QColor("#66d3c0"),QColor("#b8bb6b"),QColor("#6bccfd"),QColor("#5495fe")};
    case ColourVision::Deuteranopia:
        return {QColor("#a34c9b"),QColor("#fdb31f"),QColor("#147318"),QColor("#444ee7"),
                QColor("#f9acb6"),QColor("#819bfd"),QColor("#00df56"),QColor("#ffa1f4")};
    case ColourVision::Tritanopia:
        return {QColor("#9856db"),QColor("#fd1c39"),QColor("#2bdff9"),QColor("#fe8e36"),
                QColor("#0874a8"),QColor("#abda77"),QColor("#ad1752"),QColor("#f8bccd")};
    case ColourVision::Monochrome:
        // Evenly spaced in perceived lightness. Colour alone cannot carry the
        // series here, so PlotCanvas pairs each entry with its own dash pattern.
        return {QColor("#2b2b2b"),QColor("#4d4d4d"),QColor("#707070"),
                QColor("#949494"),QColor("#b8b8b8"),QColor("#dcdcdc")};
    case ColourVision::Standard:
    default:
        // The default is already safe for all three dichromacies, so a user who
        // never opens the setting still gets a readable figure.
        return {QColor("#9856db"),QColor("#fd1c39"),QColor("#65dfd9"),QColor("#a4275b"),
                QColor("#ebc695"),QColor("#2f8bad"),QColor("#afaffc"),QColor("#fca21b")};
    }
}

// Monochrome needs a second channel. Dash patterns are in pen-width units.
inline QVector<QVector<qreal>> seriesDashPatterns(ColourVision mode){
    if(mode!=ColourVision::Monochrome) return {};
    return {
        {},                    // solid
        {6,3},                 // dashed
        {1.5,2.5},             // dotted
        {8,3,1.5,3},           // dash-dot
        {8,3,1.5,3,1.5,3},     // dash-dot-dot
        {3,3},                 // short dash
    };
}

} // namespace graphvis
