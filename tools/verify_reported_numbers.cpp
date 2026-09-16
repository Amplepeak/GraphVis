// Tier 1, from outside: feed an engine data built from a KNOWN law and check
// the number it puts on the figure against the number the data was made with.
//
// This does not touch the shipped fixtures, so it cannot change a single
// gallery figure. `preparedFor` is public, and the prepared spec carries the
// labels the legend will show - which is exactly where these engines report
// their answer.
#include "QtPlotBackend.h"
#include "PlotSpec.h"
#include <QGuiApplication>
#include <QRegularExpression>
#include <cstdio>
#include <cmath>
#include <functional>
using namespace graphvis;

static PlotSeries col(const QString& name,const QVector<double>& v){
    PlotSeries s; s.label=name; s.y=v;
    for(int i=0;i<v.size();++i) s.x.append(double(i));
    return s;
}

// The OTHER convention. Some engines read a mapped column per series
// (series[0].y is x, series[1].y is y); others read x AND y from ONE series,
// and break after the first. Feeding the second kind two columns makes it fit
// the first column against its own row number, which is how this harness
// first "found" a drag polar with CD0 of 10.6 and an elbow knee at 1.
static PlotSeries curve(const QString& name,const QVector<double>& xs,
                        const QVector<double>& ys){
    PlotSeries s; s.label=name; s.x=xs; s.y=ys;
    return s;
}

static PlotSpec specOf(const QString& engine,const QVector<PlotSeries>& cols){
    PlotSpec s; s.engine=engine; s.title=engine;
    s.series=cols;
    s.parameters=QtPlotBackend::engineParameterDefaults(engine);
    s.style.background=Qt::white; s.style.foreground=QColor(0x11,0x11,0x11);
    return s;
}

static QStringList labelsOf(const PlotSpec& in){
    QtPlotBackend backend;
    const PlotSpec out=backend.preparedFor(in);
    QStringList ls;
    if(!out.title.isEmpty()&&out.title!=in.title) ls<<QStringLiteral("[title] ")+out.title;
    for(const PlotSeries& s:out.series) if(!s.label.isEmpty()) ls<<s.label;
    if(!out.xAxis.label.isEmpty()) ls<<QStringLiteral("[x] ")+out.xAxis.label;
    if(!out.yAxis.label.isEmpty()) ls<<QStringLiteral("[y] ")+out.yAxis.label;
    return ls;
}

int main(int argc,char** argv){
    qputenv("QT_QPA_PLATFORM","offscreen");
    QGuiApplication app(argc,argv);

    struct Case { QString engine; QVector<PlotSeries> cols; QString note; };
    QVector<Case> cases;

    // ---- Drag polar: CD = CD0 + k CL^2, with CD0 = 0.0200 and k = 0.0500.
    {
        QVector<double> cl,cd;
        for(int i=0;i<=40;++i){
            const double c=-0.4+i*0.04;
            cl.append(c); cd.append(0.0200+0.0500*c*c);
        }
        cases.append({QStringLiteral("Drag Polar"),
                      {curve("polar",cd,cl)},
                      QStringLiteral("CD0 0.02, k 0.05")});
    }
    // ---- Bland-Altman: differences are exactly +2 with SD 1, so bias 2 and
    // limits 2 +- 1.96.
    {
        QVector<double> a,b;
        const double d[10]={1,3,1,3,1,3,1,3,1,3};   // mean 2, population SD 1
        for(int i=0;i<10;++i){ a.append(10.0+i); b.append(10.0+i-d[i]); }
        cases.append({QStringLiteral("Bland-Altman"),
                      {col("method A",a),col("method B",b)},
                      QStringLiteral("bias 2, limits 2 +- 1.96")});
    }
    // ---- Gutenberg-Richter: N(>=M) = 10^(a - bM) with b = 1, so each unit of
    // magnitude divides the count by ten.
    {
        QVector<double> mag;
        for(int m=0;m<5;++m){
            const int n=int(std::pow(10.0,4-m));
            for(int k=0;k<n;++k) mag.append(1.0+m+0.001*k/double(qMax(1,n)));
        }
        cases.append({QStringLiteral("Gutenberg-Richter Plot"),
                      {col("magnitude",mag)},
                      QStringLiteral("b = 1")});
    }
    // ---- Double-mass curve: the second series accumulates at exactly twice
    // the first, so the slope must be 2.
    {
        QVector<double> p,q;
        for(int i=1;i<=40;++i){ p.append(double(i)); q.append(2.0*i); }
        cases.append({QStringLiteral("Double-Mass Curve"),
                      {col("station",p),col("reference",q)},
                      QStringLiteral("slope 2")});
    }
    // ---- Cross correlation: the second series is the first delayed by 7, so
    // the peak must be at lag 7.
    {
        QVector<double> u,v;
        for(int i=0;i<200;++i) u.append(std::sin(i*0.21)+0.4*std::sin(i*0.07));
        for(int i=0;i<200;++i) v.append(u[(i-7+200)%200]);
        cases.append({QStringLiteral("Cross Correlation"),
                      {col("a",u),col("b",v)},
                      QStringLiteral("peak lag 7")});
    }
    // ---- Calibration: y = 0.25 x + 0.10 exactly.
    {
        QVector<double> x,y;
        for(int i=0;i<=10;++i){ x.append(i*2.0); y.append(0.25*i*2.0+0.10); }
        cases.append({QStringLiteral("Calibration Curve"),
                      {curve("standard",x,y)},
                      QStringLiteral("slope 0.25, intercept 0.10")});
    }
    // ---- Beam caustic: a parabola with its waist at exactly z = 3.
    {
        QVector<double> z,w;
        for(int i=0;i<=40;++i){
            const double zz=i*0.15;
            z.append(zz); w.append(std::sqrt(4.0+9.0*(zz-3.0)*(zz-3.0)));
        }
        cases.append({QStringLiteral("Beam Caustic"),
                      {col("z",z),col("width",w)},
                      QStringLiteral("waist at z = 3, w0 = 2")});
    }
    // ---- Cottrell: i = k / sqrt(t), so a plot against 1/sqrt(t) is a line
    // through the origin of slope k = 5.
    {
        QVector<double> t,i;
        for(int k=1;k<=40;++k){ const double tt=k*0.25; t.append(tt); i.append(5.0/std::sqrt(tt)); }
        cases.append({QStringLiteral("Cottrell Plot"),
                      {col("time",t),col("current",i)},
                      QStringLiteral("slope 5")});
    }


    // ---- Lorenz curve: a uniform distribution has a Gini coefficient of
    // exactly 1/3, which is the number this engine exists to report.
    {
        QVector<double> v;
        for(int i=1;i<=2000;++i) v.append(double(i));   // uniform ranks
        cases.append({QStringLiteral("Lorenz Curve"),{col("income",v)},
                      QStringLiteral("Gini 1/3 = 0.3333")});
    }
    // ---- Coherence of a signal with ITSELF is 1 at every frequency.
    {
        QVector<double> u;
        quint32 st=0x2545F491u;
        const auto rnd=[&]{ st^=st<<13; st^=st>>17; st^=st<<5; return double(st)/4294967296.0-0.5; };
        for(int i=0;i<2048;++i) u.append(std::sin(i*0.11)+0.5*rnd());
        cases.append({QStringLiteral("Coherence Spectrum"),
                      {col("a",u),col("b",u)},
                      QStringLiteral("coherence 1 everywhere")});
    }
    // ---- Detrended fluctuation analysis: uncorrelated noise has alpha 0.5.
    {
        QVector<double> u;
        quint32 st=0x9E3779B9u;
        const auto rnd=[&]{ st^=st<<13; st^=st>>17; st^=st<<5; return double(st)/4294967296.0-0.5; };
        for(int i=0;i<4000;++i) u.append(rnd());
        QVector<double> idx;
        for(int i=0;i<u.size();++i) idx.append(double(i));
        cases.append({QStringLiteral("Detrended Fluctuation Analysis"),
                      {col("i",idx),col("value",u)},
                      QStringLiteral("alpha 0.5 for white noise")});
    }
    // ---- CUSUM of a series that sits exactly on target must end at zero, and
    // a step of +1 held for 20 points must end at +20.
    {
        QVector<double> v;
        for(int i=0;i<40;++i) v.append(i<20?10.0:11.0);
        cases.append({QStringLiteral("CUSUM Chart"),{col("measurement",v)},
                      QStringLiteral("mean 10.5; deviations sum to zero overall")});
    }
    // ---- Gutenberg-Richter with an exact decade per magnitude unit.
    // ---- Elbow plot: within-cluster scatter with a knee planted at k = 4.
    {
        QVector<double> k,w;
        for(int i=1;i<=10;++i){ k.append(double(i)); w.append(i<=4?(100.0-22.0*(i-1)):(12.0-0.5*(i-4))); }
        cases.append({QStringLiteral("Elbow Plot"),{curve("inertia",k,w)},
                      QStringLiteral("knee at k = 4")});
    }
    // ---- Q-Q against a normal sample: the fitted line should be slope ~1 and
    // intercept ~0 when the data IS standard normal.
    {
        QVector<double> v;
        // Box-Muller from a deterministic uniform stream.
        quint32 st=0x12345677u;
        const auto rnd=[&]{ st^=st<<13; st^=st>>17; st^=st<<5;
                            return (double(st)+1.0)/4294967297.0; };
        for(int i=0;i<4000;++i){
            const double u1=rnd(),u2=rnd();
            v.append(std::sqrt(-2.0*std::log(u1))*std::cos(2.0*M_PI*u2));
        }
        cases.append({QStringLiteral("Q-Q Plot"),{col("sample",v)},
                      QStringLiteral("standard normal: slope 1, intercept 0")});
    }


    // ---- THE FOUR MICHAELIS LINEARISATIONS MUST AGREE. Noiseless data from
    // v = Vmax S / (Km + S) with Vmax = 8.4 and Km = 2.5. Lineweaver-Burk,
    // Eadie-Hofstee and Hanes-Woolf rearrange the same equation differently, so
    // on exact data all three must give back the same two constants. One of
    // them disagreeing is a sign or an algebra error that no picture shows.
    {
        QVector<double> sconc,v;
        for(int i=1;i<=20;++i){
            const double S=i*0.75;
            sconc.append(S); v.append(8.4*S/(2.5+S));
        }
        // Michaelis-Menten reads x and y from ONE series; the three
        // linearisations read a column each. Same data, two conventions.
        cases.append({QStringLiteral("Michaelis-Menten"),
                      {curve("kinetics",sconc,v)},
                      QStringLiteral("Vmax 8.4, Km 2.5")});
        for(const char* eng:{"Lineweaver-Burk Plot",
                             "Eadie-Hofstee Plot","Hanes-Woolf Plot"})
            cases.append({QString::fromLatin1(eng),
                          {col("S",sconc),col("v",v)},
                          QStringLiteral("Vmax 8.4, Km 2.5")});
    }
    // ---- A pure sine at a known frequency must peak at that frequency.
    {
        QVector<double> t,y;
        const double fs=1.0, f0=0.125;          // eight samples per cycle
        for(int i=0;i<1024;++i){ t.append(i/fs); y.append(std::sin(2.0*M_PI*f0*i/fs)); }
        cases.append({QStringLiteral("Power Spectral Density"),
                      {col("t",t),col("signal",y)},
                      QStringLiteral("peak at 0.125 cycles/sample")});
        cases.append({QStringLiteral("Lomb-Scargle Periodogram"),
                      {col("t",t),col("signal",y)},
                      QStringLiteral("peak at f = 0.125, period 8")});
    }
    // ---- AR(1) with phi = 0.7: the partial autocorrelation must be 0.7 at
    // lag 1 and cut off to nothing after it. That cut-off IS the diagnostic.
    {
        QVector<double> y; double prev=0.0;
        quint32 st=0xC0FFEEu;
        const auto rnd=[&]{ st^=st<<13; st^=st>>17; st^=st<<5;
                            return double(st)/4294967296.0-0.5; };
        for(int i=0;i<4000;++i){ prev=0.7*prev+rnd(); y.append(prev); }
        cases.append({QStringLiteral("Partial Autocorrelation"),
                      {col("t",y)},QStringLiteral("phi 0.7 at lag 1, then nothing")});
        cases.append({QStringLiteral("Autocorrelation"),
                      {col("t",y)},QStringLiteral("0.7^k decay")});
    }

    for(const Case& c:cases){
        printf("\n=== %s   (%s)\n",qPrintable(c.engine),qPrintable(c.note));
        const QStringList ls=labelsOf(specOf(c.engine,c.cols));
        if(ls.isEmpty()) printf("    (no labels)\n");
        for(const QString& l:ls) printf("    %s\n",qPrintable(l));
    }
    return 0;
}
