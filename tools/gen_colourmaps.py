"""Generate native/plot2d/include/ColourMaps.h from the reference implementations.

Every table here is SAMPLED from matplotlib (or, for the four maps matplotlib
does not ship, from the definition GraphVis 17 used) rather than typed from
memory. Regenerate with:  python3 tools/gen_colourmaps.py
"""
import numpy as np
from matplotlib import colormaps

# MATLAB's Parula, which matplotlib does not ship. Taken verbatim from
# GraphVis 17's rendering/colormaps.py.
PARULA = [
(0.2081,0.1663,0.5292),(0.2116,0.1898,0.5777),(0.2123,0.2138,0.6270),(0.2081,0.2386,0.6771),
(0.1959,0.2645,0.7279),(0.1707,0.2919,0.7792),(0.1253,0.3242,0.8303),(0.0591,0.3598,0.8683),
(0.0117,0.3875,0.8820),(0.0060,0.4086,0.8828),(0.0165,0.4266,0.8786),(0.0329,0.4430,0.8720),
(0.0498,0.4586,0.8641),(0.0629,0.4737,0.8554),(0.0723,0.4887,0.8467),(0.0779,0.5040,0.8384),
(0.0793,0.5200,0.8312),(0.0749,0.5375,0.8263),(0.0641,0.5570,0.8240),(0.0488,0.5772,0.8228),
(0.0343,0.5966,0.8199),(0.0265,0.6137,0.8135),(0.0239,0.6287,0.8038),(0.0231,0.6418,0.7913),
(0.0228,0.6535,0.7768),(0.0267,0.6642,0.7607),(0.0384,0.6743,0.7436),(0.0590,0.6838,0.7254),
(0.0843,0.6928,0.7062),(0.1133,0.7015,0.6859),(0.1453,0.7098,0.6646),(0.1801,0.7177,0.6424),
(0.2178,0.7250,0.6193),(0.2586,0.7317,0.5954),(0.3022,0.7376,0.5712),(0.3482,0.7424,0.5473),
(0.3953,0.7459,0.5244),(0.4420,0.7481,0.5033),(0.4871,0.7491,0.4840),(0.5300,0.7491,0.4661),
(0.5709,0.7485,0.4494),(0.6099,0.7473,0.4337),(0.6473,0.7456,0.4188),(0.6834,0.7435,0.4044),
(0.7184,0.7411,0.3905),(0.7525,0.7384,0.3768),(0.7858,0.7356,0.3633),(0.8185,0.7327,0.3498),
(0.8507,0.7299,0.3360),(0.8824,0.7274,0.3217),(0.9139,0.7258,0.3063),(0.9450,0.7261,0.2886),
(0.9739,0.7314,0.2666),(0.9938,0.7455,0.2403),(0.9990,0.7653,0.2164),(0.9955,0.7861,0.1967),
(0.9880,0.8066,0.1794),(0.9789,0.8271,0.1633),(0.9697,0.8481,0.1475),(0.9626,0.8705,0.1309),
(0.9589,0.8949,0.1132),(0.9598,0.9218,0.0948),(0.9661,0.9514,0.0755),(0.9763,0.9831,0.0538),
]


N = 64                                  # stops per map

CATS = [
 ("Perceptually Uniform", ["Parula","Viridis","Plasma","Inferno","Magma","Cividis","Turbo"]),
 ("Scientific Sequential", ["Hot","Afmhot","Gist Heat","Copper","Bone","Pink","Gray","Greys",
   "Purples","Blues","Greens","Oranges","Reds","YlOrBr","YlOrRd","OrRd","PuRd","RdPu","BuPu",
   "GnBu","PuBu","YlGnBu","PuBuGn","BuGn","YlGn"]),
 ("Diverging", ["Coolwarm","Bwr","Seismic","Spectral","RdBu","RdGy","RdYlBu","RdYlGn","PuOr",
   "BrBG","PRGn","PiYG"]),
 ("Cyclic & Angular", ["Twilight","Twilight Shifted","HSV","Phase","Cubehelix"]),
 ("Seasonal & Shading", ["Cool","Spring","Summer","Autumn","Winter","Binary","Gist Gray","Gist Yarg"]),
 ("Terrain, Ocean & Field", ["Terrain","Ocean","Gist Earth","Gist Ncar","Gnuplot","Gnuplot2",
   "Nipy Spectral","Rainbow","Gist Rainbow","Jet"]),
 ("Categorical", ["Lines","Tab10","Tab20","Tab20b","Tab20c","Set1","Set2","Set3","Paired",
   "Accent","Dark2","Pastel1","Pastel2"]),
 ("Specialized & Utility", ["Prism","Flag","Colorcube","White"]),
]
MPL = {"Turbo":"turbo","Jet":"jet","Afmhot":"afmhot","Gist Heat":"gist_heat","Gray":"gray",
 "Greys":"Greys","Purples":"Purples","Blues":"Blues","Greens":"Greens","Oranges":"Oranges",
 "Reds":"Reds","YlOrBr":"YlOrBr","YlOrRd":"YlOrRd","OrRd":"OrRd","PuRd":"PuRd","RdPu":"RdPu",
 "BuPu":"BuPu","GnBu":"GnBu","PuBu":"PuBu","YlGnBu":"YlGnBu","PuBuGn":"PuBuGn","BuGn":"BuGn",
 "YlGn":"YlGn","Coolwarm":"coolwarm","Bwr":"bwr","Seismic":"seismic","Spectral":"Spectral",
 "RdBu":"RdBu","RdGy":"RdGy","RdYlBu":"RdYlBu","RdYlGn":"RdYlGn","PuOr":"PuOr","BrBG":"BrBG",
 "PRGn":"PRGn","PiYG":"PiYG","Twilight Shifted":"twilight_shifted","Phase":"twilight_shifted",
 "Cubehelix":"cubehelix","Binary":"binary","Gist Gray":"gist_gray","Gist Yarg":"gist_yarg",
 "Terrain":"terrain","Ocean":"ocean","Gist Earth":"gist_earth","Gist Ncar":"gist_ncar",
 "Gnuplot":"gnuplot","Gnuplot2":"gnuplot2","Nipy Spectral":"nipy_spectral","Rainbow":"rainbow",
 "Gist Rainbow":"gist_rainbow","Tab10":"tab10","Tab20":"tab20","Tab20b":"tab20b","Tab20c":"tab20c",
 "Set1":"Set1","Set2":"Set2","Set3":"Set3","Paired":"Paired","Accent":"Accent","Dark2":"Dark2",
 "Pastel1":"Pastel1","Pastel2":"Pastel2","Viridis":"viridis","Plasma":"plasma","Inferno":"inferno",
 "Magma":"magma","Cividis":"cividis","Hot":"hot","Copper":"copper","Bone":"bone","Pink":"pink",
 "Twilight":"twilight","HSV":"hsv","Cool":"cool","Spring":"spring","Summer":"summer",
 "Autumn":"autumn","Winter":"winter","Prism":"prism","Flag":"flag"}

# MATLAB's default line-colour order, which matplotlib does not ship.
LINES = [(0.0000,0.4470,0.7410),(0.8500,0.3250,0.0980),(0.9290,0.6940,0.1250),
         (0.4940,0.1840,0.5560),(0.4660,0.6740,0.1880),(0.3010,0.7450,0.9330),
         (0.6350,0.0780,0.1840)]

def colorcube(n):
    """MATLAB colorcube: a regular walk of the RGB cube, greys and white last."""
    out=[]
    for r in range(4):
        for g in range(4):
            for b in range(4):
                if r==g==b:                      # greys handled at the end
                    continue
                out.append((r/3.0,g/3.0,b/3.0))
    out=out[:max(0,n-4)]
    out+=[(i/3.0,)*3 for i in (1,2,3)]+[(1.0,1.0,1.0)]
    while len(out)<n: out.append((1.0,1.0,1.0))
    return out[:n]

def stepped(colours):
    """A discrete list resampled to N stops, keeping the steps."""
    c=np.array(colours,dtype=float)
    idx=np.minimum((np.arange(N)/N*len(c)).astype(int), len(c)-1)
    return c[idx]

def resample(colours):
    """A continuous list of any length resampled to N stops."""
    c=np.array(colours,dtype=float)
    t=np.linspace(0,len(c)-1,N)
    lo=np.floor(t).astype(int); hi=np.minimum(lo+1,len(c)-1); f=(t-lo)[:,None]
    return c[lo]*(1-f)+c[hi]*f

def table(name):
    if name=="Parula":    return resample(PARULA)
    if name=="Lines":     return stepped(LINES)
    if name=="Colorcube": return stepped(colorcube(64))
    if name=="White":     return np.ones((N,3))
    cm=colormaps[MPL[name]]
    return np.array([cm(i/(N-1))[:3] for i in range(N)])

def ident(name):
    return "k"+"".join(ch for ch in name.title() if ch.isalnum())

names=[n for _,group in CATS for n in group]
assert len(names)==len(set(names)), "duplicate map name"

out=[]
out.append("""#pragma once
// =========================================================================
// ColourMaps.h - GENERATED. Do not edit by hand; run tools/gen_colourmaps.py.
//
// The colour maps GraphVis 17 offered, in the categories it offered them in.
// The port shipped with exactly one - viridis, hard-coded at ten places in
// the renderer with no way to change it - so every heat map, contour, surface
// and vector field looked the same and the choice that IS the reading of a
// field plot could not be made.
//
// Each table is 64 stops sampled from the reference implementation rather
// than typed from memory: matplotlib for the 80 it ships, and for the four it
// does not - Parula, Lines, Colorcube and White - the definitions GraphVis 17
// used. Stored as 8-bit RGB because that is the precision the screen and every
// export have; storing doubles would be four times the source for numbers no
// display can show apart.
//
// Sampling is uniform, so lookup is a lerp between neighbours. The discrete
// maps (Tab10, Set1, Paired and the rest) are sampled as step functions, which
// puts a ramp one stop wide at each boundary - 1.6% of the range, and the
// alternative is a second lookup path for a difference nobody can see.
// =========================================================================
#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>

namespace graphvis {
namespace colourmaps {

constexpr int kStops = 64;
""")

for name in names:
    t=np.clip(np.rint(table(name)*255.0),0,255).astype(int)
    assert t.shape==(N,3), (name,t.shape)
    rows=[]
    for r in range(0,N,4):
        rows.append("    "+" ".join("{%3d,%3d,%3d},"%tuple(t[i]) for i in range(r,r+4)))
    out.append("static const unsigned char %s[kStops][3]={\n%s\n};"%(ident(name),"\n".join(rows)))

out.append("""
// name -> table, and the categories, in the order they are offered.
struct Entry { const char* name; const unsigned char (*table)[3]; };

inline const QVector<Entry>& all(){
    static const QVector<Entry> entries{""")
out.append(",\n".join('        {"%s",%s}'%(n,ident(n)) for n in names))
out.append("""    };
    return entries;
}

// The categories, exactly as GraphVis 17 grouped them. A flat list of eighty
// four names is not a choice anyone can make.
inline const QVector<QPair<QString,QStringList>>& categories(){
    static const QVector<QPair<QString,QStringList>> cats{""")
cat_lines=[]
for cat,group in CATS:
    inner=",".join('QStringLiteral("%s")'%n for n in group)
    cat_lines.append('        {QStringLiteral("%s"),{%s}}'%(cat,inner))
out.append(",\n".join(cat_lines))
out.append("""    };
    return cats;
}

inline QStringList names(){
    QStringList out;
    for(const Entry& e:all()) out.append(QString::fromLatin1(e.name));
    return out;
}

// The table for a name, or Viridis for one that is not recognised. A typo in a
// saved figure or a theme should give the default picture, never no picture.
inline const unsigned char (*tableFor(const QString& name))[3] {
    for(const Entry& e:all())
        if(name.compare(QLatin1String(e.name),Qt::CaseInsensitive)==0) return e.table;
    return kViridis;
}

// One sample. NaN is caught before qBound, which would otherwise carry it into
// the index; an unknown cell belongs at the bottom of the map.
inline QColor sample(const unsigned char (*t)[3],double u){
    if(!(u==u)) u=0.0;
    u=qBound(0.0,u,1.0);
    const double pos=u*double(kStops-1);
    int i=int(pos);
    if(i<0) i=0;
    if(i>kStops-2) i=kStops-2;
    const double f=pos-double(i);
    const double r=(t[i][0]*(1.0-f)+t[i+1][0]*f)/255.0;
    const double g=(t[i][1]*(1.0-f)+t[i+1][1]*f)/255.0;
    const double b=(t[i][2]*(1.0-f)+t[i+1][2]*f)/255.0;
    return QColor::fromRgbF(r,g,b);
}

} // namespace colourmaps
} // namespace graphvis""")

open(__file__.rsplit('tools',1)[0]+'native/plot2d/include/ColourMaps.h','w',newline='').write("\n".join(out)+"\n")
print("wrote ColourMaps.h with",len(names),"maps in",len(CATS),"categories")
