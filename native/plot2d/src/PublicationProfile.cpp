#include "PublicationProfile.h"

#include <QJsonValue>

namespace graphvis {

PublicationProfile PublicationProfile::normalised() const{
    PublicationProfile p=*this;
    p.dpi=qMax(1,p.dpi);
    p.baseFontSize=qBound(5.0,p.baseFontSize,36.0);
    p.titleSize=qBound(5.0,p.titleSize,42.0);
    p.axisLabelSize=qBound(4.0,p.axisLabelSize,36.0);
    p.tickSize=qBound(4.0,p.tickSize,32.0);
    p.legendSize=qBound(4.0,p.legendSize,32.0);
    p.lineWidth=qBound(0.2,p.lineWidth,8.0);
    p.markerSize=qBound(0.5,p.markerSize,24.0);
    p.marginPadding=qBound(0.0,p.marginPadding,0.4);
    p.figureWidthIn=qBound(0.8,p.figureWidthIn,20.0);
    p.figureHeightIn=qBound(0.6,p.figureHeightIn,20.0);
    p.colorSpace=(p.colorSpace.compare(QLatin1String("CMYK"),Qt::CaseInsensitive)==0)
                     ? QStringLiteral("CMYK") : QStringLiteral("RGB");
    return p;
}

PlotSpec PublicationProfile::applyTo(const PlotSpec& spec) const{
    const PublicationProfile p=normalised();
    PlotSpec out=spec;
    out.style.fontFamily=p.fontFamily;
    out.style.baseFontSize=p.baseFontSize;
    out.style.titleSize=p.titleSize;
    out.style.axisLabelSize=p.axisLabelSize;
    out.style.tickSize=p.tickSize;
    out.style.legendSize=p.legendSize;
    out.style.lineWidth=p.lineWidth;
    out.style.gridVisible=p.gridVisible;
    out.style.dpi=p.dpi;
    out.style.figureWidthIn=p.figureWidthIn;
    out.style.figureHeightIn=p.figureHeightIn;

    // Series line width and marker size follow the profile, but only where the
    // series has not been given one deliberately - overriding a width somebody
    // set on purpose is not what a profile is for.
    for(PlotSeries& s:out.series){
        if(!s.lineWidthExplicit)  s.lineWidth=p.lineWidth;
        if(!s.markerSizeExplicit) s.markerSize=p.markerSize;
    }

    // A journal figure is printed on white. Carrying a dark interface theme
    // into an export is the single most common way a figure arrives unusable.
    out.style.background=Qt::white;
    out.style.foreground=QColor(0x11,0x11,0x11);
    out.style.gridColor=QColor(0xd8,0xd8,0xd8);
    return out;
}

QJsonObject PublicationProfile::toJson() const{
    return QJsonObject{
        {QStringLiteral("name"),name},
        {QStringLiteral("dpi"),dpi},
        {QStringLiteral("font_family"),fontFamily},
        {QStringLiteral("base_font_size"),baseFontSize},
        {QStringLiteral("title_size"),titleSize},
        {QStringLiteral("axis_label_size"),axisLabelSize},
        {QStringLiteral("tick_size"),tickSize},
        {QStringLiteral("legend_size"),legendSize},
        {QStringLiteral("line_width"),lineWidth},
        {QStringLiteral("marker_size"),markerSize},
        {QStringLiteral("margin_padding"),marginPadding},
        {QStringLiteral("color_space"),colorSpace},
        {QStringLiteral("figure_width_in"),figureWidthIn},
        {QStringLiteral("figure_height_in"),figureHeightIn},
        {QStringLiteral("grid_visible"),gridVisible},
        {QStringLiteral("notes"),notes},
    };
}

PublicationProfile PublicationProfile::fromJson(const QJsonObject& j){
    PublicationProfile p;
    p.name=j.value(QStringLiteral("name")).toString(p.name);
    p.dpi=j.value(QStringLiteral("dpi")).toInt(p.dpi);
    p.fontFamily=j.value(QStringLiteral("font_family")).toString(p.fontFamily);
    p.baseFontSize=j.value(QStringLiteral("base_font_size")).toDouble(p.baseFontSize);
    p.titleSize=j.value(QStringLiteral("title_size")).toDouble(p.titleSize);
    p.axisLabelSize=j.value(QStringLiteral("axis_label_size")).toDouble(p.axisLabelSize);
    p.tickSize=j.value(QStringLiteral("tick_size")).toDouble(p.tickSize);
    p.legendSize=j.value(QStringLiteral("legend_size")).toDouble(p.legendSize);
    p.lineWidth=j.value(QStringLiteral("line_width")).toDouble(p.lineWidth);
    p.markerSize=j.value(QStringLiteral("marker_size")).toDouble(p.markerSize);
    p.marginPadding=j.value(QStringLiteral("margin_padding")).toDouble(p.marginPadding);
    p.colorSpace=j.value(QStringLiteral("color_space")).toString(p.colorSpace);
    p.figureWidthIn=j.value(QStringLiteral("figure_width_in")).toDouble(p.figureWidthIn);
    p.figureHeightIn=j.value(QStringLiteral("figure_height_in")).toDouble(p.figureHeightIn);
    p.gridVisible=j.value(QStringLiteral("grid_visible")).toBool(p.gridVisible);
    p.notes=j.value(QStringLiteral("notes")).toString(p.notes);
    return p.normalised();
}

QVector<PublicationProfile> builtinProfiles(){
    // Ported from GraphVis 17 core/publication.py, notes included. Widths are
    // the journals' stated column measures converted from millimetres.
    QVector<PublicationProfile> out;

    PublicationProfile screen;
    screen.name=QStringLiteral("Screen (default)");
    screen.dpi=144; screen.baseFontSize=8; screen.titleSize=10; screen.axisLabelSize=8;
    screen.tickSize=7; screen.legendSize=7; screen.lineWidth=1.2; screen.markerSize=4.0;
    screen.figureWidthIn=6.5; screen.figureHeightIn=4.0; screen.gridVisible=true;
    screen.notes=QStringLiteral("The on-screen look, for working rather than submitting. "
                                "Switch to a journal profile before exporting.");
    out.append(screen);

    PublicationProfile natureSingle;
    natureSingle.name=QStringLiteral("Nature single-column");
    natureSingle.dpi=600; natureSingle.baseFontSize=6; natureSingle.titleSize=7;
    natureSingle.axisLabelSize=6; natureSingle.tickSize=5; natureSingle.legendSize=5;
    natureSingle.lineWidth=0.75; natureSingle.markerSize=3.5; natureSingle.marginPadding=0.05;
    natureSingle.figureWidthIn=89.0/25.4; natureSingle.figureHeightIn=2.55;
    natureSingle.notes=QStringLiteral(
        "Nature baseline: 89 mm single-column width; standard Arial/Helvetica; ordinary text 5-7 pt; "
        "lines 0.25-1 pt; RGB; vector PDF/EPS preferred. 600 dpi is a conservative GraphVis raster default.");
    out.append(natureSingle);

    PublicationProfile natureDouble=natureSingle;
    natureDouble.name=QStringLiteral("Nature double-column");
    natureDouble.figureWidthIn=183.0/25.4; natureDouble.figureHeightIn=5.4;
    natureDouble.notes=QStringLiteral(
        "Nature baseline: 183 mm double-column width, 5-7 pt figure text and 0.25-1 pt lines.");
    out.append(natureDouble);

    PublicationProfile science;
    science.name=QStringLiteral("Science (AAAS) compact");
    science.dpi=300; science.baseFontSize=7; science.titleSize=8; science.axisLabelSize=7;
    science.tickSize=6; science.legendSize=6; science.lineWidth=0.8; science.markerSize=4.0;
    science.marginPadding=0.06;
    science.figureWidthIn=57.0/25.4; science.figureHeightIn=2.5;
    science.notes=QStringLiteral(
        "Science/AAAS compact baseline: editable/vector artwork preferred, 300 dpi raster baseline, "
        "compact high-contrast labels. Verify the current target Science-family journal instructions "
        "before final submission.");
    out.append(science);

    PublicationProfile ieeeSingle;
    ieeeSingle.name=QStringLiteral("IEEE journal single-column");
    ieeeSingle.dpi=600; ieeeSingle.baseFontSize=9; ieeeSingle.titleSize=10;
    ieeeSingle.axisLabelSize=9; ieeeSingle.tickSize=8; ieeeSingle.legendSize=8;
    ieeeSingle.lineWidth=1.0; ieeeSingle.markerSize=4.0; ieeeSingle.marginPadding=0.06;
    ieeeSingle.figureWidthIn=3.5; ieeeSingle.figureHeightIn=2.75;
    ieeeSingle.notes=QStringLiteral(
        "IEEE baseline: 3.5 in single-column width; graphics text approximately 9-10 pt at full size; "
        ">300 dpi colour/greyscale and >600 dpi line art; PS/EPS/PDF vector preferred.");
    out.append(ieeeSingle);

    PublicationProfile ieeeDouble=ieeeSingle;
    ieeeDouble.name=QStringLiteral("IEEE journal double-column");
    ieeeDouble.figureWidthIn=7.16; ieeeDouble.figureHeightIn=4.8;
    ieeeDouble.notes=QStringLiteral(
        "IEEE baseline: 7.16 in two-column width; >300 dpi colour/greyscale and >600 dpi line art.");
    out.append(ieeeDouble);

    PublicationProfile elsevier;
    elsevier.name=QStringLiteral("Elsevier single-column line art");
    elsevier.dpi=1000; elsevier.baseFontSize=7; elsevier.titleSize=8; elsevier.axisLabelSize=7;
    elsevier.tickSize=7; elsevier.legendSize=7; elsevier.lineWidth=1.0; elsevier.markerSize=4.0;
    elsevier.marginPadding=0.06;
    elsevier.figureWidthIn=90.0/25.4; elsevier.figureHeightIn=2.8;
    elsevier.notes=QStringLiteral(
        "Elsevier general artwork baseline: ~90 mm single-column; normal lettering ~7 pt; prominent graph "
        "lines ~1 pt (0.25 pt recommended line work minimum); 1000 dpi line art / 500 dpi combination / "
        "300 dpi halftone. Journal-specific Elsevier instructions take precedence.");
    out.append(elsevier);

    PublicationProfile apa;
    apa.name=QStringLiteral("APA 7 figure");
    apa.dpi=300; apa.baseFontSize=10; apa.titleSize=11; apa.axisLabelSize=10;
    apa.tickSize=9; apa.legendSize=9; apa.lineWidth=1.0; apa.markerSize=4.5;
    apa.marginPadding=0.08;
    apa.figureWidthIn=6.5; apa.figureHeightIn=4.0;
    apa.notes=QStringLiteral(
        "APA 7 figure baseline: simple sans-serif figure text 8-14 pt, clear dark labels and restrained "
        "decoration. APA specifies sufficient print/view resolution rather than one universal DPI; "
        "GraphVis uses 300 dpi as a practical baseline.");
    out.append(apa);

    PublicationProfile thesis;
    thesis.name=QStringLiteral("Thesis / A4 report");
    thesis.dpi=600; thesis.baseFontSize=9; thesis.titleSize=10; thesis.axisLabelSize=9;
    thesis.tickSize=8; thesis.legendSize=8; thesis.lineWidth=1.0; thesis.markerSize=4.0;
    thesis.marginPadding=0.07;
    // A4 less 25 mm margins each side.
    thesis.figureWidthIn=160.0/25.4; thesis.figureHeightIn=4.2;
    thesis.notes=QStringLiteral(
        "A4 text width less 25 mm margins, at a size that stays readable when a figure is printed "
        "one-up in a bound thesis. Not a journal standard - a sensible default for a dissertation.");
    out.append(thesis);

    return out;
}

PublicationProfile profileByName(const QString& name){
    const QVector<PublicationProfile> all=builtinProfiles();
    for(const PublicationProfile& p:all) if(p.name==name) return p;
    return all.isEmpty()?PublicationProfile():all.first();
}

} // namespace graphvis
