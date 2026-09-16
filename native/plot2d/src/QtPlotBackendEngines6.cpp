// ============================================================================
// prepareSpecCore, part 6 of 6: "Cohort Retention Curve" through "Isoconversional Plot".
//
// One arm of the engine-rewrite chain that used to be a single 19,600-line
// function inside a 1.78 MB translation unit. That unit took 2 minutes 10
// seconds to compile on its own, single-threaded, on every build - a
// translation unit cannot be divided across cores, so any change to any one of
// 434 engines paid all of it.
//
// The blocks below are VERBATIM. Nothing was reworded and no `return` was
// rewritten, which is the whole reason the group returns std::optional: a
// block that used to `return out;` still says `return out;` and converts, and
// a `return` inside one of the many lambdas here still returns from its lambda
// - which a bool-and-out-parameter signature would have required me to tell
// apart by hand, 312 times.
//
// The chain is ordered, and the order is load-bearing: several engines are
// matched by a `startsWith` that a later, more specific test would also match.
// So the groups are contiguous runs of the original sequence and are called in
// sequence. Splitting it any other way would silently re-order the catalogue.
// ============================================================================
#include "QtPlotBackendShared.h"

namespace graphvis {

std::optional<PlotSpec> QtPlotBackend::prepareEngineGroup6(const PlotSpec& in) const {
    if(in.engine==QLatin1String("Cohort Retention Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=3){
            const QVector<double>& age=in.series.at(0).y;
            const QVector<double>& retained=in.series.at(1).y;
            const QVector<double>& cohort=in.series.at(2).y;
            const int n=qMin(age.size(),qMin(retained.size(),cohort.size()));
            QMap<double,QVector<QPair<double,double>>> byCohort;
            for(int i=0;i<n;++i){
                if(!finite(age[i])||!finite(retained[i])||!finite(cohort[i])) continue;
                byCohort[cohort[i]].append(qMakePair(age[i],retained[i]));
            }
            int index=0;
            QVector<double> lastValues;
            for(auto it=byCohort.constBegin();it!=byCohort.constEnd();++it,++index){
                QVector<QPair<double,double>> rows=it.value();
                std::sort(rows.begin(),rows.end(),
                          [](const QPair<double,double>& a,const QPair<double,double>& b){
                              return a.first<b.first; });
                if(rows.isEmpty()) continue;
                PlotSeries s;
                s.color=categoryColour(in,index,std::fmod(0.48+double(index)*0.12,1.0),0.6,0.95);
                s.lineWidth=qMax(1.2,in.style.lineWidth);
                s.drawMarkers=true; s.markerSize=3.4;
                for(const QPair<double,double>& r:rows){ s.x.append(r.first); s.y.append(r.second); }
                s.label=QStringLiteral("cohort %1, %2 at age %3")
                            .arg(QString::number(it.key(),'g',12))
                            .arg(rows.last().second,0,'g',4).arg(rows.last().first,0,'g',4);
                lastValues.append(rows.last().second);
                out.series.append(s);
            }
            // Whether the newer cohorts are doing better or worse than the
            // older ones, which is the question a retention chart is drawn to
            // answer and the one nobody can read off a dozen crossing lines.
            if(lastValues.size()>=2&&!out.series.isEmpty()){
                const double first=lastValues.first(),last=lastValues.last();
                out.series[0].label=QStringLiteral("%1; newest cohort ends %2 than the oldest (%3 vs %4)")
                                        .arg(out.series[0].label)
                                        .arg(last>=first?QStringLiteral("higher"):QStringLiteral("lower"))
                                        .arg(last,0,'g',4).arg(first,0,'g',4);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("periods since joining"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("retained"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------- Bass Diffusion
    // Cumulative adoption against time, with the Bass model fitted to it. The
    // model separates two things a single S-curve cannot: adoption driven by
    // people who would have bought anyway, and adoption driven by people who
    // bought because others had. It is LINEAR in three coefficients once
    // written as adoptions against cumulative adoptions and their square, so it
    // is one normal-equation solve rather than a nonlinear search.
    if(in.engine==QLatin1String("Bass Diffusion Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& time=in.series.at(0).y;
            const QVector<double>& cumulative=in.series.at(1).y;
            const int n=qMin(time.size(),cumulative.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(time[i])||!finite(cumulative[i])) continue;
                rows.append(qMakePair(time[i],cumulative[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=5){
                PlotSeries observed;
                observed.label=QStringLiteral("cumulative adopters");
                observed.color=in.series.at(1).color;
                observed.drawLine=false; observed.drawMarkers=true; observed.markerSize=3.8;
                for(const QPair<double,double>& r:rows){ observed.x.append(r.first); observed.y.append(r.second); }
                out.series.append(observed);
                // n(t) = a + b N + c N^2, where a = p m, b = q - p and
                // c = -q/m. Three sums, one 3x3 solve, and the parameters come
                // back out of the coefficients by the quadratic formula.
                double s[3][4]={};
                int used=0;
                for(int i=1;i<rows.size();++i){
                    const double span=rows[i].first-rows[i-1].first;
                    if(!(span>1e-300)) continue;
                    const double adoptions=(rows[i].second-rows[i-1].second)/span;
                    // Paired with the MIDPOINT of N over the interval, not with
                    // its value at the start. A difference quotient estimates
                    // the derivative in the middle of the interval it was taken
                    // over, so regressing it on the left endpoint - which is
                    // what the original discrete formulation of this model does
                    // - is a first-order error that grows with the sampling
                    // step. On a series built from p 0.03 and q 0.38 it came
                    // back with p 0.036 and q 0.364, and the peak six per cent
                    // early; on the midpoint it recovers all three.
                    const double middle=0.5*(rows[i].second+rows[i-1].second);
                    const double basis[3]={1.0,middle,middle*middle};
                    for(int r=0;r<3;++r){
                        for(int c=0;c<3;++c) s[r][c]+=basis[r]*basis[c];
                        s[r][3]+=basis[r]*adoptions;
                    }
                    ++used;
                }
                bool ok=(used>=4);
                for(int col=0;col<3&&ok;++col){
                    int pivot=col;
                    for(int r=col+1;r<3;++r)
                        if(std::abs(s[r][col])>std::abs(s[pivot][col])) pivot=r;
                    if(std::abs(s[pivot][col])<1e-14){ ok=false; break; }
                    if(pivot!=col) for(int c=0;c<4;++c) std::swap(s[col][c],s[pivot][c]);
                    const double d=s[col][col];
                    for(int c=0;c<4;++c) s[col][c]/=d;
                    for(int r=0;r<3;++r){
                        if(r==col) continue;
                        const double f=s[r][col];
                        for(int c=0;c<4;++c) s[r][c]-=f*s[col][c];
                    }
                }
                if(ok){
                    const double a=s[0][3],b=s[1][3],c=s[2][3];
                    // m is the positive root of c m^2 + b m + a = 0, and it has
                    // to be positive and larger than what has already been
                    // adopted or it is not a market size.
                    const double discriminant=b*b-4.0*c*a;
                    double market=std::numeric_limits<double>::quiet_NaN();
                    if(std::abs(c)>1e-300&&discriminant>=0.0){
                        const double root=std::sqrt(discriminant);
                        const double one=(-b+root)/(2.0*c),two=(-b-root)/(2.0*c);
                        market=qMax(one,two);
                        if(!(market>0.0)) market=std::numeric_limits<double>::quiet_NaN();
                    }
                    if(finite(market)&&market>1e-300){
                        const double innovation=a/market;
                        const double imitation=b+innovation;
                        PlotSeries fit;
                        // The peak of adoption, which is the date a launch plan
                        // is written around, in closed form rather than found
                        // by scanning the fitted curve.
                        const double peakOffset=(innovation>0.0&&imitation>innovation)
                            ? std::log(imitation/innovation)/(innovation+imitation)
                            : std::numeric_limits<double>::quiet_NaN();
                        fit.label=finite(peakOffset)
                            ? QStringLiteral("p %1, q %2, m %3, peak %4 after t0")
                                  .arg(innovation,0,'g',4).arg(imitation,0,'g',4)
                                  .arg(market,0,'g',5).arg(peakOffset,0,'g',4)
                            : QStringLiteral("p %1, q %2, m %3")
                                  .arg(innovation,0,'g',4).arg(imitation,0,'g',4)
                                  .arg(market,0,'g',5);
                        fit.color=in.style.warning;
                        fit.lineWidth=qMax(1.3,in.style.lineWidth);
                        const double start=rows.first().first;
                        for(int i=0;i<=140;++i){
                            const double t=start+(rows.last().first-start)*double(i)/140.0;
                            const double e=std::exp(-(innovation+imitation)*(t-start));
                            const double denom=1.0+(imitation/innovation)*e;
                            if(!(std::abs(denom)>1e-300)) continue;
                            fit.x.append(t);
                            fit.y.append(market*(1.0-e)/denom);
                        }
                        if(fit.x.size()>=2) out.series.append(fit);
                        PlotSeries ceiling;
                        ceiling.label=QStringLiteral("fitted market size");
                        ceiling.color=in.style.gridColor;
                        ceiling.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                        ceiling.dashPattern={5,4};
                        ceiling.x={rows.first().first,rows.last().first};
                        ceiling.y={market,market};
                        out.series.append(ceiling);
                    }
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("time"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("cumulative adopters"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------------------- Pareto Chart
    // Counts by category, largest first, with the running share over them. The
    // claim being tested is that a small number of causes account for most of
    // the total, so how many categories it actually takes to reach eighty per
    // cent is computed and said - rather than left as a rule of thumb that the
    // chart is assumed to demonstrate.
    if(in.engine==QLatin1String("Pareto Chart")){
        // Drawn as outlined blocks on a Line Chart rather than through the Bar
        // engine, because that engine reads each SERIES as a group and draws
        // them side by side - so a cumulative line handed to it comes back as a
        // second row of bars standing next to the first. The blocks are the
        // same construction the abatement cost curve uses.
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& category=in.series.at(0).y;
            const QVector<double>& count=in.series.at(1).y;
            const int n=qMin(category.size(),count.size());
            QVector<QPair<double,double>> rows;   // (count, category)
            double total=0.0;
            for(int i=0;i<n;++i){
                if(!finite(category[i])||!finite(count[i])||!(count[i]>0.0)) continue;
                rows.append(qMakePair(count[i],category[i]));
                total+=count[i];
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first>b.first; });
            if(!rows.isEmpty()&&total>1e-300){
                for(int i=0;i<rows.size()&&i<200;++i){
                    PlotSeries block;
                    block.label=QString();
                    block.color=in.series.at(1).color;
                    block.lineWidth=1.0;
                    block.fillClosed=true;
                    const double left=double(i+1)-0.4,right=double(i+1)+0.4;
                    block.x={left,left,right,right,left};
                    block.y={0.0,rows[i].first,rows[i].first,0.0,0.0};
                    out.series.append(block);
                }
                // The running share goes on the RIGHT-HAND ORDINATE, as a
                // per cent.
                //
                // It used to be drawn on the count axis, scaled by the total,
                // "rather than on a second ordinate this backend does not
                // have" - which put the 80% line in the right place but left
                // the reader unable to read any other share off the figure.
                // That is the number a Pareto chart exists for: the axis now
                // carries it.
                PlotSeries running;
                running.color=in.style.warning;
                running.lineWidth=qMax(1.4,in.style.lineWidth);
                running.drawMarkers=true; running.markerSize=3.6;
                running.secondaryAxis=true;
                double cumulative=0.0; int needed=0;
                for(int i=0;i<rows.size();++i){
                    cumulative+=rows[i].first;
                    running.x.append(double(i+1));
                    running.y.append(cumulative/total*100.0);
                    if(needed==0&&cumulative>=0.8*total) needed=i+1;
                }
                running.label=(needed>0)
                    ? QStringLiteral("%1 categories, total %2; %3 of them reach 80%")
                          .arg(rows.size()).arg(total,0,'g',6).arg(needed)
                    : QStringLiteral("%1 categories, total %2")
                          .arg(rows.size()).arg(total,0,'g',6);
                out.series.append(running);
                PlotSeries level;
                level.label=QStringLiteral("80% of the total");
                level.color=in.style.danger;
                level.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                level.dashPattern={5,4};
                level.secondaryAxis=true;
                level.x={0.5,double(rows.size())+0.5};
                level.y={80.0,80.0};
                out.series.append(level);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("category, ranked"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("count"),false,0.0,unsetValue()};
        out.y2Axis=PlotAxis{QStringLiteral("cumulative share (%)"),false,0.0,100.0};
        return out;
    }

    // ------------------------------------------------------ Statistical Power Curve
    // The chance of detecting an effect that is really there, against how many
    // observations are collected, one curve per effect size. This is COMPUTED
    // from the design rather than fitted to anything: the columns say which
    // sample sizes and which effects to draw, and the power at each is what the
    // two-sample t-test gives under a normal approximation.
    if(in.engine==QLatin1String("Statistical Power Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& sample=in.series.at(0).y;
            const QVector<double>& effect=in.series.at(1).y;
            const int n=qMin(sample.size(),effect.size());
            QMap<double,QVector<double>> byEffect;
            for(int i=0;i<n;++i){
                if(!finite(sample[i])||!finite(effect[i])) continue;
                if(!(sample[i]>=2.0)) continue;
                byEffect[effect[i]].append(sample[i]);
            }
            const double alpha=0.05;
            const double critical=normalQuantile(1.0-alpha/2.0);
            // The standard normal distribution function, from the error
            // function, so power and the quantile used to get the critical
            // value are consistent with each other.
            const auto probability=[](double z){ return 0.5*std::erfc(-z/std::sqrt(2.0)); };
            int index=0;
            for(auto it=byEffect.constBegin();it!=byEffect.constEnd();++it,++index){
                QVector<double> sizes=it.value();
                std::sort(sizes.begin(),sizes.end());
                sizes.erase(std::unique(sizes.begin(),sizes.end()),sizes.end());
                if(sizes.isEmpty()) continue;
                PlotSeries s;
                s.color=categoryColour(in,index,std::fmod(0.30+double(index)*0.15,1.0),0.62,0.95);
                s.lineWidth=qMax(1.3,in.style.lineWidth);
                s.drawMarkers=true; s.markerSize=3.2;
                double eighty=std::numeric_limits<double>::quiet_NaN();
                double previousSize=0.0,previousPower=0.0;
                for(double size:sizes){
                    const double shift=std::abs(it.key())*std::sqrt(size/2.0);
                    const double power=probability(shift-critical);
                    s.x.append(size); s.y.append(power);
                    if(!finite(eighty)&&previousSize>0.0
                       &&(previousPower<0.8)!=(power<0.8)&&power!=previousPower){
                        const double f=(0.8-previousPower)/(power-previousPower);
                        eighty=previousSize+f*(size-previousSize);
                    }
                    previousSize=size; previousPower=power;
                }
                s.label=finite(eighty)
                    ? QStringLiteral("d %1, 80% power at n %2 per group")
                          .arg(it.key(),0,'g',3).arg(eighty,0,'f',1)
                    : QStringLiteral("d %1 (80% power not reached in this range)")
                          .arg(it.key(),0,'g',3);
                out.series.append(s);
            }
            if(!out.series.isEmpty()){
                const Bounds bs=boundsOf(out.series.first().x);
                if(bs.valid){
                    PlotSeries level;
                    level.label=QStringLiteral("80% power (two-sided alpha 0.05)");
                    level.color=in.style.warning;
                    level.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                    level.dashPattern={5,4};
                    level.x={bs.lo,bs.hi}; level.y={0.8,0.8};
                    out.series.append(level);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("sample size per group"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("power"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------- Sequential Test Boundaries
    // The running log-likelihood ratio of the observations, against the two
    // boundaries Wald's test stops at. The point of a sequential test is that
    // it can stop EARLY in either direction, so the value that matters is where
    // the walk first touches a boundary - and if it never does, the honest
    // report is that the test is still open rather than a verdict at the end of
    // the data.
    if(in.engine==QLatin1String("Sequential Test Boundaries")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& outcome=in.series.at(1).y;
            // The two rates being told apart and the two error rates are the
            // test's design, not the data's, so they are defaults - and they
            // are on the figure, because the boundaries mean nothing without
            // them.
            const double nullRate=0.05,alternativeRate=0.15;
            const double alpha=0.05,beta=0.05;
            const double accept=std::log(beta/(1.0-alpha));
            const double reject=std::log((1.0-beta)/alpha);
            const double hit=std::log(alternativeRate/nullRate);
            const double miss=std::log((1.0-alternativeRate)/(1.0-nullRate));
            PlotSeries walk;
            walk.color=in.series.at(1).color;
            walk.lineWidth=qMax(1.4,in.style.lineWidth);
            double score=0.0;
            int stoppedAt=-1; QString verdict;
            walk.x.append(0.0); walk.y.append(0.0);
            for(int i=0;i<outcome.size();++i){
                if(!finite(outcome[i])) continue;
                score+=(outcome[i]>0.5)?hit:miss;
                walk.x.append(double(walk.x.size()));
                walk.y.append(score);
                if(stoppedAt<0&&score>=reject){
                    stoppedAt=int(walk.x.size())-1;
                    verdict=QStringLiteral("rejected the null");
                }else if(stoppedAt<0&&score<=accept){
                    stoppedAt=int(walk.x.size())-1;
                    verdict=QStringLiteral("accepted the null");
                }
            }
            if(walk.x.size()>=2){
                walk.label=(stoppedAt>=0)
                    ? QStringLiteral("%1 at observation %2 (p0 %3, p1 %4, alpha %5, beta %6)")
                          .arg(verdict).arg(stoppedAt).arg(nullRate,0,'g',3)
                          .arg(alternativeRate,0,'g',3).arg(alpha,0,'g',3).arg(beta,0,'g',3)
                    : QStringLiteral("still open after %1 observations (p0 %2, p1 %3)")
                          .arg(walk.x.size()-1).arg(nullRate,0,'g',3).arg(alternativeRate,0,'g',3);
                out.series.append(walk);
                const struct { double level; const char* name; } bounds[]={
                    {reject,"reject the null"},{accept,"accept the null"}};
                for(const auto& edge:bounds){
                    PlotSeries limit;
                    limit.label=QString::fromLatin1(edge.name);
                    limit.color=(edge.level>0.0)?in.style.danger:in.style.positive;
                    limit.lineWidth=qMax(1.0,in.style.lineWidth*0.85);
                    limit.dashPattern={5,4};
                    limit.x={0.0,walk.x.last()};
                    limit.y={edge.level,edge.level};
                    out.series.append(limit);
                }
                if(stoppedAt>=0){
                    PlotSeries mark;
                    mark.label=QStringLiteral("stopping point");
                    mark.color=in.style.warning;
                    mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.5;
                    mark.x={double(stoppedAt)}; mark.y={walk.y[stoppedAt]};
                    out.series.append(mark);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("observations"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("log-likelihood ratio"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ====================================================================
    // Batch 11: separation science, adsorption, molecular biology,
    // magnetics, drives, antennas, soil physics, long-memory statistics
    // and four operational curves.

    // ------------------------------------------------- Chromatogram Peak Metrics
    // A chromatogram with the numbers a method is validated on computed from it
    // rather than typed in from the instrument's own report: the plate count of
    // each peak, how badly it tails, and the resolution between neighbours.
    // Resolution below about 1.5 means two peaks that are not separated,
    // whatever the picture looks like at the scale it was printed at.
    if(in.engine==QLatin1String("Chromatogram Peak Metrics")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& time=in.series.at(0).y;
            const QVector<double>& signal=in.series.at(1).y;
            const int n=qMin(time.size(),signal.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(time[i])||!finite(signal[i])) continue;
                rows.append(qMakePair(time[i],signal[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=9){
                PlotSeries trace;
                trace.color=in.series.at(1).color;
                trace.lineWidth=qMax(1.3,in.style.lineWidth);
                for(const QPair<double,double>& r:rows){ trace.x.append(r.first); trace.y.append(r.second); }
                // A baseline from the quietest tenth at each end, so a drifting
                // detector does not turn the whole trace into one broad peak.
                const int edge=qMax(1,rows.size()/10);
                double leftT=0,leftS=0,rightT=0,rightS=0;
                for(int i=0;i<edge;++i){ leftT+=rows[i].first; leftS+=rows[i].second; }
                for(int i=rows.size()-edge;i<rows.size();++i){
                    rightT+=rows[i].first; rightS+=rows[i].second;
                }
                leftT/=edge; leftS/=edge; rightT/=edge; rightS/=edge;
                const double drift=(std::abs(rightT-leftT)>1e-300)
                                   ?(rightS-leftS)/(rightT-leftT):0.0;
                auto baselineAt=[&](double t){ return leftS+drift*(t-leftT); };
                const Bounds bs=boundsOf(trace.y);
                const double threshold=bs.valid?0.05*(bs.hi-bs.lo):0.0;
                // Local maxima above the baseline by more than a twentieth of
                // the full range: a fixed absolute threshold would find
                // hundreds of peaks in a noisy trace and none in a quiet one.
                QVector<int> peaks;
                for(int i=1;i+1<rows.size();++i){
                    const double excess=rows[i].second-baselineAt(rows[i].first);
                    if(!(excess>threshold)) continue;
                    if(rows[i].second<=rows[i-1].second||rows[i].second<rows[i+1].second) continue;
                    peaks.append(i);
                }
                QStringList found;
                QVector<double> retention,halfWidth;
                for(int at:peaks){
                    const double height=rows[at].second-baselineAt(rows[at].first);
                    const double level=baselineAt(rows[at].first)+0.5*height;
                    double left=std::numeric_limits<double>::quiet_NaN();
                    double right=std::numeric_limits<double>::quiet_NaN();
                    for(int i=at;i>0;--i){
                        const double a=rows[i-1].second,b=rows[i].second;
                        if((a>=level)==(b>=level)||a==b) continue;
                        left=rows[i-1].first+(level-a)/(b-a)*(rows[i].first-rows[i-1].first);
                        break;
                    }
                    for(int i=at+1;i<rows.size();++i){
                        const double a=rows[i-1].second,b=rows[i].second;
                        if((a>=level)==(b>=level)||a==b) continue;
                        right=rows[i-1].first+(level-a)/(b-a)*(rows[i].first-rows[i-1].first);
                        break;
                    }
                    if(!finite(left)||!finite(right)||!(right>left)) continue;
                    const double width=right-left;
                    // 5.54 is the half-width form of the plate count, which is
                    // the one a chromatographer quotes; the tangent form uses
                    // 16 and a different width and would give a different
                    // number for the same peak.
                    const double plates=5.54*(rows[at].first/width)*(rows[at].first/width);
                    // Asymmetry at half height: how much longer the trailing
                    // side is than the leading one.
                    const double tailing=(rows[at].first-left>1e-300)
                                         ?(right-rows[at].first)/(rows[at].first-left):0.0;
                    retention.append(rows[at].first); halfWidth.append(width);
                    found.append(QStringLiteral("tR %1 N %2 As %3")
                                     .arg(rows[at].first,0,'g',4)
                                     .arg(plates,0,'f',0).arg(tailing,0,'f',2));
                }
                // Resolution between neighbours, which is the number that says
                // whether a separation worked at all.
                double worst=std::numeric_limits<double>::infinity();
                for(int i=1;i<retention.size();++i){
                    const double sum=halfWidth[i]+halfWidth[i-1];
                    if(!(sum>1e-300)) continue;
                    // 1.18 converts the half-height widths to the baseline
                    // widths the resolution formula is defined on.
                    worst=qMin(worst,1.18*(retention[i]-retention[i-1])/sum);
                }
                trace.label=found.isEmpty()
                    ? QStringLiteral("no peaks above 5% of the range")
                    : (finite(worst)
                        ? QStringLiteral("%1 peaks, worst resolution %2; %3")
                              .arg(found.size()).arg(worst,0,'f',2).arg(found.join(QStringLiteral("; ")))
                        : QStringLiteral("%1 peak; %2").arg(found.size()).arg(found.join(QString())));
                out.series.append(trace);
                PlotSeries base;
                base.label=QStringLiteral("baseline");
                base.color=in.style.gridColor;
                base.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                base.dashPattern={5,4};
                base.x={rows.first().first,rows.last().first};
                base.y={baselineAt(rows.first().first),baselineAt(rows.last().first)};
                out.series.append(base);
                if(!retention.isEmpty()){
                    PlotSeries apex;
                    apex.label=QStringLiteral("peaks");
                    apex.color=in.style.danger;
                    apex.drawLine=false; apex.drawMarkers=true; apex.markerSize=5.5;
                    for(int at:peaks){ apex.x.append(rows[at].first); apex.y.append(rows[at].second); }
                    out.series.append(apex);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("retention time"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("detector signal"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------- Langmuir Isotherm
    // Equilibrium concentration over adsorbed amount, against equilibrium
    // concentration. Langmuir's model is a straight line in those coordinates,
    // and it gives the two things an adsorbent is specified by: how much it can
    // hold when saturated, and how strongly it holds it.
    if(in.engine==QLatin1String("Langmuir Isotherm")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& equilibrium=in.series.at(0).y;
            const QVector<double>& adsorbed=in.series.at(1).y;
            const int n=qMin(equilibrium.size(),adsorbed.size());
            PlotSeries pts;
            pts.label=QStringLiteral("Ce / qe");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.4;
            QVector<double> fx,fy;
            double highest=0.0;
            for(int i=0;i<n;++i){
                if(!finite(equilibrium[i])||!finite(adsorbed[i])) continue;
                if(!(equilibrium[i]>0.0)||!(adsorbed[i]>0.0)) continue;
                pts.x.append(equilibrium[i]); pts.y.append(equilibrium[i]/adsorbed[i]);
                fx.append(equilibrium[i]); fy.append(equilibrium[i]/adsorbed[i]);
                highest=qMax(highest,equilibrium[i]);
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                const LineFit f=fitLine(fx,fy);
                const Bounds bx=boundsOf(fx);
                if(f.ok&&bx.valid&&f.slope>0.0){
                    const double capacity=1.0/f.slope;
                    const double affinity=(f.intercept>1e-300)?f.slope/f.intercept
                                          :std::numeric_limits<double>::quiet_NaN();
                    // The separation factor at the highest concentration
                    // measured. Between zero and one is favourable adsorption,
                    // and it is the conclusion the isotherm is fitted for.
                    const double separation=finite(affinity)
                        ? 1.0/(1.0+affinity*highest)
                        : std::numeric_limits<double>::quiet_NaN();
                    PlotSeries line;
                    line.label=finite(affinity)
                        ? QStringLiteral("qmax %1, KL %2, RL %3 at Ce %4")
                              .arg(capacity,0,'g',5).arg(affinity,0,'g',4)
                              .arg(separation,0,'f',4).arg(highest,0,'g',4)
                        : QStringLiteral("qmax %1 (intercept not positive, so KL is undefined)")
                              .arg(capacity,0,'g',5);
                    line.color=in.style.warning;
                    line.lineWidth=qMax(1.3,in.style.lineWidth);
                    line.x={0.0,bx.hi};
                    line.y={f.intercept,f.intercept+f.slope*bx.hi};
                    out.series.append(line);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("equilibrium concentration"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("Ce / qe"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------------- Zeta Potential Curve
    // Surface charge against pH. Where it crosses zero the particles no longer
    // repel each other and the suspension flocculates, so that crossing is the
    // pH to avoid - or to aim for, if settling is what is wanted. The plus and
    // minus thirty millivolt lines are the conventional stability bands.
    if(in.engine==QLatin1String("Zeta Potential Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& acidity=in.series.at(0).y;
            const QVector<double>& potential=in.series.at(1).y;
            const int n=qMin(acidity.size(),potential.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(acidity[i])||!finite(potential[i])) continue;
                rows.append(qMakePair(acidity[i],potential[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=3){
                double isoelectric=std::numeric_limits<double>::quiet_NaN();
                for(int i=1;i<rows.size();++i){
                    const double a=rows[i-1].second,b=rows[i].second;
                    if((a>=0.0)==(b>=0.0)||a==b) continue;
                    isoelectric=rows[i-1].first
                               +(0.0-a)/(b-a)*(rows[i].first-rows[i-1].first);
                    break;
                }
                PlotSeries curve;
                curve.label=finite(isoelectric)
                    ? QStringLiteral("isoelectric point at pH %1").arg(isoelectric,0,'f',2)
                    : QStringLiteral("no zero crossing in this pH range");
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.4,in.style.lineWidth);
                curve.drawMarkers=true; curve.markerSize=3.8;
                for(const QPair<double,double>& r:rows){ curve.x.append(r.first); curve.y.append(r.second); }
                out.series.append(curve);
                const struct { double level; const char* name; } bands[]={
                    {30.0,"+30 mV (stable)"},{0.0,"zero charge"},{-30.0,"-30 mV (stable)"}};
                for(const auto& band:bands){
                    PlotSeries limit;
                    limit.label=QString::fromLatin1(band.name);
                    limit.color=(band.level==0.0)?in.style.danger:in.style.gridColor;
                    limit.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                    limit.dashPattern={5,4};
                    limit.x={rows.first().first,rows.last().first};
                    limit.y={band.level,band.level};
                    out.series.append(limit);
                }
                if(finite(isoelectric)){
                    PlotSeries mark;
                    mark.label=QStringLiteral("isoelectric point");
                    mark.color=in.style.warning;
                    mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.5;
                    mark.x={isoelectric}; mark.y={0.0};
                    out.series.append(mark);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("pH"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("zeta potential (mV)"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------- Distillation Curve
    // Boiling temperature against the fraction that has come over. A fuel
    // specification is written as three points on this curve rather than as a
    // boiling point, because a mixture does not have one - and the SLOPE
    // between them is what decides whether it vaporises cleanly.
    if(in.engine==QLatin1String("Distillation Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& recovered=in.series.at(0).y;
            const QVector<double>& temperature=in.series.at(1).y;
            const int n=qMin(recovered.size(),temperature.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(recovered[i])||!finite(temperature[i])) continue;
                rows.append(qMakePair(recovered[i],temperature[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=3){
                const auto at=[&](double share){
                    if(share<=rows.first().first) return rows.first().second;
                    if(share>=rows.last().first) return rows.last().second;
                    for(int i=1;i<rows.size();++i){
                        if(rows[i].first<share) continue;
                        const double x0=rows[i-1].first,x1=rows[i].first;
                        return (x1>x0)?rows[i-1].second
                                       +(share-x0)/(x1-x0)*(rows[i].second-rows[i-1].second)
                                      :rows[i].second;
                    }
                    return rows.last().second;
                };
                const double ten=at(10.0),fifty=at(50.0),ninety=at(90.0);
                PlotSeries curve;
                curve.label=QStringLiteral("T10 %1, T50 %2, T90 %3, spread %4")
                                .arg(ten,0,'f',1).arg(fifty,0,'f',1).arg(ninety,0,'f',1)
                                .arg(ninety-ten,0,'f',1);
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.4,in.style.lineWidth);
                curve.drawMarkers=true; curve.markerSize=3.4;
                for(const QPair<double,double>& r:rows){ curve.x.append(r.first); curve.y.append(r.second); }
                out.series.append(curve);
                PlotSeries marks;
                marks.label=QStringLiteral("T10, T50, T90");
                marks.color=in.style.danger;
                marks.drawLine=false; marks.drawMarkers=true; marks.markerSize=6.0;
                marks.x={10.0,50.0,90.0};
                marks.y={ten,fifty,ninety};
                out.series.append(marks);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("volume recovered (%)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("temperature"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------- qPCR Standard Curve
    // Threshold cycle against the log of the starting quantity. A perfect
    // reaction doubles every cycle, which puts the slope at exactly -3.32 -
    // and that is why the slope is reported as an EFFICIENCY here rather than
    // as a slope: -3.32 means nothing until it is read as 100%.
    if(in.engine==QLatin1String("qPCR Standard Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& quantity=in.series.at(0).y;
            const QVector<double>& threshold=in.series.at(1).y;
            const int n=qMin(quantity.size(),threshold.size());
            PlotSeries pts;
            pts.label=QStringLiteral("standards");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.6;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(quantity[i])||!finite(threshold[i])||!(quantity[i]>0.0)) continue;
                pts.x.append(std::log10(quantity[i])); pts.y.append(threshold[i]);
                fx.append(std::log10(quantity[i])); fy.append(threshold[i]);
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                const LineFit f=fitLine(fx,fy);
                const Bounds bx=boundsOf(fx);
                if(f.ok&&bx.valid&&f.slope<0.0){
                    const double efficiency=std::pow(10.0,-1.0/f.slope)-1.0;
                    double ssRes=0.0,ssTot=0.0,mean=0.0;
                    for(double v:fy) mean+=v;
                    mean/=double(fy.size());
                    for(int i=0;i<fx.size();++i){
                        const double r=fy[i]-(f.intercept+f.slope*fx[i]);
                        ssRes+=r*r; ssTot+=(fy[i]-mean)*(fy[i]-mean);
                    }
                    const double r2=(ssTot>1e-300)?1.0-ssRes/ssTot:0.0;
                    PlotSeries line;
                    line.label=QStringLiteral("slope %1, efficiency %2%, R2 %3")
                                   .arg(f.slope,0,'f',3).arg(100.0*efficiency,0,'f',1)
                                   .arg(r2,0,'f',5);
                    line.color=in.style.warning;
                    line.lineWidth=qMax(1.3,in.style.lineWidth);
                    line.x={bx.lo,bx.hi};
                    line.y={f.intercept+f.slope*bx.lo,f.intercept+f.slope*bx.hi};
                    out.series.append(line);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("log10 starting quantity"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("threshold cycle"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------------- Melting Curve
    // Fluorescence against temperature, with its negative derivative over the
    // top. The melting temperature is the PEAK OF THE DERIVATIVE rather than
    // any feature of the curve itself - the curve has an inflection there and
    // an inflection cannot be located by eye on a noisy sigmoid, which is why
    // every instrument reports the derivative instead.
    if(in.engine==QLatin1String("Melting Curve (Tm)")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& temperature=in.series.at(0).y;
            const QVector<double>& fluorescence=in.series.at(1).y;
            const int n=qMin(temperature.size(),fluorescence.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(temperature[i])||!finite(fluorescence[i])) continue;
                rows.append(qMakePair(temperature[i],fluorescence[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=5){
                PlotSeries curve;
                curve.label=QStringLiteral("fluorescence");
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.2,in.style.lineWidth*0.9);
                for(const QPair<double,double>& r:rows){ curve.x.append(r.first); curve.y.append(r.second); }
                out.series.append(curve);
                QVector<double> derivative(rows.size(),0.0);
                for(int i=0;i<rows.size();++i){
                    const int lo=qMax(0,i-1), hi=qMin(rows.size()-1,i+1);
                    const double span=rows[hi].first-rows[lo].first;
                    derivative[i]=(std::abs(span)>1e-300)
                                  ?-(rows[hi].second-rows[lo].second)/span:0.0;
                }
                int peakAt=0;
                for(int i=1;i<derivative.size();++i) if(derivative[i]>derivative[peakAt]) peakAt=i;
                // Refined by a parabola through the three points around the
                // peak: the melting temperature is quoted to a tenth of a
                // degree and the sampling step is usually half of one.
                double melting=rows[peakAt].first;
                if(peakAt>0&&peakAt+1<rows.size()){
                    const double a=derivative[peakAt-1],b=derivative[peakAt],c=derivative[peakAt+1];
                    const double denom=a-2.0*b+c;
                    if(std::abs(denom)>1e-300){
                        const double shift=0.5*(a-c)/denom;
                        if(std::abs(shift)<=1.0)
                            melting=rows[peakAt].first
                                   +shift*0.5*(rows[peakAt+1].first-rows[peakAt-1].first);
                    }
                }
                const Bounds bf=boundsOf(curve.y), bd=boundsOf(derivative);
                if(bf.valid&&bd.valid&&bd.hi>bd.lo){
                    const double factor=(bf.hi-bf.lo)/(bd.hi-bd.lo);
                    PlotSeries slope;
                    slope.label=QStringLiteral("-dF/dT x %1, Tm %2")
                                    .arg(factor,0,'g',3).arg(melting,0,'f',2);
                    slope.color=in.style.warning;
                    slope.lineWidth=qMax(1.4,in.style.lineWidth);
                    for(int i=0;i<rows.size();++i){
                        slope.x.append(rows[i].first);
                        slope.y.append(bf.lo+(derivative[i]-bd.lo)*factor);
                    }
                    out.series.append(slope);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("temperature"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("fluorescence"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ----------------------------------------------------------- Growth Rate (OD)
    // Optical density against time on a logarithmic ordinate, where exponential
    // growth is a straight line. The rate comes from the STRAIGHTEST window
    // rather than from the whole record: a culture spends the beginning in lag
    // and the end in stationary phase, and a rate fitted across all three
    // describes none of them.
    if(in.engine==QLatin1String("Growth Rate (OD)")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& time=in.series.at(0).y;
            const QVector<double>& density=in.series.at(1).y;
            const int n=qMin(time.size(),density.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(time[i])||!finite(density[i])||!(density[i]>0.0)) continue;
                rows.append(qMakePair(time[i],density[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=6){
                PlotSeries curve;
                curve.label=QStringLiteral("optical density");
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.2,in.style.lineWidth);
                curve.drawMarkers=true; curve.markerSize=3.2;
                QVector<double> xs,ly;
                for(const QPair<double,double>& r:rows){
                    curve.x.append(r.first); curve.y.append(r.second);
                    xs.append(r.first); ly.append(std::log(r.second));
                }
                out.series.append(curve);
                // A SLIDING WINDOW, as everywhere else a fixed-length window
                // is searched over. This copied both halves of the window out
                // with `mid` and then walked it twice more - once for the mean
                // and once for the residuals - at every start position, which
                // is quadratic in the reading count and allocated two vectors
                // per position. A growth curve logged at 24,000 points took
                // three seconds.
                //
                // Six running numbers give the fit, the residual sum and the
                // total sum of squares at once:
                //   RSS = Syy - a*Sy - b*Sxy   (least squares identity)
                //   TSS = Syy - Sy*Sy/m
                // which is the same R-squared the copying version computed.
                const int window=qBound(3,rows.size()/4,rows.size());
                LineFit best; int bestAt=-1; double bestFit=-1e300;
                {
                    double sx=0,sy=0,sxx=0,sxy=0,syy=0; int m=0;
                    const auto take=[&](int i,double sign){
                        const double px=xs[i], py=ly[i];
                        if(!finite(px)||!finite(py)) return;
                        sx+=sign*px; sy+=sign*py; sxx+=sign*px*px;
                        sxy+=sign*px*py; syy+=sign*py*py; m+=int(sign);
                    };
                    for(int i=0;i<window&&i<xs.size();++i) take(i,1.0);
                    for(int start=0;start+window<=rows.size();++start){
                        if(start>0){ take(start-1,-1.0); take(start+window-1,1.0); }
                        if(m<3) continue;
                        const double denom=double(m)*sxx-sx*sx;
                        if(std::abs(denom)<1e-18) continue;
                        LineFit f;
                        f.slope=(double(m)*sxy-sx*sy)/denom;
                        f.intercept=(sy-f.slope*sx)/double(m);
                        f.ok=finite(f.slope)&&finite(f.intercept);
                        if(!f.ok||!(f.slope>0.0)) continue;
                        const double ssRes=syy-f.intercept*sy-f.slope*sxy;
                        const double ssTot=syy-sy*sy/double(m);
                        // Straightness AND steepness together: the flattest
                        // window of a stationary phase is beautifully straight
                        // and is not the growth rate.
                        const double quality=((ssTot>1e-300)?1.0-ssRes/ssTot:0.0)*f.slope;
                        // A TIE IS BROKEN BY THE EARLIEST WINDOW, DELIBERATELY.
                        // A clean exponential phase is straight along its whole
                        // length, so every window inside it scores the same
                        // R-squared and the same slope, and the winner is then
                        // decided by the last bits of the accumulation - which
                        // moves the drawn segment along the curve whenever the
                        // sums are accumulated in a different order. Requiring
                        // a REAL improvement before displacing the incumbent
                        // makes the choice the earliest such window, which is
                        // also the answer worth having: a growth rate is quoted
                        // from where exponential growth begins, not from
                        // wherever inside the phase the arithmetic landed.
                        const double mustBeat=bestFit+1e-12*std::abs(bestFit)+1e-15;
                        if(quality>mustBeat){ bestFit=quality; best=f; bestAt=start; }
                    }
                }
                if(bestAt>=0&&best.ok&&best.slope>0.0){
                    PlotSeries fit;
                    fit.label=QStringLiteral("mu %1, doubling time %2")
                                  .arg(best.slope,0,'g',4)
                                  .arg(std::log(2.0)/best.slope,0,'g',4);
                    fit.color=in.style.warning;
                    fit.lineWidth=qMax(1.3,in.style.lineWidth);
                    for(int i=bestAt;i<bestAt+window&&i<rows.size();++i){
                        fit.x.append(xs[i]);
                        fit.y.append(std::exp(best.intercept+best.slope*xs[i]));
                    }
                    out.series.append(fit);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("time"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("optical density"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Growth Percentile Chart
    // A measurement against age, with the percentile bands taken from THIS
    // population rather than from a reference table. That is the honest thing
    // to draw when no reference is supplied: a child plotted against a chart
    // built from a different population is being compared to strangers, and
    // saying which population the bands came from is the whole caveat.
    if(in.engine==QLatin1String("Growth Percentile Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& age=in.series.at(0).y;
            const QVector<double>& measure=in.series.at(1).y;
            const int n=qMin(age.size(),measure.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(age[i])||!finite(measure[i])) continue;
                rows.append(qMakePair(age[i],measure[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=20){
                PlotSeries pts;
                pts.label=QStringLiteral("%1 observations").arg(rows.size());
                pts.color=in.series.at(1).color;
                pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=2.6;
                pts.opacity=0.55;
                for(const QPair<double,double>& r:rows){ pts.x.append(r.first); pts.y.append(r.second); }
                out.series.append(pts);
                const int bins=qBound(3,rows.size()/12,20);
                const struct { double share; const char* name; } bands[]={
                    {0.03,"3rd"},{0.50,"50th"},{0.97,"97th"}};
                for(const auto& band:bands){
                    PlotSeries line;
                    line.label=QStringLiteral("%1 percentile").arg(QString::fromLatin1(band.name));
                    line.color=(std::abs(band.share-0.5)<1e-9)?in.style.warning:in.style.gridColor;
                    line.lineWidth=(std::abs(band.share-0.5)<1e-9)
                                   ?qMax(1.4,in.style.lineWidth)
                                   :qMax(1.0,in.style.lineWidth*0.85);
                    if(std::abs(band.share-0.5)>1e-9) line.dashPattern={5,4};
                    for(int b=0;b<bins;++b){
                        const int lo=int(qint64(b)*rows.size()/bins);
                        const int hi=int(qint64(b+1)*rows.size()/bins);
                        if(hi<=lo) continue;
                        QVector<double> centre,values;
                        for(int i=lo;i<hi;++i){ centre.append(rows[i].first); values.append(rows[i].second); }
                        std::sort(values.begin(),values.end());
                        double middle=0.0;
                        for(double v:centre) middle+=v;
                        // Nearest-rank, which cannot invent a value between two
                        // observations that a small bin never contained.
                        const int rank=qBound(0,int(band.share*double(values.size())),
                                              values.size()-1);
                        line.x.append(middle/double(centre.size()));
                        line.y.append(values[rank]);
                    }
                    if(line.x.size()>=2) out.series.append(line);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("age"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("measurement"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Hysteresis Loop (B-H)
    // Flux density against field strength, round the loop. Three numbers come
    // off it and a fourth comes from its AREA: where the loop crosses the
    // vertical axis is what is left with no field applied, where it crosses the
    // horizontal is the field needed to remove that, and the area enclosed is
    // the energy lost every time round - which is the core loss per cycle.
    if(in.engine==QLatin1String("Hysteresis Loop (B-H)")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& field=in.series.at(0).y;
            const QVector<double>& flux=in.series.at(1).y;
            const int n=qMin(field.size(),flux.size());
            PlotSeries loop;
            loop.color=in.series.at(1).color;
            loop.lineWidth=qMax(1.4,in.style.lineWidth);
            QVector<double> hs,bs;
            for(int i=0;i<n;++i){
                if(!finite(field[i])||!finite(flux[i])) continue;
                loop.x.append(field[i]); loop.y.append(flux[i]);
                hs.append(field[i]); bs.append(flux[i]);
            }
            if(hs.size()>=4){
                // Shoelace over the traced path, closed back to the start. The
                // sign says which way round it was traced and carries no
                // physics, so the magnitude is what is reported.
                double area=0.0;
                for(int i=0;i<hs.size();++i){
                    const int j=(i+1)%hs.size();
                    area+=hs[i]*bs[j]-hs[j]*bs[i];
                }
                area=std::abs(area)*0.5;
                // The crossings, collected in both directions rather than taken
                // at the first one found: a loop crosses each axis twice, and
                // the two branches are what make it a loop.
                double remanence=0.0,coercivity=0.0;
                for(int i=1;i<hs.size();++i){
                    if((hs[i-1]>=0.0)!=(hs[i]>=0.0)&&hs[i]!=hs[i-1]){
                        const double f=(0.0-hs[i-1])/(hs[i]-hs[i-1]);
                        remanence=qMax(remanence,std::abs(bs[i-1]+f*(bs[i]-bs[i-1])));
                    }
                    if((bs[i-1]>=0.0)!=(bs[i]>=0.0)&&bs[i]!=bs[i-1]){
                        const double f=(0.0-bs[i-1])/(bs[i]-bs[i-1]);
                        coercivity=qMax(coercivity,std::abs(hs[i-1]+f*(hs[i]-hs[i-1])));
                    }
                }
                const Bounds bb=boundsOf(bs);
                loop.label=QStringLiteral("Br %1, Hc %2, Bs %3, loss %4 per cycle")
                               .arg(remanence,0,'g',4).arg(coercivity,0,'g',4)
                               .arg(bb.valid?qMax(std::abs(bb.hi),std::abs(bb.lo)):0.0,0,'g',4)
                               .arg(area,0,'g',4);
                out.series.append(loop);
                PlotSeries axes;
                axes.label=QString();
                axes.color=in.style.gridColor;
                axes.lineWidth=qMax(0.8,in.style.lineWidth*0.7);
                const Bounds bh=boundsOf(hs);
                if(bh.valid&&bb.valid){
                    axes.x={bh.lo,bh.hi};
                    axes.y={0.0,0.0};
                    out.series.append(axes);
                    PlotSeries vertical;
                    vertical.label=QString();
                    vertical.color=in.style.gridColor;
                    vertical.lineWidth=qMax(0.8,in.style.lineWidth*0.7);
                    vertical.x={0.0,0.0}; vertical.y={bb.lo,bb.hi};
                    out.series.append(vertical);
                }
                PlotSeries marks;
                marks.label=QStringLiteral("remanence and coercivity");
                marks.color=in.style.danger;
                marks.drawLine=false; marks.drawMarkers=true; marks.markerSize=6.0;
                marks.x={0.0,coercivity,0.0,-coercivity};
                marks.y={remanence,0.0,-remanence,0.0};
                out.series.append(marks);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("field strength H"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("flux density B"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------- Torque-Speed Curve
    // What the motor can produce against what the load demands, at every speed.
    // A motor that starts is one whose curve is above the load's all the way
    // from standstill to where they meet, and the point where they meet is the
    // speed the drive will actually run at.
    if(in.engine==QLatin1String("Torque-Speed Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=3){
            const QVector<double>& speed=in.series.at(0).y;
            const QVector<double>& motor=in.series.at(1).y;
            const QVector<double>& load=in.series.at(2).y;
            const int n=qMin(speed.size(),qMin(motor.size(),load.size()));
            QVector<QPair<double,QPair<double,double>>> rows;
            for(int i=0;i<n;++i){
                if(!finite(speed[i])||!finite(motor[i])||!finite(load[i])) continue;
                rows.append(qMakePair(speed[i],qMakePair(motor[i],load[i])));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,QPair<double,double>>& a,
                         const QPair<double,QPair<double,double>>& b){
                          return a.first<b.first; });
            if(rows.size()>=3){
                PlotSeries driving,resisting;
                resisting.label=QStringLiteral("load");
                resisting.color=in.series.at(2).color;
                resisting.lineWidth=qMax(1.3,in.style.lineWidth);
                driving.color=in.series.at(1).color;
                driving.lineWidth=qMax(1.4,in.style.lineWidth);
                int breakdown=0;
                double runSpeed=std::numeric_limits<double>::quiet_NaN();
                double runTorque=std::numeric_limits<double>::quiet_NaN();
                for(int i=0;i<rows.size();++i){
                    driving.x.append(rows[i].first);   driving.y.append(rows[i].second.first);
                    resisting.x.append(rows[i].first); resisting.y.append(rows[i].second.second);
                    if(rows[i].second.first>rows[breakdown].second.first) breakdown=i;
                    if(i==0) continue;
                    const double before=rows[i-1].second.first-rows[i-1].second.second;
                    const double now=rows[i].second.first-rows[i].second.second;
                    // The LAST crossing, not the first: a motor whose curve dips
                    // below the load and comes back up crosses more than once,
                    // and only the highest-speed crossing is a stable operating
                    // point - below it the motor accelerates back up to it.
                    if((before<0.0)!=(now<0.0)&&now!=before){
                        const double f=(0.0-before)/(now-before);
                        runSpeed=rows[i-1].first+f*(rows[i].first-rows[i-1].first);
                        runTorque=rows[i-1].second.second
                                 +f*(rows[i].second.second-rows[i-1].second.second);
                    }
                }
                driving.label=finite(runSpeed)
                    ? QStringLiteral("motor; starting %1, breakdown %2 at %3, runs at %4 (torque %5)")
                          .arg(rows.first().second.first,0,'g',4)
                          .arg(rows[breakdown].second.first,0,'g',4)
                          .arg(rows[breakdown].first,0,'g',4)
                          .arg(runSpeed,0,'g',5).arg(runTorque,0,'g',4)
                    : QStringLiteral("motor; starting %1, breakdown %2 at %3 (never meets the load)")
                          .arg(rows.first().second.first,0,'g',4)
                          .arg(rows[breakdown].second.first,0,'g',4)
                          .arg(rows[breakdown].first,0,'g',4);
                out.series.append(driving);
                out.series.append(resisting);
                if(finite(runSpeed)){
                    PlotSeries mark;
                    mark.label=QStringLiteral("operating point");
                    mark.color=in.style.danger;
                    mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.5;
                    mark.x={runSpeed}; mark.y={runTorque};
                    out.series.append(mark);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("speed"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("torque"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------------ Radiation Pattern
    // Gain against bearing, drawn round rather than across, because an antenna
    // pattern is a statement about DIRECTION and a rectangular plot of it cuts
    // the lobe that straddles zero degrees in half. The three numbers quoted
    // for a pattern are computed here: how wide the main lobe is at half power,
    // how much less goes backwards than forwards, and how high the largest
    // sidelobe is.
    if(in.engine==QLatin1String("Radiation Pattern")){
        PlotSpec out=derivedAs(in,QStringLiteral("Polar Line"));
        if(in.series.size()>=2){
            const QVector<double>& bearing=in.series.at(0).y;
            const QVector<double>& gain=in.series.at(1).y;
            const int n=qMin(bearing.size(),gain.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(bearing[i])||!finite(gain[i])) continue;
                rows.append(qMakePair(bearing[i],gain[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=8){
                int peakAt=0;
                for(int i=1;i<rows.size();++i) if(rows[i].second>rows[peakAt].second) peakAt=i;
                const double peak=rows[peakAt].second;
                // Whether the column is already in decibels decides what "half
                // power" means: three below, or a half of. Read from the range
                // rather than from a column name, since a pattern in dB has
                // negative values and a linear one does not.
                bool inDecibels=false;
                for(const QPair<double,double>& r:rows) if(r.second<0.0) inDecibels=true;
                const double level=inDecibels?peak-3.0:peak*0.5;
                const auto crossing=[&](int from,int step){
                    for(int i=from;i>=0&&i<rows.size()-1&&i>0;i+=step){
                        const double a=rows[i].second,b=rows[i+step].second;
                        if((a>=level)==(b>=level)||a==b) continue;
                        const double f=(level-a)/(b-a);
                        return rows[i].first+f*(rows[i+step].first-rows[i].first);
                    }
                    return std::numeric_limits<double>::quiet_NaN();
                };
                const double lower=crossing(peakAt,-1), upper=crossing(peakAt,1);
                // The main lobe ends at the first NULL either side of the
                // peak, not at the half-power point. Taking everything beyond
                // the half-power width as sidelobe was the first attempt, and
                // on a beam whose skirt falls smoothly it reported the skirt:
                // a pattern with a real sidelobe 20 dB down came back with
                // 12.8, which is the main lobe at a bearing just outside the
                // half-power width. The nulls are found by walking outward
                // until the gain stops falling.
                int leftNull=peakAt,rightNull=peakAt;
                while(leftNull>0&&rows[leftNull-1].second<=rows[leftNull].second) --leftNull;
                while(rightNull+1<rows.size()
                      &&rows[rightNull+1].second<=rows[rightNull].second) ++rightNull;
                double sidelobe=-std::numeric_limits<double>::infinity();
                double back=rows[0].second;
                double bestOpposite=std::numeric_limits<double>::infinity();
                const double width=(finite(lower)&&finite(upper))?std::abs(upper-lower):30.0;
                for(int i=0;i<rows.size();++i){
                    if(i<leftNull||i>rightNull) sidelobe=qMax(sidelobe,rows[i].second);
                    double away=std::abs(rows[i].first-rows[peakAt].first);
                    if(away>180.0) away=360.0-away;
                    const double opposite=std::abs(away-180.0);
                    if(opposite<bestOpposite){ bestOpposite=opposite; back=rows[i].second; }
                }
                PlotSeries pattern;
                // FOUR FIGURES OF MERIT, UNDER THE FIGURE. An antenna pattern
                // is judged on its beamwidth, its front-to-back ratio and how
                // far down its worst sidelobe sits - all three were computed
                // and written into the label of the only named series, whose
                // legend row is suppressed as a caption. They also make a
                // legend row far too long to read across a polar figure, which
                // is the other reason they belong here instead.
                pattern.label=QStringLiteral("pattern");
                out.figureNote=QStringLiteral("Peak %1 at %2%3%4%5.")
                    .arg(formatMeasured(peak)).arg(formatMeasured(rows[peakAt].first))
                    .arg((finite(lower)&&finite(upper))
                         ?QStringLiteral(", half-power beamwidth %1").arg(width,0,'f',1)
                         :QString())
                    .arg(inDecibels?QStringLiteral(", front-to-back %1 dB").arg(peak-back,0,'f',1)
                                   :QStringLiteral(", front-to-back %1x")
                                        .arg((std::abs(back)>1e-300)?peak/back:0.0,0,'f',1))
                    .arg(finite(sidelobe)
                         ?(inDecibels?QStringLiteral(", sidelobe %1 dB down").arg(peak-sidelobe,0,'f',1)
                                     :QStringLiteral(", sidelobe %1 of peak")
                                          .arg((peak>1e-300)?sidelobe/peak:0.0,0,'f',3))
                         :QString());
                pattern.color=in.series.at(1).color;
                pattern.lineWidth=qMax(1.4,in.style.lineWidth);
                for(const QPair<double,double>& r:rows){ pattern.x.append(r.first); pattern.y.append(r.second); }
                out.series.append(pattern);
            }
        }
        return out;
    }

    // ------------------------------------------------- Soil Water Retention Curve
    // How much water a soil holds against how hard it is held, on a logarithmic
    // suction axis spanning five decades. Field capacity and the wilting point
    // are conventionally taken at fixed suctions, so both are interpolated and
    // the difference between them - the water a plant can actually get at - is
    // the number the curve is drawn to produce.
    if(in.engine==QLatin1String("Soil Water Retention Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& suction=in.series.at(0).y;
            const QVector<double>& content=in.series.at(1).y;
            const int n=qMin(suction.size(),content.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(suction[i])||!finite(content[i])||!(suction[i]>0.0)) continue;
                rows.append(qMakePair(suction[i],content[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=4){
                // Interpolated in LOG suction, because the measurements are
                // decades apart and a linear interpolation between 100 and
                // 15000 puts field capacity in the wrong place entirely.
                const auto at=[&](double head){
                    if(head<=rows.first().first) return rows.first().second;
                    if(head>=rows.last().first) return rows.last().second;
                    for(int i=1;i<rows.size();++i){
                        if(rows[i].first<head) continue;
                        const double x0=std::log10(rows[i-1].first),x1=std::log10(rows[i].first);
                        const double f=(x1>x0)?(std::log10(head)-x0)/(x1-x0):0.0;
                        return rows[i-1].second+f*(rows[i].second-rows[i-1].second);
                    }
                    return rows.last().second;
                };
                const double capacity=at(330.0);      // 33 kPa, in cm of head
                const double wilting=at(15000.0);     // 1500 kPa
                PlotSeries curve;
                curve.label=QStringLiteral("field capacity %1 at 330 cm, wilting %2 at 15000 cm, available %3")
                                .arg(capacity,0,'f',4).arg(wilting,0,'f',4)
                                .arg(capacity-wilting,0,'f',4);
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.4,in.style.lineWidth);
                curve.drawMarkers=true; curve.markerSize=3.6;
                for(const QPair<double,double>& r:rows){ curve.x.append(r.first); curve.y.append(r.second); }
                out.series.append(curve);
                PlotSeries marks;
                marks.label=QStringLiteral("field capacity and wilting point");
                marks.color=in.style.danger;
                marks.drawLine=false; marks.drawMarkers=true; marks.markerSize=6.0;
                marks.x={330.0,15000.0}; marks.y={capacity,wilting};
                out.series.append(marks);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("suction head (cm)"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("water content"),false,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------------- Detrended Fluctuation Analysis
    // How the fluctuation of a record grows with the window it is measured
    // over, on log-log axes. The slope separates three things an ordinary
    // spectrum blurs together: below a half the record reverses itself, at a
    // half it is uncorrelated noise, and above it the record has memory - and
    // at one it is at the boundary that shows up in an implausible number of
    // natural systems.
    if(in.engine==QLatin1String("Detrended Fluctuation Analysis")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& record=in.series.at(1).y;
            QVector<double> values;
            for(double v:record) if(finite(v)) values.append(v);
            const int n=values.size();
            if(n>=64){
                double mean=0.0;
                for(double v:values) mean+=v;
                mean/=double(n);
                // The integrated, mean-removed profile. Integrating first is
                // what makes this work on a noise-like record: without it every
                // window is the same and the slope carries nothing.
                QVector<double> profile(n,0.0);
                double running=0.0;
                for(int i=0;i<n;++i){ running+=values[i]-mean; profile[i]=running; }
                PlotSeries pts;
                pts.color=in.series.at(1).color;
                pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.4;
                QVector<double> lx,ly;
                // Windows start at 16, not at 4. Removing a straight line from
                // a four-point window removes most of what was in it, so the
                // fluctuation there is biased low and the SLOPE through it
                // biased high - which on white noise reads as long-range
                // correlation that is not present. Sixteen is the usual floor.
                for(int scale=16;scale<=n/4;scale=qMax(scale+1,int(scale*1.3))){
                    const int windows=n/scale;
                    if(windows<1) continue;
                    double total=0.0;
                    for(int w=0;w<windows;++w){
                        // A straight line removed from each window, which is
                        // the "detrended" in the name: without it a wandering
                        // record gives a slope that is about the wander.
                        QVector<double> t(scale),y(scale);
                        for(int i=0;i<scale;++i){ t[i]=double(i); y[i]=profile[w*scale+i]; }
                        const LineFit f=fitLine(t,y);
                        double residual=0.0;
                        for(int i=0;i<scale;++i){
                            const double d=f.ok?y[i]-(f.intercept+f.slope*t[i]):y[i];
                            residual+=d*d;
                        }
                        total+=residual/double(scale);
                    }
                    const double fluctuation=std::sqrt(total/double(windows));
                    if(!(fluctuation>0.0)) continue;
                    pts.x.append(double(scale)); pts.y.append(fluctuation);
                    lx.append(std::log(double(scale))); ly.append(std::log(fluctuation));
                }
                if(pts.x.size()>=3){
                    out.series.append(pts);
                    const LineFit f=fitLine(lx,ly);
                    if(f.ok){
                        const double exponent=f.slope;
                        // The standard error of the slope, because the verbal
                        // reading is worthless without it. On four thousand
                        // points this estimator's own scatter is around 0.05,
                        // and a scheme that says "uncorrelated" below 0.55 and
                        // "long-range correlated" above it is then reporting a
                        // coin toss as a finding - which is exactly what it did
                        // on white noise the first time it was run. The reading
                        // is now only given when the exponent is more than two
                        // standard errors from a half.
                        double meanX=0.0;
                        for(double v:lx) meanX+=v;
                        meanX/=double(lx.size());
                        double spreadX=0.0,residual=0.0;
                        for(int i=0;i<lx.size();++i){
                            spreadX+=(lx[i]-meanX)*(lx[i]-meanX);
                            const double d=ly[i]-(f.intercept+f.slope*lx[i]);
                            residual+=d*d;
                        }
                        const double error=(lx.size()>2&&spreadX>1e-300)
                            ? std::sqrt(residual/double(lx.size()-2)/spreadX)
                            : std::numeric_limits<double>::quiet_NaN();
                        // The verdict is given only well outside 0.4 to 0.7,
                        // and the number beside alpha is labelled as what it
                        // is: the SCATTER OF THE POINTS ABOUT THE FIT, which is
                        // not the uncertainty of alpha.
                        //
                        // That distinction was got wrong here once and is worth
                        // keeping. The log-log points sit on a line very
                        // tightly, so the regression standard error comes out
                        // around 0.01 - and using it as the decision rule
                        // declared white noise "long-range correlated" with
                        // apparent confidence. Re-running the same length of
                        // white noise moves alpha by about 0.05, five times
                        // that, because the real uncertainty is set by how long
                        // the RECORD is and not by how straight the fit looks.
                        // On top of that, DFA-1 is biased upward at small
                        // windows even after starting at sixteen. A band of 0.4
                        // to 0.7 is wide enough to absorb both.
                        const bool decided=(exponent<0.4||exponent>0.7);
                        const QString reading=!decided
                            ? QStringLiteral("0.4 to 0.7 is not distinguishable from uncorrelated on a record this long")
                           :(exponent<0.4)?QStringLiteral("anti-correlated")
                           :(exponent<0.95)?QStringLiteral("long-range correlated")
                           :(exponent<1.15)?QStringLiteral("1/f")
                                           :QStringLiteral("random walk or stronger");
                        PlotSeries line;
                        // The scaling exponent and what it means, under the
                        // figure. The fluctuation points carry no label, so
                        // this fitted line was the only named series and its
                        // row was suppressed as a caption - which left a
                        // log-log plot with a line through it and no alpha.
                        line.label=QStringLiteral("fit");
                        out.figureNote=finite(error)
                            ? QStringLiteral("alpha %1, scatter about the fit %2 - %3.")
                                  .arg(exponent,0,'f',3).arg(error,0,'f',3).arg(reading)
                            : QStringLiteral("alpha %1 - %2.").arg(exponent,0,'f',3).arg(reading);
                        line.color=in.style.warning;
                        line.lineWidth=qMax(1.3,in.style.lineWidth);
                        for(int i=0;i<pts.x.size();++i){
                            line.x.append(pts.x[i]);
                            line.y.append(std::exp(f.intercept+f.slope*std::log(pts.x[i])));
                        }
                        out.series.append(line);
                    }
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("window size"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("fluctuation"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------------- Poincare Plot
    // Each interval against the one after it. The cloud's width across the
    // identity line is beat-to-beat variability and its length along the line
    // is the slower drift, and separating those two is the whole reason this is
    // drawn instead of a histogram of the intervals.
    if(in.engine==QLatin1String("Poincare Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& interval=in.series.at(1).y;
            QVector<double> values;
            for(double v:interval) if(finite(v)) values.append(v);
            if(values.size()>=4){
                PlotSeries pts;
                pts.color=in.series.at(1).color;
                pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=3.0;
                pts.opacity=0.7;
                double sumDiff=0.0,sumSum=0.0,meanDiff=0.0,meanSum=0.0;
                const int pairs=values.size()-1;
                for(int i=0;i<pairs;++i){
                    pts.x.append(values[i]); pts.y.append(values[i+1]);
                    meanDiff+=values[i+1]-values[i];
                    meanSum+=values[i+1]+values[i];
                }
                meanDiff/=double(pairs); meanSum/=double(pairs);
                for(int i=0;i<pairs;++i){
                    const double d=(values[i+1]-values[i])-meanDiff;
                    const double s=(values[i+1]+values[i])-meanSum;
                    sumDiff+=d*d; sumSum+=s*s;
                }
                // SD1 and SD2 are the standard deviations along the two
                // diagonals, and the root two is the projection onto them
                // rather than a fudge factor.
                const double across=std::sqrt(sumDiff/double(pairs)/2.0);
                const double along=std::sqrt(sumSum/double(pairs)/2.0);
                pts.label=QStringLiteral("%1 pairs; SD1 %2, SD2 %3, ratio %4")
                              .arg(pairs).arg(across,0,'g',4).arg(along,0,'g',4)
                              .arg((across>1e-300)?along/across:0.0,0,'f',2);
                out.series.append(pts);
                // The ellipse those two describe, centred on the mean pair and
                // rotated onto the identity line - which is where it is,
                // because consecutive intervals are nearly equal.
                const double centre=0.5*meanSum;
                PlotSeries ellipse;
                ellipse.label=QStringLiteral("SD1 / SD2 ellipse");
                ellipse.color=in.style.warning;
                ellipse.lineWidth=qMax(1.3,in.style.lineWidth);
                for(int k=0;k<=180;++k){
                    const double a=2.0*M_PI*double(k)/180.0;
                    const double u=along*std::cos(a),v=across*std::sin(a);
                    // Rotated by 45 degrees, so u runs along the identity line
                    // and v across it.
                    const double c=std::sqrt(0.5);
                    ellipse.x.append(centre+c*(u-v));
                    ellipse.y.append(centre+c*(u+v));
                }
                out.series.append(ellipse);
                const Bounds bv=boundsOf(pts.x);
                if(bv.valid){
                    PlotSeries identity;
                    identity.label=QStringLiteral("identity");
                    identity.color=in.style.gridColor;
                    identity.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                    identity.dashPattern={5,4};
                    identity.x={bv.lo,bv.hi}; identity.y={bv.lo,bv.hi};
                    out.series.append(identity);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("interval n"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("interval n + 1"),false,unsetValue(),unsetValue()};
        // The same quantity on both axes, and the cloud's width across the
        // identity line against its width along it IS the measurement.
        out.equalAspect=true;
        return out;
    }

    // -------------------------------------------------------- Kingman Queue Curve
    // Waiting time against how busy the server is, over the curve utilisation
    // alone would give. The point of drawing the reference is that the rise is
    // not a matter of degree: at ninety per cent busy the wait is nine times
    // what it is at fifty, and no amount of working harder changes the shape.
    if(in.engine==QLatin1String("Kingman Queue Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& utilisation=in.series.at(0).y;
            const QVector<double>& waiting=in.series.at(1).y;
            const int n=qMin(utilisation.size(),waiting.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(utilisation[i])||!finite(waiting[i])) continue;
                if(!(utilisation[i]>0.0)||!(utilisation[i]<1.0)) continue;
                rows.append(qMakePair(utilisation[i],waiting[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=3){
                PlotSeries pts;
                pts.label=QStringLiteral("measured wait");
                pts.color=in.series.at(1).color;
                pts.lineWidth=qMax(1.3,in.style.lineWidth);
                pts.drawMarkers=true; pts.markerSize=3.8;
                for(const QPair<double,double>& r:rows){ pts.x.append(r.first); pts.y.append(r.second); }
                out.series.append(pts);
                // The utilisation term alone, scaled to pass through the
                // measurement at the LOWEST utilisation - where the variability
                // term matters least, so the scale factor is closest to being
                // the variability the queue actually has.
                const double anchor=rows.first().first;
                const double factor=rows.first().second*(1.0-anchor)/anchor;
                PlotSeries reference;
                reference.label=QStringLiteral("rho / (1 - rho), matched at rho %1")
                                    .arg(anchor,0,'f',3);
                reference.color=in.style.warning;
                reference.lineWidth=qMax(1.2,in.style.lineWidth);
                reference.dashPattern={6,4};
                for(int i=0;i<=160;++i){
                    const double rho=anchor+(qMin(0.98,rows.last().first)-anchor)
                                            *double(i)/160.0;
                    if(!(rho>0.0)||!(rho<1.0)) continue;
                    reference.x.append(rho);
                    reference.y.append(factor*rho/(1.0-rho));
                }
                if(reference.x.size()>=2) out.series.append(reference);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("utilisation"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("waiting time"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------- Little's Law Check
    // Work in progress against arrival rate multiplied by lead time. Little's
    // law says those are the same number, always, for any stable system - so
    // this is not a model being fitted but an IDENTITY being tested, and points
    // off the line mean the measurement window was too short, the system was
    // not stable over it, or the three quantities were not measured on the same
    // thing.
    if(in.engine==QLatin1String("Little's Law Check")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=3){
            const QVector<double>& arrival=in.series.at(0).y;
            const QVector<double>& inProgress=in.series.at(1).y;
            const QVector<double>& lead=in.series.at(2).y;
            const int n=qMin(arrival.size(),qMin(inProgress.size(),lead.size()));
            PlotSeries pts;
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.4;
            double worst=0.0; int off=0;
            QVector<double> xs;
            for(int i=0;i<n;++i){
                if(!finite(arrival[i])||!finite(inProgress[i])||!finite(lead[i])) continue;
                const double predicted=arrival[i]*lead[i];
                pts.x.append(predicted); pts.y.append(inProgress[i]);
                xs.append(predicted);
                if(std::abs(predicted)>1e-300){
                    const double error=std::abs(inProgress[i]-predicted)/std::abs(predicted);
                    worst=qMax(worst,error);
                    if(error>0.10) ++off;
                }
            }
            if(!pts.x.isEmpty()){
                pts.label=QStringLiteral("%1 windows; worst departure %2%, %3 over 10%")
                              .arg(pts.x.size()).arg(100.0*worst,0,'f',1).arg(off);
                out.series.append(pts);
                const Bounds bx=boundsOf(xs);
                const Bounds by=boundsOf(pts.y);
                if(bx.valid&&by.valid){
                    const double lo=qMin(bx.lo,by.lo),hi=qMax(bx.hi,by.hi);
                    PlotSeries identity;
                    identity.label=QStringLiteral("L = lambda W");
                    identity.color=in.style.warning;
                    identity.lineWidth=qMax(1.3,in.style.lineWidth);
                    identity.x={lo,hi}; identity.y={lo,hi};
                    out.series.append(identity);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("arrival rate x lead time"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("work in progress"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------------------- Burndown Chart
    // Work remaining against time, over the straight line that would finish it
    // exactly on the date. What the chart is read for is not today's number but
    // the PROJECTION: the recent rate extended forward says when the work will
    // actually run out, and the gap between that and the deadline is the thing
    // worth acting on while there is still time to act.
    if(in.engine==QLatin1String("Burndown Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& when=in.series.at(0).y;
            const QVector<double>& remaining=in.series.at(1).y;
            const int n=qMin(when.size(),remaining.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(when[i])||!finite(remaining[i])) continue;
                rows.append(qMakePair(when[i],remaining[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=3){
                PlotSeries actual;
                actual.color=in.series.at(1).color;
                actual.lineWidth=qMax(1.4,in.style.lineWidth);
                actual.drawMarkers=true; actual.markerSize=3.6;
                for(const QPair<double,double>& r:rows){ actual.x.append(r.first); actual.y.append(r.second); }
                PlotSeries ideal;
                ideal.label=QStringLiteral("straight line to the end of the record");
                ideal.color=in.style.gridColor;
                ideal.lineWidth=qMax(0.9,in.style.lineWidth*0.8);
                ideal.dashPattern={5,4};
                ideal.x={rows.first().first,rows.last().first};
                ideal.y={rows.first().second,0.0};
                // The projection uses the recent third rather than the whole
                // record, because a burndown that changed pace is exactly the
                // case a projection is wanted for and an average over the whole
                // sprint hides the change.
                const int from=qMax(0,rows.size()-qMax(2,rows.size()/3));
                QVector<double> rx,ry;
                for(int i=from;i<rows.size();++i){ rx.append(rows[i].first); ry.append(rows[i].second); }
                const LineFit f=fitLine(rx,ry);
                double finish=std::numeric_limits<double>::quiet_NaN();
                if(f.ok&&f.slope<0.0) finish=-f.intercept/f.slope;
                // "The end of the record", not "the deadline". Nothing in two
                // columns says when the work was due; the last observation is
                // only the last observation, and calling it a deadline would
                // put a commitment on the figure that the data never carried.
                actual.label=finite(finish)
                    ? QStringLiteral("remaining %1; at the recent rate it reaches zero at %2, against the end of the record at %3")
                          .arg(rows.last().second,0,'g',4).arg(finish,0,'g',6)
                          .arg(rows.last().first,0,'g',6)
                    : QStringLiteral("remaining %1; the recent rate is not falling, so there is no projected finish")
                          .arg(rows.last().second,0,'g',4);
                out.series.append(actual);
                out.series.append(ideal);
                if(finite(finish)&&finish>rows.first().first){
                    PlotSeries projection;
                    projection.label=QStringLiteral("projection");
                    projection.color=in.style.warning;
                    projection.lineWidth=qMax(1.2,in.style.lineWidth);
                    projection.dashPattern={2,3};
                    projection.x={rows[from].first,finish};
                    projection.y={f.intercept+f.slope*rows[from].first,0.0};
                    out.series.append(projection);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("time"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("work remaining"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Regression Confidence Band
    // A fitted line with two bands round it that answer two different
    // questions, and are drawn together because they are constantly confused.
    // The inner band is where the LINE is; the outer is where the next
    // OBSERVATION will be. The first shrinks as more data arrives and the
    // second does not, because it carries the scatter of the data itself.
    if(in.engine==QLatin1String("Regression Confidence Band")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& predictor=in.series.at(0).y;
            const QVector<double>& response=in.series.at(1).y;
            const int n=qMin(predictor.size(),response.size());
            PlotSeries pts;
            pts.label=QStringLiteral("observations");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=3.8;
            QVector<double> xs,ys;
            for(int i=0;i<n;++i){
                if(!finite(predictor[i])||!finite(response[i])) continue;
                pts.x.append(predictor[i]); pts.y.append(response[i]);
                xs.append(predictor[i]); ys.append(response[i]);
            }
            if(xs.size()>=4){
                out.series.append(pts);
                const LineFit f=fitLine(xs,ys);
                const Bounds bx=boundsOf(xs);
                if(f.ok&&bx.valid){
                    const int count=xs.size();
                    double meanX=0.0;
                    for(double v:xs) meanX+=v;
                    meanX/=double(count);
                    double spreadX=0.0,residual=0.0;
                    for(int i=0;i<count;++i){
                        spreadX+=(xs[i]-meanX)*(xs[i]-meanX);
                        const double d=ys[i]-(f.intercept+f.slope*xs[i]);
                        residual+=d*d;
                    }
                    const double sigma=std::sqrt(residual/double(count-2));
                    // The t quantile at n-2 degrees of freedom, by the same
                    // Cornish-Fisher expansion the half-normal plot uses. A
                    // normal quantile instead would make both bands too narrow
                    // on a small sample, which is when they matter most.
                    const double freedom=double(count-2);
                    const double z=1.959963985;
                    const double student=z+(z*z*z+z)/(4.0*freedom)
                                          +(5.0*std::pow(z,5)+16.0*z*z*z+3.0*z)
                                           /(96.0*freedom*freedom);
                    PlotSeries line,confidence,confidenceLow,prediction,predictionLow;
                    line.label=QStringLiteral("fit; slope %1, s %2, %3 observations")
                                   .arg(f.slope,0,'g',4).arg(sigma,0,'g',4).arg(count);
                    line.color=in.style.warning;
                    line.lineWidth=qMax(1.4,in.style.lineWidth);
                    confidence.label=QStringLiteral("95% for the line");
                    confidence.color=in.style.warning;
                    confidence.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                    confidence.dashPattern={5,4};
                    confidenceLow=confidence; confidenceLow.label=QString();
                    prediction.label=QStringLiteral("95% for the next observation");
                    prediction.color=in.style.gridColor;
                    prediction.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                    prediction.dashPattern={2,3};
                    predictionLow=prediction; predictionLow.label=QString();
                    for(int i=0;i<=120;++i){
                        const double x=bx.lo+(bx.hi-bx.lo)*double(i)/120.0;
                        const double fitted=f.intercept+f.slope*x;
                        const double leverage=(spreadX>1e-300)
                            ? 1.0/double(count)+(x-meanX)*(x-meanX)/spreadX
                            : 1.0/double(count);
                        const double forLine=student*sigma*std::sqrt(leverage);
                        const double forPoint=student*sigma*std::sqrt(1.0+leverage);
                        line.x.append(x); line.y.append(fitted);
                        confidence.x.append(x);    confidence.y.append(fitted+forLine);
                        confidenceLow.x.append(x); confidenceLow.y.append(fitted-forLine);
                        prediction.x.append(x);    prediction.y.append(fitted+forPoint);
                        predictionLow.x.append(x); predictionLow.y.append(fitted-forPoint);
                    }
                    out.series.append(prediction);
                    out.series.append(predictionLow);
                    out.series.append(confidence);
                    out.series.append(confidenceLow);
                    out.series.append(line);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("predictor"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("response"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ====================================================================
    // Batch 12: the last of the rewrites. Diffuse reflectance, adsorption,
    // dielectrics, titration, thermal conductivity, measurement-system
    // variation, tolerance, rare events, repeated measures, hydrograph
    // analysis, step response, jitter, well testing and thermal kinetics.

    // ------------------------------------------------------- Kubelka-Munk Plot
    // Diffuse reflectance turned into something proportional to absorption.
    // A powder cannot be measured in transmission, so its spectrum comes back
    // as reflectance - and reflectance is not proportional to how much of
    // anything is present, while the Kubelka-Munk function of it is.
    if(in.engine==QLatin1String("Kubelka-Munk Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& abscissa=in.series.at(0).y;
            const QVector<double>& reflectance=in.series.at(1).y;
            const int n=qMin(abscissa.size(),reflectance.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(abscissa[i])||!finite(reflectance[i])) continue;
                // Reflectance is a fraction. A percentage column is accepted
                // and divided down, because instruments export both and the
                // function divides by R - which turns a 50 into 0.0025 instead
                // of 0.25 and moves every feature.
                double r=reflectance[i];
                if(r>1.0&&r<=100.0) r/=100.0;
                if(!(r>0.0)||!(r<1.0)) continue;
                rows.append(qMakePair(abscissa[i],(1.0-r)*(1.0-r)/(2.0*r)));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=4){
                PlotSeries curve;
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.4,in.style.lineWidth);
                QVector<double> xs,ys;
                for(const QPair<double,double>& r:rows){
                    curve.x.append(r.first); curve.y.append(r.second);
                    xs.append(r.first); ys.append(r.second);
                }
                int peakAt=0;
                for(int i=1;i<rows.size();++i) if(rows[i].second>rows[peakAt].second) peakAt=i;
                // A band gap is only offered when the abscissa is plausibly in
                // electron volts. On a wavelength axis the same construction
                // would produce a number with no meaning, and a labelled
                // meaningless number is worse than none.
                const Bounds bx=boundsOf(xs);
                const bool inElectronVolts=(bx.valid&&bx.lo>0.3&&bx.hi<7.0);
                QString gap;
                if(inElectronVolts&&rows.size()>=8){
                    QVector<double> tx,ty;
                    for(const QPair<double,double>& r:rows){
                        tx.append(r.first);
                        ty.append((r.second*r.first)*(r.second*r.first));
                    }
                    const int window=qBound(4,rows.size()/6,rows.size());
                    const WindowFit picked=slidingBestFit(tx,ty,window,true);
                    const LineFit best=picked.fit;
                    const int bestAt=picked.at;
                    if(bestAt>=0&&best.slope>1e-300){
                        const double edge=-best.intercept/best.slope;
                        gap=QStringLiteral(", band gap %1 eV from (F(R) h v)^2").arg(edge,0,'f',3);
                    }
                }
                curve.label=QStringLiteral("F(R); peak at %1%2")
                                .arg(rows[peakAt].first,0,'g',5).arg(gap);
                out.series.append(curve);
                PlotSeries mark;
                mark.label=QStringLiteral("strongest absorption");
                mark.color=in.style.danger;
                mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.0;
                mark.x={rows[peakAt].first}; mark.y={rows[peakAt].second};
                out.series.append(mark);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("wavelength or photon energy"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("F(R) = (1-R)^2 / 2R"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Freundlich Isotherm
    // The other adsorption linearisation, and the pair is drawn separately for
    // a reason: Langmuir assumes one kind of site and a saturation limit,
    // Freundlich assumes a distribution of site energies and no limit at all.
    // Which one straightens the data is the finding, so neither should be the
    // only one offered.
    if(in.engine==QLatin1String("Freundlich Isotherm")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& equilibrium=in.series.at(0).y;
            const QVector<double>& adsorbed=in.series.at(1).y;
            const int n=qMin(equilibrium.size(),adsorbed.size());
            PlotSeries pts;
            pts.label=QStringLiteral("log qe against log Ce");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.4;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(equilibrium[i])||!finite(adsorbed[i])) continue;
                if(!(equilibrium[i]>0.0)||!(adsorbed[i]>0.0)) continue;
                pts.x.append(std::log10(equilibrium[i]));
                pts.y.append(std::log10(adsorbed[i]));
                fx.append(std::log10(equilibrium[i])); fy.append(std::log10(adsorbed[i]));
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                const LineFit f=fitLine(fx,fy);
                const Bounds bx=boundsOf(fx);
                if(f.ok&&bx.valid){
                    double ssRes=0.0,ssTot=0.0,mean=0.0;
                    for(double v:fy) mean+=v;
                    mean/=double(fy.size());
                    for(int i=0;i<fx.size();++i){
                        const double r=fy[i]-(f.intercept+f.slope*fx[i]);
                        ssRes+=r*r; ssTot+=(fy[i]-mean)*(fy[i]-mean);
                    }
                    const double r2=(ssTot>1e-300)?1.0-ssRes/ssTot:0.0;
                    PlotSeries line;
                    line.label=QStringLiteral("Kf %1, 1/n %2 (%3), R2 %4")
                                   .arg(std::pow(10.0,f.intercept),0,'g',4)
                                   .arg(f.slope,0,'f',3)
                                   .arg(f.slope<1.0?QStringLiteral("favourable")
                                                   :QStringLiteral("unfavourable"))
                                   .arg(r2,0,'f',4);
                    line.color=in.style.warning;
                    line.lineWidth=qMax(1.3,in.style.lineWidth);
                    line.x={bx.lo,bx.hi};
                    line.y={f.intercept+f.slope*bx.lo,f.intercept+f.slope*bx.hi};
                    out.series.append(line);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("log10 Ce"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("log10 qe"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------------ Cole-Cole Plot
    // The imaginary part of the permittivity against the real part. A single
    // relaxation traces a SEMICIRCLE centred on the real axis; a distribution
    // of relaxation times traces an arc whose centre has sunk BELOW it, and how
    // far it has sunk is the measurement. A circle is fitted rather than eyed,
    // and its two intercepts are the permittivity at zero and infinite
    // frequency.
    if(in.engine==QLatin1String("Cole-Cole Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& real=in.series.at(0).y;
            const QVector<double>& imaginary=in.series.at(1).y;
            const int n=qMin(real.size(),imaginary.size());
            PlotSeries pts;
            pts.label=QStringLiteral("measured");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            // A circle is LINEAR in the coefficients of x^2 + y^2 + Dx + Ey + F,
            // so it is one 3x3 solve rather than a nonlinear fit to a centre and
            // a radius.
            double s[3][4]={};
            int used=0;
            for(int i=0;i<n;++i){
                if(!finite(real[i])||!finite(imaginary[i])) continue;
                pts.x.append(real[i]); pts.y.append(imaginary[i]);
                const double basis[3]={real[i],imaginary[i],1.0};
                const double target=-(real[i]*real[i]+imaginary[i]*imaginary[i]);
                for(int r=0;r<3;++r){
                    for(int c=0;c<3;++c) s[r][c]+=basis[r]*basis[c];
                    s[r][3]+=basis[r]*target;
                }
                ++used;
            }
            if(!pts.x.isEmpty()) out.series.append(pts);
            bool ok=(used>=4);
            for(int col=0;col<3&&ok;++col){
                int pivot=col;
                for(int r=col+1;r<3;++r)
                    if(std::abs(s[r][col])>std::abs(s[pivot][col])) pivot=r;
                if(std::abs(s[pivot][col])<1e-14){ ok=false; break; }
                if(pivot!=col) for(int c=0;c<4;++c) std::swap(s[col][c],s[pivot][c]);
                const double d=s[col][col];
                for(int c=0;c<4;++c) s[col][c]/=d;
                for(int r=0;r<3;++r){
                    if(r==col) continue;
                    const double f=s[r][col];
                    for(int c=0;c<4;++c) s[r][c]-=f*s[col][c];
                }
            }
            if(ok){
                const double centreX=-0.5*s[0][3], centreY=-0.5*s[1][3];
                const double squared=centreX*centreX+centreY*centreY-s[2][3];
                if(squared>0.0){
                    const double radius=std::sqrt(squared);
                    // Where the arc meets the real axis: the two permittivities.
                    const double half=radius*radius-centreY*centreY;
                    PlotSeries arc;
                    if(half>0.0){
                        const double reach=std::sqrt(half);
                        // The depression angle, from how far the centre sits
                        // below the axis. Zero is a single relaxation time.
                        const double depression=std::atan2(-centreY,reach)*180.0/M_PI;
                        arc.label=QStringLiteral("e(inf) %1, e(s) %2, radius %3, depression %4 deg")
                                      .arg(centreX-reach,0,'g',5).arg(centreX+reach,0,'g',5)
                                      .arg(radius,0,'g',4).arg(std::abs(depression),0,'f',1);
                    }else{
                        arc.label=QStringLiteral("fitted circle centre (%1, %2), radius %3")
                                      .arg(centreX,0,'g',4).arg(centreY,0,'g',4).arg(radius,0,'g',4);
                    }
                    arc.color=in.style.warning;
                    arc.lineWidth=qMax(1.3,in.style.lineWidth);
                    for(int k=0;k<=240;++k){
                        const double a=2.0*M_PI*double(k)/240.0;
                        arc.x.append(centreX+radius*std::cos(a));
                        arc.y.append(centreY+radius*std::sin(a));
                    }
                    out.series.append(arc);
                    if(half>0.0){
                        const double reach=std::sqrt(half);
                        PlotSeries marks;
                        marks.label=QStringLiteral("axis intercepts");
                        marks.color=in.style.danger;
                        marks.drawLine=false; marks.drawMarkers=true; marks.markerSize=6.0;
                        marks.x={centreX-reach,centreX+reach}; marks.y={0.0,0.0};
                        out.series.append(marks);
                    }
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("real part"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("imaginary part"),false,unsetValue(),unsetValue()};
        // The same argument as the Nyquist plot above: one quantity, two
        // components, and an arc whose shape is the finding.
        out.equalAspect=true;
        return out;
    }

    // -------------------------------------------------- Conductometric Titration
    // Conductivity against titrant added. There is no inflection to find here
    // and no indicator to watch: the two sides are STRAIGHT LINES with
    // different slopes, because one ion is being replaced by another of
    // different mobility, and the endpoint is where they cross. That makes it
    // the method of choice for a titration too dilute or too coloured to see.
    if(in.engine==QLatin1String("Conductometric Titration")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& volume=in.series.at(0).y;
            const QVector<double>& conductivity=in.series.at(1).y;
            const int n=qMin(volume.size(),conductivity.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(volume[i])||!finite(conductivity[i])) continue;
                rows.append(qMakePair(volume[i],conductivity[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=6){
                PlotSeries pts;
                pts.color=in.series.at(1).color;
                pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;
                QVector<double> xs,ys;
                for(const QPair<double,double>& r:rows){
                    pts.x.append(r.first); pts.y.append(r.second);
                    xs.append(r.first); ys.append(r.second);
                }
                // The split point is SEARCHED for rather than assumed at the
                // middle: the endpoint is rarely halfway along the titration,
                // and fitting two halves of the data would put the crossing
                // wherever the split happened to be.
                // PREFIX SUMS, not two fresh fits and two residual passes at
                // every candidate split.
                //
                // The search copied both halves out with `mid`, fitted each,
                // and then walked every point again to total the residuals -
                // four vector allocations and three O(n) passes per split, for
                // n splits. A titration logged at 24,000 points took 12.8
                // seconds.
                //
                // A least-squares fit and its residual sum both come from six
                // running numbers: the count and the sums of x, y, x^2, xy and
                // y^2. Moving the split one place right moves one point from
                // the right side to the left, so both sides stay current in
                // constant time, and the right side is just the totals minus
                // the left. The residual identity is the standard one:
                // RSS = Syy - a*Sy - b*Sxy at the least-squares a and b.
                struct Sums {
                    double x=0,y=0,xx=0,xy=0,yy=0; int m=0;
                    void add(double px,double py,double sign){
                        x+=sign*px; y+=sign*py; xx+=sign*px*px;
                        xy+=sign*px*py; yy+=sign*py*py; m+=int(sign);
                    }
                };
                const auto fitFrom=[](const Sums& s,LineFit& fit,double& rss){
                    if(s.m<3) return false;
                    const double denom=double(s.m)*s.xx-s.x*s.x;
                    if(std::abs(denom)<1e-18) return false;
                    fit.slope=(double(s.m)*s.xy-s.x*s.y)/denom;
                    fit.intercept=(s.y-fit.slope*s.x)/double(s.m);
                    fit.ok=finite(fit.slope)&&finite(fit.intercept);
                    if(!fit.ok) return false;
                    rss=s.yy-fit.intercept*s.y-fit.slope*s.xy;
                    return true;
                };
                int bestSplit=-1; double bestError=std::numeric_limits<double>::infinity();
                LineFit bestLeft,bestRight;
                Sums whole,left;
                for(int i=0;i<xs.size();++i) whole.add(xs[i],ys[i],1.0);
                for(int i=0;i<3&&i<xs.size();++i) left.add(xs[i],ys[i],1.0);
                for(int split=3;split<=rows.size()-3;++split){
                    if(split>3) left.add(xs[split-1],ys[split-1],1.0);
                    Sums right=whole;
                    right.add(left.x,left.y,0.0);      // structure only
                    right.x=whole.x-left.x; right.y=whole.y-left.y;
                    right.xx=whole.xx-left.xx; right.xy=whole.xy-left.xy;
                    right.yy=whole.yy-left.yy; right.m=whole.m-left.m;
                    LineFit a,b; double leftRss=0.0,rightRss=0.0;
                    if(!fitFrom(left,a,leftRss)||!fitFrom(right,b,rightRss)) continue;
                    const double error=leftRss+rightRss;
                    if(error<bestError){
                        bestError=error; bestSplit=split; bestLeft=a; bestRight=b;
                    }
                }
                if(bestSplit>0&&std::abs(bestLeft.slope-bestRight.slope)>1e-300){
                    const double endpoint=(bestRight.intercept-bestLeft.intercept)
                                         /(bestLeft.slope-bestRight.slope);
                    const double atEndpoint=bestLeft.intercept+bestLeft.slope*endpoint;
                    pts.label=QStringLiteral("endpoint at %1 (slopes %2 then %3)")
                                  .arg(endpoint,0,'g',5).arg(bestLeft.slope,0,'g',3)
                                  .arg(bestRight.slope,0,'g',3);
                    out.series.append(pts);
                    // Each branch drawn PAST the endpoint, because the endpoint
                    // is the intersection of the extrapolations and the reader
                    // should see the construction that produced it.
                    PlotSeries before,after;
                    before.label=QStringLiteral("before the endpoint");
                    before.color=in.style.warning;
                    before.lineWidth=qMax(1.2,in.style.lineWidth);
                    before.x={xs.first(),qMax(endpoint,xs[bestSplit-1])};
                    before.y={bestLeft.intercept+bestLeft.slope*before.x[0],
                              bestLeft.intercept+bestLeft.slope*before.x[1]};
                    after.label=QStringLiteral("after the endpoint");
                    after.color=in.style.positive;
                    after.lineWidth=qMax(1.2,in.style.lineWidth);
                    after.x={qMin(endpoint,xs[bestSplit]),xs.last()};
                    after.y={bestRight.intercept+bestRight.slope*after.x[0],
                             bestRight.intercept+bestRight.slope*after.x[1]};
                    out.series.append(before);
                    out.series.append(after);
                    PlotSeries mark;
                    mark.label=QStringLiteral("endpoint");
                    mark.color=in.style.danger;
                    mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.5;
                    mark.x={endpoint}; mark.y={atEndpoint};
                    out.series.append(mark);
                }else{
                    pts.label=QStringLiteral("no change of slope found");
                    out.series.append(pts);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("titrant added"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("conductivity"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Hot-Wire Conductivity
    // Temperature rise against the log of time. A line heat source in an
    // infinite medium warms as the logarithm of time, and the SLOPE of that
    // line is the conductivity - which is why a transient measurement lasting
    // seconds replaces a steady-state one lasting hours.
    if(in.engine==QLatin1String("Hot-Wire Conductivity")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=3){
            const QVector<double>& time=in.series.at(0).y;
            const QVector<double>& rise=in.series.at(1).y;
            const QVector<double>& power=in.series.at(2).y;
            const int n=qMin(time.size(),qMin(rise.size(),power.size()));
            QVector<double> heats;
            for(double v:power) if(finite(v)&&v>0.0) heats.append(v);
            std::sort(heats.begin(),heats.end());
            PlotSeries pts;
            pts.label=QStringLiteral("temperature rise");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(time[i])||!finite(rise[i])||!(time[i]>0.0)) continue;
                pts.x.append(std::log(time[i])); pts.y.append(rise[i]);
                fx.append(std::log(time[i])); fy.append(rise[i]);
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                // Only the later part of the record. The early rise is
                // dominated by the wire's own heat capacity and the contact
                // between it and the sample, and neither is the sample's
                // conductivity - a fit over the whole record reads them as if
                // they were.
                const int from=fx.size()/3;
                QVector<double> lx=fx.mid(from),ly=fy.mid(from);
                const LineFit f=fitLine(lx,ly);
                const Bounds bx=boundsOf(fx);
                if(f.ok&&bx.valid&&f.slope>0.0&&!heats.isEmpty()){
                    const double heat=heats[heats.size()/2];
                    const double conductivity=heat/(4.0*M_PI*f.slope);
                    PlotSeries line;
                    line.label=QStringLiteral("lambda %1 (q %2 per unit length, last two thirds)")
                                   .arg(conductivity,0,'g',5).arg(heat,0,'g',4);
                    line.color=in.style.warning;
                    line.lineWidth=qMax(1.3,in.style.lineWidth);
                    line.x={lx.first(),bx.hi};
                    line.y={f.intercept+f.slope*lx.first(),f.intercept+f.slope*bx.hi};
                    out.series.append(line);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("ln time"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("temperature rise"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------- Multi-Vari Chart
    // Every reading, grouped by part and coloured by operator, with the part
    // means joined. The point is to see WHERE the variation lives before
    // deciding what to fix: spread within a part is the measurement, spread
    // between parts is the process, and a consistent offset between operators
    // is the method. The three are separated numerically as well, because by
    // eye the largest is the only one anybody sees.
    if(in.engine==QLatin1String("Multi-Vari Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=3){
            const QVector<double>& part=in.series.at(0).y;
            const QVector<double>& measured=in.series.at(1).y;
            const QVector<double>& operatorId=in.series.at(2).y;
            const int n=qMin(part.size(),qMin(measured.size(),operatorId.size()));
            QMap<double,QVector<double>> byOperatorX,byOperatorY;
            QMap<double,QVector<double>> byPart;
            QMap<double,QVector<double>> byOperator;
            double grand=0.0; int total=0;
            for(int i=0;i<n;++i){
                if(!finite(part[i])||!finite(measured[i])||!finite(operatorId[i])) continue;
                byOperatorX[operatorId[i]].append(part[i]);
                byOperatorY[operatorId[i]].append(measured[i]);
                byPart[part[i]].append(measured[i]);
                byOperator[operatorId[i]].append(measured[i]);
                grand+=measured[i]; ++total;
            }
            if(total>=4){
                grand/=double(total);
                int index=0;
                for(auto it=byOperatorX.constBegin();it!=byOperatorX.constEnd();++it,++index){
                    PlotSeries s;
                    s.label=QStringLiteral("operator %1").arg(QString::number(it.key(),'g',12));
                    s.color=categoryColour(in,index,std::fmod(0.08+double(index)*0.19,1.0),0.65,0.95);
                    s.drawLine=false; s.drawMarkers=true; s.markerSize=4.2;
                    s.x=it.value(); s.y=byOperatorY.value(it.key());
                    out.series.append(s);
                }
                // Sums of squares about the grand mean, split three ways. Not an
                // ANOVA table - no degrees of freedom, no F - because these are
                // being compared with each other for SIZE and not tested
                // against a null.
                double withinPart=0.0,betweenPart=0.0,betweenOperator=0.0;
                PlotSeries means;
                means.label=QStringLiteral("part means");
                means.color=in.style.warning;
                means.lineWidth=qMax(1.3,in.style.lineWidth);
                means.drawMarkers=true; means.markerSize=4.6;
                for(auto it=byPart.constBegin();it!=byPart.constEnd();++it){
                    double mean=0.0;
                    for(double v:it.value()) mean+=v;
                    mean/=double(it.value().size());
                    for(double v:it.value()) withinPart+=(v-mean)*(v-mean);
                    betweenPart+=double(it.value().size())*(mean-grand)*(mean-grand);
                    means.x.append(it.key()); means.y.append(mean);
                }
                for(auto it=byOperator.constBegin();it!=byOperator.constEnd();++it){
                    double mean=0.0;
                    for(double v:it.value()) mean+=v;
                    mean/=double(it.value().size());
                    betweenOperator+=double(it.value().size())*(mean-grand)*(mean-grand);
                }
                const double all=qMax(1e-300,withinPart+betweenPart);
                if(!out.series.isEmpty())
                    out.series[0].label=QStringLiteral("%1; within part %2%, between parts %3%, operator spread %4% of the total")
                        .arg(out.series[0].label)
                        .arg(100.0*withinPart/all,0,'f',1)
                        .arg(100.0*betweenPart/all,0,'f',1)
                        .arg(100.0*betweenOperator/all,0,'f',1);
                if(means.x.size()>=2) out.series.append(means);
                PlotSeries centre;
                centre.label=QStringLiteral("grand mean");
                centre.color=in.style.gridColor;
                centre.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                centre.dashPattern={5,4};
                const Bounds bp=boundsOf(means.x);
                if(bp.valid){
                    centre.x={bp.lo,bp.hi}; centre.y={grand,grand};
                    out.series.append(centre);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("part"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("measurement"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Tolerance Interval Plot
    // Limits that contain a stated share of the POPULATION, with stated
    // confidence - which is a different and much wider thing than a confidence
    // interval for the mean, and the two are constantly swapped. This one gets
    // wider than a confidence interval and does not shrink to nothing as the
    // sample grows, because the population's own spread never goes away.
    if(in.engine==QLatin1String("Tolerance Interval Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& value=in.series.at(1).y;
            const QVector<double>& index=in.series.at(0).y;
            QVector<double> xs,ys;
            for(int i=0;i<qMin(index.size(),value.size());++i){
                if(!finite(index[i])||!finite(value[i])) continue;
                xs.append(index[i]); ys.append(value[i]);
            }
            const int count=ys.size();
            if(count>=5){
                double mean=0.0;
                for(double v:ys) mean+=v;
                mean/=double(count);
                double variance=0.0;
                for(double v:ys) variance+=(v-mean)*(v-mean);
                variance/=double(count-1);
                const double deviation=std::sqrt(qMax(0.0,variance));
                // Howe's k for a two-sided 95/95 interval. The chi-square
                // quantile it needs comes from the Wilson-Hilferty
                // approximation rather than a table; both are named on the
                // figure, because a tolerance factor quoted without its method
                // cannot be checked.
                const double freedom=double(count-1);
                const double coverage=normalQuantile(0.975);
                const double lower=normalQuantile(0.05);
                const double chiSquare=freedom*std::pow(1.0-2.0/(9.0*freedom)
                                        +lower*std::sqrt(2.0/(9.0*freedom)),3.0);
                const double factor=(chiSquare>1e-300)
                    ? coverage*std::sqrt(freedom*(1.0+1.0/double(count))/chiSquare)
                    : std::numeric_limits<double>::quiet_NaN();
                int outside=0;
                if(finite(factor))
                    for(double v:ys)
                        if(std::abs(v-mean)>factor*deviation) ++outside;
                PlotSeries pts;
                pts.label=finite(factor)
                    ? QStringLiteral("%1 observations; k %2, interval %3 to %4, %5 outside")
                          .arg(count).arg(factor,0,'f',3)
                          .arg(mean-factor*deviation,0,'g',5)
                          .arg(mean+factor*deviation,0,'g',5).arg(outside)
                    : QStringLiteral("%1 observations").arg(count);
                pts.color=in.series.at(1).color;
                pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=3.8;
                pts.x=xs; pts.y=ys;
                out.series.append(pts);
                const Bounds bx=boundsOf(xs);
                if(bx.valid&&finite(factor)){
                    const struct { double at; const char* name; } lines[]={
                        {0.0,"mean"},
                        {1.0,"95/95 tolerance limits"},
                        {-1.0,""}};
                    for(const auto& rule:lines){
                        PlotSeries limit;
                        limit.label=QString::fromLatin1(rule.name);
                        limit.color=(rule.at==0.0)?in.style.gridColor:in.style.danger;
                        limit.lineWidth=qMax(0.9,in.style.lineWidth*0.8);
                        if(rule.at!=0.0) limit.dashPattern={5,4};
                        limit.x={bx.lo,bx.hi};
                        limit.y={mean+rule.at*factor*deviation,mean+rule.at*factor*deviation};
                        out.series.append(limit);
                    }
                    // The confidence interval for the MEAN, drawn beside it, so
                    // the difference in width is visible rather than asserted.
                    const double student=coverage+(coverage*coverage*coverage+coverage)
                                                   /(4.0*freedom);
                    const double narrow=student*deviation/std::sqrt(double(count));
                    for(int side=-1;side<=1;side+=2){
                        PlotSeries limit;
                        limit.label=(side<0)?QString()
                                            :QStringLiteral("95% for the mean, for comparison");
                        limit.color=in.style.warning;
                        limit.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                        limit.dashPattern={2,3};
                        limit.x={bx.lo,bx.hi};
                        limit.y={mean+double(side)*narrow,mean+double(side)*narrow};
                        out.series.append(limit);
                    }
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("observation"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("value"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------- Rare-Event Interval Chart
    // The TIME BETWEEN events rather than a count of them per period. When
    // events are rare, a count chart spends most of its life at zero and can
    // only ever signal upward; charting the interval instead signals a cluster
    // as a short gap, and an improvement as a long one - which is the direction
    // anybody running a safety programme actually cares about.
    if(in.engine==QLatin1String("Rare-Event Interval Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& when=in.series.at(1).y;
            QVector<double> times;
            for(double v:when) if(finite(v)) times.append(v);
            std::sort(times.begin(),times.end());
            QVector<double> gaps;
            for(int i=1;i<times.size();++i) gaps.append(times[i]-times[i-1]);
            if(gaps.size()>=4){
                double mean=0.0;
                for(double v:gaps) mean+=v;
                mean/=double(gaps.size());
                PlotSeries series;
                series.color=in.series.at(1).color;
                series.lineWidth=qMax(1.2,in.style.lineWidth);
                series.drawMarkers=true; series.markerSize=4.0;
                for(int i=0;i<gaps.size();++i){
                    series.x.append(double(i+1)); series.y.append(gaps[i]);
                }
                // Exponential limits, not three sigma. Intervals between
                // independent rare events are exponential, which is strongly
                // skewed, and a symmetric limit on it puts the lower bound
                // below zero and the upper one far too close.
                const double lowerLimit=-mean*std::log(1.0-0.00135);
                const double upperLimit=-mean*std::log(0.00135);
                int short_=0,long_=0;
                for(double v:gaps){
                    if(v<lowerLimit) ++short_;
                    if(v>upperLimit) ++long_;
                }
                series.label=QStringLiteral("mean interval %1; %2 unusually short, %3 unusually long")
                                 .arg(mean,0,'g',4).arg(short_).arg(long_);
                out.series.append(series);
                const struct { double level; const char* name; } limits[]={
                    {mean,"mean interval"},
                    {lowerLimit,"lower limit (0.135% of an exponential)"},
                    {upperLimit,"upper limit (99.865%)"}};
                for(const auto& rule:limits){
                    PlotSeries limit;
                    limit.label=QString::fromLatin1(rule.name);
                    limit.color=(rule.level==mean)?in.style.gridColor
                               :(rule.level<mean)?in.style.danger:in.style.positive;
                    limit.lineWidth=qMax(0.9,in.style.lineWidth*0.8);
                    if(rule.level!=mean) limit.dashPattern={5,4};
                    limit.x={1.0,double(gaps.size())};
                    limit.y={rule.level,rule.level};
                    out.series.append(limit);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("event"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("interval since the previous event"),false,
                           unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------------- Spaghetti Plot
    // Every subject's own trajectory, drawn thin, with the mean over the top.
    // The mean alone can be a shape that no subject has - two groups moving in
    // opposite directions average to a flat line - so the individual paths are
    // the point and the mean is the summary rather than the other way round.
    if(in.engine==QLatin1String("Spaghetti Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=3){
            const QVector<double>& time=in.series.at(0).y;
            const QVector<double>& value=in.series.at(1).y;
            const QVector<double>& subject=in.series.at(2).y;
            const int n=qMin(time.size(),qMin(value.size(),subject.size()));
            QMap<double,QVector<QPair<double,double>>> bySubject;
            QMap<double,QVector<double>> byTime;
            for(int i=0;i<n;++i){
                if(!finite(time[i])||!finite(value[i])||!finite(subject[i])) continue;
                bySubject[subject[i]].append(qMakePair(time[i],value[i]));
                byTime[time[i]].append(value[i]);
            }
            if(!bySubject.isEmpty()){
                for(auto it=bySubject.constBegin();it!=bySubject.constEnd();++it){
                    QVector<QPair<double,double>> rows=it.value();
                    std::sort(rows.begin(),rows.end(),
                              [](const QPair<double,double>& a,const QPair<double,double>& b){
                                  return a.first<b.first; });
                    PlotSeries s;
                    s.label=QString();   // one legend row per subject is not a legend
                    s.color=in.series.at(1).color;
                    s.lineWidth=qMax(0.7,in.style.lineWidth*0.55);
                    s.opacity=0.35;
                    for(const QPair<double,double>& r:rows){ s.x.append(r.first); s.y.append(r.second); }
                    if(!s.x.isEmpty()) out.series.append(s);
                }
                PlotSeries mean;
                mean.color=in.style.warning;
                mean.lineWidth=qMax(1.8,in.style.lineWidth*1.4);
                mean.drawMarkers=true; mean.markerSize=3.6;
                double lastSpread=0.0;
                for(auto it=byTime.constBegin();it!=byTime.constEnd();++it){
                    double average=0.0;
                    for(double v:it.value()) average+=v;
                    average/=double(it.value().size());
                    mean.x.append(it.key()); mean.y.append(average);
                    double spread=0.0;
                    for(double v:it.value()) spread+=(v-average)*(v-average);
                    lastSpread=(it.value().size()>1)
                               ?std::sqrt(spread/double(it.value().size()-1)):0.0;
                }
                // The subject traces are unnamed - that is the point of a
                // spaghetti plot - so this mean was the only named series and
                // its row was never drawn. The count and the spread are what
                // a reader needs to judge the bundle, so they go under the
                // figure where they are always shown.
                mean.label=QStringLiteral("mean");
                out.figureNote=QStringLiteral("Mean of %1 subjects; standard deviation "
                                              "across subjects at the last time %2.")
                                   .arg(bySubject.size()).arg(formatMeasured(lastSpread));
                out.series.append(mean);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("time"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("value"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------------ Unit Hydrograph
    // A storm hydrograph with the baseflow taken off it, because only the part
    // above the baseflow came from the rain. The separation is a straight line
    // from the start of the rise to where the falling limb returns to the
    // pre-storm flow, which is a convention rather than a measurement and is
    // named as one on the figure.
    if(in.engine==QLatin1String("Unit Hydrograph")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& time=in.series.at(0).y;
            const QVector<double>& discharge=in.series.at(1).y;
            const int n=qMin(time.size(),discharge.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(time[i])||!finite(discharge[i])) continue;
                rows.append(qMakePair(time[i],discharge[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=6){
                int peakAt=0;
                for(int i=1;i<rows.size();++i) if(rows[i].second>rows[peakAt].second) peakAt=i;
                // The rise begins at the last minimum before the peak, and the
                // recession ends where the flow next comes back down to that
                // level. Taking the first and last samples instead would put
                // the separation line across whatever the record happens to
                // start and stop at.
                int start=peakAt;
                // Strictly decreasing, not "less than or equal". With <= the
                // walk crosses a flat stretch of baseflow and carries on to the
                // beginning of the record, which put the left end of the
                // separation line wherever the record happened to start and
                // reported a time to peak measured from there - 6 instead of 4
                // on a pulse that began at 2. A flat run means the rise has not
                // started yet, so the walk stops at its upper end.
                while(start>0&&rows[start-1].second<rows[start].second) --start;
                int end=rows.size()-1;
                for(int i=peakAt+1;i<rows.size();++i)
                    if(rows[i].second<=rows[start].second){ end=i; break; }
                auto baseAt=[&](double t){
                    const double span=rows[end].first-rows[start].first;
                    if(!(std::abs(span)>1e-300)) return rows[start].second;
                    const double f=(t-rows[start].first)/span;
                    return rows[start].second
                          +qBound(0.0,f,1.0)*(rows[end].second-rows[start].second);
                };
                PlotSeries total;
                total.color=in.series.at(1).color;
                total.lineWidth=qMax(1.3,in.style.lineWidth);
                PlotSeries direct;
                direct.color=in.style.warning;
                direct.lineWidth=qMax(1.3,in.style.lineWidth);
                double volume=0.0;
                double previous=0.0;
                for(int i=0;i<rows.size();++i){
                    total.x.append(rows[i].first); total.y.append(rows[i].second);
                    const double above=(i>=start&&i<=end)
                                       ?qMax(0.0,rows[i].second-baseAt(rows[i].first)):0.0;
                    direct.x.append(rows[i].first); direct.y.append(above);
                    if(i>start&&i<=end)
                        volume+=0.5*(above+previous)*(rows[i].first-rows[i-1].first);
                    previous=above;
                }
                total.label=QStringLiteral("total flow; peak %1 at %2, time to peak %3")
                                .arg(rows[peakAt].second,0,'g',5).arg(rows[peakAt].first,0,'g',5)
                                .arg(rows[peakAt].first-rows[start].first,0,'g',4);
                direct.label=QStringLiteral("direct runoff, volume %1").arg(volume,0,'g',5);
                out.series.append(total);
                out.series.append(direct);
                PlotSeries separation;
                separation.label=QStringLiteral("straight-line baseflow separation (a convention)");
                separation.color=in.style.gridColor;
                separation.lineWidth=qMax(0.9,in.style.lineWidth*0.8);
                separation.dashPattern={5,4};
                separation.x={rows[start].first,rows[end].first};
                separation.y={rows[start].second,rows[end].second};
                out.series.append(separation);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("time"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("discharge"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Recession Curve Analysis
    // The falling limb only, on a logarithmic ordinate, where a draining
    // catchment is a straight line. The constant that comes off it says what
    // fraction of today's flow is left tomorrow, and it is a property of the
    // AQUIFER rather than of the storm - which is why it can be measured on one
    // recession and used on another.
    if(in.engine==QLatin1String("Recession Curve Analysis")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& time=in.series.at(0).y;
            const QVector<double>& discharge=in.series.at(1).y;
            const int n=qMin(time.size(),discharge.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(time[i])||!finite(discharge[i])||!(discharge[i]>0.0)) continue;
                rows.append(qMakePair(time[i],discharge[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=5){
                int peakAt=0;
                for(int i=1;i<rows.size();++i) if(rows[i].second>rows[peakAt].second) peakAt=i;
                PlotSeries whole;
                whole.label=QStringLiteral("hydrograph");
                whole.color=in.series.at(1).color;
                whole.lineWidth=qMax(1.1,in.style.lineWidth*0.9);
                for(const QPair<double,double>& r:rows){ whole.x.append(r.first); whole.y.append(r.second); }
                out.series.append(whole);
                QVector<double> lx,ly;
                for(int i=peakAt;i<rows.size();++i){
                    lx.append(rows[i].first); ly.append(std::log(rows[i].second));
                }
                const LineFit f=fitLine(lx,ly);
                if(f.ok&&f.slope<0.0&&lx.size()>=3){
                    // The constant is per unit of the time column, whatever
                    // that unit is - so it is quoted as a fraction remaining
                    // after one unit rather than as "per day", which the data
                    // never said.
                    const double remaining=std::exp(f.slope);
                    PlotSeries fit;
                    fit.label=QStringLiteral("recession constant %1 per unit time, flow halves in %2")
                                  .arg(remaining,0,'f',4)
                                  .arg(std::log(2.0)/(-f.slope),0,'g',4);
                    fit.color=in.style.warning;
                    fit.lineWidth=qMax(1.3,in.style.lineWidth);
                    for(int i=0;i<lx.size();++i){
                        fit.x.append(lx[i]);
                        fit.y.append(std::exp(f.intercept+f.slope*lx[i]));
                    }
                    out.series.append(fit);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("time"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("discharge"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Step Response Metrics
    // A step response with the four numbers a controller is tuned against. The
    // final value is taken as the average of the LAST tenth rather than as the
    // last sample, because one noisy final reading would move every one of the
    // four - the rise time, the overshoot, the settling time and the error are
    // all measured relative to it.
    if(in.engine==QLatin1String("Step Response Metrics")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& time=in.series.at(0).y;
            const QVector<double>& response=in.series.at(1).y;
            const int n=qMin(time.size(),response.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(time[i])||!finite(response[i])) continue;
                rows.append(qMakePair(time[i],response[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=8){
                const int tail=qMax(1,rows.size()/10);
                double settled=0.0;
                for(int i=rows.size()-tail;i<rows.size();++i) settled+=rows[i].second;
                settled/=double(tail);
                const double start=rows.first().second;
                const double step=settled-start;
                const auto crossing=[&](double share){
                    const double level=start+share*step;
                    for(int i=1;i<rows.size();++i){
                        const double a=rows[i-1].second,b=rows[i].second;
                        if((a>=level)==(b>=level)||a==b) continue;
                        return rows[i-1].first+(level-a)/(b-a)
                                              *(rows[i].first-rows[i-1].first);
                    }
                    return std::numeric_limits<double>::quiet_NaN();
                };
                const double tenth=crossing(0.10), ninth=crossing(0.90);
                int peakAt=0;
                for(int i=1;i<rows.size();++i)
                    if((step>=0.0?rows[i].second>rows[peakAt].second
                                 :rows[i].second<rows[peakAt].second)) peakAt=i;
                const double overshoot=(std::abs(step)>1e-300)
                    ? 100.0*(rows[peakAt].second-settled)/step
                    : 0.0;
                // Settling time is the LAST time the response leaves the band,
                // not the first time it enters: a response that enters, leaves
                // and comes back has not settled at the first entry.
                double settling=rows.first().first;
                const double band=0.02*std::abs(step);
                for(int i=0;i<rows.size();++i)
                    if(std::abs(rows[i].second-settled)>band) settling=rows[i].first;
                PlotSeries curve;
                curve.label=(finite(tenth)&&finite(ninth))
                    ? QStringLiteral("final %1, rise %2, overshoot %3%, settles by %4 (2% band)")
                          .arg(settled,0,'g',5).arg(ninth-tenth,0,'g',4)
                          .arg(qMax(0.0,overshoot),0,'f',1).arg(settling,0,'g',4)
                    : QStringLiteral("final %1, overshoot %2%")
                          .arg(settled,0,'g',5).arg(qMax(0.0,overshoot),0,'f',1);
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.4,in.style.lineWidth);
                for(const QPair<double,double>& r:rows){ curve.x.append(r.first); curve.y.append(r.second); }
                out.series.append(curve);
                const struct { double level; const char* name; } lines[]={
                    {settled,"final value"},
                    {settled+band,"2% band"},
                    {settled-band,""}};
                for(const auto& rule:lines){
                    PlotSeries limit;
                    limit.label=QString::fromLatin1(rule.name);
                    limit.color=(rule.level==settled)?in.style.warning:in.style.gridColor;
                    limit.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                    if(rule.level!=settled) limit.dashPattern={5,4};
                    limit.x={rows.first().first,rows.last().first};
                    limit.y={rule.level,rule.level};
                    out.series.append(limit);
                }
                PlotSeries mark;
                mark.label=QStringLiteral("peak");
                mark.color=in.style.danger;
                mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.0;
                mark.x={rows[peakAt].first}; mark.y={rows[peakAt].second};
                out.series.append(mark);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("time"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("response"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------------------- Jitter Bathtub
    // Bit error rate against where in the bit the receiver samples. Measuring
    // down to one error in a trillion would take days, so the two walls are
    // measured where errors are common and EXTRAPOLATED - and the extrapolation
    // is done in Q rather than in the error rate, because the walls are
    // straight in Q and curved in anything else.
    if(in.engine==QLatin1String("Jitter Bathtub")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& position=in.series.at(0).y;
            const QVector<double>& rate=in.series.at(1).y;
            const int n=qMin(position.size(),rate.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(position[i])||!finite(rate[i])) continue;
                if(!(rate[i]>0.0)||!(rate[i]<0.5)) continue;
                rows.append(qMakePair(position[i],rate[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=6){
                PlotSeries measured;
                measured.color=in.series.at(1).color;
                measured.lineWidth=qMax(1.3,in.style.lineWidth);
                measured.drawMarkers=true; measured.markerSize=3.6;
                int lowestAt=0;
                for(int i=0;i<rows.size();++i){
                    measured.x.append(rows[i].first); measured.y.append(rows[i].second);
                    if(rows[i].second<rows[lowestAt].second) lowestAt=i;
                }
                // Each wall in Q against position. Q is the number of standard
                // deviations the sampling point is from the transition, and it
                // is what the dual-Dirac model says is linear here.
                QVector<double> leftX,leftQ,rightX,rightQ;
                for(int i=0;i<rows.size();++i){
                    const double q=normalQuantile(1.0-rows[i].second);
                    if(!finite(q)) continue;
                    if(i<lowestAt){ leftX.append(rows[i].first); leftQ.append(q); }
                    else if(i>lowestAt){ rightX.append(rows[i].first); rightQ.append(q); }
                }
                const LineFit left=fitLine(leftX,leftQ),right=fitLine(rightX,rightQ);
                const double target=normalQuantile(1.0-1.0e-12);
                double openLeft=std::numeric_limits<double>::quiet_NaN();
                double openRight=std::numeric_limits<double>::quiet_NaN();
                if(left.ok&&std::abs(left.slope)>1e-300)
                    openLeft=(target-left.intercept)/left.slope;
                if(right.ok&&std::abs(right.slope)>1e-300)
                    openRight=(target-right.intercept)/right.slope;
                measured.label=(finite(openLeft)&&finite(openRight))
                    ? QStringLiteral("eye opening at 1e-12: %1 to %2, width %3")
                          .arg(openLeft,0,'g',4).arg(openRight,0,'g',4)
                          .arg(std::abs(openRight-openLeft),0,'g',4)
                    : QStringLiteral("floor %1 at %2 (not enough of one wall to extrapolate)")
                          .arg(rows[lowestAt].second,0,'g',3).arg(rows[lowestAt].first,0,'g',4);
                out.series.append(measured);
                if(finite(openLeft)&&finite(openRight)){
                    // The two extrapolated walls, drawn in the error-rate space
                    // the reader is looking at rather than left in Q.
                    for(int side=0;side<2;++side){
                        const LineFit& f=(side==0)?left:right;
                        const double edge=(side==0)?openLeft:openRight;
                        const double from=(side==0)?leftX.first():rightX.last();
                        PlotSeries wall;
                        wall.label=(side==0)?QStringLiteral("extrapolated walls"):QString();
                        wall.color=in.style.warning;
                        wall.lineWidth=qMax(1.0,in.style.lineWidth*0.85);
                        wall.dashPattern={5,4};
                        for(int k=0;k<=60;++k){
                            const double x=from+(edge-from)*double(k)/60.0;
                            const double q=f.intercept+f.slope*x;
                            const double ber=0.5*std::erfc(q/std::sqrt(2.0));
                            if(!(ber>0.0)) continue;
                            wall.x.append(x); wall.y.append(ber);
                        }
                        if(wall.x.size()>=2) out.series.append(wall);
                    }
                    PlotSeries floorLine;
                    floorLine.label=QStringLiteral("1e-12");
                    floorLine.color=in.style.danger;
                    floorLine.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                    floorLine.dashPattern={2,3};
                    floorLine.x={qMin(openLeft,rows.first().first),
                                 qMax(openRight,rows.last().first)};
                    floorLine.y={1.0e-12,1.0e-12};
                    out.series.append(floorLine);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("sampling position"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("bit error rate"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Pressure Derivative Plot
    // A well test with the DERIVATIVE of the pressure change beside it, both on
    // log-log axes. The pressure curve alone looks much the same whatever the
    // reservoir is doing; the derivative separates the flow regimes, and the
    // flat stretch it settles onto is radial flow - which is the only part of
    // the test that gives a permeability.
    //
    // Differentiated with respect to the LOG of time, which is what makes the
    // radial-flow signature a horizontal line rather than a slope of minus one.
    if(in.engine==QLatin1String("Pressure Derivative Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& elapsed=in.series.at(0).y;
            const QVector<double>& change=in.series.at(1).y;
            const int n=qMin(elapsed.size(),change.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(elapsed[i])||!finite(change[i])) continue;
                if(!(elapsed[i]>0.0)||!(change[i]>0.0)) continue;
                rows.append(qMakePair(elapsed[i],change[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=8){
                PlotSeries pressure;
                pressure.label=QStringLiteral("pressure change");
                pressure.color=in.series.at(1).color;
                pressure.lineWidth=qMax(1.3,in.style.lineWidth);
                for(const QPair<double,double>& r:rows){
                    pressure.x.append(r.first); pressure.y.append(r.second);
                }
                out.series.append(pressure);
                // Bourdet's derivative, taken over a WINDOW in log time rather
                // than between neighbours. A neighbour difference on a noisy
                // gauge produces a derivative that is all noise, which is why
                // the method is defined with a smoothing span at all.
                // A SLIDING WINDOW, not a fresh fit per point.
                //
                // The window is a fixed span in log time and the data is
                // sorted, so both its ends only ever move FORWARD as the
                // centre advances. Copying the window into two vectors and
                // calling fitLine on it made the loop quadratic in the sample
                // count - a well test of 24,000 pressure readings took 36.9
                // seconds - and allocated two vectors per point along the way.
                //
                // A least-squares slope needs five running numbers: the count
                // and the sums of x, y, x^2 and xy. Adding a point at the
                // leading edge and dropping one at the trailing edge keeps all
                // five current, so the whole pass is linear and the arithmetic
                // is the same arithmetic fitLine does.
                QVector<double> at,slope;
                const double span=0.2;   // decades either side
                double sx=0,sy=0,sxx=0,sxy=0; int m=0;
                const auto addPoint=[&](int k){
                    const double lx=std::log(rows[k].first), ly=rows[k].second;
                    if(!finite(lx)||!finite(ly)) return;
                    sx+=lx; sy+=ly; sxx+=lx*lx; sxy+=lx*ly; ++m;
                };
                const auto dropPoint=[&](int k){
                    const double lx=std::log(rows[k].first), ly=rows[k].second;
                    if(!finite(lx)||!finite(ly)) return;
                    sx-=lx; sy-=ly; sxx-=lx*lx; sxy-=lx*ly; --m;
                };
                int lo=0,hi=-1;
                for(int i=0;i<rows.size();++i){
                    const double centre=std::log10(rows[i].first);
                    // Grow at the leading edge, shrink at the trailing one.
                    // The bounds are the ones the copying version used, so the
                    // window over which the slope is taken is unchanged.
                    while(hi+1<rows.size()&&std::log10(rows[hi+1].first)<centre+span){
                        ++hi; addPoint(hi);
                    }
                    if(hi<i){ ++hi; addPoint(hi); }
                    while(lo<i&&!(std::log10(rows[lo].first)>centre-span)){
                        dropPoint(lo); ++lo;
                    }
                    if(hi-lo<2||m<3) continue;
                    const double denom=double(m)*sxx-sx*sx;
                    if(std::abs(denom)<1e-18) continue;
                    const double fitted=(double(m)*sxy-sx*sy)/denom;
                    if(!finite(fitted)||!(fitted>0.0)) continue;
                    at.append(rows[i].first); slope.append(fitted);
                }
                if(at.size()>=4){
                    PlotSeries derivative;
                    derivative.color=in.style.warning;
                    derivative.lineWidth=qMax(1.3,in.style.lineWidth);
                    derivative.drawMarkers=true; derivative.markerSize=3.0;
                    derivative.x=at; derivative.y=slope;
                    // The flattest window of the derivative in log-log space is
                    // the radial-flow plateau, found rather than assumed to be
                    // at the end of the test - it often is not, because boundary
                    // effects arrive later and lift the derivative again.
                    // The same sliding-window arithmetic as the derivative
                    // above, and for the same reason. The window is a FIXED
                    // length, so moving it along is one point added and one
                    // dropped: copying it out and fitting it fresh at every
                    // start position was the second quadratic pass in this
                    // engine, and on 24,000 readings it allocated eighteen
                    // thousand vectors of six thousand doubles.
                    //
                    // The first minimum still wins, because the comparison is
                    // still strict and the starts are still visited in order.
                    const int window=qBound(3,at.size()/4,at.size());
                    int flattestAt=-1; double flattest=std::numeric_limits<double>::infinity();
                    double plateau=0.0;
                    {
                        double wx=0,wy=0,wxx=0,wxy=0,wsum=0; int wn=0;
                        const auto take=[&](int i,double sign){
                            const double lx=std::log(at[i]), ly=std::log(slope[i]);
                            if(!finite(lx)||!finite(ly)) return;
                            wx+=sign*lx; wy+=sign*ly; wxx+=sign*lx*lx; wxy+=sign*lx*ly;
                            wsum+=sign*slope[i];
                            wn+=int(sign);
                        };
                        for(int i=0;i<window&&i<at.size();++i) take(i,1.0);
                        for(int start=0;start+window<=at.size();++start){
                            if(start>0){ take(start-1,-1.0); take(start+window-1,1.0); }
                            if(wn>=3){
                                const double denom=double(wn)*wxx-wx*wx;
                                if(std::abs(denom)>=1e-18){
                                    const double s=(double(wn)*wxy-wx*wy)/denom;
                                    const double intercept=(wy-s*wx)/double(wn);
                                    if(finite(s)&&finite(intercept)&&std::abs(s)<flattest){
                                        flattest=std::abs(s); flattestAt=start;
                                        plateau=wsum/double(window);
                                    }
                                }
                            }
                        }
                    }
                    derivative.label=(flattestAt>=0)
                        ? QStringLiteral("derivative; radial-flow plateau %1 between %2 and %3 (log-log slope %4)")
                              .arg(plateau,0,'g',4).arg(at[flattestAt],0,'g',3)
                              .arg(at[qMin(at.size()-1,flattestAt+window-1)],0,'g',3)
                              .arg(flattest,0,'f',3)
                        : QStringLiteral("derivative (no flat stretch found)");
                    out.series.append(derivative);
                    if(flattestAt>=0){
                        PlotSeries level;
                        level.label=QStringLiteral("plateau");
                        level.color=in.style.danger;
                        level.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                        level.dashPattern={5,4};
                        level.x={at.first(),at.last()};
                        level.y={plateau,plateau};
                        out.series.append(level);
                    }
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("elapsed time"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("pressure change and its derivative"),true,
                           unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Isoconversional Plot
    // Activation energy as a function of how far the reaction has gone, from
    // several runs at several heating rates. This is the companion to the
    // Kissinger plot and answers something Kissinger cannot: Kissinger uses one
    // temperature per run - the peak - and returns a single activation energy,
    // which is only meaningful if the mechanism does not change. Taking the
    // temperature at a FIXED CONVERSION in every run instead gives one energy
    // per conversion, and an energy that drifts across conversion is the
    // evidence that a single-step model does not fit.
    if(in.engine==QLatin1String("Isoconversional Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        const double gasConstant=8.314462618;
        if(in.series.size()>=3){
            const QVector<double>& heating=in.series.at(0).y;
            const QVector<double>& temperature=in.series.at(1).y;
            const QVector<double>& conversion=in.series.at(2).y;
            const int n=qMin(heating.size(),qMin(temperature.size(),conversion.size()));
            // Grouped by heating rate, each run sorted by conversion so a
            // temperature can be read off it at any conversion asked for.
            QMap<double,QVector<QPair<double,double>>> byRate;
            for(int i=0;i<n;++i){
                if(!finite(heating[i])||!finite(temperature[i])||!finite(conversion[i])) continue;
                if(!(heating[i]>0.0)||!(temperature[i]>0.0)) continue;
                byRate[heating[i]].append(qMakePair(conversion[i],temperature[i]));
            }
            for(auto it=byRate.begin();it!=byRate.end();++it)
                std::sort(it.value().begin(),it.value().end(),
                          [](const QPair<double,double>& a,const QPair<double,double>& b){
                              return a.first<b.first; });
            if(byRate.size()>=3){
                PlotSeries energies;
                energies.color=in.series.at(1).color;
                energies.lineWidth=qMax(1.4,in.style.lineWidth);
                energies.drawMarkers=true; energies.markerSize=4.0;
                double lowest=1e300,highest=-1e300;
                for(int step=1;step<=18;++step){
                    const double share=0.05*double(step);
                    QVector<double> inverse,logRate;
                    for(auto it=byRate.constBegin();it!=byRate.constEnd();++it){
                        const QVector<QPair<double,double>>& run=it.value();
                        if(run.size()<2) continue;
                        if(share<run.first().first||share>run.last().first) continue;
                        double at=run.last().second;
                        for(int i=1;i<run.size();++i){
                            if(run[i].first<share) continue;
                            const double x0=run[i-1].first,x1=run[i].first;
                            at=(x1>x0)?run[i-1].second
                                       +(share-x0)/(x1-x0)*(run[i].second-run[i-1].second)
                                     :run[i].second;
                            break;
                        }
                        if(!(at>0.0)) continue;
                        inverse.append(1000.0/at);
                        logRate.append(std::log(it.key()));
                    }
                    if(inverse.size()<3) continue;
                    // Ozawa-Flynn-Wall: ln(beta) against 1/T at fixed
                    // conversion, with a slope of -1.052 Ea/R. The 1.052 is
                    // Doyle's approximation to the temperature integral and is
                    // the reason this differs from a plain Arrhenius slope by
                    // five per cent - which is a real five per cent and is
                    // named here rather than quietly dropped.
                    const LineFit f=fitLine(inverse,logRate);
                    if(!f.ok||!(f.slope<0.0)) continue;
                    const double activation=-f.slope*gasConstant/1.052;
                    energies.x.append(share); energies.y.append(activation);
                    lowest=qMin(lowest,activation); highest=qMax(highest,activation);
                }
                if(energies.x.size()>=2){
                    const double drift=(lowest>0.0)?100.0*(highest-lowest)/lowest:0.0;
                    energies.label=QStringLiteral("Ea by conversion (Ozawa-Flynn-Wall, Doyle 1.052); %1 to %2 kJ/mol, %3% drift")
                                       .arg(lowest,0,'f',1).arg(highest,0,'f',1)
                                       .arg(drift,0,'f',1);
                    out.series.append(energies);
                    double mean=0.0;
                    for(double v:energies.y) mean+=v;
                    mean/=double(energies.y.size());
                    PlotSeries average;
                    average.label=QStringLiteral("mean %1 kJ/mol").arg(mean,0,'f',1);
                    average.color=in.style.warning;
                    average.lineWidth=qMax(0.9,in.style.lineWidth*0.8);
                    average.dashPattern={5,4};
                    average.x={energies.x.first(),energies.x.last()};
                    average.y={mean,mean};
                    out.series.append(average);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("conversion"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("activation energy (kJ/mol)"),false,
                           unsetValue(),unsetValue()};
        return out;
    }

    // ======================================================================
    // BUMP CHART - who is ahead, and when that changed.
    //
    // Every series is a competitor and every x is a round. What is drawn is
    // not the value but the ORDER: first place stays first place whether it
    // won by a point or by a thousand, which is the whole reason this is a
    // different figure from a line chart of the same columns. A line chart of
    // market share answers "how much"; this one answers "who overtook whom, and
    // where" - and those crossings are invisible in the first when the
    // magnitudes are far apart.
    //
    // Ties share the AVERAGE of the places they occupy, which is the standard
    // rank and the only one that keeps the column summing to the same total:
    // two series tied for first are both 1.5, and the next is 3. Giving them
    // both 1 would draw two lines through a place that only one of them can
    // hold, and giving them 1 and 2 would invent an order the data does not
    // contain.
    //
    // Missing values do not get a rank at all. A competitor that was not
    // present in a round has no place in it, and dropping the point leaves a
    // gap in the line rather than a rank invented from a NaN - which would
    // have put it confidently last.
    //
    // The axis is INVERTED rather than the ranks negated: first place belongs
    // at the top, and PlotAxis::inverted maps the same range the other way
    // round, so the ticks still read 1, 2, 3 downwards instead of -1, -2, -3.
    if(in.engine==QLatin1String("Bump Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()<2) return out;
        int rounds=std::numeric_limits<int>::max();
        for(const PlotSeries& s:in.series) rounds=qMin(rounds,int(s.y.size()));
        if(rounds<1) return out;

        const int runners=in.series.size();
        QVector<QVector<double>> place(runners,QVector<double>(rounds,qQNaN()));
        QVector<int> order; order.reserve(runners);
        for(int r=0;r<rounds;++r){
            order.clear();
            for(int c=0;c<runners;++c)
                if(finite(in.series.at(c).y.at(r))) order.append(c);
            // Highest value takes first place. Sorted by value, then by series
            // position so that a tie between two runners is broken the same way
            // in every round and the lines do not cross for no reason.
            std::sort(order.begin(),order.end(),[&](int a,int b){
                const double va=in.series.at(a).y.at(r), vb=in.series.at(b).y.at(r);
                if(va!=vb) return va>vb;
                return a<b;
            });
            int i=0;
            while(i<order.size()){
                int j=i;
                while(j+1<order.size()
                      &&in.series.at(order.at(j+1)).y.at(r)
                        ==in.series.at(order.at(i)).y.at(r)) ++j;
                const double shared=double(i+j)/2.0+1.0;      // 1-based, averaged
                for(int k=i;k<=j;++k) place[order.at(k)][r]=shared;
                i=j+1;
            }
        }

        for(int c=0;c<runners;++c){
            PlotSeries s=in.series.at(c);
            s.y=place.at(c);
            if(s.x.size()>rounds) s.x.resize(rounds);
            // A bump chart is read at its crossings, so the marks matter as
            // much as the lines: the dot is where a place was held and the
            // segment between two dots is where it changed.
            s.drawLine=true;
            s.drawMarkers=true;
            s.markerSize=qMax(s.markerSize,8.0); s.markerSizeExplicit=true;
            s.lineWidth=qMax(s.lineWidth,2.6);   s.lineWidthExplicit=true;
            out.series.append(s);
        }
        out.xAxis=in.xAxis;
        out.yAxis=PlotAxis{QStringLiteral("place"),false,unsetValue(),unsetValue()};
        out.yAxis.inverted=true;
        // Half a place of air at each end, so first and last place are not
        // drawn along the frame itself.
        out.yAxis.min=0.5;
        out.yAxis.max=double(runners)+0.5;
        return out;
    }

    // ======================================================================
    // DUMBBELL PLOT - transposed here so the FRAME is right.
    //
    // The painter draws a value along x at a row's height, and the frame is
    // computed from the series before any painter runs - so the series have to
    // carry the value in .x and the row in .y, exactly as the Gantt and the
    // availability timeline pack their bars. Drawn straight from the mapped
    // columns instead, the frame was ruled in whatever the x column happened to
    // be (hours) and every dot landed in a finger's width of it.
    //
    // Two mapped columns: the two measurements. Anything past the second is
    // dropped rather than drawn - three dots on a stalk is a dot plot, and the
    // catalogue has one.
    if(in.engine==QLatin1String("Dumbbell Plot")){
        PlotSpec out=derivedAs(in,in.engine);
        if(in.series.size()<2) return out;
        const PlotSeries& a=in.series.at(0);
        const PlotSeries& b=in.series.at(1);
        int rows=qMin(a.y.size(),b.y.size());
        // A row per category, and a category is a word-sized thing. Past about
        // thirty the lanes are thinner than the heads that go in them.
        constexpr int kMaxRows=30;
        rows=qMin(rows,kMaxRows);
        if(rows<1) return out;
        PlotSeries first=a,second=b;
        first.x.clear(); first.y.clear();
        second.x.clear(); second.y.clear();
        for(int i=0;i<rows;++i){
            first.x.append(a.y.at(i));  first.y.append(double(i));
            second.x.append(b.y.at(i)); second.y.append(double(i));
        }
        out.series.append(first);
        out.series.append(second);
        // The quantity, not one of the two columns measuring it: labelling
        // the axis "2019" when the figure also carries 2024 says the wrong
        // thing about half the dots on it.
        out.xAxis=PlotAxis{in.yAxis.label.isEmpty()?QStringLiteral("value")
                                                   :in.yAxis.label,
                           false,unsetValue(),unsetValue()};
        // The rows are an ordering, not a measurement, so the axis says which
        // category rather than pretending the numbers on it mean anything.
        out.yAxis=PlotAxis{in.xAxis.label.isEmpty()?QStringLiteral("category")
                                                   :in.xAxis.label,
                           false,-0.6,double(rows)-0.4};
        return out;
    }

    return std::nullopt;
}

// ==========================================================================
// DUMBBELL PLOT - the same thing measured twice, and the distance between.
//
// One row per category: a dot for the first measurement, a dot for the second,
// and a bar joining them. What is being read is the GAP - before and after,
// two years, two groups - and a pair of overlaid bar charts does not show it:
// the eye has to subtract two lengths from a shared baseline, which is the one
// arithmetic it is worst at. Here the gap is drawn, so it is compared by
// length like anything else.
//
// Categories run DOWN the page. This is not a style choice: a category label
// is a word, words are wide, and a horizontal category axis either truncates
// them or turns them on their side. Every published dumbbell is laid out this
// way for that reason.
//
// Series 0 and 1 are the two measurements, and each row of them is one
// category. Further series are ignored rather than drawn - three dots on a
// stalk is a dot plot, not a dumbbell, and the catalogue has one.
// The rewrite below hands this two series already transposed: x is the
// measured value and y is the row the category sits on. That has to happen in
// prepareSpec rather than here, because the FRAME is computed from the series
// before any painter sees them - drawing the value along x while the series
// still carried time along x gave a frame ruled 0 to 12 hours with every dot
// squeezed into a finger's width of it. The same reason drawFloatingRow's
// engines pack their bars as y={row}, x={start,end}.
void QtPlotBackend::drawDumbbell(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    if(spec.series.size()<2) return;
    const PlotSeries& from=spec.series.at(0);
    const PlotSeries& to=spec.series.at(1);
    const int rows=qMin(qMin(from.x.size(),from.y.size()),
                        qMin(to.x.size(),to.y.size()));
    if(rows<1) return;

    // The connector is thick enough to read as a measured distance and the
    // heads sit on top of it, so a zero gap is still two dots rather than a
    // smudge.
    const double lane=(f.plotArea.height())/double(qMax(1,rows));
    const double thickness=qBound(2.0,lane*0.18,10.0);
    const double head=qBound(3.0,lane*0.30,14.0);

    p->save();
    for(int i=0;i<rows;++i){
        const double a=from.x.at(i), b=to.x.at(i);
        const double row=from.y.at(i);
        if(!finite(a)||!finite(b)||!finite(row)) continue;
        const QPointF pa=toDevice(f,a,row), pb=toDevice(f,b,row);
        QColor bar=spec.style.foreground;
        bar.setAlphaF(0.35);
        QPen stalk(bar); stalk.setWidthF(thickness); stalk.setCapStyle(Qt::FlatCap);
        p->setPen(stalk);
        p->drawLine(pa,pb);
        p->setPen(Qt::NoPen);
        p->setBrush(from.color); p->drawEllipse(pa,head,head);
        p->setBrush(to.color);   p->drawEllipse(pb,head,head);
    }
    p->restore();
}

namespace {
// The value a single-number figure reports, and the range it is reported
// against.
//
// The LAST finite value, not the mean and not the maximum: a gauge and a
// bullet chart both answer "where are we now", and a column of readings ends
// at now. A mean would answer a question nobody asked of a dial.
// The next round number at or above a value: 1, 2 or 5 times a power of ten.
//
// A DIAL WHOSE TOP IS ITS OWN READING ALWAYS READS FULL.
//
// The scale used to end at the largest number in the column, which for a
// single reading is the reading - so 97.4% uptime drew a completely full ring,
// and so did 12%. The sweep is the whole of what a dial says and it was saying
// the same thing about every value it was ever given.
//
// An instrument's scale ends at a round number above the reading, which is why
// a speedometer stops at 140 and not at however fast the car went yesterday.
// 97.4 gives 100, 412 gives 500, 0.31 gives 0.5. A person who wants a
// particular scale types the axis limits and those still win.
double niceCeiling(double value){
    if(!(value>0)||!std::isfinite(value)) return 1.0;
    const double magnitude=std::pow(10.0,std::floor(std::log10(value)));
    const double steps[3]={1.0,2.0,5.0};
    for(double step:steps)
        if(value<=step*magnitude) return step*magnitude;
    return 10.0*magnitude;
}

struct Reading {
    bool ok=false;
    double value=0.0;
    double lo=0.0,hi=1.0;
    double target=0.0;
    bool hasTarget=false;
    QString label;
};
Reading readingOf(const PlotSpec& spec){
    Reading r;
    if(spec.series.isEmpty()) return r;
    const PlotSeries& s=spec.series.at(0);
    double lo=std::numeric_limits<double>::infinity(),hi=-lo;
    for(double v:s.y){
        if(!finite(v)) continue;
        r.value=v; r.ok=true;
        lo=qMin(lo,v); hi=qMax(hi,v);
    }
    if(!r.ok) return r;
    // The scale the reading is placed on. A person's own axis limits win,
    // because a dial whose ends are "the smallest and largest number in the
    // column" moves its own scale every time the data changes - so today's
    // reading cannot be compared with yesterday's picture of it.
    const bool typedLo=!isUnset(spec.yAxis.min), typedHi=!isUnset(spec.yAxis.max);
    r.lo=typedLo?spec.yAxis.min:qMin(0.0,lo);
    r.hi=typedHi?spec.yAxis.max:niceCeiling(hi);
    if(!(r.hi>r.lo)){ r.hi=r.lo+1.0; }
    // A second mapped column is the TARGET, which is the whole point of a
    // bullet chart and a useful mark on a dial. Its last finite value, for the
    // same reason as the reading's.
    if(spec.series.size()>=2){
        for(double v:spec.series.at(1).y)
            if(finite(v)){ r.target=v; r.hasTarget=true; }
    }
    r.label=spec.series.at(0).label;
    return r;
}

// Three bands behind the measure - poor, fair, good - as fractions of the
// scale. Drawn in the foreground colour at three weights rather than in red,
// amber and green: the bands are CONTEXT, and colour that shouts is colour
// competing with the measure it exists to frame. It is also the reading a
// colour-blind person gets for free, since the three are separated by
// lightness alone.
void drawQualitativeBands(QPainter* p,const QRectF& box,const PlotSpec& spec,
                          bool horizontal){
    static const double kEdge[3]={0.60,0.85,1.00};
    static const double kInk[3]={0.16,0.10,0.05};
    double startFraction=0.0;
    for(int i=0;i<3;++i){
        QColor band=spec.style.foreground;
        band.setAlphaF(kInk[i]);
        QRectF r=box;
        if(horizontal){
            r.setLeft(box.left()+box.width()*startFraction);
            r.setRight(box.left()+box.width()*kEdge[i]);
        }else{
            r.setTop(box.bottom()-box.height()*kEdge[i]);
            r.setBottom(box.bottom()-box.height()*startFraction);
        }
        p->fillRect(r,band);
        startFraction=kEdge[i];
    }
}
} // namespace

// ==========================================================================
// GAUGE - one number, against the range it is allowed to be in.
//
// A dial says two things a printed number cannot: how far through its range a
// reading sits, and whether it is near an end. That is all it says, and a
// figure that says one thing is the right figure for a wall a room glances at.
//
// Drawn as a 240-degree arc with the gap at the bottom, which is where every
// instrument puts it - the eye reads the sweep from lower-left to lower-right
// and the missing third stops a full circle's ambiguity about which way round
// it goes.
void QtPlotBackend::drawGauge(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    drawFloatingTitle(p,target,spec);
    const Reading r=readingOf(spec);
    if(!r.ok) return;

    const QRectF area=target.adjusted(target.width()*0.10,target.height()*0.16,
                                      -target.width()*0.10,-target.height()*0.10);
    // BOUNDED BY THE HEIGHT AS WELL AS THE WIDTH.
    //
    // It was qMin(width, height*1.35), which is the aspect a 240-degree arc
    // wants - and lets the diameter exceed the space there is, so the dial ran
    // off the bottom of its own figure. The ring occupies side*1.04 vertically
    // once the scale labels under it are counted, so that is what has to fit.
    const double side=qMin(area.width(),area.height()/1.10);
    if(!(side>20)) return;
    const QPointF centre(area.center().x(),area.top()+side*0.52);
    const double outer=side*0.46, thickness=qMax(6.0,side*0.11);
    const QRectF ring(centre.x()-outer,centre.y()-outer,outer*2,outer*2);

    // Qt's angles are sixteenths of a degree, anticlockwise from three
    // o'clock. The sweep starts at 210 and runs -240, which is lower-left to
    // lower-right the long way round.
    constexpr int kStart=210*16, kSpan=-240*16;
    const double through=qBound(0.0,(r.value-r.lo)/(r.hi-r.lo),1.0);

    p->save();
    p->setPen(Qt::NoPen);
    QColor track=spec.style.foreground; track.setAlphaF(0.14);
    QPen trackPen(track); trackPen.setWidthF(thickness); trackPen.setCapStyle(Qt::FlatCap);
    p->setPen(trackPen); p->setBrush(Qt::NoBrush);
    p->drawArc(ring,kStart,kSpan);

    QPen valuePen(spec.series.at(0).color);
    valuePen.setWidthF(thickness); valuePen.setCapStyle(Qt::FlatCap);
    p->setPen(valuePen);
    p->drawArc(ring,kStart,int(kSpan*through));

    // The target, as a notch across the ring rather than a second arc: it is a
    // place on the scale, not an amount of it.
    if(r.hasTarget){
        const double at=qBound(0.0,(r.target-r.lo)/(r.hi-r.lo),1.0);
        const double radians=(210.0-240.0*at)*M_PI/180.0;
        const QPointF dir(std::cos(radians),-std::sin(radians));
        QPen notch(spec.style.warning); notch.setWidthF(qMax(2.0,thickness*0.28));
        p->setPen(notch);
        p->drawLine(centre+dir*(outer-thickness*0.62),centre+dir*(outer+thickness*0.62));
    }

    // The number itself, large, in the middle. A dial without its reading
    // printed on it is an estimate, and the person asked for a measurement.
    p->setPen(spec.style.foreground);
    QFont big=font(spec,spec.style.tickSize);
    big.setPointSizeF(qMax(10.0,side*0.11));
    big.setBold(true);
    p->setFont(big);
    const QRectF numberBox(centre.x()-outer,centre.y()-outer*0.30,outer*2,outer*0.62);
    p->drawText(numberBox,Qt::AlignCenter,QString::number(r.value,'g',4));

    QFont small=font(spec,spec.style.tickSize);
    p->setFont(small);
    QColor quiet=spec.style.foreground; quiet.setAlphaF(0.7);
    p->setPen(quiet);
    p->drawText(QRectF(centre.x()-outer,centre.y()+outer*0.30,outer*2,outer*0.34),
                Qt::AlignCenter,r.label);
    // The ends of the scale, BELOW the arc's two ends rather than inside the
    // ring, where the value and the series name already are and where these
    // were being drawn over them.
    const QFontMetricsF fm(small,p->device());
    const double armY=centre.y()+outer*0.62;      // the 210/-30 degree ends
    const double armX=outer*std::cos(30.0*M_PI/180.0);
    p->drawText(QPointF(centre.x()-armX-fm.horizontalAdvance(QString::number(r.lo,'g',3))*0.5,
                        armY+fm.height()),QString::number(r.lo,'g',3));
    const QString top=QString::number(r.hi,'g',3);
    p->drawText(QPointF(centre.x()+armX-fm.horizontalAdvance(top)*0.5,
                        armY+fm.height()),top);
    p->restore();
}

// ==========================================================================
// BULLET CHART - the same reading, laid flat, with room for several.
//
// Tufte's replacement for a wall of dials: a dial spends a large square on one
// number, and a row of them cannot be compared because each carries its own
// circle. A bullet is a strip - the measure as a bar, the target as a tick
// across it, and the qualitative bands behind - so twenty of them stack into
// the space one gauge takes and every one is read against the same left edge.
//
// One strip per mapped series, so a table of measures draws a panel of them.
// The target is the LAST mapped series when there is more than one, which is
// the convention a bullet chart is built around: a bar with nothing to beat is
// a bar.
void QtPlotBackend::drawBullet(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    drawFloatingTitle(p,target,spec);
    if(spec.series.isEmpty()) return;
    const PlotSeries& measure=spec.series.at(0);
    const PlotSeries* goal=spec.series.size()>=2?&spec.series.at(1):nullptr;

    // ONE STRIP PER ROW, ON ONE SCALE.
    //
    // The first shape of this drew a strip per mapped SERIES, each scaled to
    // its own range, and it was wrong in the way that matters: strips on
    // different scales cannot be compared, and being comparable down a column
    // is the entire argument for a bullet chart over a panel of dials. Two of
    // the four rows also came out empty, because a column whose last value sits
    // at its own minimum is nought percent of its own range - which is true and
    // useless.
    //
    // So: the measure column is the measure, one row each, all against the same
    // ends; the second mapped column is the target for the row beside it, which
    // is how a table of KPIs is actually written down.
    int rows=measure.y.size();
    if(goal) rows=qMin(rows,int(goal->y.size()));
    // Past a dozen the strips are thinner than their own labels.
    constexpr int kMaxRows=12;
    rows=qMin(rows,kMaxRows);
    if(rows<1) return;

    double lo=std::numeric_limits<double>::infinity(),hi=-lo;
    for(int i=0;i<rows;++i){
        if(finite(measure.y.at(i))){ lo=qMin(lo,measure.y.at(i)); hi=qMax(hi,measure.y.at(i)); }
        if(goal&&finite(goal->y.at(i))){ lo=qMin(lo,goal->y.at(i)); hi=qMax(hi,goal->y.at(i)); }
    }
    if(!finite(lo)||!finite(hi)) return;
    // A bar is read from a baseline, and for a measure that baseline is zero
    // unless the person has said otherwise. A target outside the range still
    // has to land on the strip, which is why it was measured above.
    const double from=!isUnset(spec.yAxis.min)?spec.yAxis.min:qMin(0.0,lo);
    // The same round ceiling the dial uses, and for the same reason: the
    // longest bar reaching the end of its strip says "as far as this goes"
    // rather than anything about the measurement.
    double to=!isUnset(spec.yAxis.max)?spec.yAxis.max:niceCeiling(hi);
    if(!(to>from)) to=from+1.0;

    QRectF area=target.adjusted(target.width()*0.04,target.height()*0.16,
                                -target.width()*0.06,-target.height()*0.10);
    if(!(area.height()>20)||!(area.width()>60)) return;

    const QFont label=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(label,p->device());
    // Each row is named by its x value, which is the category column - the same
    // thing every other row-shaped engine here labels its rows with.
    const auto rowName=[&](int i){
        if(i<measure.x.size()&&finite(measure.x.at(i)))
            return QString::number(measure.x.at(i),'g',4);
        return QString::number(i+1);
    };
    double gutter=0.0;
    for(int i=0;i<rows;++i) gutter=qMax(gutter,fm.horizontalAdvance(rowName(i)));
    gutter=qMin(gutter+12.0,area.width()*0.30);

    const double lane=area.height()/double(rows);
    const double barHeight=qBound(4.0,lane*0.34,26.0);

    p->save();
    p->setFont(label);
    for(int i=0;i<rows;++i){
        const double value=measure.y.at(i);
        if(!finite(value)) continue;
        const double cy=area.top()+lane*(double(i)+0.5);
        const QRectF strip(area.left()+gutter,cy-barHeight*1.15,
                           area.width()-gutter,barHeight*2.3);
        drawQualitativeBands(p,strip,spec,true);

        p->setPen(Qt::NoPen);
        p->setBrush(measure.color);
        const double through=qBound(0.0,(value-from)/(to-from),1.0);
        p->drawRect(QRectF(strip.left(),cy-barHeight*0.5,
                           strip.width()*through,barHeight));

        if(goal&&finite(goal->y.at(i))){
            const double at=qBound(0.0,(goal->y.at(i)-from)/(to-from),1.0);
            QPen tick(spec.style.foreground); tick.setWidthF(2.6);
            p->setPen(tick);
            const double x=strip.left()+strip.width()*at;
            p->drawLine(QPointF(x,strip.top()+2),QPointF(x,strip.bottom()-2));
        }
        p->setPen(spec.style.foreground);
        p->drawText(QRectF(area.left(),cy-lane*0.5,gutter-8.0,lane),
                    Qt::AlignRight|Qt::AlignVCenter,rowName(i));
    }
    // What the bar is and what the tick is, said once. Without it the tick is
    // a mark whose meaning the reader has to be told separately.
    QColor quiet=spec.style.foreground; quiet.setAlphaF(0.75);
    p->setPen(quiet);
    p->drawText(QRectF(area.left(),area.bottom()+2,area.width(),fm.height()*1.4),
                Qt::AlignLeft|Qt::AlignVCenter,
                goal?QStringLiteral("%1 against %2").arg(measure.label,goal->label)
                    :measure.label);
    p->restore();
}

// ==========================================================================
// MARIMEKKO - a stacked bar chart where the WIDTHS mean something too.
//
// Every column is a category and every segment inside it is a share of that
// category, exactly as in a hundred-percent stacked bar. The addition is that
// the column's WIDTH is its share of the whole, so the area of a segment is
// its actual size rather than its proportion of a column that might be tiny.
// That is the reading a stacked bar throws away and the reason a Marimekko is
// drawn at all: three regions splitting a product that sells nothing look
// identical to three splitting the one that pays for the company.
//
// NOT the mosaic plot, which is in this catalogue already and takes two
// CATEGORICAL columns and counts their crossings. This takes one row per
// column and one mapped series per segment - the shape a table of figures
// actually arrives in - and reads the magnitudes it is given rather than
// counting rows.
void QtPlotBackend::drawMarimekko(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    drawFloatingTitle(p,target,spec);
    if(spec.series.isEmpty()) return;
    int columns=std::numeric_limits<int>::max();
    for(const PlotSeries& s:spec.series) columns=qMin(columns,int(s.y.size()));
    // Beyond a couple of dozen columns the widths stop being readable and the
    // labels stop fitting; a Marimekko of a thousand rows is a texture.
    constexpr int kMaxColumns=24;
    if(columns<1) return;
    columns=qMin(columns,kMaxColumns);

    QVector<double> total(columns,0.0);
    double grand=0.0;
    for(int c=0;c<columns;++c){
        for(const PlotSeries& s:spec.series){
            const double v=s.y.at(c);
            if(finite(v)&&v>0) total[c]+=v;
        }
        grand+=total.at(c);
    }
    if(!(grand>0)) return;

    const QRectF area=target.adjusted(target.width()*0.06,target.height()*0.14,
                                      -target.width()*0.04,-target.height()*0.12);
    const QFont label=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(label,p->device());
    const double gap=qMin(3.0,area.width()/double(columns)*0.06);
    p->save();
    p->setFont(label);
    double x=area.left();
    for(int c=0;c<columns;++c){
        const double w=(area.width()-gap*double(columns-1))*total.at(c)/grand;
        if(!(total.at(c)>0)){ x+=w+gap; continue; }
        double y=area.bottom();
        for(int i=0;i<spec.series.size();++i){
            const double v=spec.series.at(i).y.at(c);
            if(!finite(v)||v<=0) continue;
            const double h=area.height()*v/total.at(c);
            const QRectF cell(x,y-h,w,h);
            p->fillRect(cell,spec.series.at(i).color);
            // The share, written in the segment when the segment is big
            // enough to hold it. A Marimekko is read for its areas and
            // checked against its numbers.
            const QString share=QStringLiteral("%1%").arg(100.0*v/total.at(c),0,'f',0);
            if(h>fm.height()*1.2&&w>fm.horizontalAdvance(share)*1.4){
                QColor ink=spec.style.background;
                ink.setAlphaF(0.92);
                p->setPen(ink);
                p->drawText(cell,Qt::AlignCenter,share);
            }
            y-=h;
        }
        // The column's own width as a share, under it - the axis this figure
        // has instead of a scale.
        QColor quiet=spec.style.foreground; quiet.setAlphaF(0.75);
        p->setPen(quiet);
        p->drawText(QRectF(x,area.bottom()+2,w,fm.height()*1.4),
                    Qt::AlignCenter,
                    QStringLiteral("%1%").arg(100.0*total.at(c)/grand,0,'f',0));
        x+=w+gap;
    }

    // WHICH COLOUR IS WHICH SEGMENT.
    //
    // Drawn here rather than left to drawChrome, because this engine has no
    // axes and the chrome that carries the legend is never reached. A stack of
    // unnamed colours is a texture: the percentages say how the column divides
    // and nothing says what it divides into.
    {
        double key=area.top()-fm.height()*1.1;
        double at=area.left();
        for(const PlotSeries& s:spec.series){
            const double swatch=fm.height()*0.72;
            const double width=swatch+6.0+fm.horizontalAdvance(s.label)+16.0;
            if(at+width>area.right()) break;
            p->fillRect(QRectF(at,key-swatch*0.5,swatch,swatch),s.color);
            p->setPen(spec.style.foreground);
            p->drawText(QPointF(at+swatch+6.0,key+swatch*0.42),s.label);
            at+=width;
        }
    }
    p->restore();
}


// ---------------------------------------------------------------- Choropleth
//
// Regions from the person's OWN file, filled by a value.
//
// There is no bundled map, and that is the same decision the other geographic
// engines already made: "longitude and latitude are an equirectangular
// projection and nothing more - there is no basemap, and pretending otherwise
// would be worse than plotting honest coordinates." A choropleth changes
// nothing about that. The outlines come from the GeoJSON or shapefile the
// person imported, through the boundaries reader in importer.py, which is the
// only thing that knows where their regions are.
//
// Columns, as series: longitude, latitude, region id, value, and optionally a
// ring number. The ring is what separates an island from the mainland; without
// one, a country with offshore territory draws as a single polygon with a line
// running out across the sea and back.
//
// Read as COLUMNS rather than prepared into series, like the vector fields
// above: the painter needs every region's value at once to know the range the
// colour ramp spans, and a prepared spec has nowhere to carry it.
void QtPlotBackend::drawChoropleth(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    if(spec.series.size()<4) return;
    const QVector<double>& lon=spec.series.at(0).y;
    const QVector<double>& lat=spec.series.at(1).y;
    const QVector<double>& reg=spec.series.at(2).y;
    const QVector<double>& val=spec.series.at(3).y;
    const QVector<double> ring=spec.series.size()>=5?spec.series.at(4).y
                                                    :QVector<double>();
    const int n=qMin(qMin(lon.size(),lat.size()),qMin(reg.size(),val.size()));
    if(n<3) return;

    // One polygon per (region, ring), vertices kept IN ROW ORDER. A polygon is
    // its order - sort the rows and the outline knots itself into a star.
    struct Piece { QPolygonF pts; double value=qQNaN(); };
    QVector<Piece> pieces;
    QMap<QPair<double,double>,int> at;
    for(int i=0;i<n;++i){
        if(!finite(lon[i])||!finite(lat[i])||!finite(reg[i])) continue;
        const double r=(i<ring.size()&&finite(ring[i]))?ring[i]:0.0;
        const QPair<double,double> key(reg[i],r);
        auto it=at.find(key);
        int k;
        if(it==at.end()){ k=pieces.size(); at.insert(key,k); pieces.append(Piece()); }
        else k=*it;
        pieces[k].pts.append(QPointF(lon[i],lat[i]));
        // The first finite value in the region wins. The boundaries reader
        // repeats the attributes on every vertex, so they are all the same
        // one; taking the first means a single blank row cannot blank a region.
        if(!finite(pieces[k].value)&&finite(val[i])) pieces[k].value=val[i];
    }
    if(pieces.isEmpty()) return;

    double vLo=std::numeric_limits<double>::infinity(),vHi=-vLo;
    for(const Piece& pc:pieces)
        if(finite(pc.value)){ vLo=qMin(vLo,pc.value); vHi=qMax(vHi,pc.value); }
    if(!(vLo<=vHi)){ vLo=0.0; vHi=1.0; }
    // One region, or every region equal: a zero-width range would divide by
    // zero and paint the whole map the bottom colour of the ramp.
    if(vHi-vLo<=0.0) vHi=vLo+1.0;

    // THE VALUE SCALE, built once and handed to both halves.
    //
    // A skewed column on a linear ramp paints almost every region the same
    // colour; quantile classing is the standard answer and log10 is the answer
    // for a quantity spanning orders of magnitude. See engineParameters for
    // why this is a choice rather than a default.
    //
    // The scale goes to scalePosition below AND to the colour bar, which is
    // the whole reason it is a value rather than two pieces of arithmetic: a
    // key that disagrees with the map is indistinguishable, on the page, from
    // a key that agrees with it.
    // WHICH SCALE WAS ASKED FOR IS READ ONCE, IN applyLimits.
    //
    // This used to read `valueScale` and `classes` out of spec.parameters
    // itself. That was the only implementation when the choropleth was the only
    // engine offering the choice; it is now one of two, because applyLimits
    // turns the same two parameters into style.colourScaleKind and
    // style.colourClasses for every mapped engine. Two readings of one question
    // is the mistake this project has paid for most often - most recently in
    // this very feature, where the scale was applied in prepareSpec and the
    // renderer used a cached path that did not go through it, so the control
    // changed nothing at any setting. The classing itself stays here, because
    // the values it must divide are the REGIONS' values and only this painter
    // has them.
    ColourScale vscale;
    vscale.lo=vLo; vscale.hi=vHi;
    if(spec.style.colourScaleKind==ColourScale::Log10&&vLo>0.0){
        vscale.kind=ColourScale::Log10;
    }else if(spec.style.colourScaleKind==ColourScale::Quantile){
        QVector<double> values;
        values.reserve(pieces.size());
        for(const Piece& pc:pieces) if(finite(pc.value)) values.append(pc.value);
        const int classes=qBound(2,spec.style.colourClasses,9);
        vscale.breaks=quantileBreaks(values,classes);
        // quantileBreaks returns nothing when the values cannot be divided -
        // too few regions, or ties that would make two edges equal. Falling
        // back to the linear ramp is deliberate: a key with a zero-width class
        // on it explains nothing, and silently drawing fewer classes than were
        // asked for would be worse.
        if(!vscale.breaks.isEmpty()) vscale.kind=ColourScale::Quantile;
    }

    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);
    p->save();
    p->setClipRect(f.plotArea);
    p->setRenderHint(QPainter::Antialiasing,true);
    for(const Piece& pc:pieces){
        if(pc.pts.size()<3) continue;                 // not an area
        QPolygonF device;
        device.reserve(pc.pts.size());
        for(const QPointF& v:pc.pts) device.append(toDevice(f,v.x(),v.y()));
        // A region with no value is not the bottom of the scale - it is a gap
        // in the data, and painting it the lowest colour is a lie the reader
        // cannot see. Hatched grey says "no figure for this one".
        if(!finite(pc.value)){
            p->setBrush(QBrush(QColor(150,150,150,90),Qt::BDiagPattern));
            p->setPen(QPen(QColor(120,120,120),0.8));
        }else{
            p->setBrush(colourMapStyled(cmap,spec.style,
                                        scalePosition(pc.value,vscale)));
            p->setPen(QPen(spec.style.gridColor,0.6));
        }
        p->drawPolygon(device);
    }
    p->restore();

    drawColourBar(p,f,spec,vscale,
                  spec.series.at(3).label.isEmpty()?QStringLiteral("value")
                                                   :spec.series.at(3).label);
}

} // namespace graphvis
