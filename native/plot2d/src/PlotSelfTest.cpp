#include "PlotSelfTest.h"
#include "ColourVision.h"
#include "QtPlotBackend.h"

#include <QCryptographicHash>
#include <QHashSeed>
#include <QDir>
#include <QMultiHash>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>
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

// One engine's inputs: its own shape where it has one, the shared signal
// otherwise. Shared, because a check over the whole catalogue that invents its
// own data is a check asking every engine to read something it was not
// designed for - and then reporting the right answer to that as a fault.
struct ShapedAxes { QString x,y; };
struct SweepInputs {
    PlotSeries a,b,c,d,e;
    QHash<QString,QVector<PlotSeries>> shaped;
    // The axis names that belong with a fixture of its own - see the note
    // beside the table in sweepInputs().
    QHash<QString,ShapedAxes> shapedAxes;
    QVector<PlotSeries> forEngine(const QString& engine) const {
        return shaped.contains(engine)?shaped.value(engine)
                                      :QVector<PlotSeries>{a,b,c,d,e};
    }
};

// The sweep's inputs, built once and shared.
//
// These were local to runEngineSweep, which meant every other check over the
// whole catalogue had to invent its own data - and data invented for a check
// is data no engine was designed for, so the check reports the wrong answer
// as a fault. One table, used by the sweep and by the property checks.
SweepInputs sweepInputs(){
    SweepInputs in;
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

    // THE SERIES GET THEIR COLOURS, the same ones a real figure gets.
    //
    // They had none, so every one of them came out in PlotSeries' default blue
    // and every multi-series figure in the gallery was five identical blue
    // shapes under a five-entry legend. That is not what the program draws -
    // buildPlotSeries assigns seriesPalette(...) to each series - so the
    // gallery, which is the artefact the visual pass is done on, was showing a
    // picture nobody using GraphVis would ever see.
    //
    // It also blinded two of the sweep's own checks: the same-picture clusters
    // and the "renders exactly as a Line Chart" test both compare rendered
    // images, and with every series one colour they were comparing figures with
    // a whole channel removed. Adding the palette can only separate engines
    // that were being compared with less information, never merge them.
    {
        const QVector<QColor> palette=seriesPalette(ColourVision::Standard);
        PlotSeries* series[]={&a,&b,&c,&d,&e};
        for(int i=0;i<5;++i) series[i]->color=palette.at(i%palette.size());
    }

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
    // A column of values, with the row number as its x.
    //
    // It does NOT set a colour, deliberately. The application always assigns
    // one before it hands a spec over, so a fixture that also assigned one
    // would never ask what happens to a caller who does not - which is how a
    // grouped bar chart of three series came out as one flat blue. The backend
    // fills the default in now; this is the fixture that proves it does.
    const auto column=[](const QString& label,const QVector<double>& values){
        PlotSeries s; s.label=label; s.y=values; s.x.reserve(values.size());
        for(int i=0;i<values.size();++i) s.x.append(double(i));
        return s;
    };
    // A series whose x is a MEASUREMENT rather than a row number.
    //
    // `column` above numbers its rows, which is the right shape for the
    // engines that read a list of values. It is the wrong shape for the ones
    // that read a point: a drag polar reads (CD, CL) out of one series' own x
    // and y, an envelope reads (speed, load factor), a wind rose reads
    // (bearing, magnitude). Handed a numbered column those engines get the row
    // index as the abscissa - so the drag polar's parabola fit came back with a
    // negative CD0, failed its own sanity test, and drew the raw points with no
    // fit at all. The engine was right to refuse; it was being asked to fit a
    // signal against its own subscript.
    const auto paired=[](const QString& label,const QVector<double>& xs,
                         const QVector<double>& ys){
        PlotSeries s; s.label=label; s.x=xs; s.y=ys; return s;
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
        const double effectSize=0.35+0.5*std::sin(double(i)*0.7);
        const double se=0.08+0.03*double(i%5);
        effect.append(effectSize);
        stdErr.append(se);
        variance.append(se*se);
        ciLo.append(effectSize-1.96*se);
        ciHi.append(effectSize+1.96*se);
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
            const double angle=2.0*3.14159265358979323846*double(i)/36.0;
            pmMoment.append(qMax(0.0,220.0*std::sin(angle)));
            pmAxial.append(1400.0*std::cos(angle)+400.0);
        }
        // A pushover with an elastic branch, a knee and a plateau.
        for(int i=0;i<=50;++i){
            const double disp=double(i)*0.006;
            pushDisp.append(disp);
            pushShear.append(1800.0*(1.0-std::exp(-disp/0.035)));
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
            double prev1=0.0,prev2=0.0;
            for(int i=0;i<600;++i){
                const double v=0.6*prev1-0.3*prev2+noise();
                arSeries.append(v);
                prev2=prev1; prev1=v;
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
            const double shear=0.1*std::pow(1000.0,double(i)/29.0);
            shearRate.append(shear);
            shearStress.append(5.0+2.0*std::pow(shear,0.6));
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
            const double day=1.0+double(i)*5.0;
            phenologyDay.append(day);
            phenologyIndex.append(0.2+0.6*std::exp(-std::pow((day-200.0)/40.0,2.0)));
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
            const double extent=double(i)*0.03;
            krevelenOxygen.append(0.55-extent); krevelenHydrogen.append(1.60-2.0*extent);
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
            for(int sym=0;sym<800;++sym){
                const int iSym=sym%4,qSym=(sym/4)%4;
                symbolI.append((2.0*iSym-3.0)+0.10*jitter());
                symbolQ.append((2.0*qSym-3.0)+0.10*jitter());
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
    // A HANDFUL OF CATEGORIES, for the engines that want exactly that.
    //
    // A bar, a pie, a stem and a lollipop are all "one mark per category",
    // and the shared fixture hands them 240 rows of a continuous signal. The
    // gallery showed the result: a bar chart of 240 hairline bars, and a pie
    // of 240 hairline slices with no readable slice at any size. Every one of
    // those engines was drawing its data correctly and none of the pictures
    // could be judged, which is what a visual pass needs to do.
    //
    // Eight categories with clearly different values, and a second measure so
    // a grouped bar has two groups to draw.
    QVector<double> categoryValue,categorySecond;
    QVector<double> measuredValue,measuredError;
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
                    else { const double decay=std::exp(-ntu*(1.0-cr));
                           exact=(1.0-decay)/(1.0-cr*decay); }
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
            const double depth=double(i)*0.1;
            beamDepth.append(depth);
            depositedDose.append(std::exp(-std::pow((depth-15.0)/1.5,2.0)));
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
                const double decay=std::exp(-(innovation+imitation)*t);
                adoptionTime.append(t);
                adoptionCumulative.append(market*(1.0-decay)/(1.0+(imitation/innovation)*decay));
            }
        }
        {
            const double counts[]={50.0,25.0,12.0,6.0,4.0,2.0,1.0};
            int k=0;
            for(double count:counts){ faultCategory.append(double(++k)); faultCount.append(count); }
        {
            // Deliberately uneven, and not sorted: a bar chart of eight equal
            // bars shows nothing, and one already in descending order hides
            // whether the engine sorts or the data did.
            const double values[]={7.4,2.1,5.9,1.2,8.8,3.6,6.2,4.5};
            const double second[]={3.1,5.4,2.2,6.8,1.9,7.1,2.8,5.0};
            for(int i=0;i<8;++i){
                categoryValue.append(values[i]);
                categorySecond.append(second[i]);
            }
        }
        {
            // A measurement and its uncertainty.
            //
            // The shared signal made this one unjudgeable: the sweep handed
            // drawErrorBar a second column ranging to 7 as the error on values
            // around 4, over 240 points, so the figure was a solid wall of
            // whiskers taller than the data. Nothing was WRONG with it - that
            // is what +/- 7 looks like - but a picture nobody can read cannot
            // be checked, and an uncertainty the size of the measurement is not
            // what an error bar is drawn for.
            //
            // Fourteen points and an error that grows from 4% to 12% of the
            // value, which is the shape the engine exists to show.
            for(int i=0;i<14;++i){
                const double t=double(i);
                const double v=3.0+2.4*std::log(1.0+t)+0.35*std::sin(t*1.7);
                measuredValue.append(v);
                measuredError.append(v*(0.04+0.006*t));
            }
        }
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
            const double speed=double(i)*10.0;
            motorSpeed.append(speed);
            motorTorque.append(50.0+250.0*std::sin(M_PI*speed/1500.0));
            loadTorque.append(30.0+0.00008*speed*speed);
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
            const double arrivals=double(i)*0.5,lead=3.0+0.1*double(i);
            arrivalRate.append(arrivals); workInProgress.append(arrivals*lead); leadTime.append(lead);
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
                const double denom=re*re+im*im;
                coleReal.append(einf+(es-einf)*re/denom);
                coleImaginary.append((es-einf)*im/denom);
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
            // THE ACTIVATION ENERGY HAS TO VARY, or the figure is a flat line.
            //
            // The demonstration used a single constant Ea of 150 kJ/mol, and
            // the engine dutifully recovered 150 at every conversion: a
            // perfectly horizontal trace across the plot. That is the correct
            // answer for a one-step reaction and it is the one result an
            // isoconversional plot is never run to find - the whole reason to
            // compute Ea at each conversion separately is to see whether it
            // CHANGES, because a change means the mechanism changes partway
            // through and a single Arrhenius fit to the whole run is wrong.
            //
            // Two overlapping steps: about 135 kJ/mol early, rising through a
            // transition near 45% conversion to about 185 late.
            const double rates[]={2.0,5.0,10.0,20.0,40.0};
            const auto energyAt=[](double share){        // J/mol
                return 135000.0+50000.0/(1.0+std::exp(-(share-0.45)/0.07));
            };
            for(double beta:rates)
                for(int k=1;k<=19;++k){
                    const double share=0.05*double(k);
                    const double factor=1.052*energyAt(share)/8.3145;
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

    // Batch 22. A hundred readings of w = (u + v) squared, which IS of the
    // form an alignment nomogram represents - so the middle scale must come out
    // single-valued and the engine's own check must report an inconsistency of
    // zero. (A product, which is not of that form, is refused; that case is
    // exercised in the probe rather than here, since the sweep only asks
    // whether an engine drew.)
    QVector<double> nomoU,nomoV,nomoW;
    for(int ui=1;ui<=10;++ui)
        for(int vi=1;vi<=10;++vi){
            nomoU.append(double(ui)); nomoV.append(double(vi));
            nomoW.append(double(ui+vi)*double(ui+vi));
        }

    // Batch 23. A three-body decay with three MASSLESS daughters, where the
    // kinematic boundary collapses to the triangle s12 + s23 <= M^2 - an answer
    // that can be written down without any of the machinery the engine uses.
    // Points on a lattice of fourteenths, so thirteen of them land exactly on
    // the boundary and the engine has to count those as inside.
    QVector<double> dalitzA,dalitzB,dalitzM,dalitz1,dalitz2,dalitz3;
    for(int i=1;i<14;++i)
        for(int j=1;j<14;++j){
            dalitzA.append(double(i)/14.0);
            dalitzB.append(double(j)/14.0);
            // Four constant columns, because a parent mass and three daughter
            // masses are parameters and a mapped column is the only way this
            // program has of carrying one.
            dalitzM.append(1.0);
            dalitz1.append(0.0); dalitz2.append(0.0); dalitz3.append(0.0);
        }

    // Batch 24. One composition well inside each of the twelve USDA classes,
    // taken from the standard texts - so the engine has to name all twelve, one
    // each, and leave none unclassified. The same twelve go through the UK
    // engine, where they fall differently because the systems genuinely differ.
    QVector<double> soilSand,soilSilt,soilClay;
    {
        const double table[12][3]={{92,5,3},{82,10,8},{65,25,10},{40,40,20},
                                   {20,65,15},{10,85,5},{60,15,25},{33,34,33},
                                   {10,57,33},{52,8,40},{10,45,45},{20,20,60}};
        for(const auto& row:table){
            soilSand.append(row[0]); soilSilt.append(row[1]); soilClay.append(row[2]);
        }
    }

    // Batch 25. One rock in the middle of every QAPF field. The fields are
    // rectangles in (apex per cent, plagioclase ratio), so a field's centre is
    // known without any of the engine's machinery - and twenty-seven rocks must
    // come back in twenty-seven different fields with none unnamed. Two further
    // rows carry both quartz and feldspathoid, which no rock does, and must be
    // refused rather than plotted.
    QVector<double> qapfQ,qapfA,qapfP,qapfF;
    {
        const double table[27][5]={ // foid, lo, hi, p0, p1
            {0,90,100,0,100},{0,60,90,0,100},{0,20,60,0,10},{0,20,60,10,35},
            {0,20,60,35,65},{0,20,60,65,90},{0,20,60,90,100},{0,5,20,0,10},
            {0,5,20,10,35},{0,5,20,35,65},{0,5,20,65,90},{0,5,20,90,100},
            {0,0,5,0,10},{0,0,5,10,35},{0,0,5,35,65},{0,0,5,65,90},{0,0,5,90,100},
            {1,0,10,0,10},{1,0,10,10,35},{1,0,10,35,65},{1,0,10,65,90},
            {1,0,10,90,100},{1,10,60,0,10},{1,10,60,10,50},{1,10,60,50,90},
            {1,10,60,90,100},{1,60,100,0,100}};
        for(const auto& row:table){
            const bool foid=row[0]>0.5;
            const double apex=0.5*(row[1]+row[2]);
            const double ratio=0.5*(row[3]+row[4]);
            const double feldspar=100.0-apex;
            const double plag=feldspar*ratio/100.0;
            qapfQ.append(foid?0.0:apex);
            qapfF.append(foid?apex:0.0);
            qapfP.append(plag);
            qapfA.append(feldspar-plag);
        }
        qapfQ.append(20); qapfF.append(20); qapfA.append(30); qapfP.append(30);
        qapfQ.append(5);  qapfF.append(5);  qapfA.append(45); qapfP.append(45);
    }

    // Batch 26. The same construction against the volcanic table, which has
    // twenty-six fields rather than twenty-seven: rhyolite is one field where
    // the plutonic diagram has syenogranite and monzogranite. Several names
    // repeat - dacite twice, basalt / andesite four times over - so the count
    // to check is fields occupied, not names returned.
    QVector<double> qapfvQ,qapfvA,qapfvP,qapfvF;
    {
        const double table[26][5]={ // foid, lo, hi, p0, p1
            {0,90,100,0,100},{0,60,90,0,100},{0,20,60,0,10},{0,20,60,10,65},
            {0,20,60,65,90},{0,20,60,90,100},{0,5,20,0,10},{0,5,20,10,35},
            {0,5,20,35,65},{0,5,20,65,90},{0,5,20,90,100},
            {0,0,5,0,10},{0,0,5,10,35},{0,0,5,35,65},{0,0,5,65,90},{0,0,5,90,100},
            {1,0,10,0,10},{1,0,10,10,35},{1,0,10,35,65},{1,0,10,65,90},
            {1,0,10,90,100},{1,10,60,0,10},{1,10,60,10,50},{1,10,60,50,90},
            {1,10,60,90,100},{1,60,100,0,100}};
        for(const auto& row:table){
            const bool foid=row[0]>0.5;
            const double apex=0.5*(row[1]+row[2]);
            const double ratio=0.5*(row[3]+row[4]);
            const double feldspar=100.0-apex;
            const double plag=feldspar*ratio/100.0;
            qapfvQ.append(foid?0.0:apex);
            qapfvF.append(foid?apex:0.0);
            qapfvP.append(plag);
            qapfvA.append(feldspar-plag);
        }
    }

    // Batch 21 inputs.
    QVector<double> karyoChrom,karyoFrom,karyoTo,karyoStain;
    {
        const double length[6]={100,80,80,60,40,20};
        const double centromere[6]={0.30,0.50,0.10,0.50,0.50,0.50};
        for(int chrom=0;chrom<6;++chrom){
            const double step=length[chrom]/10.0;
            const double at=centromere[chrom]*length[chrom];
            for(int i=0;i<10;++i){
                const double lo=step*double(i),hi=step*double(i+1);
                karyoChrom.append(double(chrom+1));
                karyoFrom.append(lo); karyoTo.append(hi);
                // The band holding the centromere is marked with a negative
                // stain; the rest alternate between clear and full black so the
                // grey ramp is exercised at both ends.
                karyoStain.append((at>=lo&&at<hi)?-1.0
                                  :((i%2==0)?100.0:8.0));
            }
        }
    }
    QVector<double> circosFromSeg,circosFromPos,circosToSeg,circosToPos,circosWeight;
    {
        const double table[8][5]={{1,0,3,0,3},{1,400,3,200,3},{1,200,2,100,1},
                                  {2,50,4,90,1},{2,150,4,10,1},{3,100,1,100,1},
                                  {4,50,1,300,1},{1,100,1,350,2}};
        for(const auto& row:table){
            circosFromSeg.append(row[0]); circosFromPos.append(row[1]);
            circosToSeg.append(row[2]);   circosToPos.append(row[3]);
            circosWeight.append(row[4]);
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

    // A 3-D VECTOR FIELD, for the five engines that read six columns.
    //
    // Without this they were handed the shared five columns, `haveVector`
    // (columns >= 6) was false, draw3DField skipped every glyph, and all five
    // drew a bounding cube and nothing inside it. They were identical because
    // they were all EMPTY - the sweep found them in one same-picture group and
    // the "every engine drew data" check passed them, because the cube is ink
    // and the chrome probe clears the series and so never draws a cube to
    // compare against.
    //
    // Helical flow: solid-body rotation about the z axis with a steady rise.
    // Chosen because every one of the five reads something different out of
    // it - a cone has a direction to point, a stream tube has a path to
    // follow, and a tensor glyph has a genuine orientation rather than the
    // same one everywhere. A radial or uniform field would have left several
    // of them looking alike for a second, less obvious reason.
    QVector<double> fieldX,fieldY,fieldZ,fieldU,fieldV,fieldW;
    for(int k=0;k<9;++k){
        for(int j=0;j<9;++j){
            for(int i=0;i<9;++i){
                const double x=-1.0+0.25*double(i);
                const double y=-1.0+0.25*double(j);
                const double z=-1.0+0.25*double(k);
                fieldX.append(x); fieldY.append(y); fieldZ.append(z);
                fieldU.append(-y);          // rotation
                fieldV.append(x);
                fieldW.append(0.35);        // and a steady climb
            }
        }
    }

    // A FIELD WHOSE DIVERGENCE AND VORTICITY ARE WORTH MAPPING.
    //
    // The Duffing system above is the right field for a phase portrait - three
    // fixed points, one of each kind the painter classifies - and the wrong one
    // for these two maps. Its divergence is du/dx + dv/dy = 0 + (-0.2), the
    // same number everywhere, so the Divergence Map came out one flat colour
    // across the whole plot area: a correct picture of a constant, and a
    // catalogue entry that tells a reader nothing about what the engine does.
    //
    // A radial source at the origin, whose divergence (2 - 2r^2)e^-r^2 changes
    // sign at r = 1, and an off-centre vortex, whose vorticity is concentrated
    // where it sits. One field, both maps with structure in them, and neither
    // engine's arithmetic changed to get it.
    QVector<double> curlX,curlY,curlU,curlV;
    for(int j=0;j<29;++j){
        for(int i=0;i<29;++i){
            const double x=-1.8+3.6*double(i)/28.0;
            const double y=-1.5+3.0*double(j)/28.0;
            const double r2=x*x+y*y;
            const double spread=std::exp(-r2);
            const double vx=x-0.95, vy=y+0.35;
            const double swirl=std::exp(-2.2*(vx*vx+vy*vy));
            curlX.append(x); curlY.append(y);
            curlU.append(x*spread-vy*swirl*1.6);
            curlV.append(y*spread+vx*swirl*1.6);
        }
    }

    // A SCALAR VOLUME, for the family that reconstructs a surface out of one.
    //
    // Handed the shared five columns these read x = signal, y = paired,
    // z = p_value and the scalar = a binary label, which is not a volume: a
    // field that is 0 or 1 has one level set and it is the boundary between
    // them, so every level a surface could be drawn at gives the same shape or
    // nothing. The engines were not wrong; there was nothing there to find.
    //
    // Two blobs of different size and sign, which is the smallest field that
    // actually distinguishes the five readings: an isosurface has two closed
    // shells to draw, the caps have something to cut, a slice through the
    // middle catches both, and the contours have nested rings rather than one.
    QVector<double> volX,volY,volZ,volV;
    for(int k=0;k<14;++k){
        for(int j=0;j<14;++j){
            for(int i=0;i<14;++i){
                const double x=-1.0+2.0*double(i)/13.0;
                const double y=-1.0+2.0*double(j)/13.0;
                const double z=-1.0+2.0*double(k)/13.0;
                // THE LARGE BLOB IS CUT BY THE WALL OF THE BOX, on purpose.
                //
                // It used to sit at x = -0.35, comfortably inside the domain,
                // so nothing in the volume ever reached a wall - and Isocaps,
                // whose entire content is the caps where the enclosed region
                // is cut by the walls, correctly found none and drew the bare
                // isosurface. The two figures came out 0.07 grey levels apart
                // out of 255, the closest pair left in the gallery, and the
                // engine was right both times: there was nothing to cap.
                //
                // It straddles the CEILING, which is the one wall this
                // viewpoint can see through. Two earlier tries cut it at a
                // side wall and at the floor: both produced a real cap and
                // neither could be seen, the side wall because it is nearly
                // edge-on in this projection and the floor because the dome
                // sitting on it hides its own opening. Cut at the top, the
                // camera looks straight down into the open shell - so the
                // isosurface shows a hole and the isocaps figure shows it
                // closed, which is the whole difference between the entries.
                //
                // The second blob stays interior, which gives the figure the
                // contrast the entry is for: one shell sliced open, one whole.
                const double r1=std::hypot(std::hypot(x+0.35,y+0.2),z-0.88);
                const double r2=std::hypot(std::hypot(x-0.4,y-0.3),z-0.15);
                volX.append(x); volY.append(y); volZ.append(z);
                volV.append(std::exp(-6.0*r1*r1)+0.7*std::exp(-14.0*r2*r2));
            }
        }
    }

    // A 2-D VECTOR FIELD WITH FIXED POINTS, for the eight engines that read one.
    //
    // The shared columns gave them u = p_value and v = label. p_value is
    // exp(-0.7t), strictly positive; label is 0 or 1. NEITHER COMPONENT EVER
    // CHANGES SIGN, so the field has no zeros anywhere - and a phase portrait
    // whose whole content is the fixed points correctly found none and drew
    // streamlines, identical to Stream Field. The sweep reported the pair and
    // the code was right both times.
    //
    // The unforced Duffing system:  u = y,  v = x - x^3 - 0.2 y.
    // Chosen because its three fixed points are one of each kind the painter
    // classifies - a saddle at the origin and a stable spiral at each of
    // (+-1, 0) - so the classification is exercised rather than merely reached.
    // A rotation would have given one centre and told us nothing about the
    // other branches.
    QVector<double> flowX,flowY,flowU,flowV;
    for(int j=0;j<26;++j){
        for(int i=0;i<26;++i){
            const double x=-1.8+3.6*double(i)/25.0;
            const double y=-1.2+2.4*double(j)/25.0;
            flowX.append(x); flowY.append(y);
            flowU.append(y);
            flowV.append(x-x*x*x-0.2*y);
        }
    }

    // COORDINATES, FOR THE ENGINES THAT READ COORDINATES.
    //
    // The eight geographic engines had no fixture, so the sweep handed them the
    // shared columns: `signal` as longitude and `paired` as latitude. Both are
    // a decaying exponential plus a small sine, so every geo figure in the
    // catalogue was the same three-degree squiggle near the Gulf of Guinea, and
    // Geo Line, Ground Track and Great Circle Route came out as literally the
    // same picture - 0.000 and 0.002 grey levels apart out of 255, the closest
    // pair in the whole 434-figure gallery.
    //
    // That had been adjudicated once, as "the difference is the cut at the
    // antimeridian and these longitudes span three degrees, so nothing crosses
    // it". True, and the wrong conclusion: a catalogue figure exists to show
    // what the engine does, so demonstrating an engine on data that cannot
    // reach any of its behaviour is the same failure as the engine not having
    // the behaviour. The engines were right all along; the demonstration was
    // asking them to project a signal column.
    //
    // So: real places, chosen so each entry's own content is what shows.
    QVector<double> routeLon{-0.13,139.69};        // London -> Tokyo
    QVector<double> routeLat{51.51,35.69};

    // A flight across Europe. Regional, several waypoints, no antimeridian and
    // no polar bow - which is what a Geo Line is FOR, and the contrast that
    // makes the great circle beside it worth having.
    QVector<double> pathLon{-9.14,-3.70,2.35,8.54,12.50,16.37,21.01,26.10};
    QVector<double> pathLat{38.72,40.42,48.86,47.38,41.90,48.21,52.23,44.43};

    // A LOW-EARTH GROUND TRACK, which is the one shape that needs the cut.
    // Inclination 51.6 degrees, a 92.7-minute period, and the Earth turning
    // under it at 15 degrees an hour - so each orbit lands about 23 degrees of
    // longitude west of the last, and two and a half orbits cross the
    // antimeridian twice. Without a wrap there is nothing for the cut to do.
    QVector<double> trackLon,trackLat;
    for(int i=0;i<=300;++i){
        const double frac=double(i)/300.0*2.5;          // orbits completed
        const double arg=frac*2.0*M_PI;                 // argument of latitude
        const double inc=51.6*M_PI/180.0;
        trackLat.append(std::asin(std::sin(inc)*std::sin(arg))*180.0/M_PI);
        double lon=std::atan2(std::cos(inc)*std::sin(arg),std::cos(arg))*180.0/M_PI
                   -frac*(92.7/60.0)*15.0+30.0;
        while(lon>180.0) lon-=360.0;
        while(lon<-180.0) lon+=360.0;
        trackLon.append(lon);
    }

    // Somewhere to scatter: reported sightings around the North Sea, dense
    // enough that Geo Density has something to bin and Geo Bubble has a spread
    // of sizes.
    QVector<double> siteLon,siteLat,siteWeight;
    for(int i=0;i<160;++i){
        const double u=double(i)*0.61803398875;
        const double v=std::fmod(u,1.0);
        const double w=std::fmod(u*1.324717957,1.0);
        siteLon.append(-2.0+9.0*v+1.2*std::sin(double(i)*0.7));
        siteLat.append(51.0+7.0*w+0.8*std::cos(double(i)*0.9));
        siteWeight.append(3.0+9.0*std::fmod(u*2.2,1.0));
    }

    // A V-n DIAGRAM, which is a closed loop and not a time series.
    //
    // Positive stall parabola n = (V/Vs)^2 up to the limit load factor, flat
    // along it to the dive speed, straight down to zero there, and the
    // negative side home again. Figures are a light aircraft in the utility
    // category: Vs 50, VA 97, VD 160 kn, n from +4.4 to -1.76.
    QVector<double> vnV,vnN;
    {
        const double vs=50.0,vd=160.0,nPos=4.4,nNeg=-1.76;
        const double va=vs*std::sqrt(nPos), vsNeg=vs*std::sqrt(-nNeg);
        for(int i=0;i<=40;++i){                       // positive stall curve
            const double v=vs+(va-vs)*double(i)/40.0;
            vnV.append(v); vnN.append((v/vs)*(v/vs));
        }
        vnV.append(vd); vnN.append(nPos);             // along the limit load
        vnV.append(vd); vnN.append(0.0);              // dive speed, unloaded
        vnV.append(vd); vnN.append(nNeg);
        for(int i=40;i>=0;--i){                       // negative stall curve
            const double v=vsNeg+(vd-vsNeg)*double(i)/40.0;
            vnV.append(v); vnN.append(qMax(nNeg,-(v/vsNeg)*(v/vsNeg)));
        }
        vnV.append(vs); vnN.append(1.0);              // back to 1 g at the stall
    }

    // AN ALTITUDE-MACH ENVELOPE: the low-speed limit climbing with altitude as
    // the air thins, the ceiling, and the dynamic-pressure and buffet limit
    // coming back down the fast side.
    QVector<double> amM,amAlt;
    {
        for(int i=0;i<=30;++i){                       // stall limit, going up
            const double alt=i*1200.0;
            amM.append(0.22+alt/45000.0*0.42); amAlt.append(alt);
        }
        for(int i=0;i<=12;++i){                       // along the ceiling
            amM.append(0.64+double(i)/12.0*0.18); amAlt.append(36000.0);
        }
        for(int i=30;i>=0;--i){                       // Mmo, then q limit
            const double alt=i*1200.0;
            amM.append(qMin(0.82,0.46+alt/36000.0*0.36)); amAlt.append(alt);
        }
    }

    // A DRAG POLAR that is a drag polar: CD = 0.021 + 0.047 CL^2 with a little
    // scatter, so the fit has something to recover and the departure from the
    // parabola near the stall is visible rather than invented.
    QVector<double> polarCD,polarCL;
    for(int i=0;i<=24;++i){
        const double cl=-0.25+1.65*double(i)/24.0;
        const double wobble=0.0006*std::sin(double(i)*1.9);
        double cd=0.021+0.047*cl*cl+wobble;
        if(cl>1.15) cd+=0.055*(cl-1.15)*(cl-1.15);    // separation near stall
        polarCD.append(cd); polarCL.append(cl);
    }

    // WIND, with a direction and a speed - and the two disagreeing, which is
    // the reason both a wind rose and a polar histogram exist. The wind blows
    // most OFTEN from the south-west and hardest from the north-west, so the
    // counted figure and the magnitude-weighted one point different ways. The
    // shared columns gave both engines a monotone `signal` as the bearing, so
    // every observation landed in one sector and both drew a single wedge.
    QVector<double> windDir,windSpeed;
    for(int i=0;i<600;++i){
        const double u=std::fmod(double(i)*0.61803398875,1.0);
        const double v=std::fmod(double(i)*0.7548776662,1.0);
        // Two populations: the prevailing south-westerly, and a smaller but
        // much stronger north-westerly.
        const bool gale=(v>0.78);
        const double centre=gale?310.0:225.0;
        const double spread=gale?26.0:55.0;
        double dir=centre+spread*(u-0.5)*2.0+14.0*std::sin(double(i)*0.37);
        while(dir<0.0) dir+=360.0;
        while(dir>=360.0) dir-=360.0;
        windDir.append(dir);
        windSpeed.append(gale?(17.0+9.0*u):(4.0+7.0*u));
    }

    // A STEP IS ONLY A STEP AT A SAMPLE RATE YOU CAN SEE.
    //
    // Stairs and Step Mid were drawn on the shared 240-point base over twelve
    // units, so each tread was half a pixel wide and both figures were smooth
    // curves. They differ by half a sample - Step Mid changes level midway
    // between samples rather than at one, which is the honest reading when a
    // value is a measurement at a point rather than a level that held - and at
    // 240 points that difference is a quarter of a pixel. The perceptual pass
    // put them 0.21 grey levels apart and it was right to.
    //
    // Twelve readings is what a step chart is for: a tariff, a dose, a gear.
    QVector<double> stepT,stepLevel,stepSecond;
    for(int i=0;i<12;++i){
        stepT.append(double(i));
        stepLevel.append(3.0+2.4*std::sin(double(i)*0.72)+0.6*double(i%3));
        stepSecond.append(5.4+1.8*std::cos(double(i)*0.55)-0.35*double(i));
    }

    // A SWARM NEEDS TIES TO SPREAD.
    //
    // 3D Swarm fans rows that share a height apart so that a pile of equal
    // readings becomes a shape instead of one dot. The shared columns are a
    // smooth curve, where almost no two rows share a height - so almost
    // nothing was nudged and the figure was a 3D Scatter, 0.15 grey levels
    // away from the real one.
    //
    // Five measurement levels, forty readings each: the shape a swarm exists
    // to draw.
    QVector<double> swarmX,swarmY,swarmZ;
    for(int level=0;level<5;++level){
        for(int k=0;k<40;++k){
            const double u=std::fmod(double(level*40+k)*0.61803398875,1.0);
            swarmX.append(double(level));
            swarmY.append(1.0+3.0*u);
            swarmZ.append(2.0+double(level)*1.5);
        }
    }

    // AND THE AXIS NAMES THAT GO WITH A FIXTURE OF ITS OWN.
    //
    // Suppressing the shared "time_h"/"value" for an engine with its own data
    // is right for the engines that name their own axes - a drag polar knows
    // its abscissa is a drag coefficient - and left the plain ones with no
    // axis names at all, which is a figure that does not say what it is a
    // figure of. An engine is only entitled to overwrite a caller's label when
    // it has computed a new quantity; the rest take the name of the column
    // they were handed, and this is where the fixture says what that is.
    //
    // Absent from this table means the engine names its own.
    // BEARINGS, FOR THE FIGURES DRAWN IN A CIRCLE.
    //
    // Five polar entries - Polar Line, Polar Scatter, Polar Bubble, Compass
    // and Stereonet - were reading the shared `signal` column as an angle in
    // DEGREES. It runs from about 2 to 5, so every point in every one of those
    // figures lay in a three-degree sliver just above due east, and the
    // catalogue showed five circles with a smear against one edge. Polar Line
    // and Polar Scatter came out 0.31 grey levels apart; Compass drew a fan of
    // arrows all pointing the same way.
    //
    // An antenna pattern for the line, because a cardioid is the shape a
    // person choosing "Polar Line" is picturing: the whole circle used, a
    // maximum, a null, and a front-to-back ratio that can be read off.
    QVector<double> lobeAngle,lobeCardioid,lobeDipole;
    for(int i=0;i<=180;++i){
        const double deg=double(i)*2.0;
        const double rad=deg*3.14159265358979323846/180.0;
        lobeAngle.append(deg);
        lobeCardioid.append(0.05+0.95*std::pow(0.5*(1.0+std::cos(rad)),0.7));
        // A dipole beside it, so the figure has something to compare.
        lobeDipole.append(0.06+0.62*std::abs(std::cos(rad)));
    }

    // Observations round the compass: bearing, range and a magnitude.
    QVector<double> bearing,range,strength;
    for(int i=0;i<120;++i){
        const double u=std::fmod(double(i)*0.61803398875,1.0);
        const double v=std::fmod(double(i)*0.7548776662,1.0);
        bearing.append(std::fmod(40.0+u*360.0+30.0*std::sin(double(i)*0.41),360.0));
        range.append(0.8+4.2*v);
        strength.append(1.0+8.0*std::fmod(u*3.1,1.0));
    }

    // A dozen current vectors, which is what a compass rose is read one at a
    // time: any more and the arrows are a disc.
    QVector<double> currentDir,currentSpeed;
    for(int i=0;i<12;++i){
        currentDir.append(std::fmod(18.0+double(i)*33.0,360.0));
        currentSpeed.append(0.4+1.5*std::abs(std::sin(double(i)*0.9)));
    }

    // Bedding measurements for the stereonet: dip direction all round the
    // compass and dips from shallow to steep, so the poles land across the net
    // instead of in one cluster.
    QVector<double> dipDir,dipAngle;
    for(int i=0;i<90;++i){
        const double u=std::fmod(double(i)*0.61803398875,1.0);
        dipDir.append(std::fmod(double(i)*17.0+22.0*std::sin(double(i)*0.6),360.0));
        dipAngle.append(12.0+68.0*u);
    }

    // ---------------------------------------------------------- aviation
    //
    // Five more engines that were computing real answers from a decaying sine.
    // The fault they share is not that the figures looked odd - it is that the
    // numbers in the legends were WRONG and looked like answers: "max
    // crosswind 0.0" on a wind field three thousandths of a knot across, a
    // weight and balance envelope reporting 240 points outside limits, a top
    // of climb at "5". A reader who trusts a catalogue figure is reading a
    // fabricated measurement.

    // A payload-range diagram: the three legs every one has. Full payload out
    // to the point the tanks fill, then payload traded for fuel at maximum
    // take-off weight, then the ferry leg with nothing in the hold.
    QVector<double> prRange{0.0,2150.0,4300.0,6900.0};
    QVector<double> prPayload{19500.0,19500.0,13100.0,0.0};

    // A lift curve with a linear region and a stall, which is the whole of
    // what the engine measures: slope below the break, CLmax at it.
    QVector<double> liftAlpha,liftCl,liftClFlap;
    for(int i=0;i<=26;++i){
        const double a=-4.0+double(i);
        // Post-stall the curve DROPS AND LEVELS, it does not dive. A bare
        // quadratic falloff took CL to -6 by 22 degrees, which is not a wing:
        // a stalled aerofoil sheds most of its lift and then sits on a rough
        // plateau. The floor is what makes the figure a lift curve rather than
        // a parabola with a marker on it.
        const auto post=[](double a,double slope,double zero,double brk,double fall){
            const double linear=slope*(a+zero);
            if(a<brk) return linear;
            const double d=a-brk;
            return qMax(slope*(brk+zero)*0.45,linear-fall*d*d);
        };
        liftAlpha.append(a);
        liftCl.append(post(a,0.098,1.6,13.0,0.030));
        liftClFlap.append(post(a,0.101,6.2,10.0,0.034));
    }

    // A flight profile: climb, cruise at level, step climb, descent. In
    // minutes and feet, so "top of climb 37000" reads as an altitude.
    QVector<double> profMinute,profAltitude;
    {
        const auto leg=[&](double fromT,double toT,double fromA,double toA,int steps){
            for(int i=0;i<=steps;++i){
                const double f=double(i)/double(steps);
                profMinute.append(fromT+(toT-fromT)*f);
                profAltitude.append(fromA+(toA-fromA)*f);
            }
        };
        leg(0,22,0,33000,22); leg(22,96,33000,33000,12);
        leg(96,104,33000,37000,8); leg(104,168,37000,37000,10);
        leg(168,192,37000,0,24);
    }

    // Wind observations against a runway heading: runway 09 (090 degrees), the
    // wind backing through the sector, gusting. The engine resolves each into
    // head and cross components and reports the worst crosswind - a number a
    // pilot uses, and one that has to come from real bearings and knots.
    QVector<double> rwHeading,rwFrom,rwSpeed;
    for(int i=0;i<48;++i){
        const double u=std::fmod(double(i)*0.61803398875,1.0);
        rwHeading.append(90.0);
        rwFrom.append(40.0+110.0*u+9.0*std::sin(double(i)*0.8));
        rwSpeed.append(6.0+16.0*std::fmod(u*2.7,1.0));
    }

    // A weight and balance envelope and a set of loadings, most inside it.
    // Centre of gravity in inches aft of datum against gross weight in pounds.
    QVector<double> wbArm{82.0,82.0,88.5,93.0,93.0,84.8};
    QVector<double> wbWeight{1600.0,2100.0,2550.0,2550.0,1600.0,1600.0};
    QVector<double> loadArm,loadWeight;
    for(int i=0;i<26;++i){
        const double u=std::fmod(double(i)*0.61803398875,1.0);
        const double v=std::fmod(double(i)*0.7548776662,1.0);
        loadArm.append(80.5+14.0*u);
        loadWeight.append(1500.0+1150.0*v);
    }

    // ------------------------------------------------------- reliability
    //
    // Four of these read a column of LIFETIMES - failure times, one per row -
    // and were being handed a decaying sine. A Weibull plot of a sine returns
    // a shape parameter (6.63, "wear-out") and a characteristic life, both
    // fabricated; a reliability growth plot of one returns a Crow-AMSAA beta
    // of 5.2, which would mean reliability getting rapidly worse. These are
    // the numbers an engineer would quote.

    // Failure times from a genuine Weibull, beta 2.1 and eta 1450 hours, drawn
    // by inverting the CDF on a low-discrepancy sequence so the figure is the
    // same on every run and the fit has something real to recover.
    QVector<double> weibullLife;
    for(int i=0;i<60;++i){
        const double u=std::fmod(0.013+double(i)*0.61803398875,1.0);
        weibullLife.append(1450.0*std::pow(-std::log(1.0-u),1.0/2.1));
    }

    // A test programme that IS growing: a non-homogeneous Poisson process with
    // Crow-AMSAA beta 0.62, so failures arrive more slowly as the programme
    // runs and the fitted beta comes back under one, which is what "growth"
    // means. Times are cumulative test hours.
    QVector<double> growthTime;
    for(int i=1;i<=45;++i) growthTime.append(std::pow(double(i)/0.055,1.0/0.62));

    // A fleet whose failures come steadily, for the MTBF trend: cumulative
    // operating hours at each failure, with the interval lengthening.
    QVector<double> mtbfTime;
    {
        double at=0.0;
        for(int i=0;i<70;++i){
            const double u=std::fmod(double(i)*0.7548776662,1.0);
            at+=110.0+3.4*double(i)+70.0*u;
            mtbfTime.append(at);
        }
    }

    // S-N: a Basquin law S = 820 N^-0.11 with scatter, plus an endurance limit
    // the long-life points sit on - which is the feature an S-N curve is read
    // for and a straight line through everything hides.
    QVector<double> snCycles,snStress;
    for(int i=0;i<26;++i){
        // The sloped region has to be most of the range or the fit is fitting
        // the FLOOR. A knee at 215 MPa put nineteen of the twenty-six points
        // on the endurance limit, and the Basquin fit through all of them came
        // back as S = 336 N^-0.0256 against the 820 N^-0.11 the data was drawn
        // from - a fitted exponent four times too shallow, reported to four
        // decimal places. Three or four points on the knee is what an S-N
        // curve looks like and what leaves the slope recoverable.
        const double logN=3.6+double(i)*0.15;
        const double N=std::pow(10.0,logN);
        const double basquin=820.0*std::pow(N,-0.11);
        const double scatter=1.0+0.045*std::sin(double(i)*2.3);
        snCycles.append(N); snStress.append(qMax(150.0,basquin)*scatter);
    }

    // Lifetimes with all three phases, for the bathtub: early failures, a long
    // flat middle, and wear-out. Three populations, because that is what a
    // bathtub hazard curve IS - one smooth distribution has one hump.
    QVector<double> bathtubLife;
    for(int i=0;i<240;++i){
        const double u=std::fmod(0.007+double(i)*0.61803398875,1.0);
        const double v=std::fmod(double(i)*0.7548776662,1.0);
        if(v<0.18)      bathtubLife.append(40.0*std::pow(-std::log(1.0-u),1.0/0.65));
        else if(v<0.72) bathtubLife.append(300.0+5200.0*u);
        else            bathtubLife.append(5200.0*std::pow(-std::log(1.0-u),1.0/5.5));
    }

    // ----------------------------------- chemistry, materials and hydrology
    //
    // Each of these fits a named physical law and puts the recovered constants
    // in the legend. On the shared columns those constants were fabricated and
    // stated to four significant figures: "Ea 0.0 kJ/mol", "EC50 1.386, Hill
    // 1.26" on data with no dose in it, "Vmax 4.598, Km 0.104" on a sine. A
    // number in a legend is the most quotable thing on a figure.

    // A tensile test: Hooke's law to a 250 MPa yield, work hardening to a
    // 410 MPa ultimate, then necking. E = 70 GPa, strain as a fraction.
    // SAMPLED FINELY WHERE THE MODULUS IS. A uniform strain step of 0.0022
    // put two points below a yield strain of 0.0036, and the modulus fitted
    // from two points came back as 11 GPa against the 70 the curve was drawn
    // from - off by a factor of six, printed in the legend as 1.109e+04.
    // A tensile test samples the elastic region densely for exactly this
    // reason; the fixture has to as well or it is not demonstrating the
    // measurement, it is demonstrating a sampling artefact.
    QVector<double> ssStrain,ssStress;
    {
        const double yieldStrain=250.0/70000.0;
        const auto stressAt=[&](double e){
            if(e<yieldStrain) return 70000.0*e;                     // elastic
            if(e<0.115) return 250.0+160.0*std::pow((e-yieldStrain)/(0.115-yieldStrain),0.55);
            return 410.0-980.0*(e-0.115)*(e-0.115);                 // necking
        };
        for(int i=0;i<=24;++i){                                     // elastic
            const double e=yieldStrain*double(i)/24.0;
            ssStrain.append(e); ssStress.append(stressAt(e));
        }
        for(int i=1;i<=70;++i){                                     // plastic
            const double e=yieldStrain+(0.20-yieldStrain)*double(i)/70.0;
            ssStrain.append(e); ssStress.append(stressAt(e));
        }
    }

    // A rate constant measured at six temperatures, obeying Arrhenius with
    // Ea = 72.5 kJ/mol and A = 4.1e9 - so the fit has a real activation energy
    // to recover and a reader can check it against the number in the legend.
    QVector<double> arrTemp,arrRate;
    for(int i=0;i<8;++i){
        const double T=288.0+double(i)*9.0;
        arrRate.append(4.1e9*std::exp(-72500.0/(8.314462618*T))
                       *(1.0+0.02*std::sin(double(i)*2.1)));
        arrTemp.append(T);
    }

    // A WEAK acid titrated with a strong base: 25 mL of 0.1 M acetic acid,
    // pKa 4.76, by 0.1 M NaOH. Weak, because the engine reports the pH at the
    // half-equivalence point AS the pKa - which is the whole reason that
    // marker is on the figure, and which means nothing for a strong acid. On
    // 0.1 M HCl it dutifully reported "pKa 1.47", a number with no referent.
    QVector<double> titrantVol,titrantPh;
    for(int i=0;i<=120;++i){
        const double v=double(i)*0.4;                       // mL added
        const double acid=0.1*25.0, base=0.1*v;             // mmol
        const double total=25.0+v;
        constexpr double pKa=4.76, ka=1.7378e-5;            // 10^-4.76
        double ph;
        if(v<0.05){
            // Before any base: the weak acid alone, [H+] = sqrt(Ka c).
            ph=-std::log10(std::sqrt(ka*acid/total));
        }else if(base<acid-0.02){
            ph=pKa+std::log10(base/(acid-base));             // buffer region
        }else if(base<=acid+0.02){
            // At equivalence the conjugate base hydrolyses.
            const double cb=acid/total;
            ph=14.0+std::log10(std::sqrt((1e-14/ka)*cb));
        }else{
            ph=14.0+std::log10((base-acid)/total);           // excess base
        }
        titrantVol.append(v); titrantPh.append(qBound(1.0,ph,13.2));
    }

    // A calibration series: absorbance against concentration, Beer-Lambert
    // with a small intercept and a couple of tenths of a per cent of noise.
    QVector<double> calConc,calSignal;
    for(int i=0;i<=7;++i){
        const double c=double(i)*0.8;
        calConc.append(c);
        calSignal.append(0.012+0.1873*c+0.0025*std::sin(double(i)*2.7));
    }

    // Enzyme kinetics that actually saturate: Vmax 8.4, Km 0.42 mM.
    QVector<double> mmSubstrate,mmRate;
    for(int i=0;i<14;++i){
        const double s=0.05*std::pow(1.42,double(i));
        mmSubstrate.append(s);
        mmRate.append(8.4*s/(0.42+s)*(1.0+0.012*std::sin(double(i)*3.1)));
    }

    // A four-parameter logistic with EC50 = 35 nM and a Hill slope of 1.4,
    // across four decades of dose so both plateaus are on the figure.
    QVector<double> doseConc,doseEffect;
    for(int i=0;i<18;++i){
        const double d=0.5*std::pow(10.0,double(i)/4.5);
        doseConc.append(d);
        doseEffect.append(4.0+92.0/(1.0+std::pow(35.0/d,1.4))
                          +0.6*std::sin(double(i)*2.2));
    }

    // A gauging station's rating. The engine fits Q = a h^b, with no datum
    // offset, so the fixture is drawn from that same law - Q = 12.4 h^2.1.
    // Drawn with an offset of 0.15 m the fit could not recover it, came back
    // as Q = 7.498 h^2.690, and visibly missed the top of the range: the
    // figure was then a demonstration of a model mismatch rather than of the
    // engine. If the offset is ever wanted it belongs in the engine, as a
    // fitted datum, not smuggled into the demonstration data.
    QVector<double> gaugeStage,gaugeFlow;
    for(int i=0;i<20;++i){
        const double h=0.25+double(i)*0.12;
        gaugeStage.append(h);
        gaugeFlow.append(12.4*std::pow(h,2.1)*(1.0+0.03*std::sin(double(i)*2.6)));
    }

    // A turbine: cut-in at 3.5 m/s, cubic below rated, 2.3 MW from 12.5 m/s,
    // cut-out at 25. The three speeds are the whole of what the curve is read
    // for and none of them exists in a decaying sine.
    QVector<double> windSpeedMs,windPower;
    for(int i=0;i<=54;++i){
        const double u=double(i)*0.5;
        double kw;
        if(u<3.5)       kw=0.0;
        else if(u<12.5) kw=2300.0*std::pow((u-3.5)/9.0,3.0);
        else if(u<=25.0)kw=2300.0;
        else            kw=0.0;
        windSpeedMs.append(u); windPower.append(kw);
    }

    // ------------------------------------ schedules, logs and traffic
    //
    // Twelve more engines reading a shape the shared columns do not have: a
    // date and a count, a row and two ends of a bar, four price columns, two
    // ends of a journey. Handed five signal columns each of them drew - a
    // Gantt schedule of 240 overlapping bars in a fan, an OHLC chart of 240
    // candles a pixel wide, a borehole log of 240 beds, an availability
    // timeline reporting uptime on a sine.

    // A year of daily counts with a weekly rhythm and a summer lull, for the
    // calendar heatmap. The x column is a day number; only the difference is
    // used, so any fixed daily step works.
    QVector<double> calDay,calCount;
    for(int i=0;i<364;++i){
        const double weekday=double(i%7);
        const double season=1.0-0.45*std::exp(-std::pow((double(i)-200.0)/45.0,2.0));
        const double weekly=(weekday>=5.0)?0.35:1.0;
        calDay.append(double(i));
        calCount.append(42.0*season*weekly*(1.0+0.18*std::sin(double(i)*1.7)));
    }

    // A load history with cycles of several different ranges nested inside
    // each other, which is what rainflow counting is FOR: a single sine has
    // one range and fills one cell of the matrix.
    QVector<double> loadHistory;
    for(int i=0;i<900;++i){
        const double x=double(i);
        loadHistory.append(110.0*std::sin(x*0.017)
                          +42.0*std::sin(x*0.11+0.7)
                          +14.0*std::sin(x*0.53+1.9)
                          + 5.0*std::sin(x*1.7));
    }

    // A project: ten tasks, each a row with a start and a finish in days.
    QVector<double> taskRow,taskFrom,taskTo;
    {
        const double starts[10]={0, 5, 5,14,22,22,30,38,44,52};
        const double ends  [10]={6,14,18,24,31,34,40,47,56,60};
        for(int i=0;i<10;++i){
            taskRow.append(double(i)); taskFrom.append(starts[i]); taskTo.append(ends[i]);
        }
    }

    // Four machines over a 720-hour month, up in stretches with outages
    // between them - so the engine's uptime percentage is a real one.
    QVector<double> mcRow,mcFrom,mcTo;
    for(int machine=0;machine<4;++machine){
        double at=0.0;
        int k=0;
        while(at<720.0){
            const double u=std::fmod(double(machine*13+k)*0.61803398875,1.0);
            const double up=40.0+180.0*u;
            const double down=6.0+30.0*std::fmod(u*3.3,1.0)*(machine+1)*0.4;
            const double end=qMin(720.0,at+up);
            if(end>at){ mcRow.append(double(machine)); mcFrom.append(at); mcTo.append(end); }
            at=end+down; ++k;
            if(k>40) break;
        }
    }

    // A borehole: one track, nine beds from the surface down, each from one
    // depth to the next.
    // TWO holes, not one. A single track is a correct borehole log and a
    // three-pixel-wide figure: the reason a log is drawn rather than tabulated
    // is that beds are correlated ACROSS holes, and one hole has nothing to
    // correlate with.
    QVector<double> bedTrack,bedFrom,bedTo;
    {
        const double hole1[10]={0.0,1.8,4.2,4.9,7.6,12.4,15.0,19.3,24.8,31.0};
        const double hole2[10]={0.0,2.6,5.1,5.4,9.2,13.1,14.4,20.6,23.9,29.5};
        for(int i=0;i+1<10;++i){
            bedTrack.append(0.0); bedFrom.append(hole1[i]); bedTo.append(hole1[i+1]);
        }
        for(int i=0;i+1<10;++i){
            bedTrack.append(1.0); bedFrom.append(hole2[i]); bedTo.append(hole2[i+1]);
        }
    }

    // Sixty trading days: open, high, low and close, with the high and low
    // bracketing both ends as they must.
    QVector<double> ohlcOpen,ohlcHigh,ohlcLow,ohlcClose;
    {
        double price=118.0;
        for(int i=0;i<60;++i){
            const double u=std::fmod(double(i)*0.61803398875,1.0)-0.5;
            const double v=std::fmod(double(i)*0.7548776662,1.0);
            const double open=price;
            const double close=open*(1.0+0.021*u+0.0016*std::sin(double(i)*0.4));
            const double wick=open*0.011*(0.4+v);
            ohlcOpen.append(open);
            ohlcClose.append(close);
            ohlcHigh.append(qMax(open,close)+wick);
            ohlcLow.append(qMin(open,close)-wick);
            price=close;
        }
    }

    // Journeys between eight places: an origin and a destination, and how many
    // people made each trip.
    QVector<double> odOLon,odOLat,odDLon,odDLat,odCount;
    {
        const double lon[8]={-3.19,-2.24,-1.55,-1.47,-0.13,-2.59,-1.90,-4.25};
        const double lat[8]={55.95,53.48,53.80,53.38,51.51,51.45,52.48,55.86};
        int k=0;
        for(int a=0;a<8;++a) for(int b=0;b<8;++b){
            if(a==b) continue;
            if((a*8+b)%3) continue;                 // a sample, not the full matrix
            odOLon.append(lon[a]); odOLat.append(lat[a]);
            odDLon.append(lon[b]); odDLat.append(lat[b]);
            odCount.append(400.0+9000.0*std::fmod(double(++k)*0.61803398875,1.0));
        }
    }

    // A cumulative flow diagram: four workflow states over twelve weeks, each
    // a count of items currently in that state.
    QVector<double> cfBacklog,cfProgress,cfReview,cfDone;
    for(int i=0;i<24;++i){
        const double w=double(i);
        cfBacklog.append(qMax(0.0,46.0-1.5*w+3.0*std::sin(w*0.7)));
        cfProgress.append(6.0+2.5*std::sin(w*0.9)+0.1*w);
        cfReview.append(3.0+2.0*std::sin(w*1.3+1.0));
        cfDone.append(1.4*w*w*0.12+2.0*w);
    }

    // Stock drawn down and replenished: the sawtooth the engine measures the
    // reorder point from. Twelve cycles, so "mean level" and "minimum" mean
    // something.
    QVector<double> stockDay,stockLevel;
    {
        double level=900.0;
        for(int i=0;i<260;++i){
            stockDay.append(double(i));
            stockLevel.append(level);
            level-=38.0+6.0*std::sin(double(i)*0.9);
            if(level<180.0) level=900.0;
        }
    }

    // Traffic counts: density against FLOW, which is what the engine reads.
    //
    // Greenshields with vf = 104 km/h and kj = 145 veh/km. The engine fits
    // q = vf k - (vf/kj) k^2 and derives the speed as q/k, so handing it the
    // speed column directly - which is the other half of the same data and the
    // obvious guess - made it fit a parabola to a straight line: it reported
    // vf 8.4 against 104, and drew the flow curve, peaking at 215, on an axis
    // of speeds that never exceed 104.
    QVector<double> trafficDensity,trafficFlow;
    for(int i=0;i<44;++i){
        const double k=3.0+double(i)*3.2;
        const double v=104.0*(1.0-k/145.0);
        trafficDensity.append(k);
        trafficFlow.append(qMax(0.0,k*v)*(1.0+0.035*std::sin(double(i)*2.4)));
    }

    // A received bit stream sampled at 24 points per symbol, with jitter and
    // noise, so folding it onto two symbol periods draws an eye that has an
    // opening to measure.
    QVector<double> eyeTime,eyeVolts,eyePeriod;
    {
        quint32 bits=0x9E3779B9u;
        double last=0.0;
        for(int sym=0;sym<160;++sym){
            bits^=bits<<13; bits^=bits>>17; bits^=bits<<5;
            const double target=(bits&1u)?1.0:-1.0;
            for(int s=0;s<24;++s){
                const double f=double(s)/24.0;
                // A first-order edge, so the transition has a real rise time.
                const double level=target+(last-target)*std::exp(-f*7.0);
                const double noise=0.045*std::sin(double(sym*24+s)*2.399);
                eyeTime.append(double(sym*24+s));
                eyeVolts.append(level+noise);
            }
            last=target;
        }
        eyePeriod.append(24.0);
    }

    // Conditioned air: a set of room states through a summer day, all of them
    // below saturation as they must be.
    // The second column is RELATIVE HUMIDITY, not the humidity ratio the y
    // axis is drawn in - the engine computes the ratio from the temperature
    // and the RH, which is the whole point of a psychrometric chart. Two
    // earlier tries supplied the ratio itself, first in kg/kg and then in
    // g/kg; both were read as a relative humidity of a few per cent and the
    // states sat in a line along the floor of the chart. The unit on an axis
    // is not always the unit of the column that produced it.
    QVector<double> psyDryBulb,psyRelHumidity;
    for(int i=0;i<26;++i){
        const double u=std::fmod(double(i)*0.61803398875,1.0);
        psyDryBulb.append(19.0+11.0*u);
        psyRelHumidity.append(34.0+42.0*std::fmod(u*2.6,1.0));   // per cent
    }

    // ------------------------------------------- process control and survey
    //
    // A control chart of a rising sine reports every point out of control and
    // both limits below the data; a CUSUM of one reports "210 points beyond
    // h"; a capability study of one draws a bimodal histogram and computes a
    // Cpk from it. The charts were right - a sine IS out of control - and
    // saying so 240 times is not a demonstration of anything.
    //
    // A deterministic pseudo-normal deviate, the same on every run and every
    // compiler: xorshift32 from a fixed seed, summed Irwin-Hall style.
    //
    // The first version summed twelve terms of a GOLDEN-RATIO sequence instead
    // of a generator's output, on the reasoning that a low-discrepancy
    // sequence is reproducible and a PRNG might not be. It is reproducible and
    // it is not noise: consecutive i differ by twelve steps of the same
    // irrational rotation, so the sum is very nearly periodic, and every
    // fixture built on it came out with a visible sawtooth. The capability
    // histogram was BIMODAL - two clean humps with a gap between them - which
    // is what a reader would have taken as the finding of the figure.
    //
    // xorshift32 is a dozen lines of specified integer arithmetic; it gives
    // the same stream everywhere, and its output is actually uncorrelated.
    quint32 spcState=0x2545F491u;
    const auto normalAt=[&spcState](int,double){
        double u=0.0;
        for(int k=0;k<12;++k){
            spcState^=spcState<<13; spcState^=spcState>>17; spcState^=spcState<<5;
            u+=double(spcState)/4294967296.0;
        }
        return u-6.0;
    };

    // A process in control with a small step at sample 150 - which is what a
    // control chart is for, and what a rising sine cannot show.
    QVector<double> spcValue;
    for(int i=0;i<200;++i)
        spcValue.append(48.0+(i>=150?0.9:0.0)+0.45*normalAt(i,1.0));

    // The same process by subgroups of five, for the X-bar and R chart.
    QVector<double> xbarA,xbarB,xbarC,xbarD,xbarE;
    for(int g=0;g<40;++g){
        QVector<double>* col[5]={&xbarA,&xbarB,&xbarC,&xbarD,&xbarE};
        for(int k=0;k<5;++k)
            col[k]->append(48.0+(g>=30?0.7:0.0)+0.45*normalAt(g*5+k,3.0));
    }

    // A capability study: 180 parts, specification 47.0 to 49.0, a process
    // centred slightly high - so Cp and Cpk differ, which is the entire reason
    // both numbers exist.
    QVector<double> capValue,capLower,capUpper;
    for(int i=0;i<180;++i) capValue.append(48.25+0.28*normalAt(i,5.0));
    capLower.append(47.0); capUpper.append(49.0);

    // Defect counts for the attribute charts: 30 lots of 200 units, about 3%
    // defective, with one bad lot.
    // WITH THE LOT SIZE VARYING, which is the whole difference between half
    // of these charts and the other half.
    //
    // A constant lot size made the four attribute charts proportional to each
    // other: the p-chart and the u-chart came out 1.06 grey levels from each
    // other and 2.0 from the np-chart, because dividing every point by the
    // same number changes only the axis. The distinction that matters is that
    // a p-chart and a u-chart have STEPPED control limits - they widen on a
    // small lot and tighten on a large one - while an np-chart and a c-chart,
    // which assume a constant lot, have straight ones. On a constant lot size
    // that difference does not exist to be drawn.
    // AND A DEFECTIVE IS NOT A DEFECT. A p-chart counts the ITEMS that failed;
    // a c- or u-chart counts the FAULTS found, and one item can carry several.
    // Given the same column both pairs drew the same shape with a different
    // axis label, which is the arithmetic saying what the fixture said rather
    // than what the charts are for.
    QVector<double> defects,lotSize,faults;
    for(int i=0;i<30;++i){
        const double u=std::fmod(double(i)*0.61803398875,1.0);
        const double v=std::fmod(double(i)*0.7548776662,1.0);
        const double w=std::fmod(double(i)*0.3247179572,1.0);
        const double lot=std::floor(90.0+340.0*v);
        lotSize.append(lot);
        defects.append(std::floor((i==19?0.085:0.030)*lot+3.0*(u-0.5)*2.0));
        // About 2.4 faults per defective item, and its own bad lot at 11 -
        // so the two families of chart do not flag the same sample either.
        faults.append(std::floor((i==11?0.19:0.072)*lot+5.0*(w-0.5)*2.0));
    }

    // A meta-analysis for the funnel plot: twenty studies scattered about a
    // true effect of 0.34, the small ones more widely - which is the shape the
    // funnel is drawn to test.
    QVector<double> studyEffect,studyError;
    for(int i=0;i<20;++i){
        const double se=0.055+0.30*std::fmod(double(i)*0.7548776662,1.0);
        studyError.append(se);
        studyEffect.append(0.34+se*normalAt(i,7.0));
    }

    // A CTD cast: depth with temperature, salinity and oxygen, through a
    // thermocline - the feature the profile exists to show.
    QVector<double> ctdDepth,ctdTemp,ctdSalinity,ctdOxygen;
    for(int i=0;i<=120;++i){
        const double z=double(i)*4.0;                       // metres
        const double thermo=1.0/(1.0+std::exp((z-95.0)/14.0));
        ctdDepth.append(z);
        ctdTemp.append(4.6+13.4*thermo);
        ctdSalinity.append(35.55-0.60*thermo);
        ctdOxygen.append(5.9-3.1*thermo+1.4*std::exp(-std::pow((z-160.0)/60.0,2.0)));
    }

    // The same cast as a T-S diagram: salinity against temperature, where the
    // two water masses show as two clusters joined by a mixing line.
    QVector<double> tsSalinity,tsTemperature;
    for(int i=0;i<ctdDepth.size();++i){
        tsSalinity.append(ctdSalinity[i]);
        tsTemperature.append(ctdTemp[i]);
    }

    // An equity curve with two real drawdowns, for the drawdown engine to
    // measure the worst of.
    QVector<double> equityDay,equityValue;
    {
        double v=100.0;
        for(int i=0;i<500;++i){
            const double shock=(i>150&&i<215)?-0.0042:((i>330&&i<365)?-0.0075:0.0);
            v*=1.0+0.00085+shock+0.0035*normalAt(i,11.0)*0.25;
            equityDay.append(double(i)); equityValue.append(v);
        }
    }

    // Earthworks: cut positive, fill negative, along a 3 km alignment. The
    // engine accumulates it and finds the balance points, which only exist if
    // the sign actually changes.
    QVector<double> chainage,cutFill;
    for(int i=0;i<=60;++i){
        const double x=double(i)*50.0;                      // metres
        chainage.append(x);
        cutFill.append(900.0*std::sin(x/380.0)+240.0*std::sin(x/97.0));
    }

    // A simply supported beam, 8 m, carrying a uniform load: the engine
    // integrates the load to shear and the shear to moment, so the column is
    // the LOAD - and on a uniform one the answers are exactly known, a linear
    // shear through zero at mid-span and a parabolic moment peaking at wL^2/8.
    QVector<double> beamStation,beamLoad;
    for(int i=0;i<=80;++i){
        beamStation.append(double(i)*0.1);                  // m
        beamLoad.append(-12.0);                             // kN/m downward
    }

    // ------------------------------------------- multivariate and narrative
    //
    // Six engines whose content is a SMALL number of things compared with each
    // other. At 240 rows each of them drew a smear: a dendrogram of two
    // hundred leaves, a slope graph of sixty crossing lines, a population
    // pyramid of forty rows of a sine, a ternary scatter squeezed into a
    // thumbprint in the middle of its triangle. The caps inside the engines
    // kept them from being worse and could not make them readable.

    // Merge heights for a dendrogram of twelve items: several tight merges,
    // then a few expensive ones, so there is a height at which to cut.
    QVector<double> mergeHeight;
    {
        const double h[11]={0.28,0.31,0.35,0.44,0.52,0.61,0.93,1.12,1.58,2.40,3.70};
        for(int i=0;i<11;++i) mergeHeight.append(h[i]);
    }

    // Twelve regions before and after, most improving and three not - which is
    // the whole reading of a slope graph and needs few enough lines to follow.
    QVector<double> beforeRate,afterRate;
    {
        const double b[12]={62.1,58.4,71.0,49.8,66.2,55.5,73.4,44.9,60.0,68.8,51.2,57.7};
        const double a[12]={71.3,66.0,69.5,58.2,74.1,52.0,80.2,53.6,59.1,72.4,46.8,64.9};
        for(int i=0;i<12;++i){ beforeRate.append(b[i]); afterRate.append(a[i]); }
    }

    // A waterfall with LOSSES in it. Every step was positive, so the chart was
    // a staircase that only climbed - which is a cumulative sum, and the one
    // thing a waterfall is chosen over a cumulative sum to show is where the
    // ground was given back.
    QVector<double> bridgeStep;
    {
        const double s[9]={0.0,182.0,-64.0,95.0,-38.0,41.0,-112.0,76.0,-29.0};
        for(int i=0;i<9;++i) bridgeStep.append(s[i]);
    }

    // A population by five-year band: a broad base narrowing with age, and
    // more women than men at the top, which is what a pyramid is read for.
    QVector<double> menByBand,womenByBand;
    for(int band=0;band<18;++band){
        const double age=double(band)*5.0;
        const double base=2.45e6*std::exp(-std::pow(age/74.0,3.1));
        menByBand.append(base*(1.0-0.0032*age));
        womenByBand.append(base*(1.0+0.0041*age));
    }

    // Compositions that actually spread across the triangle: three components
    // summing to one hundred, sampled so the points cover the field rather
    // than clustering at the centroid.
    QVector<double> sandPct,siltPct,clayPct;
    for(int i=0;i<44;++i){
        const double u=std::fmod(double(i)*0.61803398875,1.0);
        const double v=std::fmod(double(i)*0.7548776662,1.0)*(1.0-u);
        sandPct.append(u*100.0);
        siltPct.append(v*100.0);
        clayPct.append((1.0-u-v)*100.0);
    }

    // Three measured species, forty individuals each, for the multivariate
    // engines - so the polylines and the Andrews curves separate into groups
    // instead of forming one band of 240.
    QVector<double> irisSepalL,irisSepalW,irisPetalL,irisPetalW;
    {
        quint32 st=0x1D872B41u;
        const auto jitter=[&st](double scale){
            st^=st<<13; st^=st>>17; st^=st<<5;
            return (double(st)/4294967296.0-0.5)*scale;
        };
        const double sl[3]={5.0,5.9,6.6}, sw[3]={3.4,2.8,3.0};
        const double pl[3]={1.5,4.3,5.6}, pw[3]={0.25,1.33,2.03};
        for(int g=0;g<3;++g) for(int k=0;k<40;++k){
            irisSepalL.append(sl[g]+jitter(0.7));
            irisSepalW.append(sw[g]+jitter(0.5));
            irisPetalL.append(pl[g]+jitter(0.9));
            irisPetalW.append(pw[g]+jitter(0.4));
        }
    }

    // ------------------------------------------- terrain, genomics, economics
    //
    // A digital elevation model: a ridge and a valley over a 2 km square, on a
    // 30-by-30 grid. The five engines that read one - slope, aspect,
    // hillshade, the profile and the hypsometric curve - were being handed the
    // shared columns and reading a decaying sine as a height, so the slope map
    // was a picture of the signal's own curvature.
    QVector<double> demX,demY,demZ;
    for(int j=0;j<30;++j){
        for(int i=0;i<30;++i){
            const double x=double(i)*70.0, y=double(j)*70.0;   // metres
            const double ridge=340.0*std::exp(-std::pow((y-1200.0)/430.0,2.0));
            const double valley=-180.0*std::exp(-std::pow((x-760.0)/300.0,2.0));
            demX.append(x); demY.append(y);
            demZ.append(220.0+ridge+valley
                        +26.0*std::sin(x/210.0)*std::cos(y/260.0));
        }
    }

    // A transect across that terrain, as longitude, latitude and height - the
    // three columns a terrain profile reads.
    QVector<double> pathLonDeg,pathLatDeg,pathElevation;
    for(int i=0;i<=120;++i){
        const double f=double(i)/120.0;
        const double x=f*2030.0, y=400.0+f*1400.0;
        const double ridge=340.0*std::exp(-std::pow((y-1200.0)/430.0,2.0));
        const double valley=-180.0*std::exp(-std::pow((x-760.0)/300.0,2.0));
        // Metres converted to degrees about a point in the Cairngorms, so the
        // engine's great-circle distances come out in real kilometres.
        pathLonDeg.append(-3.67+x/(111320.0*std::cos(57.07*3.14159265358979323846/180.0)));
        pathLatDeg.append(57.07+y/110570.0);
        pathElevation.append(220.0+ridge+valley+26.0*std::sin(x/210.0)*std::cos(y/260.0));
    }

    // A year of daily river flows with a flashy winter and a dry summer, for
    // the flow duration curve - which is read for the slope between the 10th
    // and 90th percentile and needs a real spread to have one.
    QVector<double> dailyFlow;
    {
        quint32 st=0x6C078965u;
        for(int d=0;d<365;++d){
            st^=st<<13; st^=st>>17; st^=st<<5;
            const double u=double(st)/4294967296.0;
            const double season=0.35+0.65*(0.5+0.5*std::cos(double(d)/365.0*2.0*M_PI));
            const double base=2.4*season;
            const double storm=(u>0.93)?(14.0*season*(u-0.93)/0.07):0.0;
            dailyFlow.append(base+storm+0.5*u*season);
        }
    }

    // A classifier that is good and not perfect: scores from two overlapping
    // populations with a 22% positive rate. On the shared columns both the ROC
    // and the precision-recall curve reported an area of exactly 1.000, which
    // is what a catalogue figure should never show - a perfect classifier is
    // the one result that means the data is wrong.
    QVector<double> modelScore,modelTruth;
    {
        quint32 st=0xB5026F5Au;
        const auto uniform=[&st]{
            st^=st<<13; st^=st>>17; st^=st<<5;
            return double(st)/4294967296.0;
        };
        for(int i=0;i<400;++i){
            const bool positive=(uniform()<0.22);
            double z=0.0;
            for(int k=0;k<6;++k) z+=uniform();
            z=(z-3.0)/0.707;                          // roughly standard normal
            modelTruth.append(positive?1.0:0.0);
            modelScore.append(1.0/(1.0+std::exp(-(z*0.85+(positive?1.45:-0.35)))));
        }
    }

    // Two expression samples for the MA plot: most genes unchanged, a few
    // strongly up or down, and the noise growing at low intensity - which is
    // the fan shape the plot is read for.
    QVector<double> sampleA,sampleB;
    {
        quint32 st=0x9E3779B1u;
        const auto uniform=[&st]{
            st^=st<<13; st^=st>>17; st^=st<<5;
            return double(st)/4294967296.0;
        };
        for(int i=0;i<900;++i){
            const double intensity=std::pow(2.0,3.0+11.0*uniform());
            const double noise=0.9+1.6/std::sqrt(intensity);
            double fold=1.0;
            const double u=uniform();
            if(u>0.985)      fold=4.0+6.0*uniform();
            else if(u<0.015) fold=1.0/(4.0+6.0*uniform());
            sampleA.append(intensity*(1.0+(uniform()-0.5)*(noise-0.9)));
            sampleB.append(intensity*fold*(1.0+(uniform()-0.5)*(noise-0.9)));
        }
    }

    // A GWAS: p-values along a chromosome, mostly null with two real peaks,
    // so the genome-wide line has something above it and something below.
    QVector<double> gwasP;
    {
        quint32 st=0x27D4EB2Fu;
        for(int i=0;i<2400;++i){
            st^=st<<13; st^=st>>17; st^=st<<5;
            const double u=qMax(1e-12,double(st)/4294967296.0);
            double p=u;
            const double peakA=std::exp(-std::pow((double(i)-640.0)/22.0,2.0));
            const double peakB=std::exp(-std::pow((double(i)-1780.0)/16.0,2.0));
            const double lift=qMax(peakA,peakB);
            if(lift>0.02) p=std::pow(u,1.0+70.0*lift);
            gwasP.append(p);
        }
    }

    // Household incomes with real inequality, for the Lorenz and concentration
    // curves: a log-normal, which is the distribution incomes actually follow
    // and which gives a Gini near 0.35 rather than the 0.08 a sine gives.
    QVector<double> householdIncome,healthSpend;
    {
        quint32 st=0x3C6EF372u;
        const auto uniform=[&st]{
            st^=st<<13; st^=st>>17; st^=st<<5;
            return double(st)/4294967296.0;
        };
        for(int i=0;i<600;++i){
            double z=0.0;
            for(int k=0;k<12;++k) z+=uniform();
            const double income=std::exp(10.1+0.62*(z-6.0));
            householdIncome.append(income);
            // A CONCENTRATION curve needs a second column: the outcome, and
            // the variable the population is RANKED by. Given one column the
            // engine correctly drew nothing at all, and the sweep reported it
            // as an engine that draws nothing - which was the sweep doing its
            // job on a fixture that was half a fixture.
            //
            // Public health spending falls with income: the curve then sits
            // ABOVE the diagonal, which is the reading the figure exists for
            // and the one a Lorenz curve of spending alone cannot give.
            healthSpend.append(1400.0*std::pow(income/28000.0,-0.35)
                               *(0.75+0.5*uniform()));
        }
    }

    // A species count following a log-series: a few common species and a long
    // tail of rare ones, which is the shape a rank-abundance curve exists to
    // show and which a smooth column has none of.
    QVector<double> speciesCount;
    for(int i=0;i<54;++i)
        speciesCount.append(std::ceil(2100.0*std::pow(0.84,double(i))));

    // Eigenvalues with a clear elbow at the fourth component, and the
    // within-cluster sum of squares that bends at k = 4.
    QVector<double> eigenvalue,clusterK,clusterSse;
    {
        const double ev[12]={4.62,2.81,1.74,1.12,0.44,0.36,0.29,0.24,0.19,0.14,0.09,0.05};
        for(int i=0;i<12;++i) eigenvalue.append(ev[i]);
        for(int k=1;k<=12;++k){
            clusterK.append(double(k));
            clusterSse.append(210.0*std::pow(double(k),-1.45)+18.0);
        }
    }

    // ------------------------------------------------------ events and signals
    //
    // An event plot draws one tick per event. Handed 240 evenly spaced samples
    // it drew 240 ticks in a row, which at this figure size is a solid line -
    // five parallel rules, one per column, and nothing that reads as events.
    // Spike trains from three neurons, sparse and irregular.
    QVector<double> spikeA,spikeB,spikeC;
    {
        quint32 st=0x8FE6D7A3u;
        const auto uniform=[&st]{
            st^=st<<13; st^=st>>17; st^=st<<5;
            return double(st)/4294967296.0;
        };
        QVector<double>* train[3]={&spikeA,&spikeB,&spikeC};
        const double rate[3]={7.5,2.8,14.0};                 // per second
        for(int k=0;k<3;++k){
            double at=0.0;
            while(at<6.0){
                at+=-std::log(qMax(1e-9,uniform()))/rate[k];  // Poisson arrivals
                if(at<6.0) train[k]->append(at);
            }
        }
    }

    // A LINEAR CHIRP for the spectrogram: 20 Hz rising to 220 over four
    // seconds at 2 kHz, with a short burst of a fixed tone part way through.
    // The shared columns held a signal whose spectrum does not change, and a
    // spectrogram of a stationary signal is a heatmap of horizontal stripes -
    // a correct picture, and one that shows nothing the power spectral
    // density above it does not already show more clearly.
    QVector<double> chirpTime,chirpValue;
    for(int i=0;i<8192;++i){
        const double t=double(i)/2048.0;
        const double f=20.0+200.0*(t/4.0);
        const double sweep=std::sin(2.0*M_PI*(20.0*t+100.0*t*t/4.0));
        const double burst=(t>1.6&&t<2.3)?0.55*std::sin(2.0*M_PI*640.0*t):0.0;
        chirpTime.append(t);
        chirpValue.append(sweep+burst+0.04*std::sin(double(i)*2.399));
        (void)f;
    }

    // A SPARSITY PATTERN, for the one engine whose entire content is where the
    // zeros are. The shared columns are dense - every value non-zero - so
    // every pair of variables was jointly non-zero and the spy matrix was a
    // solid block of one colour. Correct, for dense data, and a catalogue
    // entry showing nothing.
    //
    // Eight variables, each observed on a band of rows, so the pattern comes
    // out banded: variables more than two apart never appear together.
    QVector<QVector<double>> sparseCols(8);
    for(int j=0;j<8;++j){
        for(int row=0;row<64;++row){
            const int band=row%8;
            const bool present=(std::abs(band-j)<=1);
            sparseCols[j].append(present?(1.0+0.4*std::sin(double(row)*0.7)):0.0);
        }
    }

    // A CLASSIFIER THAT IS MOSTLY RIGHT, for the confusion matrix.
    //
    // It shared the mosaic plot's two category columns, where the true class
    // is i%4 and the predicted is (i/4)%3 - two independent counters. The
    // matrix came out with no diagonal at all and an accuracy of 0.250, which
    // is chance on four classes: a confusion matrix of a classifier that has
    // learned nothing. What the figure is read for is the OFF-diagonal - which
    // pairs of classes get mistaken for each other - and that needs a diagonal
    // to be off.
    //
    // 84% correct overall, with classes 1 and 2 confused for each other far
    // more often than any other pair.
    QVector<double> trueClass,predictedClass;
    {
        quint32 st=0x4A3B2C1Du;
        const auto uniform=[&st]{
            st^=st<<13; st^=st>>17; st^=st<<5;
            return double(st)/4294967296.0;
        };
        for(int i=0;i<300;++i){
            const int truth=int(uniform()*4.0)%4;
            int guess=truth;
            const double u=uniform();
            if(truth==1||truth==2){
                if(u<0.26) guess=(truth==1)?2:1;      // the pair that confuses
                else if(u<0.30) guess=int(uniform()*4.0)%4;
            }else if(u<0.09){
                guess=int(uniform()*4.0)%4;
            }
            trueClass.append(double(truth));
            predictedClass.append(double(guess));
        }
    }

    // ------------------------------------ the last of the reading-a-pair engines
    //
    // Six more that read a specific pair and were given the shared signals: a
    // volcano plot with nothing significant on it, a Bland-Altman of a signal
    // against a phase-shifted copy of itself, a L'Abbe plot of a curve against
    // another curve, a Duane plot of a time base.

    // Differential expression: 800 genes, most unchanged, a few strongly up or
    // down with small p-values - which is the shape a volcano is read for and
    // which the shared columns, whose p column is a smooth exponential decay,
    // could not produce. The catalogue entry said "not significant" and drew
    // one flat row of grey dots.
    QVector<double> foldChange,foldP;
    {
        quint32 st=0xC2B2AE35u;
        const auto uniform=[&st]{
            st^=st<<13; st^=st>>17; st^=st<<5;
            return double(st)/4294967296.0;
        };
        for(int i=0;i<800;++i){
            double z=0.0;
            for(int k=0;k<12;++k) z+=uniform();
            z-=6.0;
            const double u=uniform();
            // THE NULL FOLD CHANGE AND THE NULL p ARE INDEPENDENT.
            //
            // Deriving both from the same z made them perfectly correlated, so
            // the null cloud came out as a narrow V pinched at the origin -
            // which is a picture of the generator, not of a differential
            // expression experiment. Under the null the p-values are uniform
            // and the fold changes are small and symmetric about zero, and
            // they have nothing to do with each other; that independence is
            // what makes the cloud fill the bottom of the plot and the volcano
            // have shoulders.
            double fold=0.34*z;                       // null: small, symmetric
            double p=uniform();                       // null: uniform
            if(u>0.972){                              // up-regulated
                fold=1.4+2.6*uniform();  p=std::pow(10.0,-(4.0+9.0*uniform()));
            }else if(u<0.028){                        // down-regulated
                fold=-(1.4+2.6*uniform()); p=std::pow(10.0,-(4.0+9.0*uniform()));
            }
            foldChange.append(fold);
            foldP.append(qBound(1e-16,p,1.0));
        }
    }

    // A method comparison with a real bias and real limits of agreement: the
    // new meter reads 2.1 units high on average with a proportional error, so
    // the difference plot has both a non-zero bias line and a visible fan.
    QVector<double> referenceMethod,newMethod;
    {
        quint32 st=0x1B873593u;
        const auto uniform=[&st]{
            st^=st<<13; st^=st>>17; st^=st<<5;
            return double(st)/4294967296.0;
        };
        for(int i=0;i<90;++i){
            const double truth=40.0+120.0*uniform();
            double z=0.0;
            for(int k=0;k<12;++k) z+=uniform();
            referenceMethod.append(truth+1.1*(z-6.0));
            double w=0.0;
            for(int k=0;k<12;++k) w+=uniform();
            newMethod.append(truth+2.1+0.018*truth+1.6*(w-6.0));
        }
    }

    // Twenty-four trials: the event rate in the control arm against the rate
    // in the treated arm, with the treatment effect SHRINKING as the control
    // rate rises - which is the heterogeneity a L'Abbe plot exists to make
    // visible and which a pooled estimate cannot show.
    QVector<double> controlArm,treatedArm;
    {
        quint32 st=0x85EBCA6Bu;
        const auto uniform=[&st]{
            st^=st<<13; st^=st>>17; st^=st<<5;
            return double(st)/4294967296.0;
        };
        for(int i=0;i<24;++i){
            const double control=0.08+0.62*uniform();
            const double benefit=0.30*(1.0-control);
            treatedArm.append(qBound(0.0,control*(1.0-benefit)+0.045*(uniform()-0.5)*2.0,1.0));
            controlArm.append(control);
        }
    }

    // Held-out predictions from a decent model, for the prediction error plot:
    // R-squared near 0.9 with the scatter widening at the top of the range.
    QVector<double> observedY,predictedY;
    {
        quint32 st=0xCC9E2D51u;
        const auto uniform=[&st]{
            st^=st<<13; st^=st>>17; st^=st<<5;
            return double(st)/4294967296.0;
        };
        for(int i=0;i<160;++i){
            const double truth=12.0+68.0*uniform();
            double z=0.0;
            for(int k=0;k<12;++k) z+=uniform();
            observedY.append(truth);
            predictedY.append(truth*0.96+2.4+(z-6.0)*(2.2+0.055*truth));
        }
    }

    // A learning curve that plateaus and a validation curve that peaks: the
    // two figures answer different questions and each needs its own shape -
    // more data stops paying, against a hyperparameter having a best value.
    QVector<double> trainScore,validScore,paramTrain,paramValid;
    for(int i=0;i<24;++i){
        const double n=double(i+1);
        trainScore.append(0.995-0.06*std::exp(-n/3.0));
        validScore.append(0.93-0.40*std::exp(-n/4.5));
        const double k=double(i+1);
        paramTrain.append(qMin(0.999,0.55+0.021*k));
        paramValid.append(0.90-0.014*(k-11.0)*(k-11.0)*0.09-0.02*std::exp(-k/2.0));
    }

    const QHash<QString,ShapedAxes> shapedAxes{
        {QStringLiteral("Stairs"),{QStringLiteral("hour"),
                                   QStringLiteral("unit price (p/kWh)")}},
        {QStringLiteral("Step Mid"),{QStringLiteral("hour"),
                                     QStringLiteral("unit price (p/kWh)")}},
        {QStringLiteral("Duration Curve"),{QStringLiteral("per cent of time exceeded"),
                                           QStringLiteral("discharge (m3/s)")}},
        {QStringLiteral("Hypsometric Curve"),{QStringLiteral("fraction of area above"),
                                              QStringLiteral("elevation (m)")}},
        {QStringLiteral("Event Plot"),{QStringLiteral("time (s)"),
                                       QStringLiteral("unit")}},
        {QStringLiteral("Elbow Plot"),{QStringLiteral("clusters k"),
                                       QStringLiteral("within-cluster sum of squares")}},
        {QStringLiteral("Drawdown Curve"),{QStringLiteral("trading day"),
                                           QStringLiteral("fund value")}},
        {QStringLiteral("Mass Haul Diagram"),{QStringLiteral("chainage (m)"),
                                              QStringLiteral("cut (+) / fill (-) (m3)")}},
        {QStringLiteral("Shear and Moment"),{QStringLiteral("station along span (m)"),
                                             QStringLiteral("load (kN/m)")}},
        {QStringLiteral("Inventory Sawtooth"),{QStringLiteral("day"),
                                               QStringLiteral("units on hand")}},
        {QStringLiteral("Fundamental Diagram"),{QStringLiteral("density (veh/km)"),
                                                QStringLiteral("flow (veh/h)")}},
        {QStringLiteral("Stress-Strain Curve"),{QStringLiteral("strain"),
                                                QStringLiteral("stress (MPa)")}},
        {QStringLiteral("Titration Curve"),{QStringLiteral("titrant added (mL)"),
                                            QStringLiteral("pH")}},
        {QStringLiteral("Calibration Curve"),{QStringLiteral("concentration (mg/L)"),
                                              QStringLiteral("absorbance")}},
        {QStringLiteral("Wind Power Curve"),{QStringLiteral("wind speed (m/s)"),
                                             QStringLiteral("power (kW)")}},
        {QStringLiteral("S-N Fatigue Curve"),{QStringLiteral("cycles to failure N"),
                                              QStringLiteral("stress amplitude S (MPa)")}},
        {QStringLiteral("Payload-Range Diagram"),{QStringLiteral("range (nm)"),
                                                  QStringLiteral("payload (kg)")}},
        {QStringLiteral("Lift Curve"),{QStringLiteral("angle of attack (deg)"),
                                       QStringLiteral("lift coefficient CL")}},
        {QStringLiteral("Flight Profile"),{QStringLiteral("time (min)"),
                                           QStringLiteral("altitude (ft)")}},
        {QStringLiteral("Weight and Balance Envelope"),
            {QStringLiteral("centre of gravity (in aft of datum)"),
             QStringLiteral("gross weight (lb)")}},
    };

    const QHash<QString,QVector<PlotSeries>> shaped{
        // NO COLUMNS AT ALL, for the seven engines that plot a FORMULA.
        //
        // Their domain follows the mapped data when there is any, so that a
        // formula can be overlaid on a measurement - which is right, and which
        // meant the catalogue drew every one of them over the shared signal
        // column's range of 2.1 to 5.2. `sin(x)*cos(y)` on that window has its
        // zero set at x = pi and y = 3pi/2 and nowhere else, so the Implicit
        // Function entry was a cross: two straight lines, mathematically
        // correct and unrecognisable as what the engine does.
        //
        // An empty series list is the honest demonstration. These engines plot
        // a formula, not a dataset, and with nothing mapped they use their own
        // default domain of -10 to 10 - which is the state a person sees the
        // first time they open one.
        {QStringLiteral("Function Plot"),{}},
        {QStringLiteral("Function Contour"),{}},
        {QStringLiteral("Function Surface"),{}},
        {QStringLiteral("Function Mesh"),{}},
        {QStringLiteral("Function 3D Parametric"),{}},
        {QStringLiteral("Implicit Function"),{}},
        {QStringLiteral("Implicit Surface"),{}},
        {QStringLiteral("Volcano Plot"),
            {column("log2 fold change",foldChange),column("p value",foldP)}},
        {QStringLiteral("Bland-Altman"),
            {column("reference",referenceMethod),column("new meter",newMethod)}},
        {QStringLiteral("L'Abbé Plot"),
            {column("control arm",controlArm),column("treated arm",treatedArm)}},
        {QStringLiteral("Prediction Error Plot"),
            {column("observed",observedY),column("predicted",predictedY)}},
        {QStringLiteral("Learning Curve"),
            {column("training",trainScore),column("validation",validScore)}},
        {QStringLiteral("Validation Curve"),
            {column("training",paramTrain),column("validation",paramValid)}},
        {QStringLiteral("Spy Matrix"),
            {column("v1",sparseCols[0]),column("v2",sparseCols[1]),
             column("v3",sparseCols[2]),column("v4",sparseCols[3]),
             column("v5",sparseCols[4]),column("v6",sparseCols[5]),
             column("v7",sparseCols[6]),column("v8",sparseCols[7])}},
        {QStringLiteral("Event Plot"),{paired("unit 1",spikeA,spikeA),
                                       paired("unit 2",spikeB,spikeB),
                                       paired("unit 3",spikeC,spikeC)}},
        {QStringLiteral("Spectrogram"),{paired("chirp",chirpTime,chirpValue)}},
        {QStringLiteral("Slope Map"),{column("easting (m)",demX),
                                      column("northing (m)",demY),
                                      column("elevation (m)",demZ)}},
        {QStringLiteral("Aspect Map"),{column("easting (m)",demX),
                                       column("northing (m)",demY),
                                       column("elevation (m)",demZ)}},
        {QStringLiteral("Hillshade"),{column("easting (m)",demX),
                                      column("northing (m)",demY),
                                      column("elevation (m)",demZ)}},
        {QStringLiteral("Terrain Profile"),
            {column("longitude",pathLonDeg),column("latitude",pathLatDeg),
             column("elevation (m)",pathElevation)}},
        {QStringLiteral("Hypsometric Curve"),{column("elevation (m)",demZ)}},
        {QStringLiteral("Duration Curve"),{column("discharge (m3/s)",dailyFlow)}},
        {QStringLiteral("ROC Curve"),
            {column("score",modelScore),column("outcome",modelTruth)}},
        {QStringLiteral("Precision-Recall Curve"),
            {column("score",modelScore),column("outcome",modelTruth)}},
        {QStringLiteral("MA Plot"),
            {column("control",sampleA),column("treated",sampleB)}},
        {QStringLiteral("Manhattan Plot"),{column("chromosome 6",gwasP)}},
        {QStringLiteral("Lorenz Curve"),{column("household income",householdIncome)}},
        {QStringLiteral("Concentration Curve"),
            {column("health spending",healthSpend),
             column("household income",householdIncome)}},
        {QStringLiteral("Rank-Abundance Curve"),{column("individuals",speciesCount)}},
        {QStringLiteral("Scree Plot"),{column("eigenvalue",eigenvalue)}},
        {QStringLiteral("Elbow Plot"),{paired("within-cluster SSE",clusterK,clusterSse)}},
        {QStringLiteral("Dendrogram"),{column("merge height",mergeHeight)}},
        {QStringLiteral("Slope Graph"),
            {column("2019",beforeRate),column("2024",afterRate)}},
        {QStringLiteral("Waterfall"),{column("bridge",bridgeStep)}},
        {QStringLiteral("Population Pyramid"),
            {column("men",menByBand),column("women",womenByBand)}},
        {QStringLiteral("Ternary Scatter"),
            {column("sand",sandPct),column("silt",siltPct),column("clay",clayPct)}},
        {QStringLiteral("Parallel Coordinates"),
            {column("sepal length",irisSepalL),column("sepal width",irisSepalW),
             column("petal length",irisPetalL),column("petal width",irisPetalW)}},
        {QStringLiteral("Andrews Curves"),
            {column("sepal length",irisSepalL),column("sepal width",irisSepalW),
             column("petal length",irisPetalL),column("petal width",irisPetalW)}},
        {QStringLiteral("Control Chart"),{column("bore diameter (mm)",spcValue)}},
        {QStringLiteral("CUSUM Chart"),{column("bore diameter (mm)",spcValue)}},
        {QStringLiteral("EWMA Chart"),{column("bore diameter (mm)",spcValue)}},
        {QStringLiteral("X-bar and R Chart"),
            {column("part 1",xbarA),column("part 2",xbarB),column("part 3",xbarC),
             column("part 4",xbarD),column("part 5",xbarE)}},
        {QStringLiteral("Process Capability"),
            {column("bore diameter (mm)",capValue),
             column("LSL",capLower),column("USL",capUpper)}},
        {QStringLiteral("p-Chart"),{column("defective",defects),column("lot size",lotSize)}},
        {QStringLiteral("np-Chart"),{column("defective",defects),column("lot size",lotSize)}},
        {QStringLiteral("c-Chart"),{column("faults",faults)}},
        {QStringLiteral("u-Chart"),{column("faults",faults),column("units",lotSize)}},
        {QStringLiteral("Funnel Plot"),
            {column("effect size",studyEffect),column("standard error",studyError)}},
        {QStringLiteral("CTD Profile"),
            {column("depth (m)",ctdDepth),column("temperature (degC)",ctdTemp),
             column("salinity (PSU)",ctdSalinity),column("oxygen (ml/l)",ctdOxygen)}},
        {QStringLiteral("T-S Diagram"),
            {column("salinity (PSU)",tsSalinity),
             column("temperature (degC)",tsTemperature)}},
        {QStringLiteral("Drawdown Curve"),{paired("fund",equityDay,equityValue)}},
        {QStringLiteral("Mass Haul Diagram"),{paired("alignment",chainage,cutFill)}},
        {QStringLiteral("Shear and Moment"),{paired("uniform load",beamStation,beamLoad)}},
        {QStringLiteral("Calendar Heatmap"),{paired("commits",calDay,calCount)}},
        {QStringLiteral("Rainflow Matrix"),{column("load (MPa)",loadHistory)}},
        {QStringLiteral("Gantt Schedule"),
            {column("task",taskRow),column("start (day)",taskFrom),
             column("finish (day)",taskTo)}},
        {QStringLiteral("Availability Timeline"),
            {column("machine",mcRow),column("up from (h)",mcFrom),
             column("up to (h)",mcTo)}},
        {QStringLiteral("Borehole Log"),
            {column("BH-1",bedTrack),column("from (m)",bedFrom),
             column("to (m)",bedTo)}},
        {QStringLiteral("OHLC Candlestick"),
            {column("open",ohlcOpen),column("high",ohlcHigh),
             column("low",ohlcLow),column("close",ohlcClose)}},
        {QStringLiteral("Origin-Destination Flow"),
            {column("origin lon",odOLon),column("origin lat",odOLat),
             column("destination lon",odDLon),column("destination lat",odDLat),
             column("trips",odCount)}},
        {QStringLiteral("Cumulative Flow"),
            {column("backlog",cfBacklog),column("in progress",cfProgress),
             column("in review",cfReview),column("done",cfDone)}},
        {QStringLiteral("Inventory Sawtooth"),{paired("stock on hand",stockDay,stockLevel)}},
        {QStringLiteral("Fundamental Diagram"),
            {paired("M6 northbound",trafficDensity,trafficFlow)}},
        {QStringLiteral("Eye Diagram"),
            {paired("received",eyeTime,eyeVolts),column("samples per symbol",eyePeriod)}},
        {QStringLiteral("Psychrometric Chart"),
            {column("dry bulb (degC)",psyDryBulb),
             column("relative humidity (%)",psyRelHumidity)}},
        {QStringLiteral("Stress-Strain Curve"),
            {paired("6061-T6",ssStrain,ssStress)}},
        {QStringLiteral("Arrhenius Plot"),{paired("hydrolysis",arrTemp,arrRate)}},
        {QStringLiteral("Titration Curve"),
            {paired("0.1 M acetic acid with 0.1 M NaOH",titrantVol,titrantPh)}},
        {QStringLiteral("Calibration Curve"),
            {paired("standards",calConc,calSignal)}},
        {QStringLiteral("Michaelis-Menten"),
            {paired("alkaline phosphatase",mmSubstrate,mmRate)}},
        {QStringLiteral("Dose-Response Curve"),
            {paired("compound A",doseConc,doseEffect)}},
        {QStringLiteral("Rating Curve"),{paired("gauging station",gaugeStage,gaugeFlow)}},
        {QStringLiteral("Wind Power Curve"),{paired("2.3 MW turbine",windSpeedMs,windPower)}},
        {QStringLiteral("Weibull Probability Plot"),
            {column("bearing life (h)",weibullLife)}},
        {QStringLiteral("Reliability Growth"),
            {column("cumulative test hours",growthTime)}},
        {QStringLiteral("MTBF Trend"),{column("fleet",mtbfTime)}},
        {QStringLiteral("S-N Fatigue Curve"),
            {paired("6061-T6",snCycles,snStress)}},
        {QStringLiteral("Bathtub Curve"),{column("service life (h)",bathtubLife)}},
        {QStringLiteral("Payload-Range Diagram"),
            {paired("payload",prRange,prPayload)}},
        {QStringLiteral("Lift Curve"),{paired("clean",liftAlpha,liftCl),
                                       paired("flaps 20",liftAlpha,liftClFlap)}},
        {QStringLiteral("Flight Profile"),
            {paired("cruise profile",profMinute,profAltitude)}},
        {QStringLiteral("Runway Crosswind"),
            {column("runway heading",rwHeading),column("wind from",rwFrom),
             column("wind speed (kt)",rwSpeed)}},
        {QStringLiteral("Weight and Balance Envelope"),
            {paired("utility envelope",wbArm,wbWeight),
             paired("loadings",loadArm,loadWeight)}},
        {QStringLiteral("Polar Line"),{paired("cardioid",lobeAngle,lobeCardioid),
                                       paired("dipole",lobeAngle,lobeDipole)}},
        {QStringLiteral("Polar Scatter"),{paired("contacts",bearing,range)}},
        {QStringLiteral("Polar Bubble"),{paired("contacts",bearing,range),
                                         column("strength",strength)}},
        {QStringLiteral("Compass"),{paired("surface current",currentDir,currentSpeed)}},
        {QStringLiteral("Stereonet"),{column("dip direction",dipDir),
                                      column("dip",dipAngle)}},
        {QStringLiteral("Stairs"),{paired("tariff band",stepT,stepLevel),
                                   paired("standing charge",stepT,stepSecond)}},
        {QStringLiteral("Step Mid"),{paired("tariff band",stepT,stepLevel),
                                     paired("standing charge",stepT,stepSecond)}},
        {QStringLiteral("3D Swarm"),{column("level",swarmX),column("reading",swarmY),
                                     column("batch",swarmZ)}},
        {QStringLiteral("V-n Flight Envelope"),{paired("manoeuvre envelope",vnV,vnN)}},
        {QStringLiteral("Altitude-Mach Envelope"),{paired("envelope",amM,amAlt)}},
        {QStringLiteral("Drag Polar"),{paired("cruise config",polarCD,polarCL)}},
        {QStringLiteral("Wind Rose"),{paired("wind",windDir,windSpeed)}},
        {QStringLiteral("Polar Histogram"),{paired("wind",windDir,windSpeed)}},
        {QStringLiteral("Great Circle Route"),{column("longitude",routeLon),
                                               column("latitude",routeLat)}},
        {QStringLiteral("Geo Line"),{column("longitude",pathLon),
                                     column("latitude",pathLat)}},
        {QStringLiteral("Ground Track"),{column("longitude",trackLon),
                                         column("latitude",trackLat)}},
        {QStringLiteral("Geo Scatter"),{column("longitude",siteLon),
                                        column("latitude",siteLat)}},
        {QStringLiteral("Geo Bubble"),{column("longitude",siteLon),
                                       column("latitude",siteLat),
                                       column("catch (t)",siteWeight)}},
        {QStringLiteral("Geo Density"),{column("longitude",siteLon),
                                        column("latitude",siteLat)}},
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
        {QStringLiteral("Confusion Matrix"),{column("true",trueClass),
                                             column("predicted",predictedClass)}},
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
        // A value and the uncertainty on it: two columns, which is also the
        // column plan the engine now declares.
        {QStringLiteral("Error Bar"),{column("measurement",measuredValue),
                                      column("uncertainty",measuredError)}},
        // The category-shaped engines. One mark per category is the whole
        // form of each of these, and 240 continuous rows is not that.
        //
        // NO category column. `column` already numbers its rows 0, 1, 2 ... in
        // .x, which is what the category IS here - so passing the index as a
        // column as well added a series whose values are 0 to 7 and drew it
        // alongside the measurements: a bar chart with a staircase of bars
        // climbing through it, and a stem plot with a second set of stems that
        // are the row numbers. The mistake is the pie's, one engine along: a
        // category is a position, not a measurement.
        {QStringLiteral("Bar"),{column("measured",categoryValue),
                                column("predicted",categorySecond)}},
        {QStringLiteral("Horizontal Bar"),{column("measured",categoryValue),
                                           column("predicted",categorySecond)}},
        // Value column FIRST: drawPie reads series.first() as the amounts and
        // that series' x as the category each amount belongs to. Handed the
        // category first it drew the category INDEX as the slice sizes - a pie
        // whose slices grew 0, 1, 2 ... 7 because that is what the column was.
        // One series: the AMOUNTS, whose x is the category index - which is
        // what `column` already fills in. Handed the category column first, the
        // pie drew the category INDEX as its slice sizes, so the slices grew
        // 0, 1, 2 ... 7 because that is literally what the column was.
        {QStringLiteral("Pie"),{column("share",categoryValue)}},
        {QStringLiteral("Donut"),{column("share",categoryValue)}},
        {QStringLiteral("Stem"),{column("measured",categoryValue)}},
        {QStringLiteral("Lollipop"),{column("measured",categoryValue)}},
        // One column per PART: drawTreemap sums each series and lays the
        // totals out as tiles, so a single column is a single tile filling the
        // whole area - which is a rectangle, not a treemap. Same shape as
        // Global Sensitivity one entry above.
        {QStringLiteral("Treemap"),{column("staff",{41.0}),
                                    column("equipment",{27.0}),
                                    column("consumables",{18.0}),
                                    column("travel",{9.0}),
                                    column("overheads",{5.0})}},
        // One series per INPUT: the rewrite ranks the series against each
        // other and draws one bar for each, so a single column is a single
        // bar and a single bar is not a ranking.
        {QStringLiteral("Global Sensitivity"),{column("flow",{0.62}),
                                               column("temperature",{0.28}),
                                               column("pH",{0.45}),
                                               column("residence time",{0.11}),
                                               column("loading",{0.36})}},
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
        // Batch 19: the same edge list a third and a fourth way. Sharing it is
        // the point - a network graph, an arc diagram, an alluvial and a hive
        // plot are four readings of one object, and any disagreement between
        // them about how many nodes or edges there are is a bug in one of them.
        {QStringLiteral("Alluvial Diagram"),
            {column("from",edgeFrom),column("to",edgeTo),
             column("weight",edgeWeight)}},
        {QStringLiteral("Hive Plot"),
            {column("from",edgeFrom),column("to",edgeTo),
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
        // Batch 20. Four positions built to carry 2, 1, 0 and 0.25 bits: all
        // one symbol, an even split of two, an even split of all four, and a
        // 1/2, 1/4, 1/8, 1/8 mixture. The third must draw NOTHING, which a
        // per-cent scale or a wrong log base could not reproduce.
        {QStringLiteral("Sequence Logo"),
            {[&]{ PlotSeries s; s.label=QStringLiteral("A");
                  s.x={1,2,3,4}; s.y={80,40,20,40};
                  s.color=QColor(0x2c,0x8a,0x3e); return s; }(),
             [&]{ PlotSeries s; s.label=QStringLiteral("C");
                  s.x={1,2,3,4}; s.y={0,40,20,20};
                  s.color=QColor(0x20,0x50,0xb0); return s; }(),
             [&]{ PlotSeries s; s.label=QStringLiteral("G");
                  s.x={1,2,3,4}; s.y={0,0,20,10};
                  s.color=QColor(0xd0,0x90,0x10); return s; }(),
             [&]{ PlotSeries s; s.label=QStringLiteral("T");
                  s.x={1,2,3,4}; s.y={0,0,20,10};
                  s.color=QColor(0xc0,0x30,0x30); return s; }()}},
        // A boundary that IS the upper water line, so it must land exactly on
        // the one the engine draws from the Nernst equation, plus a second
        // boundary so the figure has something of the user's on it.
        {QStringLiteral("Pourbaix Diagram"),
            {[&]{ PlotSeries s; s.label=QStringLiteral("E = 1.229 - 0.0592 pH");
                  for(int i=0;i<=14;++i){
                      s.x.append(double(i));
                      s.y.append(1.229-0.0592*double(i));
                  }
                  s.color=QColor(0xd0,0x30,0x30); return s; }(),
             [&]{ PlotSeries s; s.label=QStringLiteral("a metal boundary");
                  for(int i=0;i<=14;++i){
                      s.x.append(double(i));
                      s.y.append(-0.44-0.1184*qMax(0.0,double(i)-9.5));
                  }
                  s.color=QColor(0x30,0x60,0xc0); return s; }()}},
        // Batch 21. Six chromosomes of lengths 100 down to 20, so the first
        // must be exactly five times the height of the last; numbers 2 and 3
        // are the same length pinched at 50% and 10%, so the constriction is
        // visibly read from the data rather than placed at a fixed fraction.
        {QStringLiteral("Karyotype Ideogram"),
            {column("chromosome",karyoChrom),column("start",karyoFrom),
             column("end",karyoTo),column("stain",karyoStain)}},
        // Four segments of extent 400, 200, 200 and 100, and two heavy links
        // placed start-to-start and end-to-end between the first and the third:
        // they must attach at OPPOSITE ends of both arcs, which a plot that
        // ignored position and attached to the segment would draw as two
        // identical chords.
        {QStringLiteral("Circos Plot"),
            {column("from",circosFromSeg),column("from pos",circosFromPos),
             column("to",circosToSeg),column("to pos",circosToPos),
             column("weight",circosWeight)}},
        {QStringLiteral("QAPF Diagram (Plutonic)"),
            {column("Q",qapfQ),column("A",qapfA),column("P",qapfP),column("F",qapfF)}},
        {QStringLiteral("QAPF Diagram (Volcanic)"),
            {column("Q",qapfvQ),column("A",qapfvA),column("P",qapfvP),
             column("F",qapfvF)}},
        {QStringLiteral("Soil Texture Triangle (UK)"),
            {column("sand",soilSand),column("silt",soilSilt),column("clay",soilClay)}},
        {QStringLiteral("Soil Texture Triangle (USDA)"),
            {column("sand",soilSand),column("silt",soilSilt),column("clay",soilClay)}},
        {QStringLiteral("Dalitz Plot"),
            {column("m2(12)",dalitzA),column("m2(23)",dalitzB),
             column("M",dalitzM),column("m1",dalitz1),
             column("m2",dalitz2),column("m3",dalitz3)}},
        {QStringLiteral("Alignment Nomogram"),
            {column("u",nomoU),column("v",nomoV),column("w",nomoW)}},
        // Batch 18.
        {QStringLiteral("Ternary Contour"),
            {column("A",mixA),column("B",mixB),column("C",mixC),
             column("value",mixValue)}},
        {QStringLiteral("Tripartite Response Spectrum"),
            {[&]{ PlotSeries s; s.label=QStringLiteral("design spectrum");
                  s.x=spectrumPeriod; s.y=spectrumVelocity; return s; }()}},
        // The 3-D vector field, six columns. See the note where it is built:
        // handed the shared five these five engines drew an empty cube each.
        {QStringLiteral("3D Quiver"),
            {column("x",fieldX),column("y",fieldY),column("z",fieldZ),
             column("u",fieldU),column("v",fieldV),column("w",fieldW)}},
        {QStringLiteral("Cone Plot"),
            {column("x",fieldX),column("y",fieldY),column("z",fieldZ),
             column("u",fieldU),column("v",fieldV),column("w",fieldW)}},
        {QStringLiteral("Stream Tube"),
            {column("x",fieldX),column("y",fieldY),column("z",fieldZ),
             column("u",fieldU),column("v",fieldV),column("w",fieldW)}},
        {QStringLiteral("Stream Ribbon"),
            {column("x",fieldX),column("y",fieldY),column("z",fieldZ),
             column("u",fieldU),column("v",fieldV),column("w",fieldW)}},
        {QStringLiteral("Tensor Glyph Field"),
            {column("x",fieldX),column("y",fieldY),column("z",fieldZ),
             column("u",fieldU),column("v",fieldV),column("w",fieldW)}},
        // The 2-D vector field. See the note where it is built: the shared
        // columns gave these a field that never changes sign.
        {QStringLiteral("Quiver Field"),
            {column("x",flowX),column("y",flowY),
             column("u",flowU),column("v",flowV)}},
        {QStringLiteral("Feather"),
            {column("x",flowX),column("y",flowY),
             column("u",flowU),column("v",flowV)}},
        {QStringLiteral("Stream Field"),
            {column("x",flowX),column("y",flowY),
             column("u",flowU),column("v",flowV)}},
        {QStringLiteral("Stream Particles"),
            {column("x",flowX),column("y",flowY),
             column("u",flowU),column("v",flowV)}},
        {QStringLiteral("Phase Portrait"),
            {column("x",flowX),column("y",flowY),
             column("u",flowU),column("v",flowV)}},
        {QStringLiteral("Flow Texture (LIC)"),
            {column("x",flowX),column("y",flowY),
             column("u",flowU),column("v",flowV)}},
        {QStringLiteral("Divergence Map"),
            {column("x",curlX),column("y",curlY),
             column("u",curlU),column("v",curlV)}},
        {QStringLiteral("Vorticity Map"),
            {column("x",curlX),column("y",curlY),
             column("u",curlU),column("v",curlV)}},
        // The scalar volume. See the note where it is built: the shared
        // columns gave these a binary field with nothing to reconstruct.
        {QStringLiteral("Volume Show"),
            {column("x",volX),column("y",volY),column("z",volZ),column("density",volV)}},
        {QStringLiteral("Volume Slice"),
            {column("x",volX),column("y",volY),column("z",volZ),column("density",volV)}},
        {QStringLiteral("Isosurface"),
            {column("x",volX),column("y",volY),column("z",volZ),column("density",volV)}},
        {QStringLiteral("Isonormals"),
            {column("x",volX),column("y",volY),column("z",volZ),column("density",volV)}},
        {QStringLiteral("Isocaps"),
            {column("x",volX),column("y",volY),column("z",volZ),column("density",volV)}},
        {QStringLiteral("Contour Slice"),
            {column("x",volX),column("y",volY),column("z",volZ),column("density",volV)}},
    };
    in.a=a; in.b=b; in.c=c; in.d=d; in.e=e;
    in.shaped=shaped;
    in.shapedAxes=shapedAxes;
    return in;
}

// Renders every supported engine and reports which of them drew nothing.
//
// The vector-export test above proves one figure exports correctly. This proves
// the other thirty do not crash, do not hang, and put ink on the page - which
// is the failure mode that matters for an engine expressed as a rewrite: a
// wrong transformation produces an empty plot, not a compile error, and nobody
// notices until they try to use that catalogue entry.
bool runEngineSweep(const QString& galleryDir){
    const SweepInputs in=sweepInputs();
    // One folder, made once. A gallery that half-wrote because the directory
    // did not exist would be worse than none: the missing engines would look
    // like engines that drew nothing.
    if(!galleryDir.isEmpty()&&!QDir().mkpath(galleryDir)){
        fprintf(stderr,"selftest: cannot create gallery directory %s\n",
                qPrintable(galleryDir));
        return false;
    }
    int galleryIndex=0;
    const PlotSeries &a=in.a,&b=in.b,&c=in.c,&d=in.d,&e=in.e;
    (void)a;(void)b;(void)c;(void)d;(void)e;
    const QHash<QString,QVector<PlotSeries>>& shaped=in.shaped;
    const QHash<QString,ShapedAxes>& shapedAxes=in.shapedAxes;
    QtPlotBackend backend;
    const QStringList engines=backend.supportedEngines();
    QStringList blank;
    // Engines that draw exactly what a plain Line Chart of the same input
    // draws. See the comparison in the loop below.
    QStringList inert;
    // Engines whose picture changes when the QHash seed does. See the check in
    // the loop below.
    QStringList unstable;
    // Every engine's picture, keyed by what it looks like, so engines that
    // draw the SAME picture as each other can be found. See the report below.
    QMultiHash<QByteArray,QString> byPicture;

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
    auto changedPixels=[](const QImage& before,const QImage& after){
        qint64 n=0;
        for(int y=0;y<before.height();++y){
            const QRgb* pa=reinterpret_cast<const QRgb*>(before.constScanLine(y));
            const QRgb* pb=reinterpret_cast<const QRgb*>(after.constScanLine(y));
            for(int x=0;x<before.width();++x) if(pa[x]!=pb[x]) ++n;
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


    for(const QString& engine:engines){
        // Named BEFORE it is rendered, and to stderr, which is unbuffered even
        // when stdout has been redirected to a file.
        //
        // This sweep can be killed outright rather than merely fail - a
        // rewrite that recurses overflows the stack, and the process dies with
        // no verdict and no clue which engine it was on. Printing the name
        // first means the last line of the log is the culprit, whatever
        // happens next. It costs one line per engine on a run that renders
        // several hundred figures.
        fprintf(stderr,"selftest: rendering %s\n",qPrintable(engine));
        PlotSpec spec;
        spec.engine=engine;
        spec.title=engine;
        // THE AXIS NAMES BELONG TO THE DATA THE ENGINE IS ACTUALLY GIVEN.
        //
        // Both labels were set to the shared time base for every engine,
        // including the ones with a fixture of their own - so the drag polar
        // came out with a drag coefficient on an axis labelled "time_h" and a
        // lift coefficient on one labelled "value", and the V-n diagram
        // labelled its airspeed the same way. The engines were not at fault:
        // each of them sets its own axis names only `if(label.isEmpty())`,
        // which is right, because in the application the label is the name of
        // the column the person mapped and an engine should not overwrite it.
        // The sweep was supplying a name for a column it had not supplied.
        //
        // A shaped fixture is the engine's OWN data, so it is left unlabelled
        // and the engine's names stand. Only the engines running on the shared
        // columns get the shared names.
        const bool ownData=shaped.contains(engine);
        if(!ownData){
            spec.xAxis.label=QStringLiteral("time_h");
            spec.yAxis.label=QStringLiteral("value");
        }else if(shapedAxes.contains(engine)){
            spec.xAxis.label=shapedAxes.value(engine).x;
            spec.yAxis.label=shapedAxes.value(engine).y;
        }
        spec.series=ownData?shaped.value(engine)
                          :QVector<PlotSeries>{a,b,c,d,e};
        // Whatever constants the engine declares, at their defaults - which is
        // the state the application starts every figure in. An engine whose
        // painter reads a parameter must draw something with nothing but its
        // own defaults, or a person who has just chosen it sees an empty frame.
        spec.parameters=QtPlotBackend::engineParameterDefaults(engine);
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

        // IS THIS FIGURE REPRODUCIBLE?
        //
        // Rendered a second time with a different QHash seed. Qt randomises
        // that seed per process, so an engine that iterates a QHash to decide
        // what to draw - or in what order - produces a different picture on
        // every run, and nothing in a single run can see it.
        //
        // It was found by accident: one figure in a 434-figure gallery had a
        // different checksum between two runs of the same binary, and it turned
        // out the hexbin painter iterated its cell counts straight out of a
        // QHash. Neighbouring hexagons share an edge and, with antialiasing,
        // the shared pixels belong to whichever was drawn last.
        //
        // A figure that is not reproducible cannot be compared with itself,
        // which is most of what this sweep does - and a person re-exporting a
        // figure for a paper should get the file they had before. Two renders
        // per engine is a second of the sweep's minute.
        {
            QHashSeed::setDeterministicGlobalSeed();
            const QImage again=imageOf(spec);
            QHashSeed::resetRandomGlobalSeed();
            if(again.size()==drawn.size()&&again!=drawn)
                unstable.append(engine);
        }

        // What does this engine's picture look like, ignoring its title?
        //
        // The title is the one thing guaranteed to differ between engines - the
        // sweep sets it to the engine name - so it is cropped off before the
        // picture is hashed. Everything else, including the axis labels and the
        // legend, is part of what an engine is entitled to differ in.
        //
        // 34 px covers the title band at this canvas size. Cropping rather than
        // re-rendering with an empty title keeps this free: the image is
        // already in hand.
        {
            const QImage body=drawn.copy(0,34,drawn.width(),drawn.height()-34);
            const QByteArray bits(reinterpret_cast<const char*>(body.constBits()),
                                  int(body.sizeInBytes()));
            byPicture.insert(QCryptographicHash::hash(bits,QCryptographicHash::Md5),
                             engine);
        }

        // Does this engine actually do anything?
        //
        // The sweep asks whether an engine put ink on the page, and an engine
        // whose rewrite never fires still does: the spec falls through
        // prepareSpecCore unchanged and the generic line painter draws the
        // input columns. The result is a real figure with the right title,
        // which is why such an engine passes every build.
        //
        // That is not hypothetical. `Gompertz H₂ Kinetics` and `L'Abbé Plot`
        // both compared their names with QLatin1String against UTF-8 literals,
        // so neither branch could ever match, and both rendered as an ordinary
        // line chart of the sweep's fixture columns - pixel for pixel the same
        // picture as each other, differing only in the title. Nothing noticed
        // for as long as they had existed.
        //
        // So: draw the same series as a plain Line Chart and compare. The
        // title is carried over deliberately, so the two images differ only
        // where the ENGINE differs. An engine that matches is doing nothing
        // its own painter or rewrite was supposed to do.
        //
        // Pre-filtered on the prepared engine name, which costs nothing: an
        // engine that rewrote to different geometry cannot be inert, so only
        // the ones that came through unchanged are worth a second render.
        if(engine!=QLatin1String("Line Chart")
           &&backend.preparedFor(spec).engine==engine){
            PlotSpec plain=spec;
            plain.engine=QStringLiteral("Line Chart");
            if(drawn==imageOf(plain)) inert.append(engine);
        }

        // Saved before the verdict, deliberately. An engine that draws nothing
        // is exactly the one somebody wants to look at, so the blank picture
        // is as much worth keeping as the good ones - and the file being
        // present but empty says something different from the file missing.
        //
        // Numbered in sweep order so the folder sorts the way the log reads,
        // and the name sanitised because engines are called things like
        // "4D / 5D Scatter" and "QAPF Diagram (Plutonic)".
        if(!galleryDir.isEmpty()){
            ++galleryIndex;
            QString safe=engine;
            safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]+")),
                         QStringLiteral("-"));
            while(safe.endsWith(QLatin1Char('-'))) safe.chop(1);
            const QString file=QStringLiteral("%1/%2_%3.png")
                                   .arg(galleryDir)
                                   .arg(galleryIndex,3,10,QLatin1Char('0'))
                                   .arg(safe);
            if(!drawn.save(file,"PNG"))
                fprintf(stderr,"selftest: could not write %s\n",qPrintable(file));
        }

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
    // Asserted, now that the list has been read.
    //
    // This arrived as a report, because every exception list in this file was
    // built by printing what a check found and deciding which entries were
    // faults and which were the design. The first run with the Gompertz and
    // L'Abbé branches repaired came back with the list EMPTY - no engine in
    // the catalogue renders as a plain line chart of its own input - so there
    // is no exception list to keep and nothing to weigh up. An engine that
    // joins this list has stopped doing whatever it was written to do.
    // Engines that draw the same picture AS EACH OTHER.
    //
    // The inert check above compares every engine against a plain Line Chart,
    // which catches an engine whose rewrite never fired. It cannot see a group
    // of engines that all rewrite to the same thing - and there are such
    // groups. The six volume-family entries share one branch:
    //
    //     const bool volume = engine.startsWith("Volume")
    //                      || engine.startsWith("Iso")
    //                      || engine=="Contour Slice";
    //
    // so Volume Show, Volume Slice, Isosurface, Isonormals, Isocaps and
    // Contour Slice render identically by construction. Somebody choosing
    // "Isosurface" gets a point cloud and has no way to learn that this backend
    // draws all six the same way.
    //
    // Some clusters are the design and are documented as such - Comet and
    // Animated Line both draw the settled trace because a still figure cannot
    // animate; Dot Plot and Strip Plot are deliberately the same points. So
    // this reports rather than fails: the point is to know the full list, and
    // then to decide for each cluster whether it is a missing implementation or
    // a limitation that ought to be written down where a user can see it.
    {
        QVector<QStringList> clusters;
        for(const QByteArray& key:QSet<QByteArray>(byPicture.keyBegin(),byPicture.keyEnd())){
            QStringList names=byPicture.values(key);
            if(names.size()<2) continue;
            std::sort(names.begin(),names.end());
            clusters.append(names);
        }
        std::sort(clusters.begin(),clusters.end(),
                  [](const QStringList& lhs,const QStringList& rhs){
                      if(lhs.size()!=rhs.size()) return lhs.size()>rhs.size();
                      return lhs.first()<rhs.first();
                  });
        int engineCount=0;
        for(const QStringList& group:clusters) engineCount+=group.size();

        // Clusters that have been looked at, with the finding.
        //
        // Eighteen lines of identical output every build is a list nobody
        // re-reads, and a NEW cluster joining it is invisible. So each one that
        // has been adjudicated carries its reason here and is counted
        // separately. Nothing is hidden: a quiet check is a less useful one,
        // and the reason is the part worth reading anyway.
        //
        // Matched on the FULL membership, joined. That is deliberate: if an
        // engine joins or leaves a cluster, the finding below no longer
        // describes what is on the page, so the cluster stops matching and
        // comes back as unadjudicated - which is the right answer, because
        // somebody has to look again.
        //
        // Same rule as the order-free list: each of these was written after
        // reading the report, not before.
        struct Adjudicated { const char* members; const char* finding; };
        // Empty, and that is the finding. The one entry that used to sit here
        // adjudicated Geo Line and Ground Track as the same picture "because
        // these longitudes span about three degrees, so nothing crosses the
        // antimeridian". The observation was right and the conclusion was
        // wrong: the reason those longitudes spanned three degrees is that the
        // geographic engines had no fixture and were being handed the shared
        // `signal` column as a longitude. Giving them coordinates separated
        // them at once - and separated Great Circle Route, which had been
        // identical to both to within 0.002 grey levels out of 255.
        //
        // An adjudication that explains why a figure cannot show what its
        // engine does is a note that the DEMONSTRATION is broken, not a reason
        // to stop counting the pair.
        // A QVector, not a C array. `Adjudicated kAdjudicated[]={}` is a
        // zero-size array, which is a GCC extension and an ERROR under
        // -Wpedantic: the cloud harness compiles with -w and let it through,
        // and the first strict build would have failed on a line whose whole
        // content is "there is nothing to excuse". An empty table has to be
        // expressible without a dialect extension, or the table can never be
        // emptied.
        static const QVector<Adjudicated> kAdjudicated{};

        QSet<QString> adjudicatedKeys;
        for(const Adjudicated& entry:kAdjudicated)
            adjudicatedKeys.insert(QString::fromLatin1(entry.members));

        int open=0;
        QSet<QString> seenKeys;
        for(const QStringList& group:clusters){
            const QString key=group.join(QStringLiteral(" = "));
            seenKeys.insert(key);
            if(!adjudicatedKeys.contains(key)) ++open;
        }

        printf("selftest: %d group(s) of engines render the same picture as each "
               "other, covering %d engines; %d adjudicated, %d still to decide\n",
               int(clusters.size()),engineCount,int(clusters.size())-open,open);
        for(const QStringList& group:clusters){
            const QString key=group.join(QStringLiteral(" = "));
            const char* finding=nullptr;
            for(const Adjudicated& entry:kAdjudicated)
                if(key==QLatin1String(entry.members)){ finding=entry.finding; break; }
            if(finding) printf("  same-picture: %s  [%s]\n",qPrintable(key),finding);
            else        printf("  same-picture: %s  [UNADJUDICATED]\n",qPrintable(key));
        }

        // A finding for a cluster that no longer exists asserts nothing and
        // reads as though it does - the same failure the order-free list is
        // checked for. It is how a note about Fill Between survives the day
        // Fill Between stops being an alias.
        QStringList stale;
        for(const Adjudicated& entry:kAdjudicated){
            const QString key=QString::fromLatin1(entry.members);
            if(!seenKeys.contains(key)) stale.append(key);
        }
        if(!stale.isEmpty()){
            std::sort(stale.begin(),stale.end());
            printf("selftest: FAILED - the same-picture findings describe %d "
                   "cluster(s) that no longer render alike: %s\n",
                   int(stale.size()),qPrintable(stale.join(QStringLiteral("; "))));
            return false;
        }
    }

    printf("selftest: %d engine(s) render exactly as a plain Line Chart of the "
           "same data\n",int(inert.size()));
    for(const QString& name:inert) printf("  inert: %s\n",qPrintable(name));
    if(!inert.isEmpty()){
        printf("selftest: FAILED - those engine(s) draw exactly what a plain "
               "Line Chart of their own input draws, which is what an engine "
               "whose rewrite never fires looks like\n");
        return false;
    }
    if(!galleryDir.isEmpty())
        printf("selftest: gallery written to %s (%d figures)\n",
               qPrintable(galleryDir),galleryIndex);
    if(!blank.isEmpty()){
        printf("selftest: ENGINES DREW (almost) NOTHING: %s\n",qPrintable(blank.join(QStringLiteral(", "))));
        return false;
    }
    if(!unstable.isEmpty()){
        printf("selftest: FAILED - %d engine(s) draw a DIFFERENT picture when the "
               "QHash seed changes, so their output is not reproducible: %s\n",
               int(unstable.size()),qPrintable(unstable.join(QStringLiteral(", "))));
        return false;
    }
    printf("selftest: every engine's picture survives a change of QHash seed\n");
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

    // ------------------------------------------------- 1b. engine parameters
    // Constants an engine declares rather than reads from a column - see
    // QtPlotBackend::engineParameters. Three things have to hold, and the
    // first two are what a hand-wired setting gets wrong:
    //
    //   the value must REACH the engine (a Dalitz drawn with a lighter parent
    //   has a smaller region, so the picture must change);
    //   the prepared-figure cache must notice it changed, or the control does
    //   nothing until something else forces a rebuild - which is exactly the
    //   fault the field-estimator settings had;
    //   and an engine that declares none must be unaffected by the mechanism
    //   existing at all.
    {
        QtPlotBackend backend;
        PlotSpec dalitz=whiteSpec(QStringLiteral("Dalitz Plot"));
        // Two columns only: the masses come from the parameters, which is the
        // whole point of the check. A grid over the region of a decay to three
        // massless daughters with M = 1.
        PlotSeries x,y;
        x.label=QStringLiteral("m2(12)"); y.label=QStringLiteral("m2(23)");
        for(int i=1;i<20;++i)
            for(int j=1;j<20;++j){
                x.x.append(0.0); x.y.append(double(i)/22.0);
                y.x.append(0.0); y.y.append(double(j)/22.0);
            }
        for(int i=0;i<x.y.size();++i){ x.x[i]=double(i); y.x[i]=double(i); }
        dalitz.series={x,y};
        dalitz.parameters=QtPlotBackend::engineParameterDefaults(QStringLiteral("Dalitz Plot"));
        if(dalitz.parameters.size()!=4)
            failures.append(QStringLiteral("Dalitz Plot: declared %1 parameters, expected 4")
                                .arg(dalitz.parameters.size()));

        PlotSpec light=dalitz;
        light.parameters.insert(QStringLiteral("parentMass"),
                                0.55*dalitz.parameter(QStringLiteral("parentMass"),1.0));
        const QImage full1=renderToImage(backend,dalitz);
        const QImage lighter=renderToImage(backend,light);
        const QImage full2=renderToImage(backend,dalitz);
        if(full1==lighter)
            failures.append(QStringLiteral("Dalitz Plot: halving the parent mass drew an "
                                           "identical figure (parameter never reached the "
                                           "engine, or the cache is stale)"));
        if(full1!=full2)
            failures.append(QStringLiteral("Dalitz Plot: the same parameters rendered "
                                           "differently (parameter cache is not stable)"));

        // An engine that declares nothing must declare nothing. A default map
        // that quietly gained entries would put keys into every spec and into
        // the fingerprint of every figure in the catalogue.
        if(!QtPlotBackend::engineParameters(QStringLiteral("Line Chart")).isEmpty())
            failures.append(QStringLiteral("Line Chart: declares engine parameters and "
                                           "should not"));
    }

    // ------------------------------------------------------- 1c. the camera
    // Turning a 3-D figure has to change the picture, and the prepared-figure
    // cache must not be able to hide it.
    //
    // This is the fault that made rotation and zoom look broken in the
    // application: the painter draws the PREPARED spec, the prepared spec comes
    // from a cache keyed on a fingerprint that does not include the camera, and
    // nothing put the caller's camera back on top of the cached copy. So every
    // frame of a drag redrew the figure at the angle it had been prepared at.
    // The cache was never invalidated by the drag itself - only by the point
    // count changing when draft thinning switched off at the end of it, which
    // is why the figure appeared to snap between a few fixed faces.
    //
    // Rendered through ONE backend on purpose. A fresh backend per render has
    // an empty cache and passes this check no matter how broken the caching is,
    // which is exactly how the fault survived a probe that looked at nothing
    // but the painters.
    {
        QVector<double> gx,gy,gz;
        for(int i=0;i<40;++i)
            for(int j=0;j<40;++j){
                const double u=-3.0+6.0*i/39.0,v=-3.0+6.0*j/39.0;
                gx.append(u); gy.append(v);
                gz.append(std::exp(-(u*u+v*v)/4.0)*std::cos(u*1.5));
            }
        const QStringList projected{
            QStringLiteral("3D Topography / Surface"),QStringLiteral("3D Scatter"),
            QStringLiteral("3D Line"),QStringLiteral("Surface + Contours"),
            QStringLiteral("Ribbon")};
        for(const QString& engine:projected){
            QtPlotBackend backend;
            PlotSpec home=whiteSpec(engine);
            PlotSeries sx,sy,sz;
            sx.label=QStringLiteral("x"); sy.label=QStringLiteral("y"); sz.label=QStringLiteral("z");
            sx.y=gx; sy.y=gy; sz.y=gz;
            for(int i=0;i<gx.size();++i){
                sx.x.append(double(i)); sy.x.append(double(i)); sz.x.append(double(i));
            }
            home.series={sx,sy,sz};
            home.view3d.azimuth=-35.0; home.view3d.elevation=24.0; home.view3d.zoom=1.0;

            PlotSpec turned=home;  turned.view3d.azimuth=40.0;
            PlotSpec tilted=home;  tilted.view3d.elevation=-15.0;
            PlotSpec closer=home;  closer.view3d.zoom=1.8;

            const QImage a=renderToImage(backend,home);
            const QImage b=renderToImage(backend,turned);
            const QImage c=renderToImage(backend,tilted);
            const QImage d=renderToImage(backend,closer);
            const QImage back=renderToImage(backend,home);
            if(a==b)
                failures.append(QStringLiteral("%1: turning the camera drew an identical "
                                               "figure (the cached prepared spec still "
                                               "carries the old angle)").arg(engine));
            if(a==c)
                failures.append(QStringLiteral("%1: tilting the camera drew an identical "
                                               "figure").arg(engine));
            if(a==d)
                failures.append(QStringLiteral("%1: zooming the camera drew an identical "
                                               "figure").arg(engine));
            if(a!=back)
                failures.append(QStringLiteral("%1: returning to the first angle drew a "
                                               "different figure").arg(engine));

            // And the same while thinned, because that is what a drag actually
            // renders: draft mode is on for every frame between press and
            // release, and it is the frames DURING the gesture that were
            // frozen.
            backend.setDraft(true);
            const QImage da=renderToImage(backend,home);
            const QImage db=renderToImage(backend,turned);
            backend.setDraft(false);
            if(da==db)
                failures.append(QStringLiteral("%1: turning the camera drew an identical "
                                               "figure in draft mode - which is every "
                                               "frame of a drag").arg(engine));
        }
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
    printf("selftest: regression checks passed (prepared-spec cache, engine parameters, "
           "3-D camera, bar positions, horizontal bars, waterfall, correlation agreement)\n");
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


// ======================================================================
// Property checks
//
// Everything above this line tests one engine at a time against an answer
// someone worked out. That does not scale to 434 engines, and the engines
// nobody has checked are exactly the ones a defect survives in.
//
// These ask questions that need no knowledge of what an engine means, so they
// can be asked of all of them at once:
//
//   ORDER      shuffle the rows. A picture with no notion of row order must be
//              byte-identical. This is the bar-position defect asked
//              mechanically: drawBar placed bar i at slot i and ignored its x
//              value entirely, so two datasets with the same values in a
//              different order drew the same picture when they should not have,
//              and one row moved to the end drew a different one when it should
//              not have either.
//
//   FLAT       a column that never varies. Every engine must survive it without
//              producing non-finite geometry - Qt says so out loud, and this
//              listens for it. The Voronoi ramp divided by a spread of zero and
//              painted every cell NaN; a regular lattice of sample sites is a
//              survey grid, not a corner case.
//
//   EDGE       nothing painted hard against the edge of the canvas. A figure
//              that touches the frame is a figure with something cut off it,
//              which is what the streamgraph's band name, the sequence logo's
//              outer stacks and the Piper's misanchored labels all looked like.
//
// The exception lists are not excuses. Each names engines for which the
// property is FALSE BY DEFINITION - a streamgraph's stacking order is its row
// order, a Circos link is a row - and each was written after reading the
// report, not before.
namespace {

// Qt complains about NaN geometry rather than crashing on it, so the warning is
// the only signal there is. Collected rather than printed: the point is to
// attribute each one to the engine that caused it.
QStringList g_qtWarnings;
QtMessageHandler g_previousHandler=nullptr;
void collectQtMessage(QtMsgType type,const QMessageLogContext& ctx,const QString& text){
    if(type==QtWarningMsg||type==QtCriticalMsg||type==QtFatalMsg) g_qtWarnings.append(text);
    if(g_previousHandler) g_previousHandler(type,ctx,text);
}

// Ink in the outermost ring of the canvas, by side. Two pixels, because
// antialiasing puts a faint edge on anything ending exactly at the boundary.
//
// The BOTTOM is not asked about. Every painter in this catalogue writes its
// caption on the last line of the target rectangle - "26 rocks across 26
// fields", "thickness is the value; there is no y scale" - so ink two pixels
// from the bottom is the house style rather than a figure running off the page.
// The other three sides have no such convention: a label against the right edge
// is a label with its end cut off.
QString edgesTouched(const QImage& img){
    const int ring=2;
    const auto dark=[&](int x,int y){
        const QRgb p=img.pixel(x,y);
        return qRed(p)<235||qGreen(p)<235||qBlue(p)<235;
    };
    bool left=false,right=false,top=false,bottom=false;
    for(int x=0;x<img.width();++x)
        for(int y=0;y<ring;++y)
            if(dark(x,y)) top=true;
    for(int y=0;y<img.height();++y)
        for(int x=0;x<ring;++x){
            if(dark(x,y)) left=true;
            if(dark(img.width()-1-x,y)) right=true;
        }
    // THE BOTTOM WAS NOT CHECKED AT ALL, and the bottom is where the x tick
    // labels, the axis note and every caption live - the likeliest edge of the
    // four to be overrun. It was dropped deliberately: asking about all four
    // named seventeen engines, asking about three named one, and the smaller
    // number was recorded as the catalogue being "in better shape than the
    // first pass suggested". That conclusion was drawn from having stopped
    // looking. All four sounding charts then ran their axis note off the
    // bottom of the canvas and survived three visual-pass rounds and every
    // property run.
    //
    // ONE ROW, NOT TWO, and that is what makes it assertable. Measured over
    // the whole gallery, eleven figures deliberately sit a footnote flush
    // against the bottom margin and put ink in the second-from-last row -
    // tight, complete, and not a fault. None of them reaches the LAST row. The
    // four clipped soundings all did, by 7 to 25 pixels. So the final row
    // separates "cut off" from "flush" exactly, fires on the real fault, and
    // is silent on the eleven.
    for(int x=0;x<img.width();++x)
        if(dark(x,img.height()-1)) bottom=true;
    QStringList sides;
    if(left) sides.append(QStringLiteral("left"));
    if(right) sides.append(QStringLiteral("right"));
    if(top) sides.append(QStringLiteral("top"));
    if(bottom) sides.append(QStringLiteral("bottom"));
    return sides.join(QStringLiteral("+"));
}

} // namespace

bool runPropertyChecks(bool report){
    const SweepInputs in=sweepInputs();
    QtPlotBackend probe;
    const QStringList engines=probe.supportedEngines();
    QStringList failures;

    // Row order is asserted on the engines whose answer CANNOT depend on it,
    // and reported on the rest.
    //
    // Invariance is not a property of the catalogue as a whole and pretending
    // otherwise would make this check noise: a line chart joins consecutive
    // rows, an area fills between them, a cumulative sum is defined by the
    // order it adds in, a streamgraph stacks in arrival order, a pie's slices
    // go round in the order they were given. Shuffling any of those asks a
    // different question of the same numbers, so an engine whose picture moves
    // when the rows are shuffled is usually answering correctly rather than
    // failing.
    //
    // Measured, 12 September 2026, over the full 434-engine catalogue: 408
    // engines were checked, 188 of them changed, and 17 of those changes are
    // asserted as defects by the set below. So a little under half the
    // catalogue legitimately depends on row order - which is why this check
    // names a specific set rather than demanding invariance of everything.
    // (The previous figure here, 178 of 395, was measured when the catalogue
    // was smaller; the proportion has held at about 46%.)
    //
    // What IS asserted is the set below: every one of them turns a column into
    // a SUMMARY - a count, a density, a quantile, a cell - and a summary that
    // moves when the rows are shuffled has read the row number for something.
    // That is the bar-position defect exactly: drawBar placed bar i at slot i
    // and never looked at its x value.
    //
    // Each name here was verified to be invariant when the list was written,
    // so a failure is a change rather than a discovery.
    // Eight of the seventeen names that used to be here matched no engine in
    // the catalogue: "Scatter", "Hexbin", "Heatmap", "Swarm Plot", "Contour",
    // "2D Density", "Raincloud Plot" and "Bubble Chart". The real engines are
    // called "4D / 5D Scatter", "Hexbin Density", "2D Heatmap", "Swarm",
    // "2D Contour", "Raincloud" and so on, so membership never matched and
    // those eight asserted nothing at all - while the run went on reporting
    // "17 of them asserted". The engines they were reaching for are exactly
    // the summary-shaped ones this check exists to protect, so a 2-D heatmap
    // that moved when the rows were shuffled would have gone straight through.
    //
    // The names are not simply corrected and re-asserted. "Scatter" was meant
    // to be "4D / 5D Scatter", and that engine DOES change when the rows are
    // shuffled - markers overdraw in the order they are painted - so adding it
    // would have turned the build red on a property it never had. An engine
    // joins this set only once a run has shown it was checked and did not
    // change, and `kOrderFreeUnknown` below makes a name that matches nothing
    // a loud failure rather than a silent pass.
    static const QSet<QString> kOrderFree{
        QStringLiteral("Histogram"),QStringLiteral("KDE Density"),
        QStringLiteral("ECDF"),QStringLiteral("Box Plot"),
        QStringLiteral("Violin Plot"),
        QStringLiteral("Correlation Matrix"),QStringLiteral("Q-Q Plot"),
        QStringLiteral("Beeswarm"),QStringLiteral("Ridgeline")};

    int orderChecked=0,flatChecked=0,edgeChecked=0;
    QStringList orderChanged,edgeTouched,flatNoisy,orderSkipped;

    // A name in the exception list that no engine answers to asserts nothing,
    // and reads in the report as though it does. Checked against the engines
    // actually being swept rather than against a second hand-kept list.
    {
        QStringList unknown;
        for(const QString& name:kOrderFree)
            if(!engines.contains(name)) unknown.append(name);
        std::sort(unknown.begin(),unknown.end());
        if(!unknown.isEmpty()){
            printf("property: FAILED - order-free list names %d engine(s) that "
                   "do not exist: %s\n",
                   int(unknown.size()),qPrintable(unknown.join(QStringLiteral(", "))));
            return false;
        }
    }

    g_qtWarnings.clear();
    g_previousHandler=qInstallMessageHandler(collectQtMessage);

    for(const QString& engine:engines){
        const QVector<PlotSeries> columns=in.forEngine(engine);
        if(columns.isEmpty()) continue;

        PlotSpec base=whiteSpec(engine);
        base.series=columns;
        base.parameters=QtPlotBackend::engineParameterDefaults(engine);

        // ---- ORDER
        //
        // Every engine that is NOT checked says so. The count alone read as
        // near-complete coverage - "checked on 408 engines" out of 434 - while
        // saying nothing about which 26 were left out or why, so an engine
        // that quietly stopped being checked looked exactly like one that
        // passed.
        if(engine.startsWith(QLatin1String("3D "))){
            // A 3-D scene is depth-sorted by the painter's algorithm, so two
            // points at the same depth may be drawn in either order and the
            // picture legitimately differs. Shuffling asks a question these
            // engines are not required to answer.
            orderSkipped.append(QStringLiteral("%1 (3-D, depth-sorted)").arg(engine));
        }else{
            PlotSpec shuffled=base;
            const int rows=shuffled.series.isEmpty()?0:int(shuffled.series.at(0).y.size());
            if(rows<8){
                orderSkipped.append(QStringLiteral("%1 (fixture has %2 rows, needs 8)")
                                        .arg(engine).arg(rows));
            }else{
                // One deterministic permutation, applied to every column
                // together: a shuffle that broke the correspondence between
                // columns would be a different dataset, not the same one in a
                // different order.
                QVector<int> order(rows);
                for(int i=0;i<rows;++i) order[i]=i;
                quint32 seed=99991u;
                for(int i=rows-1;i>0;--i){
                    seed^=seed<<13; seed^=seed>>17; seed^=seed<<5;
                    const int j=int(seed%quint32(i+1));
                    std::swap(order[i],order[j]);
                }
                bool usable=true;
                for(PlotSeries& s:shuffled.series){
                    if(s.y.size()!=rows||s.x.size()!=rows){ usable=false; break; }
                    QVector<double> x(rows),y(rows);
                    for(int i=0;i<rows;++i){ x[i]=s.x[order[i]]; y[i]=s.y[order[i]]; }
                    // x carries the row index for the column-shaped engines, so
                    // it is renumbered rather than permuted: the rows have moved,
                    // and their positions along the axis have not.
                    bool xIsIndex=true;
                    for(int i=0;i<rows;++i) if(!qFuzzyCompare(s.x[i],double(i))){ xIsIndex=false; break; }
                    s.y=y;
                    if(!xIsIndex) s.x=x;
                }
                if(!usable){
                    // A series whose x and y are different lengths cannot be
                    // permuted as one dataset.
                    orderSkipped.append(QStringLiteral("%1 (ragged columns)").arg(engine));
                }else{
                    ++orderChecked;
                    QtPlotBackend one,two;
                    if(renderToImage(one,base)!=renderToImage(two,shuffled))
                        orderChanged.append(engine);
                }
            }
        }

        // ---- FLAT
        {
            PlotSpec flat=base;
            for(PlotSeries& s:flat.series)
                for(int i=0;i<s.y.size();++i) s.y[i]=1.0;
            ++flatChecked;
            const int before=g_qtWarnings.size();
            QtPlotBackend backend;
            renderToImage(backend,flat);
            if(g_qtWarnings.size()>before)
                flatNoisy.append(QStringLiteral("%1: %2").arg(engine)
                                     .arg(g_qtWarnings.at(before)));
        }

        // ---- EDGE
        //
        // At a comfortable size, not the 480x320 the other checks use. A
        // figure crammed into a postage stamp legitimately runs out of room,
        // and asserting against that would report the size of the test canvas
        // as a defect in the engine. Ink on the edge of a 900x640 figure is
        // something the engine placed there.
        {
            ++edgeChecked;
            QtPlotBackend backend;
            const QImage wide=renderToImage(backend,base,900,640);
            const QString sides=edgesTouched(wide);
            if(!sides.isEmpty()){
                edgeTouched.append(QStringLiteral("%1 (%2)").arg(engine,sides));
                // The picture, when asked for one. A list of names says which
                // engines put ink on the edge and nothing about whether that
                // is a label hanging off the figure or a frame drawn to the
                // rim on purpose, and only the picture answers that.
                if(report&&!qEnvironmentVariableIsEmpty("GRAPHVIS_PROPERTY_DUMP")){
                    QString safe=engine;
                    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]+")),
                                 QStringLiteral("_"));
                    wide.save(qEnvironmentVariable("GRAPHVIS_PROPERTY_DUMP")
                              +QStringLiteral("/edge_")+safe+QStringLiteral(".png"));
                }
            }
        }
    }

    qInstallMessageHandler(g_previousHandler);

    if(report){
        printf("property: order-invariance checked on %d engines, %d changed "
               "(%d of them asserted)\n",
               orderChecked,int(orderChanged.size()),int(kOrderFree.size()));
        for(const QString& e:orderChanged) printf("  order: %s\n",qPrintable(e));
        printf("property: order-invariance NOT checked on %d engines\n",
               int(orderSkipped.size()));
        for(const QString& e:orderSkipped) printf("  order-skipped: %s\n",qPrintable(e));
        printf("property: flat-column checked on %d engines, %d produced warnings\n",
               flatChecked,int(flatNoisy.size()));
        for(const QString& e:flatNoisy) printf("  flat: %s\n",qPrintable(e));
        printf("property: edge checked on %d engines, %d touch the canvas edge\n",
               edgeChecked,int(edgeTouched.size()));
        for(const QString& e:edgeTouched) printf("  edge: %s\n",qPrintable(e));
    }

    for(const QString& e:orderChanged)
        if(kOrderFree.contains(e))
            failures.append(QStringLiteral("%1: reordering the rows changed the picture, "
                                           "and this engine summarises rather than "
                                           "traces them - something is reading the row "
                                           "number").arg(e));
    for(const QString& e:flatNoisy)
        failures.append(QStringLiteral("non-finite geometry on a constant column - %1").arg(e));
    for(const QString& e:edgeTouched)
        failures.append(QStringLiteral("%1: draws against the edge of the canvas").arg(e));

    if(!failures.isEmpty()){
        for(const QString& f:failures) printf("selftest: PROPERTY: %s\n",qPrintable(f));
        printf("selftest: %d property check(s) FAILED\n",int(failures.size()));
        return false;
    }
    printf("selftest: property checks passed (row order, constant columns, canvas edge)\n");
    return true;
}


} // namespace graphvis