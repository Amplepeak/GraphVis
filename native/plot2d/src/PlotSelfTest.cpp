#include "PlotSelfTest.h"
#include "QtPlotBackend.h"

#include <QFile>
#include <QHash>
#include <QImage>
#include <QFileInfo>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QElapsedTimer>
#include <QtMath>
#include <cmath>
#include <cstdio>

namespace graphvis {

// Renders every supported engine and reports which of them drew nothing.
//
// The vector-export test above proves one figure exports correctly. This proves
// the other thirty do not crash, do not hang, and put ink on the page - which
// is the failure mode that matters for an engine expressed as a rewrite: a
// wrong transformation produces an empty plot, not a compile error, and nobody
// notices until they try to use that catalogue entry.
bool runEngineSweep(){
    // Data shaped to satisfy every engine at once: a time base, a decaying
    // signal, a paired measurement for agreement plots, a probability column
    // for ROC and volcano, and a binary label.
    const int n=240;
    PlotSeries a,b,c,d,e;
    a.label=QStringLiteral("signal");
    b.label=QStringLiteral("paired");
    c.label=QStringLiteral("p_value");
    d.label=QStringLiteral("label");
    // A genuine trade-off, so Pareto Front has a front to find. With four
    // co-increasing columns only the first point is ever non-dominated, and the
    // engine correctly drew a one-point line - which looks exactly like a bug.
    e.label=QStringLiteral("cost");
    for(int i=0;i<n;++i){
        const double t=0.05+i*0.05;
        const double base=2.0+3.0*(1.0-std::exp(-t/2.0));
        a.x.append(t); a.y.append(base+0.25*std::sin(t*3.0));
        b.x.append(t); b.y.append(base+0.18*std::cos(t*2.0));
        c.x.append(t); c.y.append(qBound(1e-6,std::exp(-t*0.7),1.0));
        d.x.append(t); d.y.append((i%3==0)?1.0:0.0);
        e.x.append(t); e.y.append(9.0-base+0.3*std::sin(t*1.7));
    }

    QtPlotBackend backend;
    const QStringList engines=backend.supportedEngines();
    QStringList blank;

    // A frame with no data at all, so "did this engine draw anything" is
    // measured against the chrome rather than against a guessed number. The
    // first version of this test used a fixed threshold and reported five
    // working engines as broken, because a single thin curve puts far less ink
    // on the page than four dense ones.
    // "Did this engine draw anything" is measured against that engine's OWN
    // empty frame. A fixed threshold reported five working engines as broken,
    // because a single thin curve puts far less ink on the page than four dense
    // ones. A shared baseline then reported the polar engines as broken,
    // because they have no rectangular frame at all and draw less chrome than
    // the engine the baseline came from.
    auto imageOf=[&](const PlotSpec& probe){
        QImage canvas(640,400,QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::white);
        {
            QPainter painter(&canvas);
            painter.setRenderHint(QPainter::Antialiasing,true);
            backend.render(&painter,QRectF(0,0,640,400),probe);
        }
        return canvas;
    };
    auto inkFor=[&](const PlotSpec& probe){
        const QImage canvas=imageOf(probe);
        qint64 ink=0;
        for(int y=0;y<canvas.height();y+=2){
            const QRgb* row=reinterpret_cast<const QRgb*>(canvas.constScanLine(y));
            for(int x=0;x<canvas.width();x+=2)
                if(qRed(row[x])<250||qGreen(row[x])<250||qBlue(row[x])<250) ++ink;
        }
        return ink;
    };
    // Pixels that DIFFER between two renders of the same figure.
    //
    // This replaces counting non-white pixels, which cannot see a curve that
    // lands on the gridlines it overdraws: a blue line through a grey gridline
    // changes that pixel without adding a non-white one, so a correct thin
    // curve can come out with LESS "ink" than its own empty frame. That is not
    // a near miss, it is a measure that answers a different question from the
    // one being asked. What is wanted is "did anything appear", and the way to
    // ask it is to compare the two pictures.
    auto changedPixels=[](const QImage& a,const QImage& b){
        qint64 n=0;
        for(int y=0;y<a.height();++y){
            const QRgb* pa=reinterpret_cast<const QRgb*>(a.constScanLine(y));
            const QRgb* pb=reinterpret_cast<const QRgb*>(b.constScanLine(y));
            for(int x=0;x<a.width();++x) if(pa[x]!=pb[x]) ++n;
        }
        return n;
    };

    qint64 baseline=0;
    {
        PlotSpec empty;
        empty.engine=QStringLiteral("Line Chart");
        empty.title=QStringLiteral("baseline");
        empty.xAxis.label=QStringLiteral("time_h");
        empty.yAxis.label=QStringLiteral("value");
        empty.style.background=Qt::white;
        empty.style.foreground=QColor(0x11,0x11,0x11);
        empty.style.gridColor=QColor(0xd8,0xd8,0xd8);
        baseline=inkFor(empty);
    }
    printf("selftest: empty-frame baseline %lld px\n",static_cast<long long>(baseline));

    // A few engines do not read a measurement at all. An edge list is a list of
    // pairs, a mosaic is a contingency table, an UpSet plot is set membership,
    // a Kaplan-Meier needs an event flag beside its times, and a Smith chart
    // reads an impedance. Sweeping those with the shared signal asks them to
    // read 240 floating-point values as node identifiers or as category labels,
    // and drawing nothing is the CORRECT answer to that - so the sweep would be
    // reporting a right answer as a fault.
    //
    // Each entry below is that engine's real input shape, so the sweep still
    // demands ink from every engine; it just stops demanding it from nonsense.
    const auto column=[](const QString& label,const QVector<double>& values){
        PlotSeries s; s.label=label; s.y=values; s.x.reserve(values.size());
        for(int i=0;i<values.size();++i) s.x.append(double(i));
        return s;
    };
    QVector<double> edgeFrom,edgeTo,edgeWeight;
    for(int i=0;i<12;++i){
        edgeFrom.append(double(i%6));
        edgeTo.append(double((i+2)%6));
        edgeWeight.append(1.0+double(i%4));
    }
    QVector<double> categoryA,categoryB,tally;
    for(int i=0;i<40;++i){
        categoryA.append(double(i%4));
        categoryB.append(double((i/4)%3));
        tally.append(1.0+double((i*7)%5));
    }
    QVector<double> lifetimes,events,memberA,memberB,memberC;
    QVector<double> resistance,reactance;
    // Inputs for batch 1: a risk score with a matching outcome, cluster labels
    // with silhouette coefficients, a fleet's repair ages, and a set of study
    // effects with their standard errors and intervals.
    QVector<double> riskScore,outcome,clusterId,silhouette,repairAge,repairUnit;
    QVector<double> effect,stdErr,variance,ciLo,ciHi;
    for(int i=0;i<80;++i){
        const double p=double(i)/79.0;
        riskScore.append(p);
        // Correlated with the score rather than random, so precision and recall
        // actually separate and the figure has something to show.
        outcome.append((std::fmod(double(i)*0.37,1.0)<p)?1.0:0.0);
        clusterId.append(double(i%4));
        silhouette.append(0.75-0.5*std::fmod(double(i)*0.13,1.0));
        repairAge.append(1.0+double(i)*0.9);
        repairUnit.append(double(i%12));
    }
    for(int i=0;i<24;++i){
        const double e=0.35+0.5*std::sin(double(i)*0.7);
        const double se=0.08+0.03*double(i%5);
        effect.append(e);
        stdErr.append(se);
        variance.append(se*se);
        ciLo.append(e-1.96*se);
        ciHi.append(e+1.96*se);
    }
    // Batch 2's inputs. Each is the physical shape its engine reads.
    QVector<double> reynolds,frictionFactor,pumpFlow,pumpHead,hersey,friction;
    QVector<double> meanStress,altStress,deltaK,growthRate,reversals,strainAmp;
    QVector<double> creepTemp,creepHours,creepStress;
    QVector<double> shaftSpeed,mode1,mode2,orbitX,orbitY;
    QVector<double> liquidLimit,plasticIndex,grainSize,percentFiner;
    QVector<double> openPhase,openGain,poleRe,poleIm,busPower,busVoltage;
    QVector<double> chord,cpUpper,cpLower;
    for(int i=0;i<50;++i){
        const double u=double(i)/49.0;
        reynolds.append(5.0e3*std::pow(1.35,double(i)));       // 5e3 up to ~1e9
        frictionFactor.append(0.02+0.012*std::sin(u*6.0));
        pumpFlow.append(u*120.0);
        pumpHead.append(60.0-0.0028*u*120.0*u*120.0);          // a falling pump curve
        hersey.append(1e-4*std::pow(1.25,double(i)));
        friction.append(0.14*std::exp(-double(i)*0.18)+0.002*double(i));  // a real minimum
        meanStress.append(u*400.0);
        altStress.append(200.0*(1.0-u)*0.8+15.0);
        deltaK.append(6.0*std::pow(1.06,double(i)));
        growthRate.append(1e-9*std::pow(6.0*std::pow(1.06,double(i)),3.2));  // Paris, m=3.2
        reversals.append(1.0e3*std::pow(1.22,double(i)));
        strainAmp.append(0.02*std::pow(1.0e3*std::pow(1.22,double(i)),-0.12));
        creepTemp.append(800.0+double(i%5)*40.0);
        creepHours.append(50.0*std::pow(1.15,double(i)));
        creepStress.append(300.0*std::exp(-double(i)*0.03));
        shaftSpeed.append(double(i)*240.0);
        mode1.append(40.0+0.004*double(i)*240.0);
        mode2.append(120.0+0.002*double(i)*240.0);
        const double th=u*4.0*M_PI;
        orbitX.append(30.0*std::cos(th));
        orbitY.append(18.0*std::sin(th)+6.0*std::sin(2.0*th));  // a figure of eight
        liquidLimit.append(25.0+u*55.0);
        plasticIndex.append(0.73*(25.0+u*55.0-20.0)+4.0*std::sin(u*9.0));
        grainSize.append(0.002*std::pow(1.25,double(i)));
        percentFiner.append(qBound(0.0,100.0*u,100.0));
        openPhase.append(-30.0-u*200.0);
        openGain.append(30.0-u*55.0);
        poleRe.append(-1.0-2.0*std::fmod(double(i)*0.31,1.0));
        poleIm.append(-4.0+u*8.0);
        busPower.append(u*u*(2.0-u)*100.0);                     // rises then turns back
        busVoltage.append(1.02-0.55*u*u);
        chord.append(u);
        cpUpper.append(-2.6*std::exp(-u*3.0)+0.4);
        cpLower.append(0.9*std::exp(-u*2.0)-0.1);
    }
    // Batch 3's inputs.
    QVector<double> substrate,rate,boundLigand,freeLigand,velocity,plateHeight;
    QVector<double> relPressure,adsorbed,moleFraction,jobSignal,magnitudes;
    QVector<double> stationRain,referenceRain,windU,windV;
    QVector<double> spiroVolume,spiroFlow,lvVolume,lvPressure,clockPhase;
    QVector<double> paschenPd,paschenV,obsTime,obsFlux,trialPeriod;
    QVector<double> epochNumber,observedTime;
    for(int i=0;i<40;++i){
        const double u=double(i)/39.0;
        const double sconc=0.05*std::pow(1.16,double(i));
        substrate.append(sconc);
        rate.append(12.0*sconc/(0.8+sconc));              // Michaelis-Menten, Vmax 12, Km 0.8
        const double f=0.02*std::pow(1.15,double(i));
        freeLigand.append(f);
        boundLigand.append(90.0*f/(1.5+f));               // saturation binding
        const double uu=0.05+u*1.2;
        velocity.append(uu);
        plateHeight.append(4.0+2.5/uu+3.0*uu);            // van Deemter A=4, B=2.5, C=3
        const double pr=0.01+u*0.85;
        relPressure.append(pr);
        adsorbed.append(30.0*pr/((1.0-pr)*(1.0+9.0*pr))); // a BET isotherm, C=10
        moleFraction.append(u);
        jobSignal.append(u*(1.0-u)*4.0);                  // peaks at 0.5, a 1:1 complex
        stationRain.append(20.0+8.0*std::sin(u*7.0));
        referenceRain.append(22.0+7.0*std::cos(u*5.0));
        const double zkm=u*10.0;
        windU.append(4.0+zkm*1.8);
        windV.append(2.0+6.0*std::sin(zkm*0.35));         // a curved hodograph
        const double th=u*2.0*M_PI;
        spiroVolume.append(2.5+2.0*std::cos(th));
        spiroFlow.append(6.0*std::sin(th)*(1.0-0.4*std::cos(th)));
        lvVolume.append(90.0+45.0*std::cos(th));
        lvPressure.append(60.0+58.0*std::sin(th)+35.0*std::sin(2.0*th));
        paschenPd.append(0.1*std::pow(1.3,double(i)));
        paschenV.append(400.0+900.0*std::abs(std::log10(0.1*std::pow(1.3,double(i))/1.0)));
        obsTime.append(u*37.0);
        obsFlux.append(1.0-0.25*std::pow(std::cos(M_PI*std::fmod(u*37.0,2.7)/2.7),8.0));
        epochNumber.append(double(i));
        observedTime.append(2450000.0+double(i)*2.7+0.004*std::sin(u*6.0));
    }
    trialPeriod.append(2.7);
    // 200 magnitudes on a Gutenberg-Richter distribution: N(>=m) falls by a
    // decade per unit magnitude, which is a b-value of 1.
    for(int i=0;i<200;++i){
        const double p=(double(i)+0.5)/200.0;
        magnitudes.append(2.0-std::log10(1.0-p*0.999));
    }
    // A clock with white frequency noise plus a slow drift, so the Allan
    // deviation has a real minimum rather than a monotone slope.
    {
        double drift=0.0;
        for(int i=0;i<512;++i){
            drift+=0.0004;
            clockPhase.append(drift+0.02*std::sin(double(i)*2.399963));
        }
    }
    // Batch 4's inputs.
    QVector<double> iceFeature,icePrediction,iceId,avpY,avpFocus,avpOther;
    QVector<double> factorA,factorB,cellValue,swingLow,swingHigh;
    QVector<double> portRisk,portReturn,incEffect,incCost;
    QVector<double> lasSubject,lasTime,lasValue,swimSubject,swimStart,swimStop;
    QVector<double> leverage,studentised;
    for(int i=0;i<120;++i){
        const double u=double(i)/119.0;
        // Twelve individuals, each with its own slope, so the ICE curves fan
        // out and the average is genuinely their average.
        const double who=double(i%12);
        iceId.append(who);
        iceFeature.append(std::fmod(u*12.0,1.0));
        icePrediction.append(0.3+who*0.05+std::fmod(u*12.0,1.0)*(0.2+who*0.06));
        avpOther.append(u*10.0);
        avpFocus.append(u*10.0*0.6+3.0*std::sin(u*8.0));
        avpY.append(2.0*(u*10.0*0.6+3.0*std::sin(u*8.0))+0.5*u*10.0+std::cos(u*5.0));
        factorA.append(double(i%4));
        factorB.append(double((i/4)%3));
        // A real interaction: B changes the slope across A rather than
        // shifting it, so the lines cross.
        cellValue.append(5.0+double(i%4)*(1.0-double((i/4)%3)));
        portRisk.append(0.04+0.14*u+0.02*std::sin(u*11.0));
        portReturn.append(0.02+0.10*std::sqrt(u)+0.012*std::cos(u*9.0));
        incEffect.append(0.6+0.9*std::sin(u*6.3)+0.4*std::cos(u*2.1));
        incCost.append(9000.0*(0.6+0.9*std::sin(u*6.3))+3000.0*std::cos(u*3.7));
        lasSubject.append(double(i%20));
        lasTime.append(double(i/20));
        lasValue.append(4.0+2.0*std::sin(double(i%20)*0.4+double(i/20)*0.9));
        leverage.append(0.01+0.12*std::fmod(double(i)*0.37,1.0));
        studentised.append(3.2*std::sin(double(i)*0.9)*std::fmod(double(i)*0.11,1.0));
    }
    for(int i=0;i<14;++i){
        const double w=1.0-double(i)/14.0;
        swingLow.append(-w*40.0-2.0);
        swingHigh.append(w*55.0+2.0);
        swimSubject.append(double(i));
        swimStart.append(double(i%3)*1.5);
        swimStop.append(double(i%3)*1.5+4.0+double(i)*1.3);
    }
    // A Duane plot reads cumulative time on x and cumulative failures on y,
    // which is the one engine here that needs its x column shaped too.
    PlotSeries duaneSeries;
    duaneSeries.label=QStringLiteral("growth");
    for(int i=1;i<=60;++i){
        const double t=double(i)*25.0;
        duaneSeries.x.append(t);
        // Failures accruing more slowly than time, which is growth.
        duaneSeries.y.append(std::pow(t,0.65)*0.4);
    }
    for(int i=0;i<60;++i){
        lifetimes.append(1.0+double(i)*0.7);
        events.append((i%4==0)?0.0:1.0);          // a quarter censored
        memberA.append((i%2==0)?1.0:0.0);
        memberB.append((i%3==0)?1.0:0.0);
        memberC.append((i%5==0)?1.0:0.0);
        resistance.append(0.2+double(i)*0.08);    // a sweep across the chart
        reactance.append(-1.5+double(i)*0.05);
    }
    // Batch 5's inputs. Every one is generated from the equation the engine
    // claims to fit, with known constants, so a plot that draws a curve through
    // the wrong numbers can be caught by reading its legend rather than by
    // looking at the shape and thinking it seems about right.
    QVector<double> tafelEta,tafelJ,cvPotential,cvCurrent;
    QVector<double> rotation,limitingCurrent,scanRate,peakCurrent;
    QVector<double> monodS,monodMu,inhibitedRate;
    QVector<double> ragonePower,ragoneEnergy,ragoneFamily;
    QVector<double> pvVoltage,pvCurrent,outsideTemp,meteredEnergy;
    QVector<double> abateQty,abateCost;
    QVector<double> sigmaX,sigmaY,tauXY,pmMoment,pmAxial;
    QVector<double> pushDisp,pushShear,specPeriod,specAccel;
    QVector<double> consolTime,consolSettle,lombTime,lombValue,waffleShare;
    {
        // Butler-Volmer with j0 = 1e-4 and Tafel slopes of 120 mV/decade
        // anodic, -100 cathodic. Both branches, so the two fits have something
        // to separate.
        for(int i=0;i<80;++i){
            const double eta=-0.40+0.80*double(i)/79.0;
            if(std::abs(eta)<0.01) continue;
            const double slope=(eta>0.0)?0.120:-0.100;
            tafelEta.append(eta);
            tafelJ.append((eta>0.0?1.0:-1.0)*1e-4*std::pow(10.0,eta/slope));
        }
        // A reversible couple: a forward sweep and a reverse one that do NOT
        // retrace, so a voltammogram that sorted its points would collapse
        // visibly.
        for(int i=0;i<=120;++i){
            const double v=-0.4+0.8*double(i)/120.0;
            cvPotential.append(v);
            cvCurrent.append(4.0e-5*std::exp(-std::pow((v-0.13)/0.05,2.0)));
        }
        for(int i=120;i>=0;--i){
            const double v=-0.4+0.8*double(i)/120.0;
            cvPotential.append(v);
            cvCurrent.append(-3.6e-5*std::exp(-std::pow((v-0.07)/0.05,2.0)));
        }
        // Levich: i = 0.62 * sqrt(omega), with a kinetic term so the
        // Koutecky-Levich intercept is 1/0.004 rather than zero.
        for(int i=1;i<=12;++i){
            const double omega=double(i)*100.0;
            rotation.append(omega);
            const double diffusion=0.0006*std::sqrt(omega);
            limitingCurrent.append(1.0/(1.0/0.004+1.0/diffusion));
            scanRate.append(double(i)*0.01);
            peakCurrent.append(0.0025*std::sqrt(double(i)*0.01));
        }
        // Monod with umax 0.45 and Ks 12, and the same culture inhibited above
        // Ki 300 - so the second curve peaks at sqrt(12*300) = 60.
        for(int i=1;i<=40;++i){
            const double s=double(i)*15.0;
            monodS.append(s);
            monodMu.append(0.45*s/(12.0+s));
            inhibitedRate.append(0.45*s/(12.0+s+s*s/300.0));
        }
        // Three device families a decade apart, which is what a Ragone plot is
        // drawn to separate.
        for(int i=0;i<36;++i){
            const double u=double(i%12)/11.0;
            const double family=double(i/12);
            ragoneFamily.append(family);
            const double p=std::pow(10.0,1.0+family*1.4+u*1.2);
            ragonePower.append(p);
            ragoneEnergy.append(std::pow(10.0,2.4-family*1.1-u*0.8));
        }
        // A single-diode cell: Isc 5 A, Voc 0.6 V.
        for(int i=0;i<=60;++i){
            const double v=0.62*double(i)/60.0;
            const double current=5.0-1e-9*(std::exp(v/0.026)-1.0);
            pvVoltage.append(v);
            pvCurrent.append(qMax(0.0,current));
        }
        // A building with a 15.5 C balance point and 40 kWh per degree-day.
        for(int i=0;i<48;++i){
            const double t=-2.0+double(i)*0.6;
            outsideTemp.append(t);
            meteredEnergy.append(300.0+40.0*qMax(0.0,15.5-t)
                                 +18.0*std::sin(double(i)*1.7));
        }
        // Measures spanning negative to positive cost, and widths differing by
        // a factor of forty - which is the thing the block widths exist to
        // show and a bar chart of cost alone cannot.
        for(int i=0;i<14;++i){
            abateQty.append(0.5+std::fmod(double(i)*7.0,20.0));
            abateCost.append(-60.0+double(i)*18.0);
        }
        // Three stress states, including one in pure shear.
        sigmaX={120.0,80.0,0.0}; sigmaY={40.0,-30.0,0.0}; tauXY={30.0,50.0,45.0};
        // A column envelope: pure compression at the top, the balanced point
        // bulging out to the right, pure bending at the bottom.
        for(int i=0;i<=36;++i){
            const double a=2.0*3.14159265358979323846*double(i)/36.0;
            pmMoment.append(qMax(0.0,220.0*std::sin(a)));
            pmAxial.append(1400.0*std::cos(a)+400.0);
        }
        // A pushover with an elastic branch, a knee and a plateau.
        for(int i=0;i<=50;++i){
            const double d=double(i)*0.006;
            pushDisp.append(d);
            pushShear.append(1800.0*(1.0-std::exp(-d/0.035)));
        }
        // A code-shaped spectrum: a short-period plateau, then a 1/T branch.
        for(int i=0;i<=60;++i){
            const double t=0.05*std::pow(10.0,2.0*double(i)/60.0);
            specPeriod.append(t);
            specAccel.append(t<0.5?0.9:0.9*0.5/t);
        }
        // Primary consolidation to 8 mm, then secondary compression.
        for(int i=0;i<=40;++i){
            const double t=std::pow(10.0,-1.0+4.0*double(i)/40.0);
            consolTime.append(t);
            consolSettle.append(8.0*(1.0-std::exp(-t/60.0))+0.35*std::log10(1.0+t));
        }
        // A 4.7-unit period sampled at UNEVEN times, which is the case an FFT
        // periodogram cannot take and this one is for.
        {
            double t=0.0;
            for(int i=0;i<220;++i){
                t+=0.35+0.55*std::fmod(double(i)*0.618034,1.0);
                lombTime.append(t);
                lombValue.append(std::sin(2.0*3.14159265358979323846*t/4.7)
                                 +0.25*std::cos(double(i)*1.31));
            }
        }
        // Shares that do not divide into a hundred, so the largest-remainder
        // apportionment has something to do.
        waffleShare={37.0,29.0,21.0,13.0};
    }

    // Batch 6's inputs. Each is shaped so the engine has something real to
    // find - a series with a known period, a residual whose spread genuinely
    // grows, two signals coherent at one frequency and not elsewhere - because
    // an engine fed noise draws something and proves nothing.
    QVector<double> fittedValue,plainResidual,growingResidual;
    QVector<double> predictorX,responseY,noisyX,noisyY;
    QVector<double> arSeries,seasonTime,seasonValue,seasonPeriod;
    QVector<double> chaos,driveA,driveB;
    QVector<double> bodeFreq,bodeGain,bodePhase;
    QVector<double> planSize,planAccept;
    QVector<double> stepTime,stepCurrent,cycleIndex,cycleEfficiency;
    QVector<double> mvA,mvB,mvC,mvD;
    {
        for(int i=0;i<120;++i){
            const double u=double(i)/119.0;
            fittedValue.append(2.0+8.0*u);
            const double wobble=std::sin(double(i)*12.9898)*std::cos(double(i)*4.1414);
            plainResidual.append(0.4*wobble);
            // Spread that grows with the fit, which is what the scale-location
            // panel exists to reveal and a flat one would not test.
            growingResidual.append(0.15*wobble*(1.0+3.0*u));
            predictorX.append(u*10.0);
            // A quadratic hiding under a linear fit: the partial residual plot
            // should show the curve.
            responseY.append(1.0+2.0*(u*10.0)+0.08*(u*10.0)*(u*10.0));
            noisyX.append(u*10.0);
            noisyY.append(std::sin(u*6.0)*5.0+0.6*wobble);
        }
        // Pseudo-random, and it has to be.
        //
        // sin(i*78.233)*cos(i*12.9898) was used as "noise" here first, and it
        // is a deterministic periodic signal. An AR process driven by it comes
        // out periodic, so its partial autocorrelation does not cut off; and
        // two signals with no INDEPENDENT noise in them are perfectly coherent
        // at every frequency by construction, so a coherence spectrum of them
        // is 1.0 everywhere whatever the code does. Both engines looked broken
        // until the generator was checked. Xorshift, summed twelve at a time
        // for an approximately normal deviate.
        quint32 seed=2463534242u;
        const auto noise=[&seed]{
            double u=0.0;
            for(int k=0;k<12;++k){
                seed^=seed<<13; seed^=seed>>17; seed^=seed<<5;
                u+=double(seed)/4294967296.0;
            }
            return u-6.0;
        };
        // An AR(2) with phi1 = 0.6 and phi2 = -0.3: the partial autocorrelation
        // must come back near +0.46 at lag 1, -0.30 at lag 2, and cut off.
        {
            double a=0.0,b=0.0;
            for(int i=0;i<600;++i){
                const double v=0.6*a-0.3*b+noise();
                arSeries.append(v);
                b=a; a=v;
            }
        }
        // Trend plus a 12-long cycle plus noise.
        for(int i=0;i<180;++i){
            seasonTime.append(double(i));
            seasonValue.append(0.05*i
                               +3.0*std::sin(2.0*M_PI*double(i%12)/12.0)
                               +0.3*std::cos(double(i)*9.7));
        }
        seasonPeriod.append(12.0);
        // A record that revisits: two incommensurate tones, so the recurrence
        // plot has diagonals rather than a solid block or a scatter.
        for(int i=0;i<300;++i)
            chaos.append(std::sin(double(i)*0.21)+0.7*std::sin(double(i)*0.083));
        // Two signals sharing one tone under INDEPENDENT noise, which is the
        // only arrangement in which a coherence is not trivially 1.
        for(int i=0;i<2048;++i){
            const double shared=std::sin(2.0*M_PI*double(i)*0.05);
            driveA.append(shared+2.0*noise());
            driveB.append(0.9*shared+2.0*noise());
        }
        // A first-order loop: -20 dB per decade through 0 dB.
        for(int i=0;i<80;++i){
            const double f=std::pow(10.0,-1.0+3.0*double(i)/79.0);
            bodeFreq.append(f);
            bodeGain.append(20.0*std::log10(10.0/std::sqrt(1.0+f*f)));
            bodePhase.append(-std::atan(f)*180.0/M_PI);
        }
        planSize.append(80.0);
        planAccept.append(3.0);
        // Cottrell: i proportional to 1/sqrt(t), so the plot is a straight line
        // through the origin and its R2 should come back at 1.
        for(int i=1;i<=60;++i){
            const double t=double(i)*0.5;
            stepTime.append(t);
            stepCurrent.append(2.5/std::sqrt(t));
        }
        for(int i=0;i<40;++i){
            cycleIndex.append(double(i+1));
            cycleEfficiency.append(0.72-0.004*i+0.02*std::sin(double(i)*2.3));
        }
        // Four columns where A and B carry the same information and C is
        // independent - so a biplot's arrows should pair A with B.
        for(int i=0;i<90;++i){
            const double u=std::fmod(double(i)*0.6180339887,1.0);
            const double w=std::fmod(double(i)*0.4142135624,1.0);
            mvA.append(u*4.0);
            mvB.append(u*4.0+0.15*w);
            mvC.append(w*7.0);
            mvD.append(u*2.0-w*3.0);
        }
    }

    // Batch 7's inputs. Every one is built from a KNOWN constant so the
    // engine's own label can be checked against the number the generator used:
    // a concordia chord whose upper intercept is 1200 Ma, an isochron for a
    // 500 Ma age, a Tauc edge at 2.40 eV, a Stern-Volmer Ksv of 12. A generator
    // that only makes a plausible shape can prove that an engine draws and
    // nothing at all about whether it draws the right thing.
    QVector<double> concordia207,concordia206;
    QVector<double> isochronParent,isochronDaughter;
    QVector<double> harkerSilica,harkerMgO,harkerCaO;
    QVector<double> spiderReference,spiderSampleA,spiderSampleB;
    QVector<double> keelingConc,keelingDelta;
    QVector<double> idfDuration,idfIntensity,idfPeriod;
    QVector<double> vantHoffT,vantHoffK;
    QVector<double> taucEnergy,taucAbsorption;
    QVector<double> quencher,quenchedIntensity;
    QVector<double> tgaTemperature,tgaMass;
    QVector<double> dscTemperature,dscHeatFlow;
    QVector<double> creepTime,creepStrain;
    QVector<double> ttsFrequency,ttsModulus,ttsTemperature;
    QVector<double> shearRate,shearStress;
    QVector<double> bandFrequency,bandLevel;
    QVector<double> pkTime,pkConcentration;
    QVector<double> yieldMaturity,yieldRate,yieldDate;
    QVector<double> nyquistReal,nyquistImag;
    {
        // A discordia chord: closure at 1200 Ma, lead loss at 250 Ma, samples
        // spread along it. The engine's upper intercept must come back 1200.
        const double lambda235=9.8485e-10,lambda238=1.55125e-10;
        const double upper=1200.0e6,lower=250.0e6;
        const double ux=std::expm1(lambda235*upper),uy=std::expm1(lambda238*upper);
        const double lx=std::expm1(lambda235*lower),ly=std::expm1(lambda238*lower);
        for(int i=0;i<12;++i){
            const double f=double(i)/11.0;
            concordia207.append(lx+f*(ux-lx));
            concordia206.append(ly+f*(uy-ly));
        }
        // Rb-Sr isochron for 500 Ma with an initial ratio of 0.7050.
        {
            const double lambdaRb=1.397e-11;
            const double slope=std::expm1(lambdaRb*500.0e6);
            for(int i=0;i<10;++i){
                const double parent=0.5+double(i)*0.9;
                isochronParent.append(parent);
                isochronDaughter.append(0.7050+slope*parent);
            }
        }
        // A fractionation trend: MgO falls 0.15 and CaO 0.20 per wt% silica.
        for(int i=0;i<24;++i){
            const double silica=45.0+double(i)*(30.0/23.0);
            harkerSilica.append(silica);
            harkerMgO.append(12.0-0.15*silica);
            harkerCaO.append(20.0-0.20*silica);
        }
        // A reference falling by a decade over the series, one sample enriched
        // two-fold throughout and one with a trough in the middle.
        for(int i=0;i<14;++i){
            const double reference=10.0*std::pow(0.85,double(i));
            spiderReference.append(reference);
            spiderSampleA.append(reference*2.0);
            spiderSampleB.append(reference*(i>=5&&i<=8?0.4:1.5));
        }
        // Two-component mixing: a background of 380 at -8 and a source at -26.
        // The Keeling intercept must be -26.
        for(int i=0;i<20;++i){
            const double total=380.0+double(i)*12.0;
            keelingConc.append(total);
            keelingDelta.append(-26.0+380.0*(-8.0-(-26.0))/total);
        }
        // Intensity-duration-frequency with a duration exponent of -0.7.
        {
            const double periods[]={2.0,10.0,50.0};
            for(double period:periods){
                for(int i=0;i<10;++i){
                    const double duration=5.0*std::pow(1440.0/5.0,double(i)/9.0);
                    idfDuration.append(duration);
                    idfIntensity.append(60.0*std::pow(period,0.2)*std::pow(duration,-0.7));
                    idfPeriod.append(period);
                }
            }
        }
        // Van 't Hoff for dH = -50 kJ/mol and dS = -100 J/(mol K).
        {
            const double gasConstant=8.314462618;
            const double enthalpy=-50000.0,entropy=-100.0;
            for(int i=0;i<12;++i){
                const double t=280.0+double(i)*8.0;
                vantHoffT.append(t);
                vantHoffK.append(std::exp(-enthalpy/(gasConstant*t)+entropy/gasConstant));
            }
        }
        // A direct allowed edge at 2.40 eV: (a h v)^2 rises linearly above it
        // and is flat below, so the extrapolated intercept must be 2.40.
        for(int i=0;i<80;++i){
            const double energy=1.5+double(i)*(2.0/79.0);
            taucEnergy.append(energy);
            const double tauc=(energy>2.40)?4.0e4*(energy-2.40):0.0;
            taucAbsorption.append(std::sqrt(tauc)/energy);
        }
        // Stern-Volmer with Ksv = 12.
        for(int i=0;i<12;++i){
            const double q=double(i)*0.02;
            quencher.append(q);
            quenchedIntensity.append(1000.0/(1.0+12.0*q));
        }
        // A single decomposition step centred at 400, 100% down to 40%.
        for(int i=0;i<160;++i){
            const double temperature=25.0+double(i)*(700.0/159.0);
            tgaTemperature.append(temperature);
            tgaMass.append(4.0*(0.40+0.60/(1.0+std::exp((temperature-400.0)/18.0))));
        }
        // A sloping baseline with one Gaussian peak at 150.
        for(int i=0;i<200;++i){
            const double temperature=50.0+double(i)*(200.0/199.0);
            dscTemperature.append(temperature);
            const double excess=6.0*std::exp(-0.5*std::pow((temperature-150.0)/8.0,2.0));
            dscHeatFlow.append(-2.0+0.01*(temperature-50.0)+excess);
        }
        // Primary, secondary at a minimum rate of 2e-4, then tertiary.
        for(int i=0;i<120;++i){
            const double t=double(i)*10.0;
            creepTime.append(t);
            creepStrain.append(0.004*std::log1p(t*0.5)
                               +2.0e-4*t
                               +0.0006*std::exp((t-1000.0)/140.0));
        }
        // Three isotherms of one master curve, shifted by log aT = -0.05 dT.
        //
        // -0.15 was used first and the isotherms did not overlap: each covers
        // three decades and the shift moved them three decades apart, so they
        // met at a point. Superposition cannot be determined from that, and the
        // engine correctly refused - the generator was what needed fixing.
        {
            const double temperatures[]={5.0,25.0,45.0};
            for(double t:temperatures){
                const double logShift=-0.05*(t-25.0);
                for(int i=0;i<14;++i){
                    const double logF=-1.0+double(i)*(3.0/13.0);
                    const double reduced=logF+logShift;
                    ttsFrequency.append(std::pow(10.0,logF));
                    // A smooth sigmoid in log reduced frequency, so the shifted
                    // isotherms genuinely superpose and the recovered shifts
                    // can be compared against -0.15 dT.
                    ttsModulus.append(std::pow(10.0,6.0+2.0/(1.0+std::exp(-reduced))));
                    ttsTemperature.append(t);
                }
            }
        }
        // Herschel-Bulkley with a yield stress of 5, K of 2 and n of 0.6.
        for(int i=0;i<30;++i){
            const double rate=0.1*std::pow(1000.0,double(i)/29.0);
            shearRate.append(rate);
            shearStress.append(5.0+2.0*std::pow(rate,0.6));
        }
        // A broadband floor with a tone at 1 kHz, sampled every 10 Hz.
        for(int i=1;i<=2000;++i){
            const double f=double(i)*10.0;
            bandFrequency.append(f);
            const double tone=(f>950.0&&f<1050.0)?30.0:0.0;
            bandLevel.append(40.0-5.0*std::log10(f/20.0)+tone);
        }
        // One-compartment, first-order absorption: ka 1.2, ke 0.2, so the
        // terminal half-life must come back at ln2/0.2 = 3.466.
        for(int i=0;i<40;++i){
            const double t=double(i)*0.75;
            pkTime.append(t);
            pkConcentration.append(120.0*(std::exp(-0.2*t)-std::exp(-1.2*t)));
        }
        // Three term structures, the last one inverted.
        {
            const double dates[]={20240101.0,20240701.0,20250101.0};
            const double maturities[]={0.25,0.5,1.0,2.0,3.0,5.0,7.0,10.0,20.0,30.0};
            int which=0;
            for(double date:dates){
                for(double maturity:maturities){
                    yieldDate.append(date);
                    yieldMaturity.append(maturity);
                    yieldRate.append(which<2
                        ? 2.0+1.6*(1.0-std::exp(-maturity/4.0))
                        : 5.2-1.1*(1.0-std::exp(-maturity/4.0)));
                }
                ++which;
            }
        }
        // G(jw) = 1 / (jw (jw+1) (jw+2)), the textbook third-order loop.
        for(int i=0;i<200;++i){
            const double w=std::pow(10.0,-1.0+2.0*double(i)/199.0);
            // (jw)(jw+1)(jw+2) = -3w^2 + j(2w - w^3)
            const double dr=-3.0*w*w, di=2.0*w-w*w*w;
            const double mag2=dr*dr+di*di;
            nyquistReal.append(dr/mag2);
            nyquistImag.append(-di/mag2);
        }
    }

    // Batch 8's inputs, on the same rule: a constant the engine must recover.
    // A compaction curve peaking at 1.92 at 14% moisture, a soil with c 15 and
    // phi 30, a balanced field at V1 120, a four-stream pinch problem whose
    // utilities must differ by exactly the stream imbalance, a caustic with a
    // 0.05 waist at z 100, a season running 166.7 to 233.3.
    QVector<double> proctorMoisture,proctorDensity;
    QVector<double> normalStress,failureShear;
    QVector<double> loadPosition,sectionMoment;
    QVector<double> decisionSpeed,stopDistance,goDistance;
    QVector<double> cruiseWeight,specificRange;
    QVector<double> streamSupply,streamTarget,streamCapacity;
    QVector<double> tracerTime,tracerConcentration;
    QVector<double> harmonicOrder,harmonicMagnitude;
    QVector<double> noiseTime,noiseLevel;
    QVector<double> beamDistance,beamRadius;
    QVector<double> youdenA,youdenB;
    QVector<double> controlRun,controlValue;
    QVector<double> surveySample,surveySpecies;
    QVector<double> onsetDay,onsetCases;
    QVector<double> gainScore,gainOutcome;
    QVector<double> growthTemperature,ratkowskyRate;
    QVector<double> phenologyDay,phenologyIndex;
    {
        for(int i=0;i<9;++i){
            const double w=8.0+double(i)*1.5;
            proctorMoisture.append(w);
            proctorDensity.append(1.92-0.006*(w-14.0)*(w-14.0));
        }
        for(int i=0;i<8;++i){
            const double sigma=50.0+double(i)*40.0;
            normalStress.append(sigma);
            failureShear.append(15.0+sigma*std::tan(30.0*M_PI/180.0));
        }
        // Moment at midspan of a 20-long simply supported beam.
        for(int i=0;i<=40;++i){
            const double x=double(i)*0.5;
            loadPosition.append(x);
            sectionMoment.append(x<=10.0?x/2.0:(20.0-x)/2.0);
        }
        for(int i=0;i<=20;++i){
            const double v=100.0+double(i)*2.0;
            decisionSpeed.append(v);
            stopDistance.append(800.0+12.0*v);
            goDistance.append(3200.0-8.0*v);
        }
        for(int i=0;i<=20;++i){
            const double w=50000.0+double(i)*1000.0;
            cruiseWeight.append(w);
            specificRange.append(0.30-2.0e-9*(w-60000.0)*(w-60000.0));
        }
        // Two hot streams totalling 470 and two cold totalling 485, so the hot
        // and cold utilities must differ by exactly 15 whatever the pinch is.
        streamSupply={200.0,150.0,50.0,80.0};
        streamTarget={100.0,60.0,180.0,160.0};
        streamCapacity={2.0,3.0,2.5,2.0};
        // Three equal tanks in series with a mean residence time of 10.
        for(int i=0;i<=200;++i){
            const double t=double(i)*0.25;
            tracerTime.append(t);
            tracerConcentration.append(std::pow(0.3,3.0)*t*t*std::exp(-0.3*t)/2.0);
        }
        harmonicOrder={1.0,3.0,5.0,7.0};
        harmonicMagnitude={100.0,20.0,10.0,5.0};
        for(int i=0;i<=40;++i){ noiseTime.append(double(i)); noiseLevel.append(40.0+double(i)); }
        for(int i=0;i<=40;++i){
            const double z=50.0+double(i)*2.5;
            beamDistance.append(z);
            beamRadius.append(std::sqrt(0.0025+4.0e-6*(z-100.0)*(z-100.0)));
        }
        for(int i=0;i<18;++i){
            const double u=std::fmod(double(i)*0.6180339887,1.0)-0.5;
            youdenA.append(10.0+u*0.4); youdenB.append(20.0+u*0.4);
        }
        youdenA.append(11.5); youdenB.append(21.5);
        youdenA.append(11.4); youdenB.append(21.6);
        for(int i=0;i<30;++i){
            controlRun.append(double(i+1));
            controlValue.append(100.0+2.0*std::sin(double(i)*1.1));
        }
        controlValue[17]=112.0;
        // Twelve species: five seen once, four twice, three often. Chao1 must
        // come back 12 + 25/8 = 15.125.
        {
            int sample=0;
            const auto add=[&](double species,int times){
                for(int k=0;k<times;++k){
                    surveySample.append(double(++sample));
                    surveySpecies.append(species);
                }
            };
            for(int i=0;i<5;++i) add(double(i+1),1);
            for(int i=0;i<4;++i) add(double(i+6),2);
            for(int i=0;i<3;++i) add(double(i+10),6);
        }
        for(int i=0;i<90;++i){
            onsetDay.append(double(i));
            onsetCases.append(std::round(200.0*std::exp(-0.5*std::pow((double(i)-40.0)/12.0,2.0))));
        }
        // Already ranked, prevalence 0.25, so a perfect ranking captures 80%
        // of the positives in the top fifth.
        for(int i=0;i<400;++i){
            gainScore.append(double(400-i));
            gainOutcome.append(i<100?1.0:0.0);
        }
        for(int i=0;i<12;++i){
            const double t=8.0+double(i)*2.0;
            growthTemperature.append(t);
            ratkowskyRate.append(std::pow(0.03*(t-5.0),2.0));
        }
        for(int i=0;i<73;++i){
            const double d=1.0+double(i)*5.0;
            phenologyDay.append(d);
            phenologyIndex.append(0.2+0.6*std::exp(-std::pow((d-200.0)/40.0,2.0)));
        }
    }

    // Batch 9's inputs. A sphere rather than a pure Guinier curve for the
    // scattering pair, because a pure Guinier curve cannot tell whether the
    // fitting RANGE was chosen correctly - it is a straight line everywhere.
    // The sphere's true radius of gyration is R*sqrt(3/5), and the Guinier fit
    // over qRg <= 1.3 comes back about two per cent high: that is the
    // approximation's own bias, verified against a pure Guinier curve which
    // returns the radius exactly.
    QVector<double> scatterQ,scatterI;
    QVector<double> hillLigand,hillTheta;
    QVector<double> ellinghamT,ellinghamAlumina,ellinghamZincite;
    QVector<double> heatingRate,peakTemperature;
    QVector<double> avramiTime,avramiFraction;
    QVector<double> wilsonVelocity,wilsonCoefficient;
    QVector<double> krevelenOxygen,krevelenHydrogen;
    QVector<double> starColour,starMagnitude;
    QVector<double> orbitRadius,orbitSpeed;
    QVector<double> hubbleDistance,hubbleSpeed;
    QVector<double> symbolI,symbolQ;
    QVector<double> delayFrequency,delayPhase;
    QVector<double> profileHeight,profileSpeed;
    QVector<double> cumulativeVolume,unitCost;
    QVector<double> orderQuantity,orderingCost,holdingCost;
    QVector<double> effectIndex,effectEstimate;
    QVector<double> patientIndex,bestChange;
    {
        const double sphereRadius=25.0/std::sqrt(0.6);   // gives Rg exactly 25
        for(int i=1;i<=300;++i){
            const double q=0.0008*double(i);
            const double x=q*sphereRadius;
            const double f=3.0*(std::sin(x)-x*std::cos(x))/(x*x*x);
            scatterQ.append(q); scatterI.append(100.0*f*f);
        }
        for(int i=0;i<24;++i){
            const double ligand=0.5*std::pow(400.0,double(i)/23.0);
            hillLigand.append(ligand);
            hillTheta.append(std::pow(ligand,2.5)
                             /(std::pow(10.0,2.5)+std::pow(ligand,2.5)));
        }
        for(int i=0;i<=20;++i){
            const double t=300.0+double(i)*85.0;
            ellinghamT.append(t);
            ellinghamAlumina.append(-1100.0+0.20*t);
            ellinghamZincite.append(-700.0+0.22*t);
        }
        {
            const double activation=120000.0,gasConstant=8.314462618;
            for(int i=0;i<5;++i){
                const double tp=400.0+double(i)*15.0;
                peakTemperature.append(tp);
                heatingRate.append(tp*tp*std::exp(-activation/(gasConstant*tp)));
            }
        }
        for(int i=1;i<=60;++i){
            const double t=double(i)*0.4;
            avramiTime.append(t);
            avramiFraction.append(1.0-std::exp(-std::pow(0.1*t,3.0)));
        }
        for(int i=0;i<10;++i){
            const double v=0.5+double(i)*0.35;
            wilsonVelocity.append(v);
            wilsonCoefficient.append(1.0/(0.002+0.05*std::pow(v,-0.8)));
        }
        // Walked out along the dehydration path, so the -H2O line the engine
        // draws must land on the samples themselves.
        for(int i=0;i<12;++i){
            const double e=double(i)*0.03;
            krevelenOxygen.append(0.55-e); krevelenHydrogen.append(1.60-2.0*e);
        }
        for(int i=0;i<300;++i){
            const double colour=-0.3+2.3*double(i)/299.0;
            const double wobble=std::sin(double(i)*12.9898)*std::cos(double(i)*4.1414);
            starColour.append(colour);
            starMagnitude.append(5.0*colour-1.0+0.4*wobble);
        }
        for(int i=1;i<=30;++i){
            const double r=double(i);
            orbitRadius.append(r); orbitSpeed.append(200.0*qMin(1.0,r/5.0));
        }
        for(int i=1;i<=25;++i){
            hubbleDistance.append(double(i)*8.0);
            hubbleSpeed.append(70.0*double(i)*8.0);
        }
        {
            quint32 seed=88675123u;
            const auto jitter=[&seed]{
                double u=0.0;
                for(int k=0;k<12;++k){
                    seed^=seed<<13; seed^=seed>>17; seed^=seed<<5;
                    u+=double(seed)/4294967296.0;
                }
                return u-6.0;
            };
            for(int n=0;n<800;++n){
                const int a=n%4,b=(n/4)%4;
                symbolI.append((2.0*a-3.0)+0.10*jitter());
                symbolQ.append((2.0*b-3.0)+0.10*jitter());
            }
        }
        // A pure 1 ms delay, WRAPPED into the range an instrument reports, so
        // the unwrapping is exercised rather than assumed.
        for(int i=0;i<200;++i){
            const double f=10.0+double(i)*20.0;
            delayFrequency.append(f);
            double phase=std::fmod(-360.0*f*1.0e-3,360.0);
            if(phase>180.0) phase-=360.0;
            if(phase<-180.0) phase+=360.0;
            delayPhase.append(phase);
        }
        for(int i=0;i<8;++i){
            const double z=2.0*std::pow(2.0,double(i)*0.6);
            profileHeight.append(z);
            profileSpeed.append((0.40/0.41)*std::log(z/0.03));
        }
        for(int i=0;i<20;++i){
            const double volume=100.0*std::pow(2.0,double(i)*0.5);
            cumulativeVolume.append(volume);
            unitCost.append(100.0*std::pow(volume/100.0,std::log2(0.85)));
        }
        // Ordering DS/Q with DS = 10000 and holding Q, so the cheapest order
        // and the crossing of the two components are both exactly 100.
        for(int i=1;i<=60;++i){
            const double q=double(i)*5.0;
            orderQuantity.append(q); orderingCost.append(10000.0/q); holdingCost.append(q);
        }
        {
            const double inactive[]={0.3,-0.5,0.8,-0.2,1.1,0.6,-0.9,0.4,-1.2,0.7,-0.35,0.55};
            for(double v:inactive){
                effectIndex.append(double(effectIndex.size())); effectEstimate.append(v);
            }
            const double active[]={8.0,-10.0,12.0};
            for(double v:active){
                effectIndex.append(double(effectIndex.size())); effectEstimate.append(v);
            }
        }
        for(int i=0;i<5;++i){ patientIndex.append(double(patientIndex.size())); bestChange.append(40.0+double(i)); }
        for(int i=0;i<10;++i){ patientIndex.append(double(patientIndex.size())); bestChange.append(10.0-double(i)*3.5); }
        for(int i=0;i<5;++i){ patientIndex.append(double(patientIndex.size())); bestChange.append(-35.0-double(i)*5.0); }
    }

    // Batch 10's inputs, each built from a constant the engine has to give
    // back: a critical depth of 0.7414 for a unit discharge of 2, a duty point
    // at the square root of 800, a half-life of 8, a critical power of 250, a
    // Bass model with p 0.03 and q 0.38.
    QVector<double> channelDepth,unitDischarge;
    QVector<double> transferUnits,transferEffectiveness,capacityRatio;
    QVector<double> pumpFlow2,pumpDelivered,systemDemanded;
    QVector<double> dayHour,dayDemand,daySolar;
    QVector<double> faultCurrent,operateTime,protectiveDevice;
    QVector<double> spatialFrequency,contrastModulation;
    QVector<double> decayTime,decayActivity;
    QVector<double> absorberThickness,transmitted;
    QVector<double> beamDepth,depositedDose;
    QVector<double> effortDuration,effortPower;
    QVector<double> workRate,bloodLactate;
    QVector<double> sweepFrequency,sweepMagnitude;
    QVector<double> queueTime,queueArrivals,queueDepartures;
    QVector<double> cohortAge,cohortRetained,cohortLabel;
    QVector<double> adoptionTime,adoptionCumulative;
    QVector<double> faultCategory,faultCount;
    QVector<double> designSize,designEffect;
    QVector<double> trialIndex,trialOutcome;
    {
        for(int i=1;i<=30;++i){ channelDepth.append(0.15*double(i)); unitDischarge.append(2.0); }
        // Exactly 0.05 below the counterflow limit, at a capacity ratio of a
        // half and at one - the second being the removable singularity of the
        // effectiveness relation rather than a special kind of exchanger.
        {
            const double ratios[]={0.5,1.0};
            for(double cr:ratios)
                for(int i=1;i<=20;++i){
                    const double ntu=0.25*double(i);
                    double exact;
                    if(std::abs(1.0-cr)<1e-9) exact=ntu/(1.0+ntu);
                    else { const double e=std::exp(-ntu*(1.0-cr));
                           exact=(1.0-e)/(1.0-cr*e); }
                    transferUnits.append(ntu);
                    transferEffectiveness.append(exact-0.05);
                    capacityRatio.append(cr);
                }
        }
        for(int i=0;i<=40;++i){
            const double q=double(i)*1.5;
            pumpFlow2.append(q);
            pumpDelivered.append(50.0-0.02*q*q);
            systemDemanded.append(10.0+0.03*q*q);
        }
        for(int i=0;i<=24;++i){
            const double t=double(i);
            dayHour.append(t); dayDemand.append(40.0);
            daySolar.append((t>=6.0&&t<=18.0)?20.0*std::sin(M_PI*(t-6.0)/12.0):0.0);
        }
        // Two inverse-time curves whose times differ by a constant factor of
        // three, so the interval 2/(I/100)^2 is tightest at the largest shared
        // current and the answer is known in closed form.
        for(int device=1;device<=2;++device)
            for(int i=0;i<=20;++i){
                const double amps=200.0*std::pow(5.0,double(i)/20.0);
                faultCurrent.append(amps);
                operateTime.append((device==1?1.0:3.0)/std::pow(amps/100.0,2.0));
                protectiveDevice.append(double(device));
            }
        {
            const double knee=30.0/std::sqrt(std::log(2.0));   // puts MTF50 at 30
            for(int i=0;i<=60;++i){
                const double f=double(i)*1.5;
                spatialFrequency.append(f);
                contrastModulation.append(std::exp(-(f/knee)*(f/knee)));
            }
        }
        for(int i=0;i<=40;++i){
            const double t=double(i);
            decayTime.append(t);
            decayActivity.append(1000.0*std::exp(-std::log(2.0)*t/8.0));
        }
        for(int i=0;i<=30;++i){
            const double x=double(i)*0.8;
            absorberThickness.append(x); transmitted.append(500.0*std::exp(-0.15*x));
        }
        for(int i=0;i<=200;++i){
            const double d=double(i)*0.1;
            beamDepth.append(d);
            depositedDose.append(std::exp(-std::pow((d-15.0)/1.5,2.0)));
        }
        for(int i=0;i<8;++i){
            const double t=60.0+double(i)*120.0;
            effortDuration.append(t); effortPower.append(250.0+20000.0/t);
        }
        for(int i=0;i<=12;++i){
            const double power=120.0+double(i)*15.0;
            workRate.append(power);
            bloodLactate.append(1.0+0.5*std::exp((power-200.0)/30.0));
        }
        {
            const double damping=0.05,natural=100.0;
            for(int i=0;i<=400;++i){
                const double f=50.0+double(i)*0.25;
                const double r=f/natural;
                sweepFrequency.append(f);
                sweepMagnitude.append(1.0/std::sqrt((1.0-r*r)*(1.0-r*r)
                                                    +(2.0*damping*r)*(2.0*damping*r)));
            }
        }
        // Arrivals at ten a unit, departures at six, both capped at a hundred:
        // the queue peaks at forty and the delay is a known area.
        for(int i=0;i<=100;++i){
            const double t=double(i)*0.25;
            queueTime.append(t);
            queueArrivals.append(qMin(100.0,10.0*t));
            queueDepartures.append(qMin(100.0,6.0*t));
        }
        {
            const double lives[]={2.0,3.0,4.0};
            int which=0;
            for(double life:lives){
                for(int i=0;i<=12;++i){
                    cohortAge.append(double(i));
                    cohortRetained.append(std::exp(-double(i)/life));
                    cohortLabel.append(double(202401+which));
                }
                ++which;
            }
        }
        {
            const double innovation=0.03,imitation=0.38,market=1000.0;
            for(int i=0;i<=40;++i){
                const double t=double(i)*0.5;
                const double e=std::exp(-(innovation+imitation)*t);
                adoptionTime.append(t);
                adoptionCumulative.append(market*(1.0-e)/(1.0+(imitation/innovation)*e));
            }
        }
        {
            const double counts[]={50.0,25.0,12.0,6.0,4.0,2.0,1.0};
            int k=0;
            for(double c:counts){ faultCategory.append(double(++k)); faultCount.append(c); }
        }
        for(int i=5;i<=200;i+=5){ designSize.append(double(i)); designEffect.append(0.5); }
        for(int i=0;i<60;++i){ trialIndex.append(double(i)); trialOutcome.append(0.0); }
    }

    // Batch 11's inputs. The chromatogram is two gaussians whose plate counts
    // and resolution are known in closed form; the hysteresis loop is an
    // ellipse whose area is pi*Hm*Bm*sin(phase); the DFA record is white noise,
    // which is the one input that can show whether the exponent is biased.
    QVector<double> chromTime,chromSignal;
    QVector<double> langmuirCe,langmuirQe;
    QVector<double> zetaPh,zetaPotential;
    QVector<double> distilRecovered,distilTemperature;
    QVector<double> pcrQuantity,pcrThreshold;
    QVector<double> meltTemperature,meltFluorescence;
    QVector<double> cultureTime,cultureDensity;
    QVector<double> childAge,childHeight;
    QVector<double> fieldStrength,fluxDensity;
    QVector<double> motorSpeed,motorTorque,loadTorque;
    QVector<double> patternBearing,patternGain;
    QVector<double> suctionHead,waterContent;
    QVector<double> dfaIndex,dfaValue;
    QVector<double> beatIndex,beatInterval;
    QVector<double> queueUtilisation,queueWait;
    QVector<double> arrivalRate,workInProgress,leadTime;
    QVector<double> burnDay,burnRemaining;
    QVector<double> bandPredictor,bandResponse;
    {
        quint32 state=1234567891u;
        const auto deviate=[&state]{
            double u=0.0;
            for(int k=0;k<12;++k){
                state^=state<<13; state^=state>>17; state^=state<<5;
                u+=double(state)/4294967296.0;
            }
            return u-6.0;
        };
        for(int i=0;i<=1200;++i){
            const double t=double(i)*0.01;
            chromTime.append(t);
            chromSignal.append(std::exp(-0.5*std::pow((t-5.0)/0.2,2.0))
                              +0.8*std::exp(-0.5*std::pow((t-7.0)/0.25,2.0)));
        }
        for(int i=1;i<=12;++i){
            const double ce=double(i)*5.0;
            langmuirCe.append(ce); langmuirQe.append(50.0*0.2*ce/(1.0+0.2*ce));
        }
        for(int i=0;i<=10;++i){ zetaPh.append(double(i)); zetaPotential.append(40.0-10.0*double(i)); }
        for(int i=0;i<=20;++i){
            const double pct=double(i)*5.0;
            distilRecovered.append(pct); distilTemperature.append(50.0+2.0*pct);
        }
        // A slope of -3.32193 is exactly a doubling every cycle, so the
        // efficiency must come back at 100.0%.
        for(int i=0;i<6;++i){
            pcrQuantity.append(std::pow(10.0,double(i)));
            pcrThreshold.append(40.0-3.32193*double(i));
        }
        for(int i=0;i<=200;++i){
            const double t=50.0+double(i)*0.15;
            meltTemperature.append(t);
            meltFluorescence.append(1.0/(1.0+std::exp((t-62.0)/1.2)));
        }
        for(int i=0;i<=40;++i){
            const double t=double(i)*0.25;
            double density;
            if(t<2.0) density=0.02;
            else if(t<8.0) density=0.02*std::exp(0.5*(t-2.0));
            else density=0.02*std::exp(3.0);
            cultureTime.append(t); cultureDensity.append(density);
        }
        for(int i=0;i<400;++i){
            const double age=double(i%40)*0.5;
            childAge.append(age); childHeight.append(50.0+4.0*age+3.0*deviate());
        }
        for(int i=0;i<=360;++i){
            const double theta=2.0*M_PI*double(i)/360.0;
            fieldStrength.append(100.0*std::cos(theta));
            fluxDensity.append(1.5*std::cos(theta-30.0*M_PI/180.0));
        }
        for(int i=0;i<=150;++i){
            const double n=double(i)*10.0;
            motorSpeed.append(n);
            motorTorque.append(50.0+250.0*std::sin(M_PI*n/1500.0));
            loadTorque.append(30.0+0.00008*n*n);
        }
        // A beam with a real sidelobe 20 dB down, so the sidelobe level can be
        // told apart from the main lobe's own skirt.
        for(int i=0;i<180;++i){
            const double theta=-180.0+double(i)*2.0;
            double main=-3.0*(theta/30.0)*(theta/30.0);
            if(main<-40.0) main=-40.0;
            const double side=-20.0-0.05*(std::abs(theta)-120.0)*(std::abs(theta)-120.0);
            patternBearing.append(theta);
            patternGain.append(qMax(main,side));
        }
        {
            const double saturated=0.45,residual=0.08,alpha=0.02,shape=1.5;
            for(int i=0;i<=60;++i){
                const double head=std::pow(10.0,double(i)/12.0);
                suctionHead.append(head);
                waterContent.append(residual+(saturated-residual)
                    /std::pow(1.0+std::pow(alpha*head,shape),1.0-1.0/shape));
            }
        }
        for(int i=0;i<4096;++i){ dfaIndex.append(double(i)); dfaValue.append(deviate()); }
        for(int i=0;i<1000;++i){ beatIndex.append(double(i)); beatInterval.append(800.0+20.0*deviate()); }
        for(int i=1;i<=18;++i){
            const double rho=0.05*double(i);
            queueUtilisation.append(rho); queueWait.append(2.0*rho/(1.0-rho));
        }
        // Little's law made to hold exactly, so any departure the engine
        // reports is the engine's own arithmetic.
        for(int i=1;i<=20;++i){
            const double rate=double(i)*0.5,lead=3.0+0.1*double(i);
            arrivalRate.append(rate); workInProgress.append(rate*lead); leadTime.append(lead);
        }
        for(int i=0;i<=10;++i){ burnDay.append(double(i)); burnRemaining.append(100.0-9.0*double(i)); }
        for(int i=0;i<40;++i){
            const double x=double(i)*0.5;
            bandPredictor.append(x); bandResponse.append(3.0+2.0*x+1.5*deviate());
        }
    }

    // Batch 12's inputs. Two of them are built by INVERTING the thing the
    // engine will compute: the reflectance column comes from an F(R) with a
    // known 2.40 eV edge, and the isoconversional temperatures come from the
    // Ozawa-Flynn-Wall relation for exactly 150 kJ/mol. If the engine's
    // arithmetic is right it has to give those back.
    QVector<double> reflectEnergy,reflectance;
    QVector<double> freundlichCe,freundlichQe;
    QVector<double> coleReal,coleImaginary;
    QVector<double> titrantVolume,titrantConductivity;
    QVector<double> wireTime,wireRise,wirePower;
    QVector<double> variPart,variValue,variOperator;
    QVector<double> toleranceRun,toleranceValue;
    QVector<double> rareEvent,rareTime;
    QVector<double> repeatTime,repeatValue,repeatSubject;
    QVector<double> stormTime,stormDischarge;
    QVector<double> recessionTime,recessionDischarge;
    QVector<double> settleTime,settleResponse;
    QVector<double> jitterPosition,jitterRate;
    QVector<double> wellTime,wellPressure;
    QVector<double> isoRate,isoTemperature,isoConversion;
    {
        quint32 grain=1234567891u;
        const auto wobble=[&grain]{
            double u=0.0;
            for(int k=0;k<12;++k){
                grain^=grain<<13; grain^=grain>>17; grain^=grain<<5;
                u+=double(grain)/4294967296.0;
            }
            return u-6.0;
        };
        for(int i=0;i<80;++i){
            const double energy=1.5+double(i)*(2.0/79.0);
            const double f=std::sqrt((energy>2.40)?4.0e4*(energy-2.40):0.0)/energy;
            reflectEnergy.append(energy);
            reflectance.append((1.0+f)-std::sqrt((1.0+f)*(1.0+f)-1.0));
        }
        for(int i=1;i<=12;++i){
            const double ce=double(i)*4.0;
            freundlichCe.append(ce); freundlichQe.append(3.0*std::pow(ce,0.4));
        }
        // A Cole-Cole arc with alpha 0.3, so the depression must be 27 degrees
        // and the two axis intercepts 2 and 10.
        {
            const double einf=2.0,es=10.0,beta=0.7;
            for(int i=0;i<60;++i){
                const double x=std::pow(10.0,-3.0+6.0*double(i)/59.0);
                const double m=std::pow(x,beta);
                const double re=1.0+m*std::cos(beta*M_PI/2.0);
                const double im=m*std::sin(beta*M_PI/2.0);
                const double d=re*re+im*im;
                coleReal.append(einf+(es-einf)*re/d);
                coleImaginary.append((es-einf)*im/d);
            }
        }
        for(int i=0;i<=20;++i){
            const double v=double(i);
            titrantVolume.append(v);
            titrantConductivity.append(v<10.0?100.0-8.0*v:20.0+4.0*(v-10.0));
        }
        for(int i=1;i<=40;++i){
            const double t=double(i)*0.5;
            wireTime.append(t);
            wireRise.append(2.0+10.0/(4.0*M_PI*0.5)*std::log(t));
            wirePower.append(10.0);
        }
        for(int part=1;part<=5;++part)
            for(int op=1;op<=3;++op)
                for(int rep=0;rep<3;++rep){
                    variPart.append(double(part));
                    variOperator.append(double(op));
                    variValue.append(10.0*double(part)+0.8*double(op)+0.3*wobble());
                }
        for(int i=0;i<30;++i){
            toleranceRun.append(double(i+1));
            toleranceValue.append(100.0+5.0*wobble());
        }
        // Exponential intervals by the inverse transform, and two hundred of
        // them: with forty the sample mean of an exponential is eight per cent
        // out by chance, which looks like a defect and is not.
        {
            double at=0.0;
            for(int i=0;i<200;++i){
                grain^=grain<<13; grain^=grain>>17; grain^=grain<<5;
                const double u=qBound(1e-9,double(grain)/4294967296.0,1.0-1e-9);
                at+=-50.0*std::log(u);
                rareEvent.append(double(i+1)); rareTime.append(at);
            }
        }
        for(int subject=1;subject<=20;++subject)
            for(int i=0;i<8;++i){
                repeatTime.append(double(i)); repeatSubject.append(double(subject));
                repeatValue.append(20.0+2.0*double(i)+0.5*double(subject)+wobble());
            }
        // Baseflow 5 with a triangular direct pulse from 2 to 14 peaking 40
        // above it at 6: peak 45, time to peak 4, runoff volume 0.5*12*40 = 240.
        for(int i=0;i<=40;++i){
            const double t=double(i)*0.5;
            double direct=0.0;
            if(t>2.0&&t<=6.0) direct=40.0*(t-2.0)/4.0;
            else if(t>6.0&&t<14.0) direct=40.0*(14.0-t)/8.0;
            stormTime.append(t); stormDischarge.append(5.0+direct);
        }
        for(int i=0;i<=30;++i){
            const double t=double(i);
            recessionTime.append(t); recessionDischarge.append(100.0*std::exp(-0.2*t));
        }
        // Long enough to have settled. At six time units the ringing is still
        // four per cent, which biases the final value and every metric measured
        // against it; at twelve it is below a tenth of a per cent.
        {
            const double damping=0.3,natural=2.0;
            const double ringing=natural*std::sqrt(1.0-damping*damping);
            const double phase=std::acos(damping);
            for(int i=0;i<=600;++i){
                const double t=double(i)*0.02;
                settleTime.append(t);
                settleResponse.append(1.0-std::exp(-damping*natural*t)
                                        /std::sqrt(1.0-damping*damping)
                                        *std::sin(ringing*t+phase));
            }
        }
        // Transitions at 0.1 and 0.9 with sigma 0.03, so the eye opening
        // extrapolated to one error in a trillion must be 0.311 to 0.689.
        for(int i=0;i<=70;++i){
            const double x=0.15+double(i)*0.01;
            jitterPosition.append(x);
            jitterRate.append(0.5*std::erfc(((x-0.1)/0.03)/std::sqrt(2.0))
                             +0.5*std::erfc(((0.9-x)/0.03)/std::sqrt(2.0)));
        }
        // Wellbore storage first, then radial flow with a slope of 20 per
        // natural log of time - so the derivative plateau must be exactly 20.
        for(int i=0;i<60;++i){
            const double t=std::pow(10.0,-2.0+4.0*double(i)/59.0);
            wellTime.append(t);
            wellPressure.append(t<0.1?200.0*t:20.0*std::log(t/0.1)+20.0);
        }
        {
            const double factor=18978.0;   // 1.052 * 150000 / 8.3145
            const double rates[]={2.0,5.0,10.0,20.0,40.0};
            for(double beta:rates)
                for(int k=1;k<=19;++k){
                    const double share=0.05*double(k);
                    isoRate.append(beta);
                    isoTemperature.append(factor/(33.24+2.0*share-std::log(beta)));
                    isoConversion.append(share);
                }
        }
    }

    // Batch 13's input: a sounding with a capping inversion, so that both CAPE
    // and CIN are non-zero. The constructed sounding the engine was verified
    // against had CIN of exactly zero by design, which exercises only half of
    // the area integration.
    QVector<double> soundingPressure,soundingTemperature,soundingDewpoint;
    {
        // Surface 28 C with a 21 C dewpoint, a warm dry layer capping it near
        // 850 hPa, and a conditionally unstable troposphere above.
        const double levels[][3]={
            {1000.0,28.0,21.0},{ 975.0,26.2,20.6},{ 950.0,24.4,20.2},
            { 925.0,22.6,19.8},{ 900.0,21.5,19.0},{ 875.0,21.0,15.0},
            { 850.0,20.4,11.0},{ 800.0,17.0,10.0},{ 750.0,13.6, 9.0},
            { 700.0,10.0, 7.0},{ 650.0, 6.0, 4.0},{ 600.0, 1.8, 0.0},
            { 550.0,-2.6,-5.0},{ 500.0,-7.5,-11.0},{ 450.0,-13.0,-18.0},
            { 400.0,-19.5,-26.0},{ 350.0,-27.0,-35.0},{ 300.0,-36.0,-45.0},
            { 250.0,-46.5,-56.0},{ 200.0,-56.0,-66.0},{ 150.0,-60.0,-72.0},
            { 100.0,-62.0,-76.0}};
        for(const auto& level:levels){
            soundingPressure.append(level[0]);
            soundingTemperature.append(level[1]);
            soundingDewpoint.append(level[2]);
        }
    }

    // The Piper diagram's six columns, in milliequivalents, with waters in all
    // four quadrants of the diamond so the classification is exercised rather
    // than merely reached.
    QVector<double> waterCa,waterMg,waterNa,waterHco3,waterSo4,waterCl;
    {
        const double table[8][6]={
            {3.8,1.6,0.6,5.2,0.5,0.3},{0.4,0.3,4.6,0.5,0.6,4.2},
            {2.6,1.1,0.8,0.7,2.9,0.9},{0.5,0.4,4.2,4.6,0.3,0.2},
            {2.9,1.2,1.9,3.0,1.1,1.9},{1.1,0.7,3.0,1.4,1.5,1.9},
            {3.2,1.4,1.5,3.6,1.2,1.3},{0.9,0.5,3.3,2.9,0.8,1.0}};
        for(const auto& row:table){
            waterCa.append(row[0]); waterMg.append(row[1]); waterNa.append(row[2]);
            waterHco3.append(row[3]); waterSo4.append(row[4]); waterCl.append(row[5]);
        }
    }

    // Batch 15. A hierarchy as node, parent and the value the node holds
    // itself: two branches under one root, with the right-hand branch carrying
    // some of its own so the exposed part of a parent - which is what "self"
    // means on an icicle - appears rather than being covered by its children.
    QVector<double> treeNode,treeParent,treeOwn;
    {
        const double table[7][3]={{1,1,0},{10,1,0},{20,1,20},
                                  {11,10,30},{12,10,30},{21,20,10},{22,20,10}};
        for(const auto& row:table){
            treeNode.append(row[0]); treeParent.append(row[1]); treeOwn.append(row[2]);
        }
    }

    // Batch 16. A design response spectrum, built as the three branches a code
    // spectrum has: constant acceleration at short period, constant velocity in
    // the middle, constant displacement at long period. The corners are placed
    // so that the flat acceleration and the flat displacement are both decades,
    // which means the first and last branches land exactly on lines the engine
    // draws for itself - and a grid that is out by the factor of 2 pi, or has
    // the two families the wrong way round, shows immediately.
    QVector<double> spectrumPeriod,spectrumVelocity;
    {
        const double twoPi=2.0*M_PI;
        const double flatV=100.0,flatA=1000.0,flatD=100.0;
        const double corner1=twoPi*flatV/flatA,corner2=twoPi*flatV/flatD;
        for(int i=0;i<=90;++i){
            const double t=std::pow(10.0,-1.5+3.0*double(i)/90.0);
            spectrumPeriod.append(t);
            spectrumVelocity.append(t<corner1?flatA*t/twoPi
                                   :t>corner2?twoPi*flatD/t:flatV);
        }
    }

    // Batch 17. An ULTRAMETRIC tree - every tip exactly one unit from the root,
    // reached over branches of different lengths at different depths. All four
    // tips must therefore line up in a single column on the right, which they
    // can only do if each branch's horizontal run really is its own length
    // rather than something scaled per level.
    QVector<double> cladeNode,cladeParent,cladeLength;
    {
        const double table[7][3]={{1,1,0},{2,1,0.3},{3,1,0.5},
                                  {4,2,0.7},{5,2,0.7},{6,3,0.5},{7,3,0.5}};
        for(const auto& row:table){
            cladeNode.append(row[0]); cladeParent.append(row[1]);
            cladeLength.append(row[2]);
        }
    }

    // Streamgraph bands. The first is CONSTANT, which is the test: on an
    // ordinary stacked area a steady band riding on moving ones is bent by
    // them, and the whole reason for a free baseline is that it need not be.
    // Its two edges must stay parallel while the stack as a whole wanders.
    QVector<double> streamX,streamSteady,streamRising,streamFalling,streamHump;
    for(int i=0;i<=48;++i){
        const double u=double(i)/48.0;
        streamX.append(2000.0+20.0*u);
        streamSteady.append(10.0);
        streamRising.append(2.0+18.0*u);
        streamFalling.append(20.0-16.0*u);
        streamHump.append(4.0+22.0*std::exp(-40.0*(u-0.55)*(u-0.55)));
    }

    // Batch 18. A ternary lattice at tenths, with the measured value set to
    // exactly a hundred times the FIRST component. Every contour of that field
    // is a line of constant first component - and those are the ten per cent
    // grid lines the frame draws independently of the data, so the contours
    // must lie ON them rather than near them. The interpolant is linear inside
    // each triangle and the field is globally linear, so the agreement is
    // exact; a transposed projection, a wrong normalisation or a triangulation
    // built in a different space from the one it is drawn in all break it.
    QVector<double> mixA,mixB,mixC,mixValue;
    {
        const int side=10;
        for(int i=0;i<=side;++i)
            for(int j=0;j<=side-i;++j){
                const double pa=double(i)/side,pb=double(j)/side;
                mixA.append(pa); mixB.append(pb);
                mixC.append(1.0-pa-pb);
                mixValue.append(100.0*pa);
            }
    }

    // Voronoi sites: a scattered survey with two clusters and a sparse corner,
    // so the area shading has something to distinguish rather than a uniform
    // field that would look the same however it were computed.
    QVector<double> siteX,siteY;
    {
        quint32 grain=97531u;
        const auto next=[&grain]{
            grain^=grain<<13; grain^=grain>>17; grain^=grain<<5;
            return double(grain)/4294967296.0;
        };
        for(int i=0;i<18;++i){ siteX.append(next()*3.0);      siteY.append(next()*3.0); }
        for(int i=0;i<12;++i){ siteX.append(7.0+next()*2.0);  siteY.append(1.0+next()*2.0); }
        for(int i=0;i<6;++i){  siteX.append(next()*10.0);     siteY.append(5.0+next()*3.0); }
    }

    const QHash<QString,QVector<PlotSeries>> shaped{
        {QStringLiteral("Network Graph"),{column("from",edgeFrom),column("to",edgeTo),
                                          column("weight",edgeWeight)}},
        {QStringLiteral("Chord Diagram"),{column("from",edgeFrom),column("to",edgeTo),
                                          column("weight",edgeWeight)}},
        {QStringLiteral("Mosaic Plot"),{column("region",categoryA),column("grade",categoryB),
                                        column("count",tally)}},
        {QStringLiteral("UpSet Plot"),{column("set A",memberA),column("set B",memberB),
                                       column("set C",memberC)}},
        {QStringLiteral("Kaplan-Meier Survival"),{column("time",lifetimes),column("event",events)}},
        {QStringLiteral("Smith Chart"),{column("resistance",resistance),
                                        column("reactance",reactance)}},
        // A confusion matrix is a pair of class labels. Given the shared
        // continuous signal it sees 240 distinct classes, refuses to draw a
        // 240x240 grid, and is right to.
        {QStringLiteral("Confusion Matrix"),{column("true",categoryA),
                                             column("predicted",categoryB)}},
        // Batch 1 of the catalogue expansion. Each of these reads a specific
        // pair or triple; handed the shared continuous signal they would be
        // asked to read a smooth curve as an event flag or a cluster label.
        {QStringLiteral("Nelson-Aalen Cumulative Hazard"),
            {column("time",lifetimes),column("event",events)}},
        {QStringLiteral("Cumulative Incidence"),
            {column("time",lifetimes),column("event",events)}},
        {QStringLiteral("Discrimination Threshold"),
            {column("score",riskScore),column("outcome",outcome)}},
        {QStringLiteral("Decision Curve"),
            {column("probability",riskScore),column("outcome",outcome)}},
        {QStringLiteral("Silhouette Plot"),
            {column("cluster",clusterId),column("silhouette",silhouette)}},
        {QStringLiteral("Mean Cumulative Function"),
            {column("age",repairAge),column("unit",repairUnit)}},
        {QStringLiteral("Radial Plot"),
            {column("effect",effect),column("standard error",stdErr)}},
        {QStringLiteral("Cumulative Meta-Analysis"),
            {column("effect",effect),column("variance",variance)}},
        {QStringLiteral("Caterpillar Plot"),
            {column("estimate",effect),column("lower",ciLo),column("upper",ciHi)}},
        {QStringLiteral("Duane Plot"),{duaneSeries}},
        // Batch 2. Each of these reads a physical pair or triple, and the
        // shared smooth signal is not one: a Reynolds number that goes to 12
        // never leaves the laminar region, and a plasticity chart of a decaying
        // exponential classifies nothing.
        {QStringLiteral("Moody Diagram"),
            {column("Re",reynolds),column("f",frictionFactor)}},
        {QStringLiteral("Pump Performance Curve"),
            {column("flow",pumpFlow),column("head",pumpHead)}},
        {QStringLiteral("Stribeck Curve"),
            {column("Hersey",hersey),column("mu",friction)}},
        {QStringLiteral("Haigh Diagram"),
            {column("mean stress",meanStress),column("alternating",altStress)}},
        {QStringLiteral("Crack Growth Rate"),
            {column("dK",deltaK),column("da/dN",growthRate)}},
        {QStringLiteral("Strain-Life Curve"),
            {column("2Nf",reversals),column("strain",strainAmp)}},
        {QStringLiteral("Larson-Miller Curve"),
            {column("T",creepTemp),column("hours",creepHours),column("stress",creepStress)}},
        {QStringLiteral("Campbell Diagram"),
            {column("rpm",shaftSpeed),column("mode 1",mode1),column("mode 2",mode2)}},
        {QStringLiteral("Shaft Orbit"),
            {column("probe X",orbitX),column("probe Y",orbitY)}},
        {QStringLiteral("Plasticity Chart"),
            {column("LL",liquidLimit),column("PI",plasticIndex)}},
        {QStringLiteral("Particle Size Distribution"),
            {column("size",grainSize),column("finer",percentFiner)}},
        {QStringLiteral("Nichols Chart"),
            {column("phase",openPhase),column("gain",openGain)}},
        {QStringLiteral("Pole-Zero Map"),
            {column("real",poleRe),column("imag",poleIm)}},
        {QStringLiteral("P-V Nose Curve"),
            {column("P",busPower),column("V",busVoltage)}},
        {QStringLiteral("Airfoil Cp Distribution"),
            {column("x/c",chord),column("upper",cpUpper),column("lower",cpLower)}},
        // Batch 3.
        {QStringLiteral("Lineweaver-Burk Plot"),
            {column("[S]",substrate),column("v",rate)}},
        {QStringLiteral("Eadie-Hofstee Plot"),
            {column("[S]",substrate),column("v",rate)}},
        {QStringLiteral("Hanes-Woolf Plot"),
            {column("[S]",substrate),column("v",rate)}},
        {QStringLiteral("Scatchard Plot"),
            {column("bound",boundLigand),column("free",freeLigand)}},
        {QStringLiteral("van Deemter Plot"),
            {column("u",velocity),column("H",plateHeight)}},
        {QStringLiteral("BET Plot"),
            {column("P/P0",relPressure),column("Q",adsorbed)}},
        {QStringLiteral("Job Plot"),
            {column("fraction",moleFraction),column("signal",jobSignal)}},
        {QStringLiteral("Gutenberg-Richter Plot"),{column("magnitude",magnitudes)}},
        {QStringLiteral("Double-Mass Curve"),
            {column("station",stationRain),column("reference",referenceRain)}},
        {QStringLiteral("Hodograph"),{column("u",windU),column("v",windV)}},
        {QStringLiteral("Flow-Volume Loop"),
            {column("volume",spiroVolume),column("flow",spiroFlow)}},
        {QStringLiteral("Pressure-Volume Loop"),
            {column("volume",lvVolume),column("pressure",lvPressure)}},
        {QStringLiteral("Allan Deviation"),{column("y",clockPhase)}},
        {QStringLiteral("Paschen Curve"),
            {column("pd",paschenPd),column("Vb",paschenV)}},
        {QStringLiteral("Phase-Folded Light Curve"),
            {column("time",obsTime),column("flux",obsFlux),column("period",trialPeriod)}},
        {QStringLiteral("O-C Diagram"),
            {column("epoch",epochNumber),column("observed",observedTime)}},
        // Batch 4.
        {QStringLiteral("ICE Plot"),
            {column("feature",iceFeature),column("prediction",icePrediction),
             column("id",iceId)}},
        {QStringLiteral("Added-Variable Plot"),
            {column("y",avpY),column("focus",avpFocus),column("other",avpOther)}},
        {QStringLiteral("Interaction Plot"),
            {column("A",factorA),column("B",factorB),column("response",cellValue)}},
        {QStringLiteral("Tornado Diagram"),
            {column("low",swingLow),column("high",swingHigh)}},
        {QStringLiteral("Efficient Frontier"),
            {column("risk",portRisk),column("return",portReturn)}},
        {QStringLiteral("Snail Trail"),
            {column("risk",portRisk),column("return",portReturn)}},
        {QStringLiteral("Cost-Effectiveness Plane"),
            {column("effect",incEffect),column("cost",incCost)}},
        {QStringLiteral("Acceptability Curve"),
            {column("effect",incEffect),column("cost",incCost)}},
        {QStringLiteral("Lasagna Plot"),
            {column("subject",lasSubject),column("time",lasTime),column("value",lasValue)}},
        {QStringLiteral("Swimmer Plot"),
            {column("subject",swimSubject),column("start",swimStart),column("stop",swimStop)}},
        {QStringLiteral("Influence Plot"),
            {column("leverage",leverage),column("residual",studentised)}},
        // Batch 5.
        {QStringLiteral("Tafel Plot"),
            {column("overpotential",tafelEta),column("current density",tafelJ)}},
        {QStringLiteral("Cyclic Voltammogram"),
            {column("potential",cvPotential),column("current",cvCurrent)}},
        {QStringLiteral("Levich Plot"),
            {column("rotation",rotation),column("limiting current",limitingCurrent)}},
        {QStringLiteral("Koutecky-Levich Plot"),
            {column("rotation",rotation),column("current",limitingCurrent)}},
        {QStringLiteral("Randles-Sevcik Plot"),
            {column("scan rate",scanRate),column("peak current",peakCurrent)}},
        {QStringLiteral("Monod Growth Curve"),
            {column("substrate",monodS),column("growth rate",monodMu)}},
        {QStringLiteral("Substrate Inhibition Curve"),
            {column("substrate",monodS),column("growth rate",inhibitedRate)}},
        {QStringLiteral("Ragone Plot"),
            {column("power",ragonePower),column("energy",ragoneEnergy),
             column("family",ragoneFamily)}},
        {QStringLiteral("I-V Curve"),
            {column("voltage",pvVoltage),column("current",pvCurrent)}},
        {QStringLiteral("Degree-Day Signature"),
            {column("temperature",outsideTemp),column("energy",meteredEnergy)}},
        {QStringLiteral("Abatement Cost Curve"),
            {column("abatement",abateQty),column("cost",abateCost)}},
        {QStringLiteral("Mohr's Circle"),
            {column("sigma x",sigmaX),column("sigma y",sigmaY),column("tau xy",tauXY)}},
        {QStringLiteral("P-M Interaction Diagram"),
            {column("moment",pmMoment),column("axial",pmAxial)}},
        {QStringLiteral("Pushover Capacity Curve"),
            {column("displacement",pushDisp),column("base shear",pushShear)}},
        {QStringLiteral("Response Spectrum"),
            {column("period",specPeriod),column("Sa 5%",specAccel)}},
        {QStringLiteral("Consolidation Curve"),
            {column("time",consolTime),column("settlement",consolSettle)}},
        {QStringLiteral("Lomb-Scargle Periodogram"),
            {column("time",lombTime),column("value",lombValue)}},
        {QStringLiteral("Waffle Chart"),{column("share",waffleShare)}},
        // Batch 6.
        {QStringLiteral("Cook's Distance Plot"),
            {column("fitted",fittedValue),column("residual",plainResidual)}},
        {QStringLiteral("Scale-Location Plot"),
            {column("fitted",fittedValue),column("residual",growingResidual)}},
        {QStringLiteral("Partial Residual Plot"),
            {column("predictor",predictorX),column("response",responseY)}},
        {QStringLiteral("Savitzky-Golay Smoothing"),
            {column("x",noisyX),column("y",noisyY)}},
        {QStringLiteral("LOWESS Trend"),
            {column("x",noisyX),column("y",noisyY)}},
        {QStringLiteral("Partial Autocorrelation"),
            {column("t",arSeries),column("value",arSeries)}},
        {QStringLiteral("Seasonal Decomposition"),
            {column("time",seasonTime),column("value",seasonValue),
             column("period",seasonPeriod)}},
        {QStringLiteral("Seasonal Subseries Plot"),
            {column("time",seasonTime),column("value",seasonValue)}},
        {QStringLiteral("Recurrence Plot"),
            {column("t",chaos),column("value",chaos)}},
        {QStringLiteral("Wavelet Scalogram"),
            {column("t",chaos),column("value",chaos)}},
        {QStringLiteral("Coherence Spectrum"),
            {column("a",driveA),column("b",driveB)}},
        {QStringLiteral("Bode Plot"),
            {column("frequency",bodeFreq),column("gain",bodeGain),
             column("phase",bodePhase)}},
        {QStringLiteral("Operating Characteristic Curve"),
            {column("n",planSize),column("accept",planAccept)}},
        {QStringLiteral("Cottrell Plot"),
            {column("time",settleTime),column("current",stepCurrent)}},
        {QStringLiteral("Coulombic Efficiency Trend"),
            {column("cycle",cycleIndex),column("efficiency",cycleEfficiency)}},
        {QStringLiteral("Biplot"),
            {column("A",mvA),column("B",mvB),column("C",mvC),column("D",mvD)}},
        {QStringLiteral("Star Glyph Plot"),
            {column("A",mvA),column("B",mvB),column("C",mvC),column("D",mvD)}},
        {QStringLiteral("Sunflower Plot"),
            {column("x",noisyX),column("y",noisyY)}},
        // Batch 7.
        {QStringLiteral("Concordia Diagram"),
            {column("207Pb/235U",concordia207),column("206Pb/238U",concordia206)}},
        {QStringLiteral("Isochron Plot"),
            {column("87Rb/86Sr",isochronParent),column("87Sr/86Sr",isochronDaughter)}},
        {QStringLiteral("Harker Diagram"),
            {column("SiO2",harkerSilica),column("MgO",harkerMgO),column("CaO",harkerCaO)}},
        {QStringLiteral("Normalised Spider Diagram"),
            {column("chondrite",spiderReference),column("sample A",spiderSampleA),
             column("sample B",spiderSampleB)}},
        {QStringLiteral("Keeling Plot"),
            {column("CO2",keelingConc),column("d13C",keelingDelta)}},
        {QStringLiteral("IDF Curve"),
            {column("duration",idfDuration),column("intensity",idfIntensity),
             column("return period",idfPeriod)}},
        {QStringLiteral("Van 't Hoff Plot"),
            {column("T",vantHoffT),column("K",vantHoffK)}},
        {QStringLiteral("Tauc Plot"),
            {column("energy",taucEnergy),column("alpha",taucAbsorption)}},
        {QStringLiteral("Stern-Volmer Plot"),
            {column("quencher",quencher),column("intensity",quenchedIntensity)}},
        {QStringLiteral("TGA / DTG Curve"),
            {column("temperature",tgaTemperature),column("mass",tgaMass)}},
        {QStringLiteral("DSC Thermogram"),
            {column("temperature",dscTemperature),column("heat flow",dscHeatFlow)}},
        {QStringLiteral("Creep Curve"),
            {column("time",creepTime),column("strain",creepStrain)}},
        {QStringLiteral("Master Curve (TTS)"),
            {column("frequency",ttsFrequency),column("modulus",ttsModulus),
             column("temperature",ttsTemperature)}},
        {QStringLiteral("Rheology Flow Curve"),
            {column("shear rate",shearRate),column("shear stress",shearStress)}},
        {QStringLiteral("Octave Band Spectrum"),
            {column("frequency",bandFrequency),column("level",bandLevel)}},
        {QStringLiteral("Pharmacokinetic Profile"),
            {column("time",pkTime),column("concentration",pkConcentration)}},
        {QStringLiteral("Yield Curve"),
            {column("maturity",yieldMaturity),column("yield",yieldRate),
             column("date",yieldDate)}},
        {QStringLiteral("Nyquist Stability Plot"),
            {column("real",nyquistReal),column("imaginary",nyquistImag)}},
        // Batch 8.
        {QStringLiteral("Proctor Compaction Curve"),
            {column("moisture",proctorMoisture),column("dry density",proctorDensity)}},
        {QStringLiteral("Mohr-Coulomb Envelope"),
            {column("normal stress",normalStress),column("shear at failure",failureShear)}},
        {QStringLiteral("Influence Line"),
            {column("position",loadPosition),column("moment",sectionMoment)}},
        {QStringLiteral("Balanced Field Length"),
            {column("V1",decisionSpeed),column("accelerate-stop",stopDistance),
             column("accelerate-go",goDistance)}},
        {QStringLiteral("Specific Range"),
            {column("weight",cruiseWeight),column("specific range",specificRange)}},
        {QStringLiteral("Composite Curves (Pinch)"),
            {column("supply T",streamSupply),column("target T",streamTarget),
             column("CP",streamCapacity)}},
        {QStringLiteral("Residence Time Distribution"),
            {column("time",tracerTime),column("tracer",tracerConcentration)}},
        {QStringLiteral("Harmonic Spectrum"),
            {column("order",harmonicOrder),column("magnitude",harmonicMagnitude)}},
        {QStringLiteral("Sound Level Statistics"),
            {column("time",noiseTime),column("level",noiseLevel)}},
        {QStringLiteral("Beam Caustic"),
            {column("z",beamDistance),column("radius",beamRadius)}},
        {QStringLiteral("Youden Plot"),
            {column("material A",youdenA),column("material B",youdenB)}},
        {QStringLiteral("Levey-Jennings Chart"),
            {column("run",controlRun),column("result",controlValue)}},
        {QStringLiteral("Species Accumulation Curve"),
            {column("sample",surveySample),column("species",surveySpecies)}},
        {QStringLiteral("Epidemic Curve"),
            {column("day",onsetDay),column("cases",onsetCases)}},
        {QStringLiteral("Effective Reproduction Number"),
            {column("day",onsetDay),column("cases",onsetCases)}},
        {QStringLiteral("Cumulative Gain Chart"),
            {column("score",gainScore),column("outcome",gainOutcome)}},
        {QStringLiteral("Ratkowsky Square-Root Plot"),
            {column("temperature",growthTemperature),column("rate",ratkowskyRate)}},
        {QStringLiteral("Phenology Curve"),
            {column("day of year",phenologyDay),column("index",phenologyIndex)}},
        // Batch 9.
        {QStringLiteral("Guinier Plot"),
            {column("q",scatterQ),column("I",scatterI)}},
        {QStringLiteral("Kratky Plot"),
            {column("q",scatterQ),column("I",scatterI)}},
        {QStringLiteral("Hill Plot"),
            {column("ligand",hillLigand),column("occupancy",hillTheta)}},
        {QStringLiteral("Ellingham Diagram"),
            {column("T",ellinghamT),column("Al2O3",ellinghamAlumina),
             column("ZnO",ellinghamZincite)}},
        {QStringLiteral("Kissinger Plot"),
            {column("heating rate",heatingRate),column("peak T",peakTemperature)}},
        {QStringLiteral("Avrami Plot"),
            {column("time",avramiTime),column("transformed",avramiFraction)}},
        {QStringLiteral("Wilson Plot"),
            {column("velocity",wilsonVelocity),column("U",wilsonCoefficient)}},
        {QStringLiteral("Van Krevelen Diagram"),
            {column("O/C",krevelenOxygen),column("H/C",krevelenHydrogen)}},
        {QStringLiteral("Hertzsprung-Russell Diagram"),
            {column("B-V",starColour),column("Mv",starMagnitude)}},
        {QStringLiteral("Rotation Curve"),
            {column("radius",orbitRadius),column("speed",orbitSpeed)}},
        {QStringLiteral("Hubble Diagram"),
            {column("distance",hubbleDistance),column("speed",hubbleSpeed)}},
        {QStringLiteral("Constellation Diagram"),
            {column("I",symbolI),column("Q",symbolQ)}},
        {QStringLiteral("Group Delay"),
            {column("frequency",delayFrequency),column("phase",delayPhase)}},
        {QStringLiteral("Wind Profile (Log Law)"),
            {column("height",profileHeight),column("speed",profileSpeed)}},
        {QStringLiteral("Experience Curve"),
            {column("cumulative",cumulativeVolume),column("unit cost",unitCost)}},
        {QStringLiteral("EOQ Cost Curve"),
            {column("quantity",orderQuantity),column("ordering",orderingCost),
             column("holding",holdingCost)}},
        {QStringLiteral("Half-Normal Plot"),
            {column("effect",effectIndex),column("estimate",effectEstimate)}},
        {QStringLiteral("Response Waterfall"),
            {column("patient",patientIndex),column("best change",bestChange)}},
        // Batch 10.
        {QStringLiteral("Specific Energy Diagram"),
            {column("depth",channelDepth),column("q",unitDischarge)}},
        {QStringLiteral("NTU-Effectiveness Curve"),
            {column("NTU",transferUnits),column("effectiveness",transferEffectiveness),
             column("Cr",capacityRatio)}},
        {QStringLiteral("Pump Operating Point"),
            {column("flow",pumpFlow2),column("pump head",pumpDelivered),
             column("system head",systemDemanded)}},
        {QStringLiteral("Duck Curve (Net Load)"),
            {column("hour",dayHour),column("demand",dayDemand),column("solar",daySolar)}},
        {QStringLiteral("Protection Coordination Curve"),
            {column("current",faultCurrent),column("time",operateTime),
             column("device",protectiveDevice)}},
        {QStringLiteral("MTF Curve"),
            {column("cycles/mm",spatialFrequency),column("modulation",contrastModulation)}},
        {QStringLiteral("Radioactive Decay Fit"),
            {column("time",decayTime),column("activity",decayActivity)}},
        {QStringLiteral("Attenuation Curve"),
            {column("thickness",absorberThickness),column("intensity",transmitted)}},
        {QStringLiteral("Depth-Dose Curve"),
            {column("depth",beamDepth),column("dose",depositedDose)}},
        {QStringLiteral("Critical Power Curve"),
            {column("duration",effortDuration),column("power",effortPower)}},
        {QStringLiteral("Lactate Threshold Curve"),
            {column("work rate",workRate),column("lactate",bloodLactate)}},
        {QStringLiteral("Half-Power Bandwidth"),
            {column("frequency",sweepFrequency),column("magnitude",sweepMagnitude)}},
        {QStringLiteral("Cumulative Vehicle Count"),
            {column("time",queueTime),column("arrivals",queueArrivals),
             column("departures",queueDepartures)}},
        {QStringLiteral("Cohort Retention Curve"),
            {column("age",cohortAge),column("retained",cohortRetained),
             column("cohort",cohortLabel)}},
        {QStringLiteral("Bass Diffusion Curve"),
            {column("time",adoptionTime),column("adopters",adoptionCumulative)}},
        {QStringLiteral("Pareto Chart"),
            {column("category",faultCategory),column("count",faultCount)}},
        {QStringLiteral("Statistical Power Curve"),
            {column("n",designSize),column("d",designEffect)}},
        {QStringLiteral("Sequential Test Boundaries"),
            {column("trial",trialIndex),column("outcome",trialOutcome)}},
        // Batch 11.
        {QStringLiteral("Chromatogram Peak Metrics"),
            {column("time",chromTime),column("signal",chromSignal)}},
        {QStringLiteral("Langmuir Isotherm"),
            {column("Ce",langmuirCe),column("qe",langmuirQe)}},
        {QStringLiteral("Zeta Potential Curve"),
            {column("pH",zetaPh),column("zeta",zetaPotential)}},
        {QStringLiteral("Distillation Curve"),
            {column("recovered",distilRecovered),column("T",distilTemperature)}},
        {QStringLiteral("qPCR Standard Curve"),
            {column("quantity",pcrQuantity),column("Ct",pcrThreshold)}},
        {QStringLiteral("Melting Curve (Tm)"),
            {column("temperature",meltTemperature),column("fluorescence",meltFluorescence)}},
        {QStringLiteral("Growth Rate (OD)"),
            {column("time",cultureTime),column("OD",cultureDensity)}},
        {QStringLiteral("Growth Percentile Chart"),
            {column("age",childAge),column("height",childHeight)}},
        {QStringLiteral("Hysteresis Loop (B-H)"),
            {column("H",fieldStrength),column("B",fluxDensity)}},
        {QStringLiteral("Torque-Speed Curve"),
            {column("speed",motorSpeed),column("motor",motorTorque),
             column("load",loadTorque)}},
        {QStringLiteral("Radiation Pattern"),
            {column("bearing",patternBearing),column("gain",patternGain)}},
        {QStringLiteral("Soil Water Retention Curve"),
            {column("suction",suctionHead),column("theta",waterContent)}},
        {QStringLiteral("Detrended Fluctuation Analysis"),
            {column("i",dfaIndex),column("value",dfaValue)}},
        {QStringLiteral("Poincare Plot"),
            {column("beat",beatIndex),column("RR",beatInterval)}},
        {QStringLiteral("Kingman Queue Curve"),
            {column("rho",queueUtilisation),column("wait",queueWait)}},
        {QStringLiteral("Little's Law Check"),
            {column("arrival",arrivalRate),column("WIP",workInProgress),
             column("lead",leadTime)}},
        {QStringLiteral("Burndown Chart"),
            {column("day",burnDay),column("remaining",burnRemaining)}},
        {QStringLiteral("Regression Confidence Band"),
            {column("x",bandPredictor),column("y",bandResponse)}},
        // Batch 12.
        {QStringLiteral("Kubelka-Munk Plot"),
            {column("eV",reflectEnergy),column("R",reflectance)}},
        {QStringLiteral("Freundlich Isotherm"),
            {column("Ce",freundlichCe),column("qe",freundlichQe)}},
        {QStringLiteral("Cole-Cole Plot"),
            {column("real",coleReal),column("imaginary",coleImaginary)}},
        {QStringLiteral("Conductometric Titration"),
            {column("volume",titrantVolume),column("conductivity",titrantConductivity)}},
        {QStringLiteral("Hot-Wire Conductivity"),
            {column("time",wireTime),column("rise",wireRise),column("q",wirePower)}},
        {QStringLiteral("Multi-Vari Chart"),
            {column("part",variPart),column("value",variValue),
             column("operator",variOperator)}},
        {QStringLiteral("Tolerance Interval Plot"),
            {column("run",toleranceRun),column("value",toleranceValue)}},
        {QStringLiteral("Rare-Event Interval Chart"),
            {column("event",rareEvent),column("time",rareTime)}},
        {QStringLiteral("Spaghetti Plot"),
            {column("time",repeatTime),column("value",repeatValue),
             column("subject",repeatSubject)}},
        {QStringLiteral("Unit Hydrograph"),
            {column("time",stormTime),column("discharge",stormDischarge)}},
        {QStringLiteral("Recession Curve Analysis"),
            {column("time",recessionTime),column("discharge",recessionDischarge)}},
        {QStringLiteral("Step Response Metrics"),
            {column("time",settleTime),column("response",settleResponse)}},
        {QStringLiteral("Jitter Bathtub"),
            {column("position",jitterPosition),column("BER",jitterRate)}},
        {QStringLiteral("Pressure Derivative Plot"),
            {column("time",wellTime),column("dp",wellPressure)}},
        {QStringLiteral("Isoconversional Plot"),
            {column("beta",isoRate),column("T",isoTemperature),
             column("alpha",isoConversion)}},
        // Batch 13.
        {QStringLiteral("Skew-T Log-P"),
            {column("pressure",soundingPressure),
             column("temperature",soundingTemperature),
             column("dewpoint",soundingDewpoint)}},
        // Batch 14: the same sounding through the other three transforms.
        // Sharing the input is deliberate. The four charts differ only in
        // where a point lands on the page, so they must agree about the
        // condensation level and the energies; a disagreement between these
        // four renders is a transform that has leaked into the physics.
        {QStringLiteral("Emagram"),
            {column("pressure",soundingPressure),
             column("temperature",soundingTemperature),
             column("dewpoint",soundingDewpoint)}},
        {QStringLiteral("Stuve Diagram"),
            {column("pressure",soundingPressure),
             column("temperature",soundingTemperature),
             column("dewpoint",soundingDewpoint)}},
        {QStringLiteral("Tephigram"),
            {column("pressure",soundingPressure),
             column("temperature",soundingTemperature),
             column("dewpoint",soundingDewpoint)}},
        {QStringLiteral("Piper Diagram"),
            {column("Ca",waterCa),column("Mg",waterMg),column("Na+K",waterNa),
             column("HCO3",waterHco3),column("SO4",waterSo4),column("Cl",waterCl)}},
        // Batch 15. The Stiff diagram takes the Piper's six columns unchanged,
        // which is the point of it: the same six ions, read as shapes rather
        // than as positions, so the two figures can be put side by side.
        {QStringLiteral("Stiff Diagram"),
            {column("Ca",waterCa),column("Mg",waterMg),column("Na+K",waterNa),
             column("HCO3",waterHco3),column("SO4",waterSo4),column("Cl",waterCl)}},
        {QStringLiteral("Arc Diagram"),{column("from",edgeFrom),column("to",edgeTo),
                                        column("weight",edgeWeight)}},
        {QStringLiteral("Icicle Plot"),
            {column("node",treeNode),column("parent",treeParent),
             column("own",treeOwn)}},
        {QStringLiteral("Flame Graph"),
            {column("node",treeNode),column("parent",treeParent),
             column("own",treeOwn)}},
        {QStringLiteral("Voronoi Diagram"),
            {[&]{ PlotSeries s; s.label=QStringLiteral("site");
                  s.x=siteX; s.y=siteY; return s; }()}},
        // Batch 16. The Durov takes the Piper's and the Stiff's six columns
        // unchanged - three readings of one analysis, which is the point of
        // having all three.
        {QStringLiteral("Durov Diagram"),
            {column("Ca",waterCa),column("Mg",waterMg),column("Na+K",waterNa),
             column("HCO3",waterHco3),column("SO4",waterSo4),column("Cl",waterCl)}},
        // Batch 17.
        {QStringLiteral("Cladogram"),
            {column("node",cladeNode),column("parent",cladeParent),
             column("length",cladeLength)}},
        {QStringLiteral("Streamgraph"),
            {[&]{ PlotSeries s; s.label=QStringLiteral("steady");
                  s.x=streamX; s.y=streamSteady;
                  s.color=QColor(0x3b,0x6e,0xa5); return s; }(),
             [&]{ PlotSeries s; s.label=QStringLiteral("rising");
                  s.x=streamX; s.y=streamRising;
                  s.color=QColor(0x8a,0xb5,0x6b); return s; }(),
             [&]{ PlotSeries s; s.label=QStringLiteral("falling");
                  s.x=streamX; s.y=streamFalling;
                  s.color=QColor(0xd0,0x7a,0x4a); return s; }(),
             [&]{ PlotSeries s; s.label=QStringLiteral("hump");
                  s.x=streamX; s.y=streamHump;
                  s.color=QColor(0x8a,0x6b,0xa5); return s; }()}},
        // Batch 18.
        {QStringLiteral("Ternary Contour"),
            {column("A",mixA),column("B",mixB),column("C",mixC),
             column("value",mixValue)}},
        {QStringLiteral("Tripartite Response Spectrum"),
            {[&]{ PlotSeries s; s.label=QStringLiteral("design spectrum");
                  s.x=spectrumPeriod; s.y=spectrumVelocity; return s; }()}},
    };

    for(const QString& engine:engines){
        PlotSpec spec;
        spec.engine=engine;
        spec.title=engine;
        spec.xAxis.label=QStringLiteral("time_h");
        spec.yAxis.label=QStringLiteral("value");
        spec.series=shaped.contains(engine)?shaped.value(engine)
                                          :QVector<PlotSeries>{a,b,c,d,e};
        spec.style.background=Qt::white;
        spec.style.foreground=QColor(0x11,0x11,0x11);
        spec.style.gridColor=QColor(0xd8,0xd8,0xd8);

        QImage canvas(640,400,QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::white);
        {
            QPainter painter(&canvas);
            painter.setRenderHint(QPainter::Antialiasing,true);
            backend.render(&painter,QRectF(0,0,640,400),spec);
        }

        const QImage drawn=imageOf(spec);

        // The chrome probe has to be the PREPARED spec with its series cleared,
        // not the original. The formula engines synthesise their own data from
        // an expression, so clearing the input series does not stop them: the
        // probe drew the whole function, on a different default domain, and
        // reported more ink than the real render. Taking the prepared spec means
        // the probe runs the final engine, which has nothing left to invent.
        PlotSpec chromeOnly=backend.preparedFor(spec);
        chromeOnly.series.clear();
        chromeOnly.expression.clear();
        // ...and it has to be drawn on the SAME frame. With the series gone the
        // axes fall back to a default range, so the empty probe gets different
        // tick values, different label widths and a differently inset plot
        // area - a different amount of chrome. That is not a control, and it
        // reported a correct Nelson-Aalen curve as blank: a thin staircase puts
        // less ink on the page than the extra gridlines its own empty frame
        // drew, so "drew something" came out negative.
        //
        // Pinning both axes to the range the real render used makes the chrome
        // identical by construction, and every differing pixel data. The same
        // mistake, in the same shape, has now turned up in three separate
        // sweeps; this is the fix in all three.
        {
            const QtPlotBackend::DataRange r=backend.rangeFor(spec);
            const auto pin=[](PlotAxis& axis,double lo,double hi,bool log){
                if(!(std::isfinite(lo)&&std::isfinite(hi)&&hi>lo)) return;
                // rangeFor reports a log axis in LOG space; PlotAxis holds data
                // values.
                axis.min=log?std::pow(10.0,lo):lo;
                axis.max=log?std::pow(10.0,hi):hi;
            };
            pin(chromeOnly.xAxis,r.xLo,r.xHi,r.xLog);
            pin(chromeOnly.yAxis,r.yLo,r.yHi,r.yLog);
        }
        const qint64 changed=changedPixels(drawn,imageOf(chromeOnly));

        // 300 changed pixels at full resolution is well under a single thin
        // curve across a 640-wide plot, and far more than antialiasing noise
        // between two identical frames - which is zero, because the frames ARE
        // identical once the axes are pinned.
        if(changed<300){
            // Say what the rewrite produced, not just that the picture is
            // empty: no series at all and a series of no points are different
            // faults with different causes.
            const PlotSpec prepared=backend.preparedFor(spec);
            int points=0;
            for(const PlotSeries& s:prepared.series) points+=qMin(s.x.size(),s.y.size());
            blank.append(QStringLiteral("%1 [%2 px changed, %3 series, %4 pts, as %5]")
                             .arg(engine).arg(changed)
                             .arg(prepared.series.size()).arg(points).arg(prepared.engine));
        }
    }

    printf("selftest: swept %d engines\n",int(engines.size()));
    if(!blank.isEmpty()){
        printf("selftest: ENGINES DREW (almost) NOTHING: %s\n",qPrintable(blank.join(QStringLiteral(", "))));
        return false;
    }
    printf("selftest: every engine drew data\n");
    return true;
}

// ---------------------------------------------------------------------------
// Targeted regression checks.
//
// The engine sweep answers "did this engine put ink on the page". Three real
// defects passed that test while producing the wrong picture, so each one gets
// a check that fails for the specific reason it was wrong.
namespace {

QImage renderToImage(QtPlotBackend& backend,const PlotSpec& spec,int w=480,int h=320){
    QImage canvas(w,h,QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::white);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing,true);
    backend.render(&painter,QRectF(0,0,w,h),spec);
    painter.end();
    return canvas;
}

PlotSpec whiteSpec(const QString& engine){
    PlotSpec spec;
    spec.engine=engine;
    spec.title=engine;
    spec.xAxis.label=QStringLiteral("x");
    spec.yAxis.label=QStringLiteral("y");
    spec.style.background=Qt::white;
    spec.style.foreground=QColor(0x11,0x11,0x11);
    spec.style.gridColor=QColor(0xd8,0xd8,0xd8);
    return spec;
}

} // namespace

bool runRegressionChecks(){
    QStringList failures;

    // ------------------------------------------------------------ 1. cache
    // render() caches the prepared spec against a fingerprint of its inputs,
    // because prepareSpec is where the KDE sums, the FFTs and the iterative
    // fits actually happen and it was re-running all of it on every repaint.
    // A cache that never invalidates is a far worse bug than the slowness it
    // cures, so this asserts both halves: the same input twice draws the same
    // picture, and a changed input draws a different one.
    {
        const QStringList heavy{
            QStringLiteral("KDE Density"), QStringLiteral("Histogram"),
            QStringLiteral("Power Spectral Density"), QStringLiteral("Violin Plot"),
            QStringLiteral("Gompertz H2 Kinetics"), QStringLiteral("ECDF"),
            QStringLiteral("Autocorrelation"), QStringLiteral("Box Plot")};

        PlotSeries base;
        base.label=QStringLiteral("signal");
        for(int i=0;i<300;++i){
            const double t=0.05+i*0.05;
            base.x.append(t);
            base.y.append(2.0+3.0*(1.0-std::exp(-t/2.0))+0.4*std::sin(t*2.7));
        }

        for(const QString& engine:heavy){
            QtPlotBackend backend;   // fresh, so the first render is a cache miss
            PlotSpec specA=whiteSpec(engine);
            specA.series={base};

            PlotSpec specB=specA;
            // Deliberately NOT an affine change. Several of these engines are
            // scale- and shift-invariant by definition - an autocorrelation of
            // 2y+5 is the autocorrelation of y - so multiplying the data would
            // make this check pass on an engine that never re-read it. This
            // changes the shape of the signal.
            for(int i=0;i<specB.series[0].y.size();++i){
                const double v=specB.series[0].y[i];
                specB.series[0].y[i]=v*(1.0+0.6*std::sin(i*0.31))+0.9*std::cos(i*0.07);
            }

            const QImage a1=renderToImage(backend,specA);
            const QImage b =renderToImage(backend,specB);
            const QImage a2=renderToImage(backend,specA);

            if(a1!=a2)
                failures.append(QStringLiteral("%1: identical input rendered differently "
                                               "(prepared-spec cache is not stable)").arg(engine));
            if(a1==b)
                failures.append(QStringLiteral("%1: changed data rendered identically "
                                               "(prepared-spec cache is stale)").arg(engine));
        }

        // And the point of the cache: the second render of the same spec must
        // not repeat the expensive preparation. Reported, not asserted - a
        // timing threshold in a build check is a flaky test.
        QtPlotBackend timed;
        PlotSpec kde=whiteSpec(QStringLiteral("KDE Density"));
        PlotSeries wide; wide.label=QStringLiteral("v");
        for(int i=0;i<4000;++i){ wide.x.append(i*0.01); wide.y.append(std::sin(i*0.013)*3.0+i*0.0007); }
        kde.series={wide};
        QElapsedTimer clock;
        clock.start();
        renderToImage(timed,kde);
        const qint64 cold=clock.nsecsElapsed();
        clock.restart();
        renderToImage(timed,kde);
        const qint64 warm=clock.nsecsElapsed();
        printf("selftest: prepared-spec cache, KDE over 4000 points: cold %.1f ms, warm %.1f ms\n",
               double(cold)/1e6,double(warm)/1e6);
    }

    // -------------------------------------------------------------- 2. bars
    // drawBar used to place bar i at plotArea.left() + slot*(i+0.5), ignoring
    // s.x[i] entirely, so bars stood under an axis they did not correspond to.
    // Two datasets with the same values and different x columns therefore drew
    // pixel-identical pictures apart from the tick labels. Different x spacing
    // must move the bars.
    {
        QtPlotBackend backend;
        PlotSpec even=whiteSpec(QStringLiteral("Bar"));
        PlotSeries s1; s1.label=QStringLiteral("count"); s1.drawLine=false;
        s1.x={1,2,3,4,5}; s1.y={3,7,4,9,5};
        even.series={s1};
        // Same five values, but clustered at the left of the axis. If position
        // comes from the index this is the same picture.
        PlotSpec uneven=even;
        uneven.series[0].x={1,2,3,4,20};

        const QImage evenImg=renderToImage(backend,even);
        const QImage unevenImg=renderToImage(backend,uneven);
        if(evenImg==unevenImg)
            failures.append(QStringLiteral("Bar: bars did not move when the x column changed "
                                           "(positioned by array index, not by x value)"));
    }

    // --------------------------------------------------- 2b. horizontal bars
    // drawHorizontalBar had exactly the fault drawBar did, and worse: it read
    // the value out of y (so bar length was scaled against the axis fitted to
    // the category column) and took the row from the point's index. Both
    // engines that reach it emit one series per bar, so n was always 1 and
    // every bar landed in the same band. Value on x, category on y.
    {
        QtPlotBackend backend;
        PlotSpec spread=whiteSpec(QStringLiteral("Horizontal Bar"));
        QVector<PlotSeries> bars;
        const double values[5]={4.0,9.0,2.0,7.0,5.0};
        for(int i=0;i<5;++i){
            PlotSeries bar;
            bar.label=QStringLiteral("v%1").arg(i);
            bar.drawLine=false;
            bar.x={values[i]};        // value
            bar.y={double(i+1)};      // category row
            bars.append(bar);
        }
        spread.series=bars;

        // Same five bars, all on the same row. If rows come from the array
        // index this is the same picture.
        PlotSpec stacked=spread;
        for(PlotSeries& bar:stacked.series) bar.y={1.0};

        if(renderToImage(backend,spread)==renderToImage(backend,stacked))
            failures.append(QStringLiteral("Horizontal Bar: moving every bar onto one row did not "
                                           "change the plot (rows come from the array index)"));

        // And the length has to follow the value: doubling every value must
        // change the picture.
        PlotSpec longer=spread;
        for(PlotSeries& bar:longer.series) bar.x={bar.x.first()*2.0+1.0};
        if(renderToImage(backend,spread)==renderToImage(backend,longer))
            failures.append(QStringLiteral("Horizontal Bar: changing the values did not change "
                                           "the bar lengths"));

        // The Global Sensitivity rewrite has to produce that shape, not the
        // transposed one it used to.
        PlotSpec sens=whiteSpec(QStringLiteral("Global Sensitivity"));
        PlotSeries s1,s2,s3;
        s1.label=QStringLiteral("flow");    s1.x={1}; s1.y={0.7};
        s2.label=QStringLiteral("temp");    s2.x={1}; s2.y={0.2};
        s3.label=QStringLiteral("pH");      s3.x={1}; s3.y={0.45};
        sens.series={s1,s2,s3};
        const PlotSpec prepSens=backend.preparedFor(sens);
        for(const PlotSeries& bar:prepSens.series){
            // Category rows are 1,2,3...; sensitivity indices here are < 1. If
            // the two are the wrong way round this catches it.
            if(bar.y.isEmpty()||bar.y.first()<1.0){
                failures.append(QStringLiteral("Global Sensitivity: category row is not in y "
                                               "(got %1) - the rewrite is transposed")
                                    .arg(bar.y.isEmpty()?QStringLiteral("nothing"):QString::number(bar.y.first())));
                break;
            }
        }
    }

    // --------------------------------------------------------- 3. waterfall
    // Waterfall used to rewrite onto "Error Bar", which reads series[0] as the
    // values and series[1] as their uncertainty. Forty one-point step series
    // collapsed into a single drawn point, which the sweep scored as ink.
    {
        QtPlotBackend backend;
        PlotSpec fall=whiteSpec(QStringLiteral("Waterfall"));
        PlotSeries steps; steps.label=QStringLiteral("delta");
        steps.x={1,2,3,4,5,6};
        steps.y={12,-4,7,-3,9,-2};
        fall.series={steps};

        const PlotSpec prepared=backend.preparedFor(fall);
        // Six steps plus the total, each a floating bar of {low, high}.
        if(prepared.series.size()<7)
            failures.append(QStringLiteral("Waterfall: rewrote to %1 series, expected 7")
                                .arg(prepared.series.size()));
        for(const PlotSeries& bar:prepared.series){
            if(bar.x.size()!=1||bar.y.size()!=2){
                failures.append(QStringLiteral("Waterfall: a step carries %1 x and %2 y values, "
                                               "expected 1 and 2").arg(bar.x.size()).arg(bar.y.size()));
                break;
            }
        }
        // Every bar has to be drawn, so reversing the order of the steps - which
        // changes every running total - must change the picture.
        PlotSpec reordered=fall;
        reordered.series[0].y={-2,9,-3,7,-4,12};
        if(renderToImage(backend,fall)==renderToImage(backend,reordered))
            failures.append(QStringLiteral("Waterfall: reordering the steps did not change the plot"));
    }

    // ----------------------------------------------- 4. statistical agreement
    // Four separate copies of the mean/covariance loop, three of the quantile
    // and three of the KDE meant two panels could report different numbers for
    // the same two columns. They share one implementation now, so this asserts
    // the numbers actually agree rather than that the code merely compiles.
    {
        QtPlotBackend backend;
        PlotSeries a,b;
        a.label=QStringLiteral("alpha"); b.label=QStringLiteral("beta");
        for(int i=0;i<400;++i){
            const double t=i*0.05;
            const double u=std::sin(t*1.7)+0.35*std::cos(t*0.9);
            a.x.append(t); a.y.append(3.0+u);
            b.x.append(t); b.y.append(1.5+0.8*u+0.4*std::sin(t*4.1));
        }

        // Correlation Matrix reports r for the (alpha, beta) cell...
        PlotSpec corr=whiteSpec(QStringLiteral("Correlation Matrix"));
        corr.series={a,b};
        const PlotSpec prepCorr=backend.preparedFor(corr);
        double matrixR=std::numeric_limits<double>::quiet_NaN();
        if(prepCorr.series.size()>=3){
            const PlotSeries& xi=prepCorr.series.at(0);
            const PlotSeries& yi=prepCorr.series.at(1);
            const PlotSeries& vi=prepCorr.series.at(2);
            for(int i=0;i<qMin(xi.y.size(),qMin(yi.y.size(),vi.y.size()));++i)
                if(int(xi.y[i])==0&&int(yi.y[i])==1) matrixR=vi.y[i];
        }

        // ...and Scatter + Marginals puts the same r in its series label.
        PlotSpec marg=whiteSpec(QStringLiteral("Scatter + Marginals"));
        marg.series={a,b};
        const PlotSpec prepMarg=backend.preparedFor(marg);
        double labelR=std::numeric_limits<double>::quiet_NaN();
        for(const PlotSeries& s:prepMarg.series){
            if(!s.label.startsWith(QLatin1String("r = "))) continue;
            bool ok=false;
            const double v=s.label.mid(4).toDouble(&ok);
            if(ok) labelR=v;
        }

        if(matrixR!=matrixR||labelR!=labelR){
            failures.append(QStringLiteral("Correlation agreement: could not read r from "
                                           "Correlation Matrix (%1) or Scatter + Marginals (%2)")
                                .arg(matrixR).arg(labelR));
        }else if(std::abs(matrixR-labelR)>0.0015){   // the label is printed to 3 dp
            failures.append(QStringLiteral("Correlation Matrix says r=%1 but Scatter + Marginals "
                                           "says r=%2 for the same two columns")
                                .arg(matrixR,0,'f',6).arg(labelR,0,'f',6));
        }
    }

    if(!failures.isEmpty()){
        for(const QString& f:failures) printf("selftest: REGRESSION: %s\n",qPrintable(f));
        printf("selftest: %d regression check(s) FAILED\n",int(failures.size()));
        return false;
    }
    printf("selftest: regression checks passed (prepared-spec cache, bar positions, "
           "horizontal bars, waterfall, correlation agreement)\n");
    return true;
}

bool runPlotSelfTest(const QString& outputPdf){
    // A decaying oscillation plus a monotone rise: exercises negative values,
    // a zero crossing, two series and therefore the legend.
    PlotSeries decay; decay.label=QStringLiteral("voltage_V"); decay.color=QColor(0x1f,0x6f,0xd0);
    PlotSeries rise;  rise.label=QStringLiteral("temperature_C"); rise.color=QColor(0xd0,0x62,0x1f);
    for(int i=0;i<400;++i){
        const double t=i*0.02;
        decay.x.append(t); decay.y.append(2.5*std::exp(-t/3.0)*std::cos(2*M_PI*0.8*t));
        rise.x.append(t);  rise.y.append(20.0+8.0*(1.0-std::exp(-t/2.2)));
    }

    PlotSpec spec;
    spec.engine=QStringLiteral("Line Chart");
    spec.title=QStringLiteral("GraphVis vector export self-test");
    spec.xAxis.label=QStringLiteral("time_s");
    spec.yAxis.label=QStringLiteral("value");
    spec.series={decay,rise};

    // Nature single-column, straight from GraphVis 17 core/publication.py:
    // 89 mm wide, 6 pt text, 0.75 pt lines, 600 dpi.
    spec.style.fontFamily=QStringLiteral("Arial");
    spec.style.baseFontSize=6; spec.style.titleSize=7; spec.style.axisLabelSize=6;
    spec.style.tickSize=5; spec.style.legendSize=5; spec.style.lineWidth=0.75;
    spec.style.gridVisible=false;
    spec.style.background=Qt::white;
    spec.style.foreground=QColor(0x11,0x11,0x11);
    spec.style.gridColor=QColor(0xd8,0xd8,0xd8);

    const double widthIn=89.0/25.4, heightIn=2.55;
    const int dpi=600;

    QPdfWriter writer(outputPdf);
    writer.setResolution(dpi);
    writer.setPageSize(QPageSize(QSizeF(widthIn,heightIn),QPageSize::Inch));
    writer.setPageMargins(QMarginsF(0,0,0,0));

    QPainter painter(&writer);
    if(!painter.isActive()){ fprintf(stderr,"selftest: QPainter could not open %s\n",qPrintable(outputPdf)); return false; }
    const QRectF target(0,0,widthIn*dpi,heightIn*dpi);
    painter.setWindow(target.toRect());
    QtPlotBackend backend;
    backend.render(&painter,target,spec);
    painter.end();

    QFile f(outputPdf);
    if(!f.open(QIODevice::ReadOnly)){ fprintf(stderr,"selftest: no output written\n"); return false; }
    const QByteArray bytes=f.readAll();
    f.close();

    // A rasterised page would be one big image XObject with no font resources.
    // Real vector text means /Font entries and no /Subtype /Image.
    const bool isPdf=bytes.startsWith("%PDF-");
    const bool hasFonts=bytes.contains("/Font");
    const bool hasImage=bytes.contains("/Subtype /Image")||bytes.contains("/Subtype/Image");

    printf("selftest: %s\n",qPrintable(QFileInfo(outputPdf).absoluteFilePath()));
    printf("selftest: %lld bytes, header=%s, fonts=%s, rasterImage=%s\n",
           static_cast<long long>(bytes.size()),
           isPdf?"PDF":"NOT-PDF", hasFonts?"yes":"no", hasImage?"yes":"no");
    printf("selftest: engines supported by Qt 2-D: %s\n",
           qPrintable(QtPlotBackend().supportedEngines().join(QStringLiteral(", "))));

    const bool ok=isPdf&&hasFonts&&!hasImage&&bytes.size()>1000;
    printf("selftest: %s\n",ok?"VECTOR PDF OK":"FAILED");
    return ok;
}

} // namespace graphvis
