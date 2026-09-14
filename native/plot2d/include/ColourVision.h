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
// Each palette is measured against ALL THREE deficiencies, not only its own.
//
// The figures here used to be one per palette - the score for the deficiency it
// was built for - and that silence was being read as a conclusion. The
// outstanding-work note said "a palette specialised for one deficiency is
// unsafe for another", which was a guess nobody had checked. Measured with
// tools/measure_colourmap_cvd.py it is true of two of the three and false of
// the third:
//
//                 normal  protan  deutan  tritan   worst cross-pair
//   Standard        39.0    24.5    23.5    26.5   safe for all three
//   Protanopia      38.7    29.7    27.7     6.7   #66d3c0 / #6bccfd under tritan
//   Deuteranopia    32.0    12.6    28.2     7.9   #f9acb6 / #ffa1f4 under tritan
//   Tritanopia      45.6    16.2    17.9    29.9   safe for all three
//   Monochrome      13.0    13.0    13.0    13.0   colour carries nothing anyway
//
// So Protanopia and Deuteranopia each collapse a pair for a tritanope - both
// times a pair the deficiency they were built for separates perfectly well.
// That matters because a figure in a paper is read by people with all three,
// and choosing one of those two to be considerate produces a figure some other
// reader cannot use. plotColourVisionSummary says so when one is selected.
//
// Standard is safe for all three, which is why it is the default and why it is
// named for no deficiency at all.
enum class ColourVision {
    Standard = 0,
    Protanopia = 1,
    Deuteranopia = 2,
    Tritanopia = 3,
    Monochrome = 4,    // greyscale, backed by dash patterns
};

// The deficiency this palette FAILS, or empty when it is safe for all three.
// Measured, not asserted - see the table above.
inline QString crossVisionRisk(ColourVision mode){
    switch(mode){
    case ColourVision::Protanopia:
    case ColourVision::Deuteranopia:
        return QStringLiteral("tritanopia");
    default:
        return QString();
    }
}

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

// A SECOND CHANNEL, for every colour-vision mode and not only Monochrome.
// Dash patterns are in pen-width units.
//
// This used to return nothing except in Monochrome, on the reasoning that a
// dichromat still receives hue and so needs no help beyond a tuned palette.
// Rendered and simulated, that reasoning does not survive contact with a real
// figure: six series of the Standard palette put through the protanope
// transform leave series 2 and series 5 as two near-identical mustards, and the
// Protanopia palette - the one tuned for that reader - still hands them three
// blues. Eight colours can be made pairwise distinct in CIELAB and still fail
// as eight thin lines on a dark ground, because a 2-pixel stroke carries far
// less colour signal than the patch the measurement was made on.
//
// So hue stops being the only thing telling two lines apart the moment somebody
// says their colour vision needs accommodating. A dash pattern survives every
// deficiency, every simulation, a greyscale print and a photocopy.
//
// Standard returns nothing, deliberately: it is the default, it is safe for all
// three dichromacies, and dashing every figure by default would be this
// program deciding what everyone's plots look like.
inline QVector<QVector<qreal>> seriesDashPatterns(ColourVision mode){
    if(mode==ColourVision::Standard) return {};
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
