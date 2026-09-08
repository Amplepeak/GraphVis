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
