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
    QVector<PublicationProfile> out;

    // Working profiles first: the two anyone actually uses day to day.
    PublicationProfile screen;
    screen.name=QStringLiteral("Screen (default)");
    screen.dpi=144; screen.baseFontSize=8; screen.titleSize=10; screen.axisLabelSize=8;
    screen.tickSize=7; screen.legendSize=7; screen.lineWidth=1.2; screen.markerSize=4.0;
    screen.figureWidthIn=6.5; screen.figureHeightIn=4.0; screen.gridVisible=true;
    screen.notes=QStringLiteral("The on-screen look, for working rather than submitting. "
                                "Switch to a publisher profile before exporting.");
    out.append(screen);

    PublicationProfile thesis;
    thesis.name=QStringLiteral("Thesis / A4 report");
    thesis.dpi=600; thesis.baseFontSize=9; thesis.titleSize=10; thesis.axisLabelSize=9;
    thesis.tickSize=8; thesis.legendSize=8; thesis.lineWidth=1.0; thesis.markerSize=4.0;
    thesis.marginPadding=0.07;
    thesis.figureWidthIn=160.0/25.4; thesis.figureHeightIn=4.2;   // A4 less 25 mm margins
    thesis.notes=QStringLiteral(
        "A4 text width less 25 mm margins, at a size that stays readable printed one-up in a bound "
        "thesis. Not a publisher standard - a sensible default. University format guides "
        "(Pittsburgh, UCSF, Indiana, UNC, ASU) specify page size, margins and a 9-11 pt floor for "
        "text inside figures rather than figure widths.");
    out.append(thesis);

    PublicationProfile poster;
    poster.name=QStringLiteral("Conference poster (A1)");
    poster.dpi=300; poster.baseFontSize=24; poster.titleSize=32; poster.axisLabelSize=24;
    poster.tickSize=20; poster.legendSize=20; poster.lineWidth=2.5; poster.markerSize=9.0;
    poster.marginPadding=0.08;
    poster.figureWidthIn=250.0/25.4; poster.figureHeightIn=180.0/25.4;
    poster.notes=QStringLiteral(
        "A1 portrait, 594 x 841 mm, at 300 dpi with body text from 24 pt. A figure that is legible "
        "in a paper is unreadable at two metres, which is the distance a poster is actually read "
        "from.");
    out.append(poster);

    PublicationProfile apa;
    apa.name=QStringLiteral("APA 7 figure");
    apa.dpi=300; apa.baseFontSize=10; apa.titleSize=11; apa.axisLabelSize=10;
    apa.tickSize=9; apa.legendSize=9; apa.lineWidth=1.0; apa.markerSize=4.5;
    apa.marginPadding=0.08;
    apa.figureWidthIn=6.5; apa.figureHeightIn=4.0;
    apa.notes=QStringLiteral(
        "APA 7: sans-serif figure text 8-14 pt, clear dark labels, restrained decoration. APA "
        "specify sufficient resolution rather than one DPI; 300 is a practical baseline. Their own "
        "pages refuse automated access, so this is not a directly sourced profile.");
    out.append(apa);


    // Every number below is what the publisher's own author guidelines state.
    // A field a publisher does not publish is called out in that profile's
    // notes as a GraphVis default rather than quietly filled in - a profile
    // that looks authoritative and is not is worse than no profile, because it
    // is the one thing here nobody will think to check.
    //
    // Sizes are millimetres, as every publisher states them, and converted once
    // here rather than at each site.
    const auto add=[&out](const QString& name,double widthMm,double heightMm,int dpi,
                          double base,double title,double axis,double tick,double legend,
                          double lineWidth,double markerSize,
                          const QString& font,const QString& notes){
        PublicationProfile p;
        p.name=name;
        p.dpi=dpi;
        p.fontFamily=font;
        p.baseFontSize=base; p.titleSize=title; p.axisLabelSize=axis;
        p.tickSize=tick; p.legendSize=legend;
        p.lineWidth=lineWidth; p.markerSize=markerSize;
        p.marginPadding=0.06;
        p.figureWidthIn=widthMm/25.4;
        // The stated maximum height is a ceiling, not a shape. A figure as tall
        // as the page is almost never what anyone wants, so the default is the
        // narrower of a readable aspect ratio and that ceiling.
        p.figureHeightIn=qMin(heightMm/25.4,(widthMm/25.4)*0.72);
        p.gridVisible=false;
        p.notes=notes;
        out.append(p);
    };

    add(QStringLiteral("Nature single-column"),89,170,300,
        6,7,6,5,5,0.75,3.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "Nature: 89 mm single column, 183 mm double, 247 mm max height; figure text 5-7 pt; Helvetica or Arial; vector AI/EPS/PDF. 300 dpi is Nature's halftone minimum."));
    add(QStringLiteral("Nature double-column"),183,247,300,
        6,7,6,5,5,0.75,3.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "Nature: 183 mm double column, 247 mm max height; figure text 5-7 pt; Helvetica or Arial."));
    add(QStringLiteral("Science (AAAS) single-column"),55,100,300,
        7,8,7,6,6,0.75,3.0,
        QStringLiteral("Helvetica"),
        QStringLiteral(
            "Science/AAAS: their own figure-preparation pages refuse automated access, so this is a conservative single-column baseline rather than a sourced one. CHECK the current Science instructions before submitting."));
    add(QStringLiteral("Elsevier single-column"),90,130,1000,
        8,9,8,7,7,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "Elsevier: 90 mm single, 140 mm 1.5-column, 190 mm double; normal lettering 7 pt (6 pt floor); 1000 dpi line art, 500 combination, 300 halftone; EPS or PDF."));
    add(QStringLiteral("Elsevier 1.5-column"),140,170,1000,
        8,9,8,7,7,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "Elsevier: 140 mm 1.5-column; lettering 7 pt; 1000 dpi line art, 500 combination, 300 halftone."));
    add(QStringLiteral("Elsevier double-column"),190,230,1000,
        8,9,8,7,7,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "Elsevier: 190 mm double column; lettering 7 pt; 1000 dpi line art; EPS preferred."));
    add(QStringLiteral("Springer single-column"),84,170,1200,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Helvetica"),
        QStringLiteral(
            "Springer: 84 mm single, 174 mm double, 234 mm max height; lettering 8-12 pt (2-3 mm); 1200 dpi line art, 600 combination, 300 halftone; EPS preferred."));
    add(QStringLiteral("Springer double-column"),174,234,1200,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Helvetica"),
        QStringLiteral(
            "Springer: 174 mm double column, 234 mm max height; lettering 8-12 pt; 1200 dpi line art."));
    add(QStringLiteral("Wiley journal figure"),180,230,600,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "Wiley: 80 mm minimum canvas, 180 mm maximum width; 600 dpi line art, 300 halftone; PDF preferred for line art."));
    add(QStringLiteral("Taylor & Francis"),174,234,1200,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "Taylor & Francis: 1200 dpi line art, 600 greyscale and combination, 300 colour; EPS with fonts embedded; Times, Helvetica, Arial or Symbol. T&F state no column width - the width here is a conservative default, not their specification."));
    add(QStringLiteral("SAGE journal figure"),170,230,800,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "SAGE: 800 dpi line art, 300 halftone; EPS. SAGE state no column width - the width here is a default rather than their specification."));
    add(QStringLiteral("Oxford University Press"),80,235,600,
        8,9,8,7,7,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "OUP (Journal of Petrology): 80 mm single, 170 mm double, 235 mm max height; minimum lettering 2 mm high; 600 dpi line art, 300 halftone; EPS or SVG with fonts embedded."));
    add(QStringLiteral("Oxford University Press double-column"),170,235,600,
        8,9,8,7,7,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "OUP: 170 mm double column, 235 mm max height; 600 dpi line art, 300 halftone."));
    add(QStringLiteral("Cambridge University Press"),90,240,1000,
        10,11,10,9,9,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "Cambridge: 1000 dpi line art (1200 for fine lines), 600 combination, 300 halftone; minimum text 9 pt; Arial, Courier, Symbol, Times or Times New Roman; EPS or PDF with fonts embedded."));
    add(QStringLiteral("Cambridge double-column"),180,240,1000,
        10,11,10,9,9,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "Cambridge: 180 mm double column, 240 mm max height; minimum text 9 pt; 1000 dpi line art."));
    add(QStringLiteral("PLOS ONE"),132,222,300,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "PLOS: 66.8 mm minimum, 132 mm to match the text column, 190.5 mm maximum, 222.3 mm max height; text 8-12 pt; Arial, Times or Symbol ONLY; 300-600 dpi; vector EPS."));
    add(QStringLiteral("PLOS ONE full width"),190.5,222,300,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "PLOS: 190.5 mm maximum width, 222.3 mm max height; text 8-12 pt; Arial, Times or Symbol only."));
    add(QStringLiteral("MDPI"),170,230,600,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "MDPI: 600 dpi, published as TIFF. MDPI state no column width or minimum font size for figures - both here are defaults."));
    add(QStringLiteral("Frontiers single-column"),85,230,300,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "Frontiers: 85 mm single, 180 mm double, one page maximum height; minimum 8 pt; 300 dpi; EPS on acceptance."));
    add(QStringLiteral("Frontiers double-column"),180,230,300,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "Frontiers: 180 mm double column, one page maximum height; minimum 8 pt; 300 dpi."));
    add(QStringLiteral("IEEE single-column"),88.9,220,600,
        10,11,10,9,9,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "IEEE: 88.9 mm single, 182 mm double, 220 mm max height; graphics text 9-10 pt at full size; 600 dpi line art, 300 halftone; PS, EPS or PDF."));
    add(QStringLiteral("IEEE double-column"),182,220,600,
        10,11,10,9,9,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "IEEE: 182 mm double column, 220 mm max height; text 9-10 pt at full size; 600 dpi line art."));
    add(QStringLiteral("ACM (sigconf)"),86.9,220,300,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "ACM TAPS: 86.9 mm single and 177.8 mm double for sigconf/sigplan (acmsmall 139.7 mm, acmlarge 158.8 mm); 300 dpi; PS, EPS, PDF or EMF."));
    add(QStringLiteral("ACM double-column"),177.8,220,300,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "ACM TAPS: 177.8 mm double column for sigconf/sigplan; 300 dpi."));
    add(QStringLiteral("ACS single-column"),84.7,232.9,1200,
        5.5,6.5,5.5,4.5,4.5,0.75,3.0,
        QStringLiteral("Helvetica"),
        QStringLiteral(
            "ACS: 84.7 mm single (240 pt), 177.8 mm double, 232.9 mm max depth including caption; minimum 4.5 pt text; Helvetica or Arial; 1200 dpi line art, 600 greyscale, 300 colour; EPS."));
    add(QStringLiteral("ACS double-column"),177.8,232.9,1200,
        5.5,6.5,5.5,4.5,4.5,0.75,3.0,
        QStringLiteral("Helvetica"),
        QStringLiteral(
            "ACS: 177.8 mm double column, 232.9 mm max depth including the caption; minimum 4.5 pt; EPS."));
    add(QStringLiteral("RSC single-column"),83,233,600,
        8,9,8,7,7,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "RSC (Chemical Science): 83 mm single, 171 mm double, 233 mm max height; minimum 7 pt; 600 dpi; EPS or PDF, converted to TIFF on publication."));
    add(QStringLiteral("RSC double-column"),171,233,600,
        8,9,8,7,7,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "RSC: 171 mm double column, 233 mm max height; minimum 7 pt; 600 dpi."));
    add(QStringLiteral("AIP (Applied Physics Letters)"),85,211,600,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "AIP: 85 mm single, 170 mm double, 211 mm max height; minimum 8 pt; 600 dpi line art, 264 dpi halftone (600 combination, 300 colour online); EPS preferred, also PS, SVG, PDF."));
    add(QStringLiteral("AIP double-column"),170,211,600,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "AIP: 170 mm double column, 211 mm max height; minimum 8 pt; 600 dpi line art."));
    add(QStringLiteral("APS (Physical Review)"),85,210,600,
        8,9,8,7,7,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "APS: 85 mm single column; minimum capital-letter height 2 mm at final size; 600 dpi for scanned art; PS or EPS. APS state no double-column width or halftone floor."));
    add(QStringLiteral("IOP (Physics in Medicine & Biology)"),85,210,600,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "IOP: 85 mm single, 150 mm double; text 8-12 pt at final size; Times, Helvetica, Courier or Symbol; vector EPS or PDF."));
    add(QStringLiteral("IOP double-column"),150,210,600,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "IOP: 150 mm double column; text 8-12 pt at final size; vector EPS or PDF."));
    add(QStringLiteral("Copernicus / EGU"),80,220,300,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "Copernicus/EGU: 80 mm minimum width; ONE font family throughout, Arial or Helvetica; 300 dpi; EPS or PDF first choice. Copernicus state no maximum width."));
    add(QStringLiteral("EMBO Press"),87,230,600,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Helvetica"),
        QStringLiteral(
            "EMBO: 87 mm single, 180 mm double; 600 dpi where there is fine detail or text, 300 otherwise; AI, EPS or high-resolution PDF; Helvetica, Times, Symbol or Courier."));
    add(QStringLiteral("EMBO double-column"),180,230,600,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Helvetica"),
        QStringLiteral(
            "EMBO: 180 mm double column; 600 dpi for fine detail or text, 300 otherwise."));
    add(QStringLiteral("AIAA"),82.6,220,600,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "AIAA: 82.6 mm single, 177.8 mm double; minimum 8 pt; 600 dpi line art, 300 halftone; EPS."));
    add(QStringLiteral("AIAA double-column"),177.8,220,600,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "AIAA: 177.8 mm double column; minimum 8 pt; 600 dpi line art, 300 halftone; EPS."));
    add(QStringLiteral("ASME"),90.5,190,600,
        7,8,7,6,6,0.75,3.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "ASME: 90.5 mm single, 190.5 mm double, full page 9 x 7.5 in; minimum 6 pt; Arial, Helvetica, Times New Roman or Courier as TrueType or Adobe fonts."));
    add(QStringLiteral("ASME double-column"),190.5,190,600,
        7,8,7,6,6,0.75,3.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "ASME: 190.5 mm double column, full page 9 x 7.5 in; minimum 6 pt."));
    add(QStringLiteral("ASCE conference"),165,220,600,
        13,14,13,12,12,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "ASCE: 1 in margins; 12 pt preferred within figures; serif such as Times Roman. ASCE state no column width - the width here is the text width at those margins."));
    add(QStringLiteral("AMS (meteorology)"),80,220,1000,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "AMS: 80 mm single, 140 mm double, 165 mm for wider than two columns; 1000-1200 dpi line art, 600-900 combination, 300 halftone; EPS or PDF."));
    add(QStringLiteral("AMS double-column"),140,220,1000,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "AMS: 140 mm double column, 165 mm maximum; 1000-1200 dpi line art, 300 halftone."));
    add(QStringLiteral("GSA (geology)"),91,225,1200,
        8,9,8,7,7,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "GSA: 91 mm single, 122.8 mm two-column and 185 mm page width in their three-column layout (59 mm for one of three); 225 mm max height; text 7-12 pt, part labels 13-16 pt; 1200 dpi line art, 300-600 halftone; AI, EPS or PDF."));
    add(QStringLiteral("GSA full page"),185,225,1200,
        8,9,8,7,7,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "GSA: 185 mm page width, 225 mm max height; text 7-12 pt; 1200 dpi line art; AI, EPS or PDF."));
    add(QStringLiteral("Royal Society"),85,220,300,
        8.5,9.5,8.5,7.5,7.5,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "Royal Society: minimum 7.5 pt, 9 pt preferred; Times New Roman; 300 dpi; PS or EPS. They state no column width - the width here is a default."));
    add(QStringLiteral("ASM (microbiology)"),177.8,228.6,300,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "ASM: 177.8 mm maximum width, 228.6 mm maximum height; 300-600 dpi; EPS; Arial, Helvetica or Times New Roman."));
    add(QStringLiteral("Karger"),57,223,300,
        8,9,8,7,7,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "Karger: width anywhere from 57 to 180 mm, 223 mm max height; text 7-11 pt; Arial or Segoe UI; 300 dpi (600 combination); EPS, AI, PDF or SVG."));
    add(QStringLiteral("Karger full width"),180,223,300,
        8,9,8,7,7,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "Karger: 180 mm maximum width, 223 mm max height; text 7-11 pt; Arial or Segoe UI."));
    add(QStringLiteral("PeerJ"),170,220,300,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "PeerJ: 300 dpi, 3000 px ideal and 900 px minimum on the long edge; text at least 2 mm high; PDF or EPS. PeerJ state no column width - the width here is a default."));
    add(QStringLiteral("PubMed Central deposit"),170,220,900,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "PMC: 900-1200 dpi line art, 500-900 combination, 300 halftone; minimum 8 pt (10 pt in equations); EPS accepted, SVG is NOT."));
    add(QStringLiteral("Ecological Society of America"),85,240,600,
        7,8,7,6,6,0.75,3.0,
        QStringLiteral("Helvetica"),
        QStringLiteral(
            "ESA: 85 mm single, 180 mm double, 240 mm max height (76 mm minimum on accepted manuscripts); text 6-10 pt; 300-600 dpi; EPS, PS, PDF or AI."));
    add(QStringLiteral("Ecological Society of America double-column"),180,240,600,
        7,8,7,6,6,0.75,3.0,
        QStringLiteral("Helvetica"),
        QStringLiteral(
            "ESA: 180 mm double column, 240 mm max height; text 6-10 pt; 600 dpi on accepted manuscripts."));
    add(QStringLiteral("F1000Research"),150,220,600,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "F1000Research: width 75-150 mm; minimum 8 pt; 600 dpi line art, 500 mixed, 300 halftone; EPS or Illustrator; Times New Roman."));
    add(QStringLiteral("Cell and Tissue Research"),84,234,1200,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "Springer (Cell and Tissue Research): 39 mm sub-column, 84 mm single, 129 mm 1.5-column, 174 mm double, 234 mm max height; 1200 dpi line art, 600 combination, 300 halftone."));
    add(QStringLiteral("Journal of Materials Research"),76.2,220,1200,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "Cambridge (Journal of Materials Research): 76.2 mm minimum, 152.4 mm double; 1200 dpi line art, 350 dpi halftone; EPS."));
    add(QStringLiteral("Journal of Paleontology"),90,240,1200,
        10,11,10,9,9,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "Cambridge (Journal of Paleontology): 90 mm single, 180 mm double, 240 mm max height; minimum 9 pt; 1200 dpi line art, 600 halftone, 800 combination; sans serif; EPS with fonts embedded."));
    add(QStringLiteral("Paleobiology"),70,210,1200,
        10,11,10,9,9,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "Cambridge (Paleobiology): 70 mm single, 150 mm double, 210 mm max height; minimum 9 pt; 1200 dpi line art, 600 halftone, 800 combination."));
    add(QStringLiteral("The Lancet"),170,220,300,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "Lancet: 300 dpi minimum; EPS, AI, PDF, PSD, TIFF or JPEG. The Lancet publish no column widths, heights, font sizes or fonts and refer authors to an artwork helpline - everything here except the resolution and formats is a GraphVis default."));
    add(QStringLiteral("AGU"),170,220,300,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Arial"),
        QStringLiteral(
            "AGU: JPG, TIFF, EPS, PS or PDF, with colour-accessibility guidance. AGU publish no widths, DPI floor, font sizes or fonts - everything here except the formats is a GraphVis default."));
    add(QStringLiteral("Optica / OSA"),66,210,300,
        9,10,9,8,8,1.0,4.0,
        QStringLiteral("Times New Roman"),
        QStringLiteral(
            "Optica: 66 mm (2.6 in) maximum for figures placed side by side; caption text 8 pt; Arial for headers, Times New Roman for body. Optica's electronic-art page publishes no other specifics."));

    return out;
}

PublicationProfile profileByName(const QString& name){
    const QVector<PublicationProfile> all=builtinProfiles();
    for(const PublicationProfile& p:all) if(p.name==name) return p;
    return all.isEmpty()?PublicationProfile():all.first();
}

} // namespace graphvis
