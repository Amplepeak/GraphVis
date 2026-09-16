// ============================================================================
// prepareSpecCore, part 5 of 6: "Proctor Compaction Curve" through "Cumulative Vehicle Count".
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

std::optional<PlotSpec> QtPlotBackend::prepareEngineGroup5(const PlotSpec& in) const {
    if(in.engine==QLatin1String("Proctor Compaction Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& moisture=in.series.at(0).y;
            const QVector<double>& density=in.series.at(1).y;
            const int n=qMin(moisture.size(),density.size());
            PlotSeries pts;
            pts.label=QStringLiteral("compaction points");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.6;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(moisture[i])||!finite(density[i])) continue;
                pts.x.append(moisture[i]); pts.y.append(density[i]);
                fx.append(moisture[i]); fy.append(density[i]);
            }
            if(!pts.x.isEmpty()) out.series.append(pts);
            const QuadFit q=fitQuadratic(fx,fy);
            const Bounds bx=boundsOf(fx);
            if(q.ok&&bx.valid&&q.a<0.0){
                const double optimum=-q.b/(2.0*q.a);
                const double peak=q.a*optimum*optimum+q.b*optimum+q.c;
                PlotSeries curve;
                curve.label=QStringLiteral("max dry density %1 at %2 moisture")
                                .arg(peak,0,'g',5).arg(optimum,0,'g',4);
                curve.color=in.style.warning;
                curve.lineWidth=qMax(1.3,in.style.lineWidth);
                for(int i=0;i<=80;++i){
                    const double w=bx.lo+(bx.hi-bx.lo)*double(i)/80.0;
                    curve.x.append(w); curve.y.append(q.a*w*w+q.b*w+q.c);
                }
                out.series.append(curve);
                PlotSeries mark;
                mark.label=QStringLiteral("optimum");
                mark.color=in.style.danger;
                mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.5;
                mark.x={optimum}; mark.y={peak};
                out.series.append(mark);
                // The zero-air-voids line, which no compaction can cross. It
                // needs the density of water in the SAME units as the data, and
                // those units are not in the file - so they are inferred from
                // the magnitude and NAMED, and where neither range fits, the
                // line is left off rather than drawn at a guessed scale. A
                // reference curve placed at the wrong scale is worse than an
                // absent one: it looks like a limit and is not.
                double waterDensity=0.0; QString units;
                if(peak>0.8&&peak<3.0){ waterDensity=1.0; units=QStringLiteral("Mg/m3"); }
                else if(peak>60.0&&peak<200.0){ waterDensity=62.4; units=QStringLiteral("pcf"); }
                if(waterDensity>0.0){
                    // Moisture as a percentage or a fraction, again inferred
                    // from the magnitude the optimum sits at.
                    const double asFraction=(optimum>1.0)?0.01:1.0;
                    const double gravity=2.65;
                    PlotSeries zav;
                    zav.label=QStringLiteral("zero air voids, Gs 2.65, water %1 %2")
                                  .arg(waterDensity,0,'g',4).arg(units);
                    zav.color=in.style.gridColor;
                    zav.lineWidth=qMax(1.0,in.style.lineWidth*0.85);
                    zav.dashPattern={5,4};
                    for(int i=0;i<=80;++i){
                        const double w=bx.lo+(bx.hi-bx.lo)*double(i)/80.0;
                        const double wf=w*asFraction;
                        zav.x.append(w);
                        zav.y.append(gravity*waterDensity/(1.0+wf*gravity));
                    }
                    out.series.append(zav);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("moisture content"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("dry density"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------ Mohr-Coulomb Failure Envelope
    // Shear stress at failure against the normal stress it failed under, one
    // point per test. The line through them has an intercept and a slope, and
    // those are the two soil parameters every bearing capacity and slope
    // stability calculation is built on: cohesion, and the angle of friction.
    if(in.engine==QLatin1String("Mohr-Coulomb Envelope")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& normal=in.series.at(0).y;
            const QVector<double>& shear=in.series.at(1).y;
            const int n=qMin(normal.size(),shear.size());
            PlotSeries pts;
            pts.label=QStringLiteral("failure points");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.6;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(normal[i])||!finite(shear[i])) continue;
                pts.x.append(normal[i]); pts.y.append(shear[i]);
                fx.append(normal[i]); fy.append(shear[i]);
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                const LineFit f=fitLine(fx,fy);
                const Bounds bx=boundsOf(fx);
                if(f.ok&&bx.valid){
                    // A negative slope is not a friction angle. Reported as a
                    // fit that failed rather than as an angle of -12 degrees.
                    const double phi=(f.slope>0.0)?std::atan(f.slope)*180.0/M_PI
                                                  :std::numeric_limits<double>::quiet_NaN();
                    PlotSeries envelope;
                    envelope.label=finite(phi)
                        ? QStringLiteral("c %1, phi %2 deg").arg(f.intercept,0,'g',4).arg(phi,0,'f',2)
                        : QStringLiteral("envelope slopes down - not a friction angle");
                    envelope.color=in.style.warning;
                    envelope.lineWidth=qMax(1.3,in.style.lineWidth);
                    // Drawn back to zero normal stress, because the intercept
                    // there IS the cohesion and a line stopped at the first
                    // test leaves it to be extrapolated by eye.
                    envelope.x={0.0,bx.hi};
                    envelope.y={f.intercept,f.intercept+f.slope*bx.hi};
                    out.series.append(envelope);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("normal stress"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("shear stress at failure"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------- Influence Line
    // One response - a reaction, a moment, a shear at one section - against the
    // POSITION of a unit load rather than against anything the structure does
    // over time. Its peak says where to put the load to make that response
    // worst, which is the question a design check asks.
    if(in.engine==QLatin1String("Influence Line")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& position=in.series.at(0).y;
            const QVector<double>& response=in.series.at(1).y;
            const int n=qMin(position.size(),response.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(position[i])||!finite(response[i])) continue;
                rows.append(qMakePair(position[i],response[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(!rows.isEmpty()){
                int most=0,least=0;
                for(int i=1;i<rows.size();++i){
                    if(rows[i].second>rows[most].second) most=i;
                    if(rows[i].second<rows[least].second) least=i;
                }
                PlotSeries line;
                line.label=QStringLiteral("max %1 at %2, min %3 at %4")
                               .arg(rows[most].second,0,'g',4).arg(rows[most].first,0,'g',4)
                               .arg(rows[least].second,0,'g',4).arg(rows[least].first,0,'g',4);
                line.color=in.series.at(1).color;
                line.lineWidth=qMax(1.3,in.style.lineWidth);
                for(const QPair<double,double>& r:rows){ line.x.append(r.first); line.y.append(r.second); }
                out.series.append(line);
                // Zero, because the SIGN is what decides where load is placed
                // and a line without it has to be read against a gridline.
                PlotSeries zero;
                zero.label=QString();
                zero.color=in.style.gridColor;
                zero.lineWidth=qMax(0.8,in.style.lineWidth*0.7);
                zero.x={rows.first().first,rows.last().first};
                zero.y={0.0,0.0};
                out.series.append(zero);
                PlotSeries extremes;
                extremes.label=QStringLiteral("extremes");
                extremes.color=in.style.danger;
                extremes.drawLine=false; extremes.drawMarkers=true; extremes.markerSize=6.0;
                extremes.x={rows[most].first,rows[least].first};
                extremes.y={rows[most].second,rows[least].second};
                out.series.append(extremes);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("position of unit load"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("response"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------- Balanced Field Length
    // Accelerate-stop and accelerate-go distance against decision speed. One
    // rises with V1 and the other falls, and where they cross is the shortest
    // runway on which the takeoff can be either continued or abandoned - which
    // is the number the runway has to be longer than.
    if(in.engine==QLatin1String("Balanced Field Length")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=3){
            const QVector<double>& decisionSpeed=in.series.at(0).y;
            const QVector<double>& stopDistance=in.series.at(1).y;
            const QVector<double>& goDistance=in.series.at(2).y;
            const int n=qMin(decisionSpeed.size(),qMin(stopDistance.size(),goDistance.size()));
            QVector<QPair<double,QPair<double,double>>> rows;
            for(int i=0;i<n;++i){
                if(!finite(decisionSpeed[i])||!finite(stopDistance[i])||!finite(goDistance[i])) continue;
                rows.append(qMakePair(decisionSpeed[i],qMakePair(stopDistance[i],goDistance[i])));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,QPair<double,double>>& a,
                         const QPair<double,QPair<double,double>>& b){
                          return a.first<b.first; });
            if(rows.size()>=2){
                PlotSeries stop,go;
                stop.label=QStringLiteral("accelerate-stop");
                stop.color=in.series.at(1).color;
                stop.lineWidth=qMax(1.3,in.style.lineWidth);
                go.label=QStringLiteral("accelerate-go");
                go.color=in.series.at(2).color;
                go.lineWidth=qMax(1.3,in.style.lineWidth);
                double crossV=std::numeric_limits<double>::quiet_NaN();
                double crossD=std::numeric_limits<double>::quiet_NaN();
                for(int i=0;i<rows.size();++i){
                    stop.x.append(rows[i].first); stop.y.append(rows[i].second.first);
                    go.x.append(rows[i].first);   go.y.append(rows[i].second.second);
                    if(i==0) continue;
                    const double before=rows[i-1].second.first-rows[i-1].second.second;
                    const double now=rows[i].second.first-rows[i].second.second;
                    if((before<0.0)!=(now<0.0)&&now!=before&&!finite(crossV)){
                        const double f=(0.0-before)/(now-before);
                        crossV=rows[i-1].first+f*(rows[i].first-rows[i-1].first);
                        crossD=rows[i-1].second.first
                              +f*(rows[i].second.first-rows[i-1].second.first);
                    }
                }
                if(finite(crossV)){
                    stop.label=QStringLiteral("accelerate-stop; balanced field %1 at V1 %2")
                                   .arg(crossD,0,'g',5).arg(crossV,0,'g',4);
                    PlotSeries mark;
                    mark.label=QStringLiteral("balanced field");
                    mark.color=in.style.danger;
                    mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.5;
                    mark.x={crossV}; mark.y={crossD};
                    out.series.append(stop); out.series.append(go); out.series.append(mark);
                }else{
                    // The two never cross in the range flown, and saying so is
                    // the finding: the field is unbalanced over every V1 tested.
                    stop.label=QStringLiteral("accelerate-stop (no crossing in this V1 range)");
                    out.series.append(stop); out.series.append(go);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("decision speed V1"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("distance"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ----------------------------------------------------------- Specific Range
    // Distance flown per unit of fuel, against weight. It has a maximum, and
    // the cruise schedule is built around staying near it as the aircraft
    // burns down - so the peak and the band within one per cent of it are more
    // useful than the curve alone.
    if(in.engine==QLatin1String("Specific Range")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& weight=in.series.at(0).y;
            const QVector<double>& range=in.series.at(1).y;
            const int n=qMin(weight.size(),range.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(weight[i])||!finite(range[i])) continue;
                rows.append(qMakePair(weight[i],range[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(!rows.isEmpty()){
                int best=0;
                for(int i=1;i<rows.size();++i) if(rows[i].second>rows[best].second) best=i;
                PlotSeries curve;
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.3,in.style.lineWidth);
                curve.drawMarkers=true; curve.markerSize=3.4;
                for(const QPair<double,double>& r:rows){ curve.x.append(r.first); curve.y.append(r.second); }
                // Where the sampling is coarse the peak sits between samples,
                // so it is refined by a parabola through the three points
                // around it rather than reported as whichever sample was
                // highest - the peak of a smooth curve almost never is one.
                double peakAt=rows[best].first, peakValue=rows[best].second;
                if(best>0&&best+1<rows.size()){
                    const double x0=rows[best-1].first,x1=rows[best].first,x2=rows[best+1].first;
                    const double y0=rows[best-1].second,y1=rows[best].second,y2=rows[best+1].second;
                    const double d0=(x0-x1)*(x0-x2),d1=(x1-x0)*(x1-x2),d2=(x2-x0)*(x2-x1);
                    if(std::abs(d0)>1e-300&&std::abs(d1)>1e-300&&std::abs(d2)>1e-300){
                        const double a=y0/d0+y1/d1+y2/d2;
                        const double b=-(y0*(x1+x2)/d0+y1*(x0+x2)/d1+y2*(x0+x1)/d2);
                        if(a<0.0){
                            const double vertex=-b/(2.0*a);
                            if(vertex>x0&&vertex<x2){
                                peakAt=vertex;
                                const double c=y1-a*x1*x1-b*x1;
                                peakValue=a*vertex*vertex+b*vertex+c;
                            }
                        }
                    }
                }
                curve.label=QStringLiteral("peak %1 at weight %2")
                                .arg(peakValue,0,'g',5).arg(peakAt,0,'g',5);
                out.series.append(curve);
                PlotSeries band;
                band.label=QStringLiteral("within 1% of peak");
                band.color=in.style.warning;
                band.lineWidth=qMax(1.0,in.style.lineWidth*0.85);
                band.dashPattern={5,4};
                band.x={rows.first().first,rows.last().first};
                band.y={peakValue*0.99,peakValue*0.99};
                out.series.append(band);
                PlotSeries mark;
                mark.label=QStringLiteral("best range");
                mark.color=in.style.danger;
                mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.5;
                mark.x={peakAt}; mark.y={peakValue};
                out.series.append(mark);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("weight"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("specific range"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------ Composite Curves (Pinch)
    // Every hot stream added into one curve and every cold stream into another,
    // then the cold curve slid along the enthalpy axis until the closest
    // vertical approach between them is the minimum the exchangers can work
    // across. What is left hanging off each end is the utility that MUST be
    // bought: no amount of network design recovers it, and that is the whole
    // point of drawing this before designing anything.
    if(in.engine==QLatin1String("Composite Curves (Pinch)")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=3){
            const QVector<double>& supply=in.series.at(0).y;
            const QVector<double>& target=in.series.at(1).y;
            const QVector<double>& capacity=in.series.at(2).y;
            const int n=qMin(supply.size(),qMin(target.size(),capacity.size()));
            // Hot or cold is not a fourth column: a stream that is cooled has
            // a supply temperature above its target, and one that is heated
            // does not. Asking for a flag as well would let the flag disagree
            // with the temperatures.
            struct Stream { double lo,hi,cp; };
            QVector<Stream> hot,cold;
            for(int i=0;i<n;++i){
                if(!finite(supply[i])||!finite(target[i])||!finite(capacity[i])) continue;
                if(!(capacity[i]>0.0)||supply[i]==target[i]) continue;
                const Stream s{qMin(supply[i],target[i]),qMax(supply[i],target[i]),capacity[i]};
                if(supply[i]>target[i]) hot.append(s); else cold.append(s);
            }
            // A composite curve is enthalpy accumulated upward through the
            // temperature intervals the streams' endpoints cut the range into.
            const auto composite=[](const QVector<Stream>& streams){
                QVector<QPair<double,double>> curve; // (enthalpy, temperature)
                QVector<double> levels;
                levels.reserve(streams.size()*2);
                for(const Stream& s:streams){ levels.append(s.lo); levels.append(s.hi); }
                std::sort(levels.begin(),levels.end());
                levels.erase(std::unique(levels.begin(),levels.end()),levels.end());
                if(levels.size()<2) return curve;
                // THE CAPACITY OF EACH INTERVAL BY A RUNNING TOTAL, not by
                // asking every stream about every interval. Every endpoint is
                // itself a level, so a stream covers a CONTIGUOUS RUN of
                // intervals: the one above its low endpoint through the one
                // below its high endpoint, and nothing outside. Adding its
                // capacity where that run begins and taking it away after the
                // run ends leaves a prefix sum that is the capacity of each
                // interval in turn - one pass instead of one pass per
                // interval. A stream table of 24,000 rows took four seconds.
                QVector<double> delta(levels.size()+1,0.0);
                for(const Stream& s:streams){
                    const int loAt=int(std::lower_bound(levels.cbegin(),levels.cend(),s.lo)
                                       -levels.cbegin());
                    const int hiAt=int(std::lower_bound(levels.cbegin(),levels.cend(),s.hi)
                                       -levels.cbegin());
                    if(hiAt<=loAt) continue;
                    delta[loAt+1]+=s.cp;
                    delta[hiAt+1]-=s.cp;
                }
                double enthalpy=0.0,cp=0.0;
                curve.reserve(levels.size());
                curve.append(qMakePair(0.0,levels.first()));
                for(int i=1;i<levels.size();++i){
                    cp+=delta[i];
                    enthalpy+=cp*(levels[i]-levels[i-1]);
                    curve.append(qMakePair(enthalpy,levels[i]));
                }
                return curve;
            };
            const QVector<QPair<double,double>> hotCurve=composite(hot);
            const QVector<QPair<double,double>> coldCurve=composite(cold);
            if(hotCurve.size()>=2&&coldCurve.size()>=2){
                const auto temperatureAt=[](const QVector<QPair<double,double>>& c,double h){
                    if(h<=c.first().first) return c.first().second;
                    if(h>=c.last().first) return c.last().second;
                    // BINARY SEARCH. A composite curve is sorted by enthalpy
                    // by construction, and this is asked once per breakpoint
                    // inside a bisection that runs eighty times - a linear
                    // scan here is what made the whole search quadratic.
                    int hi=int(std::lower_bound(c.cbegin(),c.cend(),h,
                                   [](const QPair<double,double>& p,double v){
                                       return p.first<v; })
                               -c.cbegin());
                    hi=qBound(1,hi,c.size()-1);
                    const double h0=c[hi-1].first,h1=c[hi].first;
                    const double t0=c[hi-1].second,t1=c[hi].second;
                    return (h1>h0)?t0+(h-h0)/(h1-h0)*(t1-t0):t1;
                };
                // The approach the network is designed for. A default, not a
                // measurement, so it is named on the figure.
                const double minimumApproach=10.0;
                // Both composites are PIECEWISE LINEAR, so the difference
                // between them is piecewise linear too and its minimum can only
                // be at a breakpoint of one curve or the other. Sampling a
                // uniform grid instead was the first attempt: 400 points and
                // then 1200 both left the utilities 0.01 short of the exact
                // energy balance, because the grid does not land on the pinch.
                // Evaluating at the breakpoints is both exact and cheaper.
                const auto approachPoints=[&](double offset){
                    const double lo=qMax(hotCurve.first().first,coldCurve.first().first+offset);
                    const double hi=qMin(hotCurve.last().first,coldCurve.last().first+offset);
                    QVector<double> at;
                    if(!(hi>lo)) return at;
                    at.append(lo); at.append(hi);
                    for(const QPair<double,double>& p:hotCurve)
                        if(p.first>lo&&p.first<hi) at.append(p.first);
                    for(const QPair<double,double>& p:coldCurve)
                        if(p.first+offset>lo&&p.first+offset<hi) at.append(p.first+offset);
                    return at;
                };
                const auto closest=[&](double offset){
                    const QVector<double> at=approachPoints(offset);
                    if(at.isEmpty()) return std::numeric_limits<double>::infinity();
                    double worst=std::numeric_limits<double>::infinity();
                    for(double h:at)
                        worst=qMin(worst,temperatureAt(hotCurve,h)-temperatureAt(coldCurve,h-offset));
                    return worst;
                };
                // The gap widens as the cold curve slides right, so the
                // smallest offset that opens it to the minimum approach is
                // found by bisection rather than by stepping through offsets.
                double lo=0.0,hi=qMax(hotCurve.last().first,coldCurve.last().first)*2.0+1.0;
                if(closest(lo)<minimumApproach){
                    for(int it=0;it<80;++it){
                        const double mid=0.5*(lo+hi);
                        if(closest(mid)<minimumApproach) lo=mid; else hi=mid;
                    }
                }else{
                    hi=lo;
                }
                const double offset=hi;
                const double coldUtility=offset;
                const double hotUtility=qMax(0.0,(coldCurve.last().first+offset)
                                                 -hotCurve.last().first);
                // The pinch is where the approach is tightest.
                double pinchHot=std::numeric_limits<double>::quiet_NaN();
                {
                    double worst=std::numeric_limits<double>::infinity();
                    for(double h:approachPoints(offset)){
                        const double gap=temperatureAt(hotCurve,h)-temperatureAt(coldCurve,h-offset);
                        if(gap<worst){ worst=gap; pinchHot=temperatureAt(hotCurve,h); }
                    }
                }
                PlotSeries hotSeries;
                hotSeries.label=finite(pinchHot)
                    ? QStringLiteral("hot composite; pinch at %1, dTmin %2")
                          .arg(pinchHot,0,'g',4).arg(minimumApproach,0,'g',3)
                    : QStringLiteral("hot composite");
                hotSeries.color=in.style.danger;
                hotSeries.lineWidth=qMax(1.4,in.style.lineWidth);
                for(const QPair<double,double>& p:hotCurve){
                    hotSeries.x.append(p.first); hotSeries.y.append(p.second);
                }
                PlotSeries coldSeries;
                coldSeries.label=QStringLiteral("cold composite; hot utility %1, cold utility %2")
                                     .arg(hotUtility,0,'g',4).arg(coldUtility,0,'g',4);
                coldSeries.color=in.series.at(0).color;
                coldSeries.lineWidth=qMax(1.4,in.style.lineWidth);
                for(const QPair<double,double>& p:coldCurve){
                    coldSeries.x.append(p.first+offset); coldSeries.y.append(p.second);
                }
                out.series.append(hotSeries);
                out.series.append(coldSeries);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("enthalpy"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("temperature"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------- Residence Time Distribution
    // A tracer pulse at the inlet and its concentration at the outlet, turned
    // into the distribution of times the fluid actually spends inside. The
    // mean is the real residence time rather than the volume over the flow,
    // and the variance says how far from plug flow the vessel is - reported
    // here as the number of equal stirred tanks that would behave the same.
    if(in.engine==QLatin1String("Residence Time Distribution")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& time=in.series.at(0).y;
            const QVector<double>& tracer=in.series.at(1).y;
            const int n=qMin(time.size(),tracer.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(time[i])||!finite(tracer[i])) continue;
                rows.append(qMakePair(time[i],qMax(0.0,tracer[i])));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=3){
                double total=0.0;
                for(int i=1;i<rows.size();++i)
                    total+=0.5*(rows[i].second+rows[i-1].second)*(rows[i].first-rows[i-1].first);
                if(total>1e-300){
                    PlotSeries e;
                    e.color=in.series.at(1).color;
                    e.lineWidth=qMax(1.3,in.style.lineWidth);
                    for(const QPair<double,double>& r:rows){
                        e.x.append(r.first); e.y.append(r.second/total);
                    }
                    // Moments by the same trapezoid rule the normalisation
                    // used, so the mean of the plotted curve is the mean
                    // reported and the two cannot disagree.
                    double mean=0.0;
                    for(int i=1;i<rows.size();++i){
                        const double a=rows[i-1].first*rows[i-1].second/total;
                        const double b=rows[i].first*rows[i].second/total;
                        mean+=0.5*(a+b)*(rows[i].first-rows[i-1].first);
                    }
                    double variance=0.0;
                    for(int i=1;i<rows.size();++i){
                        const double da=(rows[i-1].first-mean)*(rows[i-1].first-mean)
                                        *rows[i-1].second/total;
                        const double db=(rows[i].first-mean)*(rows[i].first-mean)
                                        *rows[i].second/total;
                        variance+=0.5*(da+db)*(rows[i].first-rows[i-1].first);
                    }
                    const double tanks=(variance>1e-300)?mean*mean/variance
                                                        :std::numeric_limits<double>::quiet_NaN();
                    e.label=finite(tanks)
                        ? QStringLiteral("E(t); mean %1, variance %2, tanks in series %3")
                              .arg(mean,0,'g',4).arg(variance,0,'g',4).arg(tanks,0,'f',2)
                        : QStringLiteral("E(t); mean %1").arg(mean,0,'g',4);
                    out.series.append(e);
                    PlotSeries mark;
                    mark.label=QStringLiteral("mean residence time");
                    mark.color=in.style.danger;
                    mark.lineWidth=qMax(1.0,in.style.lineWidth*0.85);
                    mark.dashPattern={5,4};
                    const Bounds be=boundsOf(e.y);
                    mark.x={mean,mean};
                    mark.y={0.0,be.valid?be.hi:1.0};
                    out.series.append(mark);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("time"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("E(t)"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------------- Harmonic Spectrum
    // Magnitude by harmonic order, with the total harmonic distortion computed
    // from it. THD is a single number quoted about a supply, and quoting it
    // beside the spectrum it came from is the difference between a claim and
    // a measurement.
    if(in.engine==QLatin1String("Harmonic Spectrum")){
        PlotSpec out=derivedAs(in,QStringLiteral("Stem"));
        if(in.series.size()>=2){
            const QVector<double>& order=in.series.at(0).y;
            const QVector<double>& magnitude=in.series.at(1).y;
            const int n=qMin(order.size(),magnitude.size());
            PlotSeries lines;
            lines.color=in.series.at(1).color;
            lines.drawMarkers=true; lines.markerSize=3.6;
            double fundamental=0.0,harmonics=0.0;
            for(int i=0;i<n;++i){
                if(!finite(order[i])||!finite(magnitude[i])) continue;
                lines.x.append(order[i]); lines.y.append(magnitude[i]);
                // The fundamental is order 1, not the largest magnitude. On a
                // badly distorted supply those are not the same, and taking
                // the largest would report a THD against a harmonic.
                if(std::abs(order[i]-1.0)<0.5) fundamental=magnitude[i];
                else if(order[i]>1.5) harmonics+=magnitude[i]*magnitude[i];
            }
            if(!lines.x.isEmpty()){
                // THD IS THE ANSWER, so it goes under the figure and not into
                // a legend row. This spectrum has ONE named series, and a
                // legend of one row is suppressed as a caption - so total
                // harmonic distortion, the entire reason anyone draws a
                // harmonic spectrum, was computed and then shown nowhere.
                lines.label=QStringLiteral("magnitude");
                out.figureNote=(std::abs(fundamental)>1e-300)
                    ? QStringLiteral("THD %1%, over the orders above the fundamental.")
                          .arg(100.0*std::sqrt(harmonics)/std::abs(fundamental),0,'f',2)
                    : QStringLiteral("No order-1 component, so THD is undefined here.");
                out.series.append(lines);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("harmonic order"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("magnitude"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------- Sound Level Statistics
    // The level exceeded for each percentage of the measurement period. L90 is
    // the background the site falls back to between events, L10 is what the
    // events themselves reach, and Leq is the energy average - which is not
    // any percentile and is usually well above the median.
    if(in.engine==QLatin1String("Sound Level Statistics")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& level=in.series.at(1).y;
            QVector<double> sorted;
            double energy=0.0;
            for(double v:level){
                if(!finite(v)) continue;
                sorted.append(v);
                energy+=std::pow(10.0,v/10.0);
            }
            if(!sorted.isEmpty()){
                std::sort(sorted.begin(),sorted.end(),[](double a,double b){ return a>b; });
                const double equivalent=10.0*std::log10(qMax(1e-300,energy/double(sorted.size())));
                const auto percentile=[&](double exceeded){
                    const int at=qBound(0,int(exceeded/100.0*double(sorted.size())),sorted.size()-1);
                    return sorted[at];
                };
                PlotSeries curve;
                curve.label=QStringLiteral("L10 %1, L50 %2, L90 %3, Leq %4")
                                .arg(percentile(10),0,'f',1).arg(percentile(50),0,'f',1)
                                .arg(percentile(90),0,'f',1).arg(equivalent,0,'f',1);
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.3,in.style.lineWidth);
                for(int i=0;i<sorted.size();++i){
                    curve.x.append(100.0*double(i)/double(qMax(1,sorted.size()-1)));
                    curve.y.append(sorted[i]);
                }
                out.series.append(curve);
                PlotSeries leq;
                leq.label=QStringLiteral("Leq");
                leq.color=in.style.warning;
                leq.lineWidth=qMax(1.0,in.style.lineWidth*0.85);
                leq.dashPattern={5,4};
                leq.x={0.0,100.0}; leq.y={equivalent,equivalent};
                out.series.append(leq);
                PlotSeries marks;
                marks.label=QStringLiteral("L10, L50, L90");
                marks.color=in.style.danger;
                marks.drawLine=false; marks.drawMarkers=true; marks.markerSize=5.5;
                marks.x={10.0,50.0,90.0};
                marks.y={percentile(10),percentile(50),percentile(90)};
                out.series.append(marks);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("percentage of time exceeded"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("level (dB)"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------------ Beam Caustic
    // Beam radius against distance through a focus. The square of the radius
    // is a PARABOLA in distance for any beam, ideal or not, so fitting the
    // square rather than the radius gives the waist, its position and the
    // divergence in one linear solve and without an iteration to converge.
    if(in.engine==QLatin1String("Beam Caustic")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& distance=in.series.at(0).y;
            const QVector<double>& radius=in.series.at(1).y;
            const int n=qMin(distance.size(),radius.size());
            PlotSeries pts;
            pts.label=QStringLiteral("measured radius");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(distance[i])||!finite(radius[i])||!(radius[i]>0.0)) continue;
                pts.x.append(distance[i]); pts.y.append(radius[i]);
                fx.append(distance[i]); fy.append(radius[i]*radius[i]);
            }
            if(!pts.x.isEmpty()) out.series.append(pts);
            const QuadFit q=fitQuadratic(fx,fy);
            const Bounds bx=boundsOf(fx);
            if(q.ok&&bx.valid&&q.a>0.0){
                const double waistAt=-q.b/(2.0*q.a);
                const double waistSquared=q.c-q.b*q.b/(4.0*q.a);
                if(waistSquared>0.0){
                    const double waist=std::sqrt(waistSquared);
                    const double divergence=std::sqrt(q.a);   // half-angle
                    const double rayleigh=waist/divergence;
                    PlotSeries curve;
                    // M squared needs the wavelength, and the wavelength is not
                    // in the file. What IS determined by the fit is the
                    // product, so the product is what is reported - a beam
                    // quality quoted against an assumed wavelength would be a
                    // number about the assumption.
                    curve.label=QStringLiteral("waist %1 at %2, divergence %3, zR %4; M2 x lambda = %5")
                                    .arg(waist,0,'g',4).arg(waistAt,0,'g',4)
                                    .arg(divergence,0,'g',4).arg(rayleigh,0,'g',4)
                                    .arg(M_PI*waist*divergence,0,'g',4);
                    curve.color=in.style.warning;
                    curve.lineWidth=qMax(1.3,in.style.lineWidth);
                    for(int i=0;i<=120;++i){
                        const double z=bx.lo+(bx.hi-bx.lo)*double(i)/120.0;
                        const double w2=q.a*z*z+q.b*z+q.c;
                        if(!(w2>0.0)) continue;
                        curve.x.append(z); curve.y.append(std::sqrt(w2));
                    }
                    out.series.append(curve);
                    PlotSeries mark;
                    mark.label=QStringLiteral("waist");
                    mark.color=in.style.danger;
                    mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.5;
                    mark.x={waistAt}; mark.y={waist};
                    out.series.append(mark);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("distance along the beam"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("beam radius"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------------------- Youden Plot
    // Two similar materials measured once each by every laboratory. A lab with
    // a random error scatters around the middle; a lab with a SYSTEMATIC one
    // is high on both or low on both, and lands along the 45 degree line. That
    // separation is the whole reason two materials are sent instead of two
    // replicates of one.
    if(in.engine==QLatin1String("Youden Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& first=in.series.at(0).y;
            const QVector<double>& second=in.series.at(1).y;
            const int n=qMin(first.size(),second.size());
            QVector<double> xs,ys;
            for(int i=0;i<n;++i){
                if(!finite(first[i])||!finite(second[i])) continue;
                xs.append(first[i]); ys.append(second[i]);
            }
            if(xs.size()>=3){
                // Medians, not means: one laboratory that is out by a factor
                // of ten would drag a mean to itself and centre the picture on
                // the outlier it exists to identify.
                QVector<double> sx=xs,sy=ys;
                std::sort(sx.begin(),sx.end()); std::sort(sy.begin(),sy.end());
                const double centreX=sx[sx.size()/2], centreY=sy[sy.size()/2];
                double spread=0.0;
                for(int i=0;i<xs.size();++i)
                    spread+=(xs[i]-centreX)*(xs[i]-centreX)+(ys[i]-centreY)*(ys[i]-centreY);
                spread=std::sqrt(spread/double(2*xs.size()));
                const double radius=2.448*spread;   // the 95% Youden circle
                PlotSeries pts;
                int outside=0;
                for(int i=0;i<xs.size();++i){
                    pts.x.append(xs[i]); pts.y.append(ys[i]);
                    if(std::hypot(xs[i]-centreX,ys[i]-centreY)>radius) ++outside;
                }
                pts.label=QStringLiteral("%1 laboratories, %2 outside the circle")
                              .arg(xs.size()).arg(outside);
                pts.color=in.series.at(1).color;
                pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.4;
                out.series.append(pts);
                PlotSeries circle;
                circle.label=QStringLiteral("95% circle, radius %1").arg(radius,0,'g',4);
                circle.color=in.style.warning;
                circle.lineWidth=qMax(1.2,in.style.lineWidth);
                for(int k=0;k<=180;++k){
                    const double a=2.0*M_PI*double(k)/180.0;
                    circle.x.append(centreX+radius*std::cos(a));
                    circle.y.append(centreY+radius*std::sin(a));
                }
                out.series.append(circle);
                PlotSeries diagonal;
                diagonal.label=QStringLiteral("systematic direction");
                diagonal.color=in.style.gridColor;
                diagonal.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                diagonal.dashPattern={5,4};
                diagonal.x={centreX-2.0*radius,centreX+2.0*radius};
                diagonal.y={centreY-2.0*radius,centreY+2.0*radius};
                out.series.append(diagonal);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("material A"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("material B"),false,unsetValue(),unsetValue()};
        // The 95% CIRCLE is the acceptance region and the 45-degree line is
        // the systematic direction, so the two materials have to be measured
        // the same way: fitted to its own range on each axis the circle came
        // out as an ellipse and the diagonal was no longer at 45 degrees. See
        // PlotSpec::equalAspect.
        out.equalAspect=true;
        return out;
    }

    // -------------------------------------------------------- Levey-Jennings
    // Control results in run order against the mean and its standard-deviation
    // bands. The bands are what make a run interpretable, and the Westgard
    // rules are what make them a decision rather than an impression: one point
    // beyond three deviations, or two in a row beyond two on the SAME side.
    if(in.engine==QLatin1String("Levey-Jennings Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& value=in.series.at(1).y;
            QVector<double> xs,ys;
            const QVector<double>& index=in.series.at(0).y;
            for(int i=0;i<qMin(index.size(),value.size());++i){
                if(!finite(index[i])||!finite(value[i])) continue;
                xs.append(index[i]); ys.append(value[i]);
            }
            if(ys.size()>=3){
                double mean=0.0;
                for(double v:ys) mean+=v;
                mean/=double(ys.size());
                double variance=0.0;
                for(double v:ys) variance+=(v-mean)*(v-mean);
                variance/=double(ys.size()-1);
                const double deviation=std::sqrt(qMax(0.0,variance));
                int beyondThree=0,twoInARow=0;
                for(int i=0;i<ys.size();++i){
                    if(std::abs(ys[i]-mean)>3.0*deviation) ++beyondThree;
                    if(i==0||deviation<=0.0) continue;
                    const double a=(ys[i-1]-mean)/deviation, b=(ys[i]-mean)/deviation;
                    if(a>2.0&&b>2.0) ++twoInARow;
                    if(a<-2.0&&b<-2.0) ++twoInARow;
                }
                PlotSeries run;
                run.label=QStringLiteral("mean %1, s %2; 1-3s %3, 2-2s %4")
                              .arg(mean,0,'g',5).arg(deviation,0,'g',4)
                              .arg(beyondThree).arg(twoInARow);
                run.color=in.series.at(1).color;
                run.lineWidth=qMax(1.2,in.style.lineWidth);
                run.drawMarkers=true; run.markerSize=3.8;
                run.x=xs; run.y=ys;
                out.series.append(run);
                const Bounds bx=boundsOf(xs);
                if(bx.valid){
                    const struct { double sigma; const char* name; } bands[]={
                        {0.0,"mean"},{1.0,"+1s"},{-1.0,"-1s"},{2.0,"+2s"},{-2.0,"-2s"},
                        {3.0,"+3s"},{-3.0,"-3s"}};
                    for(const auto& band:bands){
                        PlotSeries limit;
                        limit.label=QString::fromLatin1(band.name);
                        limit.color=(std::abs(band.sigma)>=3.0)?in.style.danger
                                   :(std::abs(band.sigma)>=2.0)?in.style.warning
                                                               :in.style.gridColor;
                        limit.lineWidth=qMax(0.8,in.style.lineWidth*0.7);
                        if(band.sigma!=0.0) limit.dashPattern={5,4};
                        limit.x={bx.lo,bx.hi};
                        limit.y={mean+band.sigma*deviation,mean+band.sigma*deviation};
                        out.series.append(limit);
                    }
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("run"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("control result"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------ Species Accumulation
    // Distinct species found against sampling effort, in the order the samples
    // were taken. A curve still climbing at the last sample means the survey
    // is not finished, and Chao1 estimates how much is left from how many
    // species were seen exactly once or exactly twice.
    if(in.engine==QLatin1String("Species Accumulation Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& sample=in.series.at(0).y;
            const QVector<double>& species=in.series.at(1).y;
            const int n=qMin(sample.size(),species.size());
            QHash<QString,int> occurrences;
            QSet<QString> seen;
            auto key=[](double v){ return QString::number(v,'g',12); };
            PlotSeries curve;
            curve.color=in.series.at(1).color;
            curve.lineWidth=qMax(1.3,in.style.lineWidth);
            for(int i=0;i<n;++i){
                if(!finite(sample[i])||!finite(species[i])) continue;
                const QString k=key(species[i]);
                occurrences[k]+=1;
                seen.insert(k);
                curve.x.append(double(curve.x.size()+1));
                curve.y.append(double(seen.size()));
            }
            if(!curve.x.isEmpty()){
                int singles=0,doubles=0;
                for(auto it=occurrences.constBegin();it!=occurrences.constEnd();++it){
                    if(it.value()==1) ++singles;
                    else if(it.value()==2) ++doubles;
                }
                // Chao1, with the bias-corrected form where nothing was seen
                // exactly twice: the uncorrected estimator divides by zero
                // there, and a survey that found no doubletons is exactly the
                // case an estimate is wanted for.
                const double observed=double(seen.size());
                const double chao=(doubles>0)
                    ? observed+double(singles)*double(singles)/(2.0*double(doubles))
                    : observed+double(singles)*double(singles-1)/2.0;
                curve.label=QStringLiteral("%1 observed, Chao1 %2 (%3 singletons, %4 doubletons)")
                                .arg(int(observed)).arg(chao,0,'f',1).arg(singles).arg(doubles);
                out.series.append(curve);
                PlotSeries estimate;
                estimate.label=QStringLiteral("Chao1 estimate");
                estimate.color=in.style.warning;
                estimate.lineWidth=qMax(1.0,in.style.lineWidth*0.85);
                estimate.dashPattern={5,4};
                estimate.x={curve.x.first(),curve.x.last()};
                estimate.y={chao,chao};
                out.series.append(estimate);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("samples"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("species found"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ----------------------------------------------------------- Epidemic Curve
    // Cases by date of onset, with a centred moving average over them. The
    // average is centred rather than trailing because the question is where
    // the peak WAS, and a trailing average puts every peak late by half its
    // own window - which is how a turning point gets announced a week after it
    // happened.
    if(in.engine==QLatin1String("Epidemic Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Stem"));
        if(in.series.size()>=2){
            const QVector<double>& when=in.series.at(0).y;
            const QVector<double>& cases=in.series.at(1).y;
            const int n=qMin(when.size(),cases.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(when[i])||!finite(cases[i])) continue;
                rows.append(qMakePair(when[i],cases[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(!rows.isEmpty()){
                int peakAt=0; double total=0.0;
                PlotSeries bars;
                bars.color=in.series.at(1).color;
                for(int i=0;i<rows.size();++i){
                    bars.x.append(rows[i].first); bars.y.append(rows[i].second);
                    total+=rows[i].second;
                    if(rows[i].second>rows[peakAt].second) peakAt=i;
                }
                bars.label=QStringLiteral("%1 cases, peak %2 on %3")
                               .arg(total,0,'g',6).arg(rows[peakAt].second,0,'g',5)
                               .arg(rows[peakAt].first,0,'g',10);
                out.series.append(bars);
                const int half=qBound(1,rows.size()/24,7);
                PlotSeries smooth;
                smooth.label=QStringLiteral("centred %1-point mean").arg(2*half+1);
                smooth.color=in.style.warning;
                smooth.lineWidth=qMax(1.4,in.style.lineWidth);
                for(int i=0;i<rows.size();++i){
                    double sum=0.0; int used=0;
                    for(int k=-half;k<=half;++k){
                        const int at=i+k;
                        if(at<0||at>=rows.size()) continue;
                        sum+=rows[at].second; ++used;
                    }
                    smooth.x.append(rows[i].first);
                    smooth.y.append(used>0?sum/double(used):0.0);
                }
                out.series.append(smooth);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("date of onset"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("cases"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------- Effective Reproduction Number
    // How many further cases each case is currently producing, from the growth
    // rate of the series and an assumed generation interval. The assumption is
    // unavoidable - the same case curve gives a different R for a different
    // generation time - so it is stated on the figure rather than buried.
    if(in.engine==QLatin1String("Effective Reproduction Number")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& when=in.series.at(0).y;
            const QVector<double>& cases=in.series.at(1).y;
            const int n=qMin(when.size(),cases.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(when[i])||!finite(cases[i])) continue;
                rows.append(qMakePair(when[i],cases[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            const double generation=5.0;   // intervals, stated in the label
            const int window=7;
            if(rows.size()>window){
                PlotSeries r;
                r.label=QStringLiteral("R over a %1-point window, generation interval %2")
                            .arg(window).arg(generation,0,'g',3);
                r.color=in.series.at(1).color;
                r.lineWidth=qMax(1.3,in.style.lineWidth);
                for(int i=window-1;i<rows.size();++i){
                    QVector<double> t,logCases;
                    for(int k=i-window+1;k<=i;++k){
                        if(!(rows[k].second>0.0)) continue;
                        t.append(rows[k].first);
                        logCases.append(std::log(rows[k].second));
                    }
                    // Wallinga-Lipsitch for an exponential growth rate: R is
                    // exp(r * generation) only for a fixed generation time,
                    // which is what "assumed" means here.
                    const LineFit f=fitLine(t,logCases);
                    if(!f.ok) continue;
                    r.x.append(rows[i].first);
                    r.y.append(std::exp(f.slope*generation));
                }
                if(!r.x.isEmpty()){
                    r.label=QStringLiteral("R over a %1-point window, generation interval %2; latest %3")
                                .arg(window).arg(generation,0,'g',3).arg(r.y.last(),0,'f',3);
                    out.series.append(r);
                    PlotSeries unity;
                    unity.label=QStringLiteral("R = 1");
                    unity.color=in.style.danger;
                    unity.lineWidth=qMax(1.0,in.style.lineWidth*0.85);
                    unity.dashPattern={5,4};
                    unity.x={r.x.first(),r.x.last()};
                    unity.y={1.0,1.0};
                    out.series.append(unity);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("date"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("effective reproduction number"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Cumulative Gain
    // What share of the positives is captured by contacting what share of the
    // population, ranked by the model's score. It answers the question a lift
    // curve answers as a ratio, in the units a campaign is actually planned
    // in, and the diagonal is what contacting people at random would achieve.
    if(in.engine==QLatin1String("Cumulative Gain Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& score=in.series.at(0).y;
            const QVector<double>& outcome=in.series.at(1).y;
            const int n=qMin(score.size(),outcome.size());
            QVector<QPair<double,double>> rows;
            double positives=0.0;
            for(int i=0;i<n;++i){
                if(!finite(score[i])||!finite(outcome[i])) continue;
                const double label=(outcome[i]>0.5)?1.0:0.0;
                rows.append(qMakePair(score[i],label));
                positives+=label;
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first>b.first; });
            if(!rows.isEmpty()&&positives>0.0){
                PlotSeries gain;
                gain.color=in.series.at(1).color;
                gain.lineWidth=qMax(1.3,in.style.lineWidth);
                gain.x.append(0.0); gain.y.append(0.0);
                double captured=0.0,atTwenty=0.0;
                for(int i=0;i<rows.size();++i){
                    captured+=rows[i].second;
                    const double share=double(i+1)/double(rows.size());
                    gain.x.append(100.0*share);
                    gain.y.append(100.0*captured/positives);
                    if(share<=0.20) atTwenty=100.0*captured/positives;
                }
                gain.label=QStringLiteral("model; %1% of positives in the top 20%")
                               .arg(atTwenty,0,'f',1);
                out.series.append(gain);
                PlotSeries chance;
                chance.label=QStringLiteral("random");
                chance.color=in.style.gridColor;
                chance.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                chance.dashPattern={5,4};
                chance.x={0.0,100.0}; chance.y={0.0,100.0};
                out.series.append(chance);
                // The best any ranking could do, which is a ceiling rather than
                // a comparison: it is set by the prevalence, not by the model.
                const double prevalence=positives/double(rows.size());
                PlotSeries perfect;
                perfect.label=QStringLiteral("perfect ranking");
                perfect.color=in.style.warning;
                perfect.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                perfect.dashPattern={2,3};
                perfect.x={0.0,100.0*prevalence,100.0};
                perfect.y={0.0,100.0,100.0};
                out.series.append(perfect);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("population contacted (%)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("positives captured (%)"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ----------------------------------------------------- Ratkowsky Square-Root
    // The square root of a growth rate against temperature, which is a
    // straight line over the whole suboptimal range where an Arrhenius plot is
    // not. Where that line crosses zero is the notional minimum growth
    // temperature - notional because growth stops before it, which is exactly
    // why it is an extrapolated parameter and is labelled as one.
    if(in.engine==QLatin1String("Ratkowsky Square-Root Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& temperature=in.series.at(0).y;
            const QVector<double>& rate=in.series.at(1).y;
            const int m=qMin(temperature.size(),rate.size());
            PlotSeries pts;
            pts.label=QStringLiteral("sqrt(rate)");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;
            QVector<double> fx,fy;
            for(int i=0;i<m;++i){
                if(!finite(temperature[i])||!finite(rate[i])||!(rate[i]>0.0)) continue;
                pts.x.append(temperature[i]); pts.y.append(std::sqrt(rate[i]));
                fx.append(temperature[i]); fy.append(std::sqrt(rate[i]));
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                const LineFit f=fitLine(fx,fy);
                const Bounds bx=boundsOf(fx);
                if(f.ok&&bx.valid&&f.slope>0.0){
                    const double minimum=-f.intercept/f.slope;
                    PlotSeries line;
                    line.label=QStringLiteral("b %1, Tmin %2 (extrapolated)")
                                   .arg(f.slope,0,'g',4).arg(minimum,0,'f',2);
                    line.color=in.style.warning;
                    line.lineWidth=qMax(1.3,in.style.lineWidth);
                    line.x={minimum,bx.hi};
                    line.y={0.0,f.intercept+f.slope*bx.hi};
                    out.series.append(line);
                    PlotSeries mark;
                    mark.label=QStringLiteral("Tmin");
                    mark.color=in.style.danger;
                    mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.0;
                    mark.x={minimum}; mark.y={0.0};
                    out.series.append(mark);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("temperature"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("sqrt(growth rate)"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------- Phenology Curve
    // A vegetation index through a year, with the dates the canopy greens up
    // and senesces. Those dates are what the series is measured for, and they
    // are taken at the HALF AMPLITUDE crossings rather than at the steepest
    // point, because the half-amplitude date survives a noisy record and the
    // date of maximum slope does not.
    if(in.engine==QLatin1String("Phenology Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& day=in.series.at(0).y;
            const QVector<double>& index=in.series.at(1).y;
            const int n=qMin(day.size(),index.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(day[i])||!finite(index[i])) continue;
                rows.append(qMakePair(day[i],index[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=5){
                PlotSeries raw;
                raw.label=QStringLiteral("index");
                raw.color=in.series.at(1).color;
                raw.lineWidth=qMax(1.0,in.style.lineWidth*0.8);
                raw.drawMarkers=true; raw.markerSize=2.8;
                for(const QPair<double,double>& r:rows){ raw.x.append(r.first); raw.y.append(r.second); }
                out.series.append(raw);
                // A centred mean broadens a symmetric season symmetrically:
                // the peak date survives it and the LENGTH does not, because
                // green-up moves early and senescence late by the same amount.
                // A 7-point window over a 5-day series cost 4 days of season on
                // clean data, so the window is kept short and named on the
                // figure - the reader can then judge how much of the season
                // length is the vegetation and how much is the filter.
                const int half=qBound(1,rows.size()/48,3);
                QVector<double> smoothed(rows.size(),0.0);
                for(int i=0;i<rows.size();++i){
                    double sum=0.0; int used=0;
                    for(int k=-half;k<=half;++k){
                        const int at=i+k;
                        if(at<0||at>=rows.size()) continue;
                        sum+=rows[at].second; ++used;
                    }
                    smoothed[i]=used>0?sum/double(used):rows[i].second;
                }
                int peakAt=0,troughAt=0;
                for(int i=1;i<smoothed.size();++i){
                    if(smoothed[i]>smoothed[peakAt]) peakAt=i;
                    if(smoothed[i]<smoothed[troughAt]) troughAt=i;
                }
                const double midpoint=0.5*(smoothed[peakAt]+smoothed[troughAt]);
                double greenUp=std::numeric_limits<double>::quiet_NaN();
                double senescence=std::numeric_limits<double>::quiet_NaN();
                for(int i=1;i<=peakAt;++i){
                    if((smoothed[i-1]<midpoint)!=(smoothed[i]<midpoint)
                       &&smoothed[i]!=smoothed[i-1]&&!finite(greenUp)){
                        const double f=(midpoint-smoothed[i-1])/(smoothed[i]-smoothed[i-1]);
                        greenUp=rows[i-1].first+f*(rows[i].first-rows[i-1].first);
                    }
                }
                for(int i=peakAt+1;i<smoothed.size();++i){
                    if((smoothed[i-1]<midpoint)!=(smoothed[i]<midpoint)
                       &&smoothed[i]!=smoothed[i-1]&&!finite(senescence)){
                        const double f=(midpoint-smoothed[i-1])/(smoothed[i]-smoothed[i-1]);
                        senescence=rows[i-1].first+f*(rows[i].first-rows[i-1].first);
                    }
                }
                PlotSeries smooth;
                smooth.label=QStringLiteral("%1-point mean; peak %2")
                                 .arg(2*half+1).arg(rows[peakAt].first,0,'g',5);
                if(finite(greenUp)&&finite(senescence))
                    smooth.label=QStringLiteral("%1-point mean; green-up %2, peak %3, senescence %4, season %5")
                                     .arg(2*half+1)
                                     .arg(greenUp,0,'f',1).arg(rows[peakAt].first,0,'g',5)
                                     .arg(senescence,0,'f',1).arg(senescence-greenUp,0,'f',1);
                smooth.color=in.style.warning;
                smooth.lineWidth=qMax(1.4,in.style.lineWidth);
                for(int i=0;i<rows.size();++i){
                    smooth.x.append(rows[i].first); smooth.y.append(smoothed[i]);
                }
                out.series.append(smooth);
                PlotSeries level;
                level.label=QStringLiteral("half amplitude");
                level.color=in.style.gridColor;
                level.lineWidth=qMax(0.8,in.style.lineWidth*0.7);
                level.dashPattern={5,4};
                level.x={rows.first().first,rows.last().first};
                level.y={midpoint,midpoint};
                out.series.append(level);
                if(finite(greenUp)&&finite(senescence)){
                    PlotSeries marks;
                    marks.label=QStringLiteral("transitions");
                    marks.color=in.style.danger;
                    marks.drawLine=false; marks.drawMarkers=true; marks.markerSize=6.0;
                    marks.x={greenUp,senescence};
                    marks.y={midpoint,midpoint};
                    out.series.append(marks);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("day of year"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("vegetation index"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ====================================================================
    // Batch 9: scattering, thermal kinetics, astronomy, radio, and three
    // decision curves from operations and trials.

    // ------------------------------------------------------------ Guinier Plot
    // The natural log of the scattered intensity against the square of the
    // momentum transfer. At small angles that is a straight line whatever the
    // particle is, and its slope gives the radius of gyration without a model
    // for the shape - which is why it is the first thing done to a scattering
    // curve and the last thing that can be done without assuming something.
    //
    // The approximation only holds while q times Rg is below about 1.3, and Rg
    // is what is being measured, so the range cannot be chosen before the
    // answer is known. It is found by iterating: fit the low-q end, take the
    // Rg that gives, restrict to q <= 1.3/Rg, and fit again.
    if(in.engine==QLatin1String("Guinier Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& q=in.series.at(0).y;
            const QVector<double>& intensity=in.series.at(1).y;
            const int n=qMin(q.size(),intensity.size());
            QVector<QPair<double,double>> rows;   // (q^2, ln I), sorted by q^2
            for(int i=0;i<n;++i){
                if(!finite(q[i])||!finite(intensity[i])) continue;
                if(!(q[i]>0.0)||!(intensity[i]>0.0)) continue;
                rows.append(qMakePair(q[i]*q[i],std::log(intensity[i])));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=4){
                PlotSeries pts;
                pts.label=QStringLiteral("ln I");
                pts.color=in.series.at(1).color;
                pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=3.8;
                for(const QPair<double,double>& r:rows){ pts.x.append(r.first); pts.y.append(r.second); }
                out.series.append(pts);
                int take=qMax(4,rows.size()/5);
                double gyration=0.0; LineFit best;
                for(int pass=0;pass<6;++pass){
                    QVector<double> gx,gy;
                    for(int i=0;i<take&&i<rows.size();++i){ gx.append(rows[i].first); gy.append(rows[i].second); }
                    const LineFit f=fitLine(gx,gy);
                    if(!f.ok||!(f.slope<0.0)) break;
                    best=f;
                    gyration=std::sqrt(-3.0*f.slope);
                    const double limit=(1.3/gyration)*(1.3/gyration);   // q^2 at qRg = 1.3
                    int next=0;
                    while(next<rows.size()&&rows[next].first<=limit) ++next;
                    next=qBound(4,next,rows.size());
                    if(next==take) break;
                    take=next;
                }
                if(best.ok&&gyration>0.0){
                    const double used=std::sqrt(rows[qMin(take,rows.size())-1].first)*gyration;
                    PlotSeries fit;
                    fit.label=QStringLiteral("Rg %1, I0 %2, fitted to qRg %3")
                                  .arg(gyration,0,'g',5).arg(std::exp(best.intercept),0,'g',5)
                                  .arg(used,0,'f',2);
                    fit.color=in.style.warning;
                    fit.lineWidth=qMax(1.3,in.style.lineWidth);
                    fit.x={0.0,rows[qMin(take,rows.size())-1].first};
                    fit.y={best.intercept,
                           best.intercept+best.slope*rows[qMin(take,rows.size())-1].first};
                    out.series.append(fit);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("q^2"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("ln I"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------------- Kratky Plot
    // Intensity multiplied by q squared, against q. The multiplication is what
    // makes the plot worth drawing: a compact particle's intensity falls as
    // q to the fourth, so q squared times it comes back down and the curve is a
    // bell, while a chain that is not folded falls as q squared and the product
    // levels off instead. The SHAPE is the measurement.
    if(in.engine==QLatin1String("Kratky Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& q=in.series.at(0).y;
            const QVector<double>& intensity=in.series.at(1).y;
            const int n=qMin(q.size(),intensity.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(q[i])||!finite(intensity[i])||!(q[i]>0.0)) continue;
                rows.append(qMakePair(q[i],q[i]*q[i]*intensity[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=5){
                int peakAt=0;
                for(int i=1;i<rows.size();++i) if(rows[i].second>rows[peakAt].second) peakAt=i;
                // Where the curve ends relative to its own peak. Reported as a
                // RATIO with the threshold named, rather than as a verdict of
                // "folded" or "unfolded" - the reader knows their sample and
                // this engine does not.
                double tail=0.0; int used=0;
                for(int i=rows.size()-qMax(1,rows.size()/10);i<rows.size();++i){
                    tail+=rows[i].second; ++used;
                }
                tail=(used>0)?tail/double(used):0.0;
                const double ratio=(std::abs(rows[peakAt].second)>1e-300)
                                   ?tail/rows[peakAt].second:0.0;
                PlotSeries curve;
                curve.label=QStringLiteral("peak at q %1; tail is %2 of the peak (a bell falls below ~0.3)")
                                .arg(rows[peakAt].first,0,'g',4).arg(ratio,0,'f',2);
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.3,in.style.lineWidth);
                for(const QPair<double,double>& r:rows){ curve.x.append(r.first); curve.y.append(r.second); }
                out.series.append(curve);
                PlotSeries mark;
                mark.label=QStringLiteral("peak");
                mark.color=in.style.danger;
                mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.0;
                mark.x={rows[peakAt].first}; mark.y={rows[peakAt].second};
                out.series.append(mark);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("q"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("q^2 I(q)"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------------- Hill Plot
    // The log of bound over unbound, against the log of the ligand. The slope
    // is the Hill coefficient - how cooperative the binding is - and where the
    // line crosses zero is the concentration at which half the sites are
    // occupied. A sigmoid on linear axes shows the same data and gives neither
    // number without a nonlinear fit.
    if(in.engine==QLatin1String("Hill Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& ligand=in.series.at(0).y;
            const QVector<double>& response=in.series.at(1).y;
            const int n=qMin(ligand.size(),response.size());
            const Bounds br=boundsOf(response);
            if(br.valid&&br.hi>br.lo){
                // Saturation is taken from the data's own range unless the
                // response is already a fraction. Assuming a maximum that was
                // never reached puts every point below one and bends the line.
                const bool alreadyFraction=(br.lo>=0.0&&br.hi<=1.0);
                PlotSeries pts;
                pts.label=alreadyFraction
                    ? QStringLiteral("log(theta / (1 - theta))")
                    : QStringLiteral("scaled to the observed range");
                pts.color=in.series.at(1).color;
                pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;
                QVector<double> fx,fy;
                for(int i=0;i<n;++i){
                    if(!finite(ligand[i])||!finite(response[i])||!(ligand[i]>0.0)) continue;
                    const double theta=alreadyFraction?response[i]
                                                      :(response[i]-br.lo)/(br.hi-br.lo);
                    // Zero and one are asymptotes, not points: their logit is
                    // infinite and including them would drag the fit to
                    // whichever end had more of them.
                    if(!(theta>1e-9)||!(theta<1.0-1e-9)) continue;
                    const double x=std::log10(ligand[i]);
                    const double y=std::log10(theta/(1.0-theta));
                    pts.x.append(x); pts.y.append(y);
                    fx.append(x); fy.append(y);
                }
                if(!pts.x.isEmpty()){
                    out.series.append(pts);
                    const LineFit f=fitLine(fx,fy);
                    const Bounds bx=boundsOf(fx);
                    if(f.ok&&bx.valid&&std::abs(f.slope)>1e-12){
                        const double half=std::pow(10.0,-f.intercept/f.slope);
                        PlotSeries line;
                        line.label=QStringLiteral("Hill coefficient %1, half occupancy at %2")
                                       .arg(f.slope,0,'f',3).arg(half,0,'g',4);
                        line.color=in.style.warning;
                        line.lineWidth=qMax(1.3,in.style.lineWidth);
                        line.x={bx.lo,bx.hi};
                        line.y={f.intercept+f.slope*bx.lo,f.intercept+f.slope*bx.hi};
                        out.series.append(line);
                    }
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("log10 [ligand]"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("log10 (theta / (1 - theta))"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Ellingham Diagram
    // The free energy of oxide formation against temperature, one line per
    // metal, over the line for carbon burning to carbon monoxide. Where the
    // carbon line drops BELOW a metal's line is the temperature at which
    // carbon will take the oxygen off that oxide, and that crossing is the
    // reason the diagram is drawn rather than tabulated.
    if(in.engine==QLatin1String("Ellingham Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& temperature=in.series.at(0).y;
            // The magnitude decides the units. Free energies of oxide
            // formation are hundreds of kJ or hundreds of thousands of J per
            // mole of O2, and the carbon line has to be drawn in whichever the
            // data is in - so it is inferred from the data and named, rather
            // than assumed and silently wrong by a factor of a thousand.
            double largest=0.0;
            for(int c=1;c<in.series.size();++c)
                for(double v:in.series.at(c).y)
                    if(finite(v)) largest=qMax(largest,std::abs(v));
            const bool inKilojoules=(largest<5000.0);
            const double scale=inKilojoules?0.001:1.0;
            Bounds bt=boundsOf(temperature);
            for(int c=1;c<in.series.size();++c){
                const QVector<double>& energy=in.series.at(c).y;
                const int n=qMin(temperature.size(),energy.size());
                PlotSeries s;
                s.label=in.series.at(c).label;
                s.color=in.series.at(c).color;
                s.lineWidth=qMax(1.2,in.style.lineWidth);
                for(int i=0;i<n;++i){
                    if(!finite(temperature[i])||!finite(energy[i])) continue;
                    s.x.append(temperature[i]); s.y.append(energy[i]);
                }
                if(!s.x.isEmpty()) out.series.append(s);
            }
            if(bt.valid){
                // 2C + O2 -> 2CO: about -223 kJ per mole of O2 at 298 K, and
                // it goes DOWN with temperature because the reaction makes
                // more gas than it consumes. That rising entropy is what lets
                // carbon reduce almost anything if it is hot enough.
                PlotSeries carbon;
                carbon.label=QStringLiteral("2C + O2 -> 2CO (%1)")
                                 .arg(inKilojoules?QStringLiteral("kJ/mol O2")
                                                  :QStringLiteral("J/mol O2"));
                carbon.color=in.style.warning;
                carbon.lineWidth=qMax(1.4,in.style.lineWidth);
                carbon.dashPattern={6,4};
                for(int i=0;i<=60;++i){
                    const double t=bt.lo+(bt.hi-bt.lo)*double(i)/60.0;
                    carbon.x.append(t);
                    carbon.y.append((-223000.0-175.3*t)*scale);
                }
                out.series.append(carbon);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("temperature (K)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("free energy of formation per mole O2"),false,
                           unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------------ Kissinger Plot
    // The peak temperature of a thermal event moves with the heating rate, and
    // how much it moves is the activation energy. One run gives a peak and no
    // kinetics; four runs at four rates give a straight line whose slope is
    // Ea over R, which is the cheapest activation energy in thermal analysis.
    if(in.engine==QLatin1String("Kissinger Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        const double gasConstant=8.314462618;
        if(in.series.size()>=2){
            const QVector<double>& rate=in.series.at(0).y;
            const QVector<double>& peak=in.series.at(1).y;
            const int n=qMin(rate.size(),peak.size());
            PlotSeries pts;
            pts.label=QStringLiteral("runs");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.6;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(rate[i])||!finite(peak[i])) continue;
                if(!(rate[i]>0.0)||!(peak[i]>0.0)) continue;
                const double x=1000.0/peak[i];
                const double y=std::log(rate[i]/(peak[i]*peak[i]));
                pts.x.append(x); pts.y.append(y);
                fx.append(x); fy.append(y);
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                const LineFit f=fitLine(fx,fy);
                const Bounds bx=boundsOf(fx);
                if(f.ok&&bx.valid){
                    // x is 1000/T, so the slope already carries the factor
                    // that turns J/mol into kJ/mol.
                    const double activation=-f.slope*gasConstant;
                    PlotSeries line;
                    line.label=QStringLiteral("Ea %1 kJ/mol").arg(activation,0,'f',2);
                    line.color=in.style.warning;
                    line.lineWidth=qMax(1.3,in.style.lineWidth);
                    line.x={bx.lo,bx.hi};
                    line.y={f.intercept+f.slope*bx.lo,f.intercept+f.slope*bx.hi};
                    out.series.append(line);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("1000 / Tp (1/K)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("ln(beta / Tp^2)"),false,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------------------------------- Avrami Plot
    // The double log of the untransformed fraction against the log of time.
    // The slope is the Avrami exponent, and it is read as a statement about
    // MECHANISM rather than rate: near 1 is one-dimensional growth on sites
    // that are all there from the start, near 4 is three-dimensional growth
    // with nuclei still appearing.
    if(in.engine==QLatin1String("Avrami Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& time=in.series.at(0).y;
            const QVector<double>& fraction=in.series.at(1).y;
            const int n=qMin(time.size(),fraction.size());
            const Bounds bf=boundsOf(fraction);
            const bool alreadyFraction=(bf.valid&&bf.lo>=0.0&&bf.hi<=1.0);
            PlotSeries pts;
            pts.label=alreadyFraction?QStringLiteral("transformed fraction")
                                     :QStringLiteral("scaled to the observed range");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(time[i])||!finite(fraction[i])||!(time[i]>0.0)) continue;
                const double x=alreadyFraction?fraction[i]
                              :(bf.hi>bf.lo?(fraction[i]-bf.lo)/(bf.hi-bf.lo):0.0);
                if(!(x>1e-9)||!(x<1.0-1e-9)) continue;
                const double lx=std::log(time[i]);
                const double ly=std::log(-std::log(1.0-x));
                pts.x.append(lx); pts.y.append(ly);
                fx.append(lx); fy.append(ly);
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                const LineFit f=fitLine(fx,fy);
                const Bounds bx=boundsOf(fx);
                if(f.ok&&bx.valid&&std::abs(f.slope)>1e-12){
                    const double exponent=f.slope;
                    const double constant=std::exp(f.intercept/exponent);
                    const double halfTime=std::pow(std::log(2.0)/std::pow(constant,exponent),
                                                   1.0/exponent);
                    PlotSeries line;
                    line.label=QStringLiteral("n %1, k %2, half transformed at t %3")
                                   .arg(exponent,0,'f',3).arg(constant,0,'g',4)
                                   .arg(halfTime,0,'g',4);
                    line.color=in.style.warning;
                    line.lineWidth=qMax(1.3,in.style.lineWidth);
                    line.x={bx.lo,bx.hi};
                    line.y={f.intercept+f.slope*bx.lo,f.intercept+f.slope*bx.hi};
                    out.series.append(line);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("ln t"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("ln(-ln(1 - X))"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------------- Wilson Plot
    // The reciprocal of the overall heat transfer coefficient against the
    // velocity to the minus four fifths. Everything that does not depend on
    // the tube-side velocity - the wall, the outside film and any fouling -
    // sits in the intercept, and separating it from the velocity-dependent
    // part is the only way to measure fouling without dismantling the
    // exchanger.
    if(in.engine==QLatin1String("Wilson Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& velocity=in.series.at(0).y;
            const QVector<double>& coefficient=in.series.at(1).y;
            const int n=qMin(velocity.size(),coefficient.size());
            PlotSeries pts;
            pts.label=QStringLiteral("1 / U");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.4;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(velocity[i])||!finite(coefficient[i])) continue;
                if(!(velocity[i]>0.0)||!(coefficient[i]>0.0)) continue;
                // The 0.8 is Dittus-Boelter's exponent, and it is the
                // assumption the whole method rests on - so it is on the axis
                // label rather than only in the code.
                const double x=std::pow(velocity[i],-0.8);
                const double y=1.0/coefficient[i];
                pts.x.append(x); pts.y.append(y);
                fx.append(x); fy.append(y);
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                const LineFit f=fitLine(fx,fy);
                const Bounds bx=boundsOf(fx);
                if(f.ok&&bx.valid){
                    PlotSeries line;
                    line.label=QStringLiteral("velocity-independent resistance %1, slope %2")
                                   .arg(f.intercept,0,'g',4).arg(f.slope,0,'g',4);
                    line.color=in.style.warning;
                    line.lineWidth=qMax(1.3,in.style.lineWidth);
                    // Back to zero, because the intercept there is the answer.
                    line.x={0.0,bx.hi};
                    line.y={f.intercept,f.intercept+f.slope*bx.hi};
                    out.series.append(line);
                    PlotSeries mark;
                    mark.label=QStringLiteral("intercept");
                    mark.color=in.style.danger;
                    mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.0;
                    mark.x={0.0}; mark.y={f.intercept};
                    out.series.append(mark);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("velocity^-0.8 (Dittus-Boelter)"),false,
                           unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("1 / U"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Van Krevelen Diagram
    // Hydrogen to carbon against oxygen to carbon, atom for atom. Every way of
    // altering organic matter moves a sample along a line whose SLOPE is set
    // by the reaction stoichiometry and nothing else - losing water moves it
    // one way, losing carbon dioxide another, losing methane a third - so
    // where a sample has gone from tells you what happened to it.
    if(in.engine==QLatin1String("Van Krevelen Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& oxygen=in.series.at(0).y;
            const QVector<double>& hydrogen=in.series.at(1).y;
            const int n=qMin(oxygen.size(),hydrogen.size());
            PlotSeries pts;
            pts.label=QStringLiteral("samples");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.4;
            int freshest=-1;
            for(int i=0;i<n;++i){
                if(!finite(oxygen[i])||!finite(hydrogen[i])) continue;
                pts.x.append(oxygen[i]); pts.y.append(hydrogen[i]);
                if(freshest<0||hydrogen[i]>hydrogen[freshest]) freshest=i;
            }
            if(!pts.x.isEmpty()) out.series.append(pts);
            if(freshest>=0){
                // Drawn from the least altered sample - the highest H/C - and
                // said so, because the lines are a claim about where the rest
                // CAME FROM and that claim needs a stated origin.
                const double startO=oxygen[freshest], startH=hydrogen[freshest];
                // Walked out along the actual stoichiometry rather than drawn
                // as a straight line at a differentiated slope. Two of the
                // three reactions remove a CARBON as well, so the denominator
                // of both ratios changes and the path curves - and worse for a
                // straight-line version, the three do not even move the same
                // way along the oxygen axis: dehydration goes down and left,
                // decarboxylation up and left, demethanation down and RIGHT.
                // One shared slope and one shared direction would have drawn
                // at least one of them backwards.
                //
                // Per unit of carbon, removing e of each reagent gives:
                //   -H2O   C stays,  H -2e,  O -e
                //   -CO2   C -e,     H same, O -2e
                //   -CH4   C -e,     H -4e,  O same
                const int steps=40;
                for(int which=0;which<3;++which){
                    PlotSeries line;
                    line.label=(which==0)?QStringLiteral("dehydration (-H2O)")
                              :(which==1)?QStringLiteral("decarboxylation (-CO2)")
                                        :QStringLiteral("demethanation (-CH4)");
                    line.color=in.style.gridColor;
                    line.lineWidth=qMax(0.9,in.style.lineWidth*0.8);
                    line.dashPattern={5,4};
                    for(int k=0;k<=steps;++k){
                        const double e=0.35*double(k)/double(steps);
                        double carbon=1.0,hydrogen0=startH,oxygen0=startO;
                        if(which==0){ hydrogen0-=2.0*e; oxygen0-=e; }
                        else if(which==1){ carbon-=e; oxygen0-=2.0*e; }
                        else { carbon-=e; hydrogen0-=4.0*e; }
                        // Stop where the composition stops being a composition
                        // rather than drawing a negative ratio.
                        if(!(carbon>1e-6)||hydrogen0<0.0||oxygen0<0.0) break;
                        line.x.append(oxygen0/carbon);
                        line.y.append(hydrogen0/carbon);
                    }
                    if(line.x.size()>=2) out.series.append(line);
                }
                PlotSeries origin;
                origin.label=QStringLiteral("least altered (highest H/C)");
                origin.color=in.style.danger;
                origin.drawLine=false; origin.drawMarkers=true; origin.markerSize=6.5;
                origin.x={startO}; origin.y={startH};
                out.series.append(origin);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("O / C (atomic)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("H / C (atomic)"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------- Hertzsprung-Russell Diagram
    // Brightness against colour, with brightness increasing UPWARD. Both of
    // the astronomical conventions here are inversions rather than decoration:
    // a smaller magnitude is a brighter star, and a hotter star is bluer, so
    // an axis drawn the arithmetic way puts the diagram upside down and the
    // main sequence in the wrong corner.
    if(in.engine==QLatin1String("Hertzsprung-Russell Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        bool temperatureAxis=false;
        if(in.series.size()>=2){
            const QVector<double>& colour=in.series.at(0).y;
            const QVector<double>& magnitude=in.series.at(1).y;
            const int n=qMin(colour.size(),magnitude.size());
            PlotSeries pts;
            pts.label=QStringLiteral("stars");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=3.2;
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(colour[i])||!finite(magnitude[i])) continue;
                pts.x.append(colour[i]); pts.y.append(magnitude[i]);
                rows.append(qMakePair(colour[i],magnitude[i]));
            }
            // A colour index runs from about -0.4 to 2; a temperature is in
            // thousands. Which one the column holds decides which way the axis
            // runs, and getting it from the magnitude is more reliable than
            // getting it from a column name.
            const Bounds bc=boundsOf(pts.x);
            temperatureAxis=(bc.valid&&bc.hi>1000.0);
            if(!rows.isEmpty()){
                out.series.append(pts);
                // The ridge line: the median magnitude in each colour bin. It
                // is not a theoretical main sequence and is not labelled as
                // one - it is where THESE stars are, which is what can be
                // claimed without a stellar model.
                std::sort(rows.begin(),rows.end(),
                          [](const QPair<double,double>& a,const QPair<double,double>& b){
                              return a.first<b.first; });
                const int bins=qBound(4,rows.size()/12,24);
                PlotSeries ridge;
                ridge.label=QStringLiteral("median magnitude in %1 colour bins").arg(bins);
                ridge.color=in.style.warning;
                ridge.lineWidth=qMax(1.3,in.style.lineWidth);
                for(int b=0;b<bins;++b){
                    const int lo=int(qint64(b)*rows.size()/bins);
                    const int hi=int(qint64(b+1)*rows.size()/bins);
                    if(hi<=lo) continue;
                    QVector<double> centre,values;
                    for(int i=lo;i<hi;++i){ centre.append(rows[i].first); values.append(rows[i].second); }
                    std::sort(values.begin(),values.end());
                    double mid=0.0;
                    for(double v:centre) mid+=v;
                    ridge.x.append(mid/double(centre.size()));
                    ridge.y.append(values[values.size()/2]);
                }
                if(ridge.x.size()>=2) out.series.append(ridge);
            }
        }
        out.xAxis=PlotAxis{temperatureAxis?QStringLiteral("effective temperature (K)")
                                          :QStringLiteral("colour index"),
                           false,unsetValue(),unsetValue(),temperatureAxis};
        out.yAxis=PlotAxis{QStringLiteral("absolute magnitude"),false,
                           unsetValue(),unsetValue(),true};
        return out;
    }

    // ------------------------------------------------------------ Rotation Curve
    // Orbital speed against radius, against what Kepler's law would give for
    // the same central mass. The two agree while most of the mass is inside
    // the orbit and separate when it is not, and the SIZE of that separation
    // at the outer radii is the observation the whole subject rests on.
    if(in.engine==QLatin1String("Rotation Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& radius=in.series.at(0).y;
            const QVector<double>& speed=in.series.at(1).y;
            const int n=qMin(radius.size(),speed.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(radius[i])||!finite(speed[i])||!(radius[i]>0.0)) continue;
                rows.append(qMakePair(radius[i],speed[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=4){
                int peakAt=0;
                for(int i=1;i<rows.size();++i) if(rows[i].second>rows[peakAt].second) peakAt=i;
                PlotSeries observed;
                observed.label=QStringLiteral("measured");
                observed.color=in.series.at(1).color;
                observed.lineWidth=qMax(1.3,in.style.lineWidth);
                observed.drawMarkers=true; observed.markerSize=3.4;
                for(const QPair<double,double>& r:rows){
                    observed.x.append(r.first); observed.y.append(r.second);
                }
                out.series.append(observed);
                // Anchored at the peak of the measured curve, which is the
                // radius most nearly enclosing the visible mass. Anchoring at
                // the first point instead would tie the reference to whichever
                // innermost measurement happened to be noisiest.
                const double anchorR=rows[peakAt].first, anchorV=rows[peakAt].second;
                PlotSeries keplerian;
                keplerian.color=in.style.warning;
                keplerian.lineWidth=qMax(1.2,in.style.lineWidth);
                keplerian.dashPattern={6,4};
                for(const QPair<double,double>& r:rows){
                    if(r.first<anchorR) continue;
                    keplerian.x.append(r.first);
                    keplerian.y.append(anchorV*std::sqrt(anchorR/r.first));
                }
                // How far the outer third has departed from it - the number
                // the picture exists to make.
                double measured=0.0,expected=0.0; int used=0;
                for(int i=rows.size()-qMax(1,rows.size()/3);i<rows.size();++i){
                    if(rows[i].first<anchorR) continue;
                    measured+=rows[i].second;
                    expected+=anchorV*std::sqrt(anchorR/rows[i].first);
                    ++used;
                }
                keplerian.label=(used>0&&expected>1e-300)
                    ? QStringLiteral("Keplerian from r %1; outer third is %2x it")
                          .arg(anchorR,0,'g',4).arg(measured/expected,0,'f',2)
                    : QStringLiteral("Keplerian from r %1").arg(anchorR,0,'g',4);
                if(keplerian.x.size()>=2) out.series.append(keplerian);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("radius"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("orbital speed"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------------- Hubble Diagram
    // Recession speed against distance, fitted THROUGH THE ORIGIN because
    // Hubble's law has no constant term - something at zero distance is not
    // receding. A free intercept fits better and means nothing, and the slope
    // it leaves is not the Hubble constant.
    if(in.engine==QLatin1String("Hubble Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& distance=in.series.at(0).y;
            const QVector<double>& speed=in.series.at(1).y;
            const int n=qMin(distance.size(),speed.size());
            PlotSeries pts;
            pts.label=QStringLiteral("galaxies");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            double sxy=0.0,sxx=0.0;
            QVector<double> xs,ys;
            for(int i=0;i<n;++i){
                if(!finite(distance[i])||!finite(speed[i])) continue;
                pts.x.append(distance[i]); pts.y.append(speed[i]);
                xs.append(distance[i]); ys.append(speed[i]);
                sxy+=distance[i]*speed[i]; sxx+=distance[i]*distance[i];
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                const Bounds bx=boundsOf(xs);
                if(sxx>1e-300&&bx.valid){
                    const double constant=sxy/sxx;
                    double ssRes=0.0,ssTot=0.0,mean=0.0;
                    for(double v:ys) mean+=v;
                    mean/=double(ys.size());
                    for(int i=0;i<xs.size();++i){
                        const double r=ys[i]-constant*xs[i];
                        ssRes+=r*r; ssTot+=(ys[i]-mean)*(ys[i]-mean);
                    }
                    const double r2=(ssTot>1e-300)?1.0-ssRes/ssTot:0.0;
                    // The Hubble time, in Gyr, for a constant in km/s/Mpc.
                    // 977.79 is a Mpc in km divided by a Gyr in seconds; it is
                    // a unit conversion and not a cosmology, so it is only
                    // stated when the constant is plausibly in those units.
                    const bool astronomicalUnits=(constant>1.0&&constant<1000.0);
                    PlotSeries fit;
                    fit.label=astronomicalUnits
                        ? QStringLiteral("H0 %1, Hubble time %2 Gyr, R2 %3")
                              .arg(constant,0,'f',2).arg(977.79/constant,0,'f',2).arg(r2,0,'f',4)
                        : QStringLiteral("slope %1 through the origin, R2 %2")
                              .arg(constant,0,'g',5).arg(r2,0,'f',4);
                    fit.color=in.style.warning;
                    fit.lineWidth=qMax(1.3,in.style.lineWidth);
                    fit.x={0.0,bx.hi};
                    fit.y={0.0,constant*bx.hi};
                    out.series.append(fit);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("distance"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("recession speed"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Constellation Diagram
    // The received symbols in the complex plane, against where they should
    // have been. Which grid they should have been on is not stated anywhere in
    // a pair of columns, so it is FOUND: the square QAM orders are each
    // normalised to the same power as the symbols and the one that fits best
    // is named, along with the error vector magnitude against it.
    if(in.engine==QLatin1String("Constellation Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& inPhase=in.series.at(0).y;
            const QVector<double>& quadrature=in.series.at(1).y;
            const int n=qMin(inPhase.size(),quadrature.size());
            QVector<QPair<double,double>> symbols;
            double power=0.0;
            for(int i=0;i<n;++i){
                if(!finite(inPhase[i])||!finite(quadrature[i])) continue;
                symbols.append(qMakePair(inPhase[i],quadrature[i]));
                power+=inPhase[i]*inPhase[i]+quadrature[i]*quadrature[i];
            }
            if(!symbols.isEmpty()&&power>1e-300){
                const double rms=std::sqrt(power/double(symbols.size()));
                PlotSeries pts;
                pts.color=in.series.at(1).color;
                pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=2.6;
                for(const QPair<double,double>& s:symbols){
                    pts.x.append(s.first); pts.y.append(s.second);
                }
                int bestSide=0; double bestError=std::numeric_limits<double>::infinity();
                QVector<QPair<double,double>> bestGrid;
                for(int side:{2,4,8}){
                    // A square grid at the odd offsets, scaled so its own RMS
                    // matches the symbols'. Comparing against an unnormalised
                    // grid would pick whichever order happened to have the
                    // closest overall amplitude rather than the closest shape.
                    QVector<QPair<double,double>> grid;
                    double gridPower=0.0;
                    for(int a=0;a<side;++a)
                        for(int b=0;b<side;++b){
                            const double i0=2.0*a-(side-1), q0=2.0*b-(side-1);
                            grid.append(qMakePair(i0,q0));
                            gridPower+=i0*i0+q0*q0;
                        }
                    const double gridRms=std::sqrt(gridPower/double(grid.size()));
                    if(!(gridRms>1e-300)) continue;
                    for(QPair<double,double>& g:grid){
                        g.first*=rms/gridRms; g.second*=rms/gridRms;
                    }
                    double error=0.0;
                    for(const QPair<double,double>& s:symbols){
                        double nearest=std::numeric_limits<double>::infinity();
                        for(const QPair<double,double>& g:grid)
                            nearest=qMin(nearest,(s.first-g.first)*(s.first-g.first)
                                                +(s.second-g.second)*(s.second-g.second));
                        error+=nearest;
                    }
                    error=std::sqrt(error/double(symbols.size()));
                    if(error<bestError){ bestError=error; bestSide=side; bestGrid=grid; }
                }
                if(bestSide>0){
                    pts.label=QStringLiteral("%1 symbols; nearest %2-QAM, EVM %3%")
                                  .arg(symbols.size()).arg(bestSide*bestSide)
                                  .arg(100.0*bestError/rms,0,'f',2);
                    out.series.append(pts);
                    PlotSeries ideal;
                    ideal.label=QStringLiteral("ideal %1-QAM").arg(bestSide*bestSide);
                    ideal.color=in.style.warning;
                    ideal.drawLine=false; ideal.drawMarkers=true; ideal.markerSize=6.0;
                    for(const QPair<double,double>& g:bestGrid){
                        ideal.x.append(g.first); ideal.y.append(g.second);
                    }
                    out.series.append(ideal);
                }else{
                    out.series.append(pts);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("in phase"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("quadrature"),false,unsetValue(),unsetValue()};
        // Two components of one complex symbol, so the constellation is a
        // SQUARE lattice and the cloud around each ideal point is round. A
        // frame that fits each axis separately turns the error vector into an
        // ellipse and the lattice into a rectangle. See PlotSpec::equalAspect.
        out.equalAspect=true;
        return out;
    }

    // ------------------------------------------------------------- Group Delay
    // How long each frequency is held up, which is the slope of the phase and
    // not the phase itself. A filter with a flat amplitude response can still
    // smear a pulse, and the only place that shows is here.
    //
    // The phase is unwrapped first. Without it every wrap from -180 to +180
    // becomes a spike in the derivative that is an artefact of the recording
    // range rather than anything the circuit does.
    if(in.engine==QLatin1String("Group Delay")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& frequency=in.series.at(0).y;
            const QVector<double>& phase=in.series.at(1).y;
            const int n=qMin(frequency.size(),phase.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(frequency[i])||!finite(phase[i])) continue;
                rows.append(qMakePair(frequency[i],phase[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=3){
                QVector<double> unwrapped(rows.size(),0.0);
                unwrapped[0]=rows[0].second;
                double turns=0.0;
                for(int i=1;i<rows.size();++i){
                    double step=rows[i].second-rows[i-1].second;
                    while(step>180.0){ step-=360.0; turns-=360.0; }
                    while(step<-180.0){ step+=360.0; turns+=360.0; }
                    unwrapped[i]=rows[i].second+turns;
                }
                PlotSeries delay;
                delay.color=in.series.at(1).color;
                delay.lineWidth=qMax(1.3,in.style.lineWidth);
                double sum=0.0; int used=0;
                double smallest=std::numeric_limits<double>::infinity();
                double largest=-std::numeric_limits<double>::infinity();
                for(int i=0;i<rows.size();++i){
                    const int lo=qMax(0,i-1), hi=qMin(rows.size()-1,i+1);
                    const double df=rows[hi].first-rows[lo].first;
                    if(!(std::abs(df)>1e-300)) continue;
                    // -dphi/domega, with the phase in degrees: the 360 turns
                    // degrees into cycles and the cycle per hertz is a second.
                    const double tau=-(unwrapped[hi]-unwrapped[lo])/df/360.0;
                    delay.x.append(rows[i].first); delay.y.append(tau);
                    sum+=tau; ++used;
                    smallest=qMin(smallest,tau); largest=qMax(largest,tau);
                }
                if(used>0){
                    // A RIPPLE OF 1.583e-17 IS NOT A RIPPLE.
                    //
                    // On linear phase the group delay is constant, and the
                    // spread across it is whatever the last bits of the
                    // subtraction left behind - on a 1 ms delay that came out
                    // as 1.583e-17 s, reported in the same words and the same
                    // form as a real measurement. A reader has no way to tell
                    // a filter that is flat from one that ripples by a part in
                    // ten thousand, which is the distinction the number exists
                    // to make.
                    //
                    // The floor is the arithmetic's, not the filter's: each
                    // delay is a difference of doubles around the mean, so
                    // nothing below about 1e-15 of the mean survives, and a
                    // few operations later 1e-12 is still comfortably under
                    // any ripple a filter could have.
                    const double mean=sum/double(used);
                    const double ripple=largest-smallest;
                    delay.label=(ripple<=1e-12*std::abs(mean))
                        ? QStringLiteral("mean %1, flat to the arithmetic")
                              .arg(formatMeasured(mean))
                        : QStringLiteral("mean %1, ripple %2 peak to peak")
                              .arg(formatMeasured(mean)).arg(formatMeasured(ripple));
                    out.series.append(delay);
                    PlotSeries average;
                    average.label=QStringLiteral("mean delay");
                    average.color=in.style.warning;
                    average.lineWidth=qMax(1.0,in.style.lineWidth*0.85);
                    average.dashPattern={5,4};
                    average.x={rows.first().first,rows.last().first};
                    average.y={sum/double(used),sum/double(used)};
                    out.series.append(average);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("frequency"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("group delay"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Wind Profile (Log Law)
    // Wind speed against height, with height on a logarithmic axis so that the
    // logarithmic profile is the straight line it is supposed to be. Two
    // numbers come out of it: the friction velocity, which is the momentum
    // being taken out of the flow, and the roughness length, which is the
    // surface that takes it.
    if(in.engine==QLatin1String("Wind Profile (Log Law)")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& height=in.series.at(0).y;
            const QVector<double>& speed=in.series.at(1).y;
            const int n=qMin(height.size(),speed.size());
            PlotSeries pts;
            pts.label=QStringLiteral("measured");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.6;
            QVector<double> fx,fy;   // fit speed against ln height
            for(int i=0;i<n;++i){
                if(!finite(height[i])||!finite(speed[i])||!(height[i]>0.0)) continue;
                pts.x.append(speed[i]); pts.y.append(height[i]);
                fx.append(std::log(height[i])); fy.append(speed[i]);
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                const LineFit f=fitLine(fx,fy);
                const Bounds bh=boundsOf(pts.y);
                if(f.ok&&bh.valid&&f.slope>0.0){
                    const double karman=0.41;
                    const double friction=f.slope*karman;
                    const double roughness=std::exp(-f.intercept/f.slope);
                    PlotSeries profile;
                    profile.label=QStringLiteral("u* %1 (von Karman 0.41), z0 %2")
                                      .arg(friction,0,'g',4).arg(roughness,0,'g',4);
                    profile.color=in.style.warning;
                    profile.lineWidth=qMax(1.3,in.style.lineWidth);
                    // Drawn as a polyline through the fit rather than as two
                    // endpoints, because the y axis is logarithmic and a
                    // two-point line across it is a different curve.
                    for(int i=0;i<=80;++i){
                        const double z=bh.lo*std::pow(bh.hi/qMax(1e-300,bh.lo),double(i)/80.0);
                        if(!(z>0.0)) continue;
                        profile.x.append(f.intercept+f.slope*std::log(z));
                        profile.y.append(z);
                    }
                    out.series.append(profile);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("wind speed"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("height"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------- Experience Curve
    // Unit cost against cumulative production, both logarithmic, where Wright's
    // law is a straight line. What is reported is the LEARNING RATE - the cost
    // of a unit after volume has doubled, as a fraction of what it was - since
    // that is the form the number is quoted and compared in, and it is not the
    // exponent.
    if(in.engine==QLatin1String("Experience Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& produced=in.series.at(0).y;
            const QVector<double>& cost=in.series.at(1).y;
            const int n=qMin(produced.size(),cost.size());
            QVector<QPair<double,double>> rows;
            QVector<double> lx,ly;
            for(int i=0;i<n;++i){
                if(!finite(produced[i])||!finite(cost[i])) continue;
                if(!(produced[i]>0.0)||!(cost[i]>0.0)) continue;
                rows.append(qMakePair(produced[i],cost[i]));
                lx.append(std::log(produced[i])); ly.append(std::log(cost[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=3){
                PlotSeries pts;
                pts.label=QStringLiteral("unit cost");
                pts.color=in.series.at(1).color;
                pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
                for(const QPair<double,double>& r:rows){ pts.x.append(r.first); pts.y.append(r.second); }
                out.series.append(pts);
                const LineFit f=fitLine(lx,ly);
                if(f.ok){
                    const double learning=std::pow(2.0,f.slope);
                    PlotSeries curve;
                    curve.label=QStringLiteral("learning rate %1% per doubling (exponent %2)")
                                    .arg(100.0*learning,0,'f',1).arg(f.slope,0,'f',3);
                    curve.color=in.style.warning;
                    curve.lineWidth=qMax(1.3,in.style.lineWidth);
                    const double lo=rows.first().first,hi=rows.last().first;
                    for(int i=0;i<=80;++i){
                        const double volume=lo*std::pow(hi/qMax(1e-300,lo),double(i)/80.0);
                        curve.x.append(volume);
                        curve.y.append(std::exp(f.intercept+f.slope*std::log(volume)));
                    }
                    out.series.append(curve);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("cumulative production"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("unit cost"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------------ EOQ Cost Curve
    // Ordering cost, holding cost and their sum against order quantity. The
    // classic result is that the optimum is where the two components are
    // EQUAL, and drawing both is what makes that visible - but it is only true
    // for the textbook cost structure, so the crossing and the actual minimum
    // are found separately and both are reported. Where they disagree, the
    // costs are not the textbook ones and the formula does not apply.
    if(in.engine==QLatin1String("EOQ Cost Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=3){
            const QVector<double>& quantity=in.series.at(0).y;
            const QVector<double>& ordering=in.series.at(1).y;
            const QVector<double>& holding=in.series.at(2).y;
            const int n=qMin(quantity.size(),qMin(ordering.size(),holding.size()));
            QVector<QPair<double,QPair<double,double>>> rows;
            for(int i=0;i<n;++i){
                if(!finite(quantity[i])||!finite(ordering[i])||!finite(holding[i])) continue;
                rows.append(qMakePair(quantity[i],qMakePair(ordering[i],holding[i])));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,QPair<double,double>>& a,
                         const QPair<double,QPair<double,double>>& b){
                          return a.first<b.first; });
            if(rows.size()>=3){
                PlotSeries order,hold,total;
                order.label=QStringLiteral("ordering");
                order.color=in.series.at(1).color;
                order.lineWidth=qMax(1.1,in.style.lineWidth*0.9);
                hold.label=QStringLiteral("holding");
                hold.color=in.series.at(2).color;
                hold.lineWidth=qMax(1.1,in.style.lineWidth*0.9);
                total.color=in.style.warning;
                total.lineWidth=qMax(1.4,in.style.lineWidth);
                int cheapest=0;
                double crossing=std::numeric_limits<double>::quiet_NaN();
                for(int i=0;i<rows.size();++i){
                    const double o=rows[i].second.first, h=rows[i].second.second;
                    order.x.append(rows[i].first); order.y.append(o);
                    hold.x.append(rows[i].first);  hold.y.append(h);
                    total.x.append(rows[i].first); total.y.append(o+h);
                    if(o+h<rows[cheapest].second.first+rows[cheapest].second.second) cheapest=i;
                    if(i==0) continue;
                    const double before=rows[i-1].second.first-rows[i-1].second.second;
                    const double now=o-h;
                    if((before<0.0)!=(now<0.0)&&now!=before&&!finite(crossing)){
                        const double f=(0.0-before)/(now-before);
                        crossing=rows[i-1].first+f*(rows[i].first-rows[i-1].first);
                    }
                }
                total.label=finite(crossing)
                    ? QStringLiteral("total; cheapest at Q %1, components equal at Q %2")
                          .arg(rows[cheapest].first,0,'g',5).arg(crossing,0,'g',5)
                    : QStringLiteral("total; cheapest at Q %1 (components never equal here)")
                          .arg(rows[cheapest].first,0,'g',5);
                out.series.append(order);
                out.series.append(hold);
                out.series.append(total);
                PlotSeries mark;
                mark.label=QStringLiteral("cheapest");
                mark.color=in.style.danger;
                mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.5;
                mark.x={rows[cheapest].first};
                mark.y={rows[cheapest].second.first+rows[cheapest].second.second};
                out.series.append(mark);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("order quantity"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("annual cost"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ----------------------------------------------------------- Half-Normal Plot
    // The size of each estimated effect against where a purely random effect
    // of that rank would fall. Inactive effects are noise and lie on a line
    // through the origin; the ones that matter leave it at the top. Signs are
    // dropped because a screening design is asking WHICH factors matter, and a
    // large negative effect matters exactly as much as a large positive one.
    if(in.engine==QLatin1String("Half-Normal Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            QVector<double> magnitudes;
            for(double v:in.series.at(1).y) if(finite(v)) magnitudes.append(std::abs(v));
            std::sort(magnitudes.begin(),magnitudes.end());
            const int m=magnitudes.size();
            if(m>=4){
                PlotSeries pts;
                pts.color=in.series.at(1).color;
                pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.4;
                for(int i=0;i<m;++i){
                    // Half-normal plotting position: the quantile of the FOLDED
                    // normal, which is why the probability is pushed into the
                    // upper half before it is inverted.
                    const double p=(double(i)+0.5)/double(m);
                    pts.x.append(normalQuantile(0.5+0.5*p));
                    pts.y.append(magnitudes[i]);
                }
                // Lenth's pseudo standard error: a robust scale taken from the
                // effects themselves, so a screening design with no replicates
                // still has an error estimate. The two-stage median is what
                // stops the active effects inflating the very scale they are
                // meant to be judged against.
                QVector<double> sorted=magnitudes;
                const double firstPass=1.5*sorted[m/2];
                QVector<double> quiet;
                for(double v:sorted) if(v<2.5*firstPass) quiet.append(v);
                const double pse=quiet.isEmpty()?firstPass:1.5*quiet[quiet.size()/2];
                // Lenth's margin of error uses a t quantile at m/3 degrees of
                // freedom. Approximated by a Cornish-Fisher expansion rather
                // than a table: within about one per cent over the range of
                // designs this is used on, and stated as an approximation.
                const double freedom=qMax(1.0,double(m)/3.0);
                const double z=1.959963985;
                const double student=z+(z*z*z+z)/(4.0*freedom)
                                      +(5.0*std::pow(z,5)+16.0*z*z*z+3.0*z)/(96.0*freedom*freedom);
                const double margin=student*pse;
                int active=0;
                for(double v:magnitudes) if(v>margin) ++active;
                pts.label=QStringLiteral("%1 effects; Lenth PSE %2, margin %3, %4 active")
                              .arg(m).arg(pse,0,'g',4).arg(margin,0,'g',4).arg(active);
                out.series.append(pts);
                const Bounds bx=boundsOf(pts.x);
                if(bx.valid){
                    PlotSeries threshold;
                    threshold.label=QStringLiteral("Lenth margin of error");
                    threshold.color=in.style.danger;
                    threshold.lineWidth=qMax(1.0,in.style.lineWidth*0.85);
                    threshold.dashPattern={5,4};
                    threshold.x={bx.lo,bx.hi};
                    threshold.y={margin,margin};
                    out.series.append(threshold);
                    // The noise line, through the origin because an effect of
                    // zero size sits at a quantile of zero.
                    PlotSeries noise;
                    noise.label=QStringLiteral("inactive effects");
                    noise.color=in.style.gridColor;
                    noise.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                    noise.x={0.0,bx.hi};
                    noise.y={0.0,pse*bx.hi};
                    out.series.append(noise);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("half-normal quantile"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("absolute effect"),false,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------------------------- Response Waterfall
    // One bar per patient, the best change in tumour size, sorted worst to
    // best. The two lines are RECIST's thresholds, and they are what turn a
    // row of bars into a result: above plus twenty per cent is progression,
    // below minus thirty is a partial response, and everything between is
    // stable disease however it looks.
    if(in.engine==QLatin1String("Response Waterfall")){
        PlotSpec out=derivedAs(in,QStringLiteral("Stem"));
        if(in.series.size()>=2){
            QVector<double> change;
            for(double v:in.series.at(1).y) if(finite(v)) change.append(v);
            std::sort(change.begin(),change.end(),[](double a,double b){ return a>b; });
            if(!change.isEmpty()){
                int progressed=0,stable=0,responded=0;
                PlotSeries bars;
                bars.color=in.series.at(1).color;
                for(int i=0;i<change.size();++i){
                    bars.x.append(double(i+1)); bars.y.append(change[i]);
                    if(change[i]>=20.0) ++progressed;
                    else if(change[i]<=-30.0) ++responded;
                    else ++stable;
                }
                bars.label=QStringLiteral("%1 patients; %2 responded, %3 stable, %4 progressed; ORR %5%")
                               .arg(change.size()).arg(responded).arg(stable).arg(progressed)
                               .arg(100.0*double(responded)/double(change.size()),0,'f',1);
                out.series.append(bars);
                const struct { double level; const char* name; } lines[]={
                    {20.0,"progression (+20%)"},{-30.0,"partial response (-30%)"}};
                for(const auto& rule:lines){
                    PlotSeries limit;
                    limit.label=QString::fromLatin1(rule.name);
                    limit.color=(rule.level>0.0)?in.style.danger:in.style.positive;
                    limit.lineWidth=qMax(1.0,in.style.lineWidth*0.85);
                    limit.dashPattern={5,4};
                    limit.x={1.0,double(change.size())};
                    limit.y={rule.level,rule.level};
                    out.series.append(limit);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("patient, ranked"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("best change from baseline (%)"),false,
                           unsetValue(),unsetValue()};
        return out;
    }

    // ====================================================================
    // Batch 10: open-channel and process hydraulics, power systems, imaging,
    // radiation, exercise physiology, traffic and two sequential decisions.

    // ------------------------------------------------- Specific Energy Diagram
    // Depth against the energy the flow carries past a section. The curve has a
    // MINIMUM, and everything about open-channel behaviour follows from it:
    // below that energy no flow of this discharge is possible, above it there
    // are two depths that carry the same discharge with the same energy, and
    // which one a channel is in decides whether a disturbance can travel back
    // upstream.
    //
    // The energy is computed from the discharge rather than read from a column,
    // because the whole point is the shape of the curve at a FIXED discharge.
    if(in.engine==QLatin1String("Specific Energy Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& depth=in.series.at(0).y;
            const QVector<double>& discharge=in.series.at(1).y;
            // One discharge per curve. The median of the column is used when
            // it varies, so a noisy or repeated reading does not split the
            // family into a hundred near-identical curves.
            QVector<double> flows;
            for(double v:discharge) if(finite(v)&&v>0.0) flows.append(v);
            std::sort(flows.begin(),flows.end());
            const Bounds bd=boundsOf(depth);
            if(!flows.isEmpty()&&bd.valid&&bd.hi>0.0){
                const double unitDischarge=flows[flows.size()/2];
                const double gravity=9.81;
                auto energyAt=[&](double y){
                    return y+unitDischarge*unitDischarge/(2.0*gravity*y*y);
                };
                const double critical=std::cbrt(unitDischarge*unitDischarge/gravity);
                const double minimumEnergy=1.5*critical;
                PlotSeries curve;
                curve.label=QStringLiteral("q %1, yc %2, Emin %3 (g 9.81)")
                                .arg(unitDischarge,0,'g',4).arg(critical,0,'g',4)
                                .arg(minimumEnergy,0,'g',4);
                curve.color=in.series.at(0).color;
                curve.lineWidth=qMax(1.4,in.style.lineWidth);
                const double lo=qMax(0.05*critical,0.02*bd.hi);
                const double hi=qMax(bd.hi,2.5*critical);
                for(int i=0;i<=200;++i){
                    const double y=lo+(hi-lo)*double(i)/200.0;
                    if(!(y>0.0)) continue;
                    curve.x.append(energyAt(y)); curve.y.append(y);
                }
                out.series.append(curve);
                // E = y is the asymptote the upper branch approaches as the
                // velocity head vanishes. Without it the two branches read as
                // one bent line rather than as two regimes.
                PlotSeries asymptote;
                asymptote.label=QStringLiteral("E = y (no velocity head)");
                asymptote.color=in.style.gridColor;
                asymptote.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                asymptote.dashPattern={5,4};
                asymptote.x={0.0,hi}; asymptote.y={0.0,hi};
                out.series.append(asymptote);
                PlotSeries measured;
                measured.label=QStringLiteral("measured depths");
                measured.color=in.series.at(1).color;
                measured.drawLine=false; measured.drawMarkers=true; measured.markerSize=4.4;
                for(double y:depth){
                    if(!finite(y)||!(y>0.0)) continue;
                    measured.x.append(energyAt(y)); measured.y.append(y);
                }
                if(!measured.x.isEmpty()) out.series.append(measured);
                PlotSeries mark;
                mark.label=QStringLiteral("critical depth");
                mark.color=in.style.danger;
                mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.5;
                mark.x={minimumEnergy}; mark.y={critical};
                out.series.append(mark);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("specific energy"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("depth"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------ NTU-Effectiveness Curve
    // How much of the available temperature difference an exchanger actually
    // recovers, against how big it is. The measured points are drawn over the
    // COUNTERFLOW relation for the same capacity ratio, which is the ceiling
    // for that geometry - so the gap is the finding, and it is a gap from a
    // formula rather than from a fitted line through the same points.
    if(in.engine==QLatin1String("NTU-Effectiveness Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=3){
            const QVector<double>& units=in.series.at(0).y;
            const QVector<double>& effectiveness=in.series.at(1).y;
            const QVector<double>& ratio=in.series.at(2).y;
            const int n=qMin(units.size(),qMin(effectiveness.size(),ratio.size()));
            QMap<double,QVector<QPair<double,double>>> byRatio;
            for(int i=0;i<n;++i){
                if(!finite(units[i])||!finite(effectiveness[i])||!finite(ratio[i])) continue;
                if(!(units[i]>0.0)) continue;
                byRatio[ratio[i]].append(qMakePair(units[i],effectiveness[i]));
            }
            int index=0;
            for(auto it=byRatio.constBegin();it!=byRatio.constEnd();++it,++index){
                QVector<QPair<double,double>> rows=it.value();
                std::sort(rows.begin(),rows.end(),
                          [](const QPair<double,double>& a,const QPair<double,double>& b){
                              return a.first<b.first; });
                const double capacity=qBound(0.0,it.key(),1.0);
                PlotSeries pts;
                pts.label=QStringLiteral("Cr %1, measured").arg(it.key(),0,'g',3);
                pts.color=categoryColour(in,index,std::fmod(0.55+double(index)*0.14,1.0),0.62,0.95);
                pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;
                double worst=0.0;
                for(const QPair<double,double>& r:rows){ pts.x.append(r.first); pts.y.append(r.second); }
                PlotSeries reference;
                reference.color=pts.color;
                reference.lineWidth=qMax(1.2,in.style.lineWidth);
                const Bounds bu=boundsOf(pts.x);
                if(bu.valid){
                    auto counterflow=[&](double ntu){
                        // Cr = 1 is the removable singularity of the general
                        // relation, not a special case of the exchanger: the
                        // limit is NTU/(1+NTU) and computing the general form
                        // there divides zero by zero.
                        if(std::abs(1.0-capacity)<1e-9) return ntu/(1.0+ntu);
                        const double e=std::exp(-ntu*(1.0-capacity));
                        return (1.0-e)/(1.0-capacity*e);
                    };
                    for(int i=0;i<=120;++i){
                        const double ntu=bu.lo+(bu.hi-bu.lo)*double(i)/120.0;
                        if(!(ntu>0.0)) continue;
                        reference.x.append(ntu); reference.y.append(counterflow(ntu));
                    }
                    for(const QPair<double,double>& r:rows)
                        worst=qMax(worst,counterflow(r.first)-r.second);
                    reference.label=QStringLiteral("Cr %1, counterflow limit (worst shortfall %2)")
                                        .arg(it.key(),0,'g',3).arg(worst,0,'f',3);
                }
                out.series.append(pts);
                if(reference.x.size()>=2) out.series.append(reference);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("NTU"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("effectiveness"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------------- Pump Operating Point
    // The pump's head against the head the system demands, on one pair of axes.
    // Neither curve alone says what the pump will actually do; where they cross
    // does, and that duty point is the only flow the installation can settle at.
    if(in.engine==QLatin1String("Pump Operating Point")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=3){
            const QVector<double>& flow=in.series.at(0).y;
            const QVector<double>& pumpHead=in.series.at(1).y;
            const QVector<double>& systemHead=in.series.at(2).y;
            const int n=qMin(flow.size(),qMin(pumpHead.size(),systemHead.size()));
            QVector<QPair<double,QPair<double,double>>> rows;
            for(int i=0;i<n;++i){
                if(!finite(flow[i])||!finite(pumpHead[i])||!finite(systemHead[i])) continue;
                rows.append(qMakePair(flow[i],qMakePair(pumpHead[i],systemHead[i])));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,QPair<double,double>>& a,
                         const QPair<double,QPair<double,double>>& b){
                          return a.first<b.first; });
            if(rows.size()>=2){
                PlotSeries pump,system;
                pump.color=in.series.at(1).color;
                pump.lineWidth=qMax(1.4,in.style.lineWidth);
                system.label=QStringLiteral("system demand");
                system.color=in.series.at(2).color;
                system.lineWidth=qMax(1.4,in.style.lineWidth);
                double dutyFlow=std::numeric_limits<double>::quiet_NaN();
                double dutyHead=std::numeric_limits<double>::quiet_NaN();
                for(int i=0;i<rows.size();++i){
                    pump.x.append(rows[i].first);   pump.y.append(rows[i].second.first);
                    system.x.append(rows[i].first); system.y.append(rows[i].second.second);
                    if(i==0) continue;
                    const double before=rows[i-1].second.first-rows[i-1].second.second;
                    const double now=rows[i].second.first-rows[i].second.second;
                    if((before<0.0)!=(now<0.0)&&now!=before&&!finite(dutyFlow)){
                        const double f=(0.0-before)/(now-before);
                        dutyFlow=rows[i-1].first+f*(rows[i].first-rows[i-1].first);
                        dutyHead=rows[i-1].second.first
                                +f*(rows[i].second.first-rows[i-1].second.first);
                    }
                }
                pump.label=finite(dutyFlow)
                    ? QStringLiteral("pump; duty point %1 at head %2")
                          .arg(dutyFlow,0,'g',5).arg(dutyHead,0,'g',5)
                    : QStringLiteral("pump (the two curves do not cross in this range)");
                out.series.append(pump);
                out.series.append(system);
                if(finite(dutyFlow)){
                    PlotSeries mark;
                    mark.label=QStringLiteral("duty point");
                    mark.color=in.style.danger;
                    mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.5;
                    mark.x={dutyFlow}; mark.y={dutyHead};
                    out.series.append(mark);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("flow"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("head"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Duck Curve (Net Load)
    // Demand minus what the renewables produced, through the day. The shape is
    // the reason the plot has a nickname: solar hollows out the middle of the
    // day and the load it was covering all arrives at once when it stops. What
    // the system has to be built for is not the peak but the RAMP, so the
    // steepest rise is computed and reported in load per hour.
    if(in.engine==QLatin1String("Duck Curve (Net Load)")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=3){
            const QVector<double>& when=in.series.at(0).y;
            const QVector<double>& demand=in.series.at(1).y;
            const QVector<double>& renewable=in.series.at(2).y;
            const int n=qMin(when.size(),qMin(demand.size(),renewable.size()));
            QVector<QPair<double,QPair<double,double>>> rows;
            for(int i=0;i<n;++i){
                if(!finite(when[i])||!finite(demand[i])||!finite(renewable[i])) continue;
                rows.append(qMakePair(when[i],qMakePair(demand[i],renewable[i])));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,QPair<double,double>>& a,
                         const QPair<double,QPair<double,double>>& b){
                          return a.first<b.first; });
            if(rows.size()>=3){
                PlotSeries gross,clean,net;
                gross.label=QStringLiteral("demand");
                gross.color=in.series.at(1).color;
                gross.lineWidth=qMax(1.1,in.style.lineWidth*0.9);
                clean.label=QStringLiteral("renewable generation");
                clean.color=in.style.positive;
                clean.lineWidth=qMax(1.1,in.style.lineWidth*0.9);
                net.color=in.style.warning;
                net.lineWidth=qMax(1.5,in.style.lineWidth);
                int lowest=0,highest=0; double steepest=0.0,steepestAt=0.0;
                QVector<double> netLoad;
                for(int i=0;i<rows.size();++i){
                    const double value=rows[i].second.first-rows[i].second.second;
                    netLoad.append(value);
                    gross.x.append(rows[i].first); gross.y.append(rows[i].second.first);
                    clean.x.append(rows[i].first); clean.y.append(rows[i].second.second);
                    net.x.append(rows[i].first);   net.y.append(value);
                    if(value<netLoad[lowest]) lowest=i;
                    if(value>netLoad[highest]) highest=i;
                    if(i==0) continue;
                    const double span=rows[i].first-rows[i-1].first;
                    if(!(span>1e-300)) continue;
                    const double rate=(value-netLoad[i-1])/span;
                    if(rate>steepest){ steepest=rate; steepestAt=rows[i].first; }
                }
                net.label=QStringLiteral("net load; trough %1 at %2, peak %3, steepest ramp %4 per unit time at %5")
                              .arg(netLoad[lowest],0,'g',5).arg(rows[lowest].first,0,'g',4)
                              .arg(netLoad[highest],0,'g',5)
                              .arg(steepest,0,'g',4).arg(steepestAt,0,'g',4);
                out.series.append(gross);
                out.series.append(clean);
                out.series.append(net);
                PlotSeries marks;
                marks.label=QStringLiteral("trough and peak");
                marks.color=in.style.danger;
                marks.drawLine=false; marks.drawMarkers=true; marks.markerSize=6.0;
                marks.x={rows[lowest].first,rows[highest].first};
                marks.y={netLoad[lowest],netLoad[highest]};
                out.series.append(marks);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("time of day"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("load"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------- Protection Coordination Curve
    // Operating time against fault current, one curve per device, on log-log
    // axes. Coordination means the device nearest the fault always operates
    // first, so what matters is the smallest TIME GAP between a device and the
    // one upstream of it - and that gap is computed at every current the two
    // curves share rather than eyeballed where they look closest.
    if(in.engine==QLatin1String("Protection Coordination Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=3){
            const QVector<double>& current=in.series.at(0).y;
            const QVector<double>& operating=in.series.at(1).y;
            const QVector<double>& device=in.series.at(2).y;
            const int n=qMin(current.size(),qMin(operating.size(),device.size()));
            QMap<double,QVector<QPair<double,double>>> byDevice;
            for(int i=0;i<n;++i){
                if(!finite(current[i])||!finite(operating[i])||!finite(device[i])) continue;
                if(!(current[i]>0.0)||!(operating[i]>0.0)) continue;
                byDevice[device[i]].append(qMakePair(current[i],operating[i]));
            }
            QVector<QVector<QPair<double,double>>> curves;
            int index=0;
            for(auto it=byDevice.constBegin();it!=byDevice.constEnd();++it,++index){
                QVector<QPair<double,double>> rows=it.value();
                std::sort(rows.begin(),rows.end(),
                          [](const QPair<double,double>& a,const QPair<double,double>& b){
                              return a.first<b.first; });
                curves.append(rows);
                PlotSeries s;
                s.label=QStringLiteral("device %1").arg(it.key(),0,'g',6);
                s.color=categoryColour(in,index,std::fmod(0.06+double(index)*0.17,1.0),0.68,0.95);
                s.lineWidth=qMax(1.3,in.style.lineWidth);
                for(const QPair<double,double>& r:rows){ s.x.append(r.first); s.y.append(r.second); }
                out.series.append(s);
            }
            if(curves.size()>=2&&!out.series.isEmpty()){
                const auto timeAt=[](const QVector<QPair<double,double>>& c,double i){
                    // Closed at both ends. Excluding them was a guard against
                    // extrapolating, and it also refused the two currents most
                    // likely to carry the answer: on a pair of curves that end
                    // together, the tightest interval is usually AT the end,
                    // and it was being reported one grid step short of it.
                    if(i<c.first().first||i>c.last().first)
                        return std::numeric_limits<double>::quiet_NaN();
                    int hi=1;
                    while(hi<c.size()&&c[hi].first<i) ++hi;
                    hi=qBound(1,hi,c.size()-1);
                    const double x0=c[hi-1].first,x1=c[hi].first;
                    const double y0=c[hi-1].second,y1=c[hi].second;
                    return (x1>x0)?y0+(i-x0)/(x1-x0)*(y1-y0):y1;
                };
                double tightest=std::numeric_limits<double>::infinity();
                double tightestAt=0.0;
                for(int a=0;a+1<curves.size();++a){
                    const double lo=qMax(curves[a].first().first,curves[a+1].first().first);
                    const double hi=qMin(curves[a].last().first,curves[a+1].last().first);
                    for(int k=0;k<=200&&hi>lo;++k){
                        const double i=lo*std::pow(hi/qMax(1e-300,lo),double(k)/200.0);
                        const double ta=timeAt(curves[a],i),tb=timeAt(curves[a+1],i);
                        if(!finite(ta)||!finite(tb)) continue;
                        const double gap=std::abs(tb-ta);
                        if(gap<tightest){ tightest=gap; tightestAt=i; }
                    }
                }
                if(finite(tightest))
                    out.series[0].label=QStringLiteral("%1; tightest interval %2 at current %3")
                                            .arg(out.series[0].label)
                                            .arg(tightest,0,'g',4).arg(tightestAt,0,'g',5);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("current"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("operating time"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ----------------------------------------------------------------- MTF Curve
    // How much contrast survives at each spatial frequency. A lens is quoted by
    // the frequency at which half the contrast is left, so that crossing is
    // interpolated rather than read off the nearest measured frequency - the
    // measurement grid is usually coarse exactly where the curve is steepest.
    if(in.engine==QLatin1String("MTF Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& spatial=in.series.at(0).y;
            const QVector<double>& modulation=in.series.at(1).y;
            const int n=qMin(spatial.size(),modulation.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(spatial[i])||!finite(modulation[i])) continue;
                rows.append(qMakePair(spatial[i],modulation[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=3){
                const auto crossing=[&](double level){
                    for(int i=1;i<rows.size();++i){
                        const double a=rows[i-1].second,b=rows[i].second;
                        if((a>=level)==(b>=level)||a==b) continue;
                        const double f=(level-a)/(b-a);
                        return rows[i-1].first+f*(rows[i].first-rows[i-1].first);
                    }
                    return std::numeric_limits<double>::quiet_NaN();
                };
                const double half=crossing(0.5), tenth=crossing(0.1);
                PlotSeries curve;
                curve.label=finite(half)
                    ? (finite(tenth)?QStringLiteral("MTF50 %1, MTF10 %2").arg(half,0,'g',5).arg(tenth,0,'g',5)
                                    :QStringLiteral("MTF50 %1").arg(half,0,'g',5))
                    : QStringLiteral("contrast never falls to half in this range");
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.4,in.style.lineWidth);
                curve.drawMarkers=true; curve.markerSize=3.4;
                for(const QPair<double,double>& r:rows){ curve.x.append(r.first); curve.y.append(r.second); }
                out.series.append(curve);
                const struct { double level; const char* name; } marks[]={
                    {0.5,"50% contrast"},{0.1,"10% contrast"}};
                for(const auto& rule:marks){
                    PlotSeries level;
                    level.label=QString::fromLatin1(rule.name);
                    level.color=(rule.level>0.3)?in.style.warning:in.style.gridColor;
                    level.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                    level.dashPattern={5,4};
                    level.x={rows.first().first,rows.last().first};
                    level.y={rule.level,rule.level};
                    out.series.append(level);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("spatial frequency"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("modulation"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Radioactive Decay Fit
    // Activity against time on a logarithmic ordinate, where a single decaying
    // species is a straight line. The half-life comes off the slope, and the
    // straightness is itself a result: a curve that bends has more than one
    // species in it, or a background that was never subtracted.
    if(in.engine==QLatin1String("Radioactive Decay Fit")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& time=in.series.at(0).y;
            const QVector<double>& activity=in.series.at(1).y;
            const int n=qMin(time.size(),activity.size());
            PlotSeries pts;
            pts.label=QStringLiteral("measured");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(time[i])||!finite(activity[i])||!(activity[i]>0.0)) continue;
                pts.x.append(time[i]); pts.y.append(activity[i]);
                fx.append(time[i]); fy.append(std::log(activity[i]));
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                const LineFit f=fitLine(fx,fy);
                const Bounds bx=boundsOf(fx);
                if(f.ok&&bx.valid&&f.slope<0.0){
                    const double constant=-f.slope;
                    double ssRes=0.0,ssTot=0.0,mean=0.0;
                    for(double v:fy) mean+=v;
                    mean/=double(fy.size());
                    for(int i=0;i<fx.size();++i){
                        const double r=fy[i]-(f.intercept+f.slope*fx[i]);
                        ssRes+=r*r; ssTot+=(fy[i]-mean)*(fy[i]-mean);
                    }
                    const double r2=(ssTot>1e-300)?1.0-ssRes/ssTot:0.0;
                    PlotSeries fit;
                    fit.label=QStringLiteral("half-life %1, lambda %2, R2 %3")
                                  .arg(std::log(2.0)/constant,0,'g',5)
                                  .arg(constant,0,'g',4).arg(r2,0,'f',5);
                    fit.color=in.style.warning;
                    fit.lineWidth=qMax(1.3,in.style.lineWidth);
                    // Drawn as a polyline because the ordinate is logarithmic
                    // and a two-point line across a log axis is a curve that
                    // does not pass through the fit it claims to be.
                    for(int i=0;i<=80;++i){
                        const double t=bx.lo+(bx.hi-bx.lo)*double(i)/80.0;
                        fit.x.append(t); fit.y.append(std::exp(f.intercept+f.slope*t));
                    }
                    out.series.append(fit);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("time"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("activity"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------- Attenuation Curve
    // Transmitted intensity against absorber thickness. Exponential attenuation
    // is a straight line on a logarithmic ordinate, and the two numbers that
    // come off it are the linear attenuation coefficient and the half-value
    // layer - the thickness that stops half of what arrives, which is what a
    // shielding calculation is actually written in.
    if(in.engine==QLatin1String("Attenuation Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& thickness=in.series.at(0).y;
            const QVector<double>& intensity=in.series.at(1).y;
            const int n=qMin(thickness.size(),intensity.size());
            PlotSeries pts;
            pts.label=QStringLiteral("measured");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(thickness[i])||!finite(intensity[i])||!(intensity[i]>0.0)) continue;
                pts.x.append(thickness[i]); pts.y.append(intensity[i]);
                fx.append(thickness[i]); fy.append(std::log(intensity[i]));
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                const LineFit f=fitLine(fx,fy);
                const Bounds bx=boundsOf(fx);
                if(f.ok&&bx.valid&&f.slope<0.0){
                    const double attenuation=-f.slope;
                    const double halfValue=std::log(2.0)/attenuation;
                    PlotSeries fit;
                    fit.label=QStringLiteral("mu %1, half-value layer %2, I0 %3")
                                  .arg(attenuation,0,'g',5).arg(halfValue,0,'g',5)
                                  .arg(std::exp(f.intercept),0,'g',5);
                    fit.color=in.style.warning;
                    fit.lineWidth=qMax(1.3,in.style.lineWidth);
                    for(int i=0;i<=80;++i){
                        const double x=bx.lo+(bx.hi-bx.lo)*double(i)/80.0;
                        fit.x.append(x); fit.y.append(std::exp(f.intercept+f.slope*x));
                    }
                    out.series.append(fit);
                    PlotSeries mark;
                    mark.label=QStringLiteral("half-value layer");
                    mark.color=in.style.danger;
                    mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.0;
                    mark.x={halfValue}; mark.y={0.5*std::exp(f.intercept)};
                    out.series.append(mark);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("absorber thickness"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("transmitted intensity"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ----------------------------------------------------------- Depth-Dose Curve
    // Dose against depth. A treatment is planned on the DISTAL edge rather than
    // on the peak, because the peak is where the dose is highest and the edge
    // is where the healthy tissue behind it starts - so the depths at which the
    // dose has fallen back through 90% and 80% of its maximum are computed on
    // the far side of the peak and marked.
    if(in.engine==QLatin1String("Depth-Dose Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& depth=in.series.at(0).y;
            const QVector<double>& dose=in.series.at(1).y;
            const int n=qMin(depth.size(),dose.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(depth[i])||!finite(dose[i])) continue;
                rows.append(qMakePair(depth[i],dose[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=4){
                int peakAt=0;
                for(int i=1;i<rows.size();++i) if(rows[i].second>rows[peakAt].second) peakAt=i;
                const double peak=rows[peakAt].second;
                const auto distal=[&](double share){
                    const double level=share*peak;
                    for(int i=peakAt+1;i<rows.size();++i){
                        const double a=rows[i-1].second,b=rows[i].second;
                        if((a>=level)==(b>=level)||a==b) continue;
                        const double f=(level-a)/(b-a);
                        return rows[i-1].first+f*(rows[i].first-rows[i-1].first);
                    }
                    return std::numeric_limits<double>::quiet_NaN();
                };
                const double r90=distal(0.90), r80=distal(0.80);
                PlotSeries curve;
                curve.label=(finite(r90)&&finite(r80))
                    ? QStringLiteral("peak %1 at depth %2; R90 %3, R80 %4")
                          .arg(peak,0,'g',5).arg(rows[peakAt].first,0,'g',5)
                          .arg(r90,0,'g',5).arg(r80,0,'g',5)
                    : QStringLiteral("peak %1 at depth %2 (no distal fall-off in this range)")
                          .arg(peak,0,'g',5).arg(rows[peakAt].first,0,'g',5);
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.4,in.style.lineWidth);
                for(const QPair<double,double>& r:rows){ curve.x.append(r.first); curve.y.append(r.second); }
                out.series.append(curve);
                if(finite(r90)&&finite(r80)){
                    PlotSeries marks;
                    marks.label=QStringLiteral("peak, R90, R80");
                    marks.color=in.style.danger;
                    marks.drawLine=false; marks.drawMarkers=true; marks.markerSize=6.0;
                    marks.x={rows[peakAt].first,r90,r80};
                    marks.y={peak,0.90*peak,0.80*peak};
                    out.series.append(marks);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("depth"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("dose"),false,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------------------------- Critical Power Curve
    // Sustainable power against how long it was sustained for. Power against
    // one over duration is a STRAIGHT LINE, and the two numbers it gives are
    // the whole model: the intercept is the power that can be held more or less
    // indefinitely, and the slope is the fixed amount of work available above
    // it - a battery that empties once, whatever rate it is spent at.
    if(in.engine==QLatin1String("Critical Power Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& duration=in.series.at(0).y;
            const QVector<double>& power=in.series.at(1).y;
            const int n=qMin(duration.size(),power.size());
            PlotSeries pts;
            pts.label=QStringLiteral("efforts");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.6;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(duration[i])||!finite(power[i])||!(duration[i]>0.0)) continue;
                pts.x.append(1.0/duration[i]); pts.y.append(power[i]);
                fx.append(1.0/duration[i]); fy.append(power[i]);
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
                    line.label=QStringLiteral("CP %1, W' %2, R2 %3")
                                   .arg(f.intercept,0,'g',5).arg(f.slope,0,'g',5)
                                   .arg(r2,0,'f',5);
                    line.color=in.style.warning;
                    line.lineWidth=qMax(1.3,in.style.lineWidth);
                    // Back to zero, where an infinitely long effort would sit
                    // and where the intercept is therefore read.
                    line.x={0.0,bx.hi};
                    line.y={f.intercept,f.intercept+f.slope*bx.hi};
                    out.series.append(line);
                    PlotSeries mark;
                    mark.label=QStringLiteral("critical power");
                    mark.color=in.style.danger;
                    mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.5;
                    mark.x={0.0}; mark.y={f.intercept};
                    out.series.append(mark);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("1 / duration"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("power"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------------- Lactate Threshold
    // Blood lactate against work rate, with two thresholds that are defined
    // differently on purpose. The fixed four millimole crossing is comparable
    // between people and arbitrary for any one of them; the D-max point is the
    // work rate furthest from the chord joining the first and last measurement,
    // so it is defined by the shape of THIS curve and is not comparable in the
    // same way. Both are drawn because neither answers the other's question.
    if(in.engine==QLatin1String("Lactate Threshold Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& intensity=in.series.at(0).y;
            const QVector<double>& lactate=in.series.at(1).y;
            const int n=qMin(intensity.size(),lactate.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(intensity[i])||!finite(lactate[i])) continue;
                rows.append(qMakePair(intensity[i],lactate[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=3){
                double fixed=std::numeric_limits<double>::quiet_NaN();
                for(int i=1;i<rows.size();++i){
                    const double a=rows[i-1].second,b=rows[i].second;
                    if((a>=4.0)==(b>=4.0)||a==b) continue;
                    const double f=(4.0-a)/(b-a);
                    fixed=rows[i-1].first+f*(rows[i].first-rows[i-1].first);
                    break;
                }
                // Perpendicular distance to the chord, computed from the cross
                // product so it does not need the chord's slope and does not
                // blow up on a vertical one.
                const double x0=rows.first().first,y0=rows.first().second;
                const double x1=rows.last().first,y1=rows.last().second;
                const double length=std::hypot(x1-x0,y1-y0);
                int furthest=0; double best=-1.0;
                if(length>1e-300)
                    for(int i=0;i<rows.size();++i){
                        const double d=std::abs((x1-x0)*(y0-rows[i].second)
                                               -(x0-rows[i].first)*(y1-y0))/length;
                        if(d>best){ best=d; furthest=i; }
                    }
                PlotSeries curve;
                curve.label=finite(fixed)
                    ? QStringLiteral("4 mmol at %1; D-max at %2")
                          .arg(fixed,0,'g',5).arg(rows[furthest].first,0,'g',5)
                    : QStringLiteral("lactate never reaches 4 mmol; D-max at %1")
                          .arg(rows[furthest].first,0,'g',5);
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.4,in.style.lineWidth);
                curve.drawMarkers=true; curve.markerSize=3.8;
                for(const QPair<double,double>& r:rows){ curve.x.append(r.first); curve.y.append(r.second); }
                out.series.append(curve);
                PlotSeries chord;
                chord.label=QStringLiteral("D-max chord");
                chord.color=in.style.gridColor;
                chord.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                chord.dashPattern={5,4};
                chord.x={x0,x1}; chord.y={y0,y1};
                out.series.append(chord);
                PlotSeries level;
                level.label=QStringLiteral("4 mmol");
                level.color=in.style.warning;
                level.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                level.dashPattern={5,4};
                level.x={rows.first().first,rows.last().first};
                level.y={4.0,4.0};
                out.series.append(level);
                PlotSeries mark;
                mark.label=QStringLiteral("D-max");
                mark.color=in.style.danger;
                mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.5;
                mark.x={rows[furthest].first}; mark.y={rows[furthest].second};
                out.series.append(mark);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("work rate"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("blood lactate"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Half-Power Bandwidth
    // A resonance peak with the two frequencies either side of it where the
    // response has fallen to one over root two of the maximum. The width
    // between them gives the quality factor and the damping ratio, which is the
    // only way to get damping out of a frequency sweep without fitting a model
    // to the whole curve.
    if(in.engine==QLatin1String("Half-Power Bandwidth")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& frequency=in.series.at(0).y;
            const QVector<double>& magnitude=in.series.at(1).y;
            const int n=qMin(frequency.size(),magnitude.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(frequency[i])||!finite(magnitude[i])) continue;
                rows.append(qMakePair(frequency[i],magnitude[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=5){
                int peakAt=0;
                for(int i=1;i<rows.size();++i) if(rows[i].second>rows[peakAt].second) peakAt=i;
                const double peak=rows[peakAt].second;
                const double level=peak/std::sqrt(2.0);
                double lower=std::numeric_limits<double>::quiet_NaN();
                double upper=std::numeric_limits<double>::quiet_NaN();
                for(int i=peakAt;i>0;--i){
                    const double a=rows[i-1].second,b=rows[i].second;
                    if((a>=level)==(b>=level)||a==b) continue;
                    const double f=(level-a)/(b-a);
                    lower=rows[i-1].first+f*(rows[i].first-rows[i-1].first);
                    break;
                }
                for(int i=peakAt+1;i<rows.size();++i){
                    const double a=rows[i-1].second,b=rows[i].second;
                    if((a>=level)==(b>=level)||a==b) continue;
                    const double f=(level-a)/(b-a);
                    upper=rows[i-1].first+f*(rows[i].first-rows[i-1].first);
                    break;
                }
                PlotSeries curve;
                const double centre=rows[peakAt].first;
                if(finite(lower)&&finite(upper)&&upper>lower){
                    const double quality=centre/(upper-lower);
                    curve.label=QStringLiteral("f0 %1, bandwidth %2, Q %3, zeta %4")
                                    .arg(centre,0,'g',5).arg(upper-lower,0,'g',4)
                                    .arg(quality,0,'f',2).arg(0.5/quality,0,'f',4);
                }else{
                    curve.label=QStringLiteral("f0 %1 (the response does not fall to half power on both sides)")
                                    .arg(centre,0,'g',5);
                }
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.4,in.style.lineWidth);
                for(const QPair<double,double>& r:rows){ curve.x.append(r.first); curve.y.append(r.second); }
                out.series.append(curve);
                PlotSeries halfPower;
                halfPower.label=QStringLiteral("half power (peak / sqrt 2)");
                halfPower.color=in.style.warning;
                halfPower.lineWidth=qMax(0.9,in.style.lineWidth*0.75);
                halfPower.dashPattern={5,4};
                halfPower.x={rows.first().first,rows.last().first};
                halfPower.y={level,level};
                out.series.append(halfPower);
                if(finite(lower)&&finite(upper)){
                    PlotSeries marks;
                    marks.label=QStringLiteral("half-power points");
                    marks.color=in.style.danger;
                    marks.drawLine=false; marks.drawMarkers=true; marks.markerSize=6.0;
                    marks.x={lower,upper}; marks.y={level,level};
                    out.series.append(marks);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("frequency"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("response magnitude"),false,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------------------- Cumulative Vehicle Count
    // Cumulative arrivals and cumulative departures against time. Everything
    // about a queue is in the gap between the two curves: the VERTICAL gap is
    // how many vehicles are waiting, the HORIZONTAL gap is how long each waits,
    // and the AREA between them is the total delay. Reading all three off one
    // pair of curves is why this is drawn instead of three separate plots.
    if(in.engine==QLatin1String("Cumulative Vehicle Count")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=3){
            const QVector<double>& when=in.series.at(0).y;
            const QVector<double>& arrivals=in.series.at(1).y;
            const QVector<double>& departures=in.series.at(2).y;
            const int n=qMin(when.size(),qMin(arrivals.size(),departures.size()));
            QVector<QPair<double,QPair<double,double>>> rows;
            for(int i=0;i<n;++i){
                if(!finite(when[i])||!finite(arrivals[i])||!finite(departures[i])) continue;
                rows.append(qMakePair(when[i],qMakePair(arrivals[i],departures[i])));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,QPair<double,double>>& a,
                         const QPair<double,QPair<double,double>>& b){
                          return a.first<b.first; });
            if(rows.size()>=2){
                PlotSeries arriving,departing;
                departing.label=QStringLiteral("cumulative departures");
                departing.color=in.series.at(2).color;
                departing.lineWidth=qMax(1.3,in.style.lineWidth);
                arriving.color=in.series.at(1).color;
                arriving.lineWidth=qMax(1.3,in.style.lineWidth);
                double longest=0.0,longestAt=0.0,delay=0.0;
                for(int i=0;i<rows.size();++i){
                    const double queue=rows[i].second.first-rows[i].second.second;
                    arriving.x.append(rows[i].first);  arriving.y.append(rows[i].second.first);
                    departing.x.append(rows[i].first); departing.y.append(rows[i].second.second);
                    if(queue>longest){ longest=queue; longestAt=rows[i].first; }
                    if(i==0) continue;
                    const double before=rows[i-1].second.first-rows[i-1].second.second;
                    delay+=0.5*(qMax(0.0,queue)+qMax(0.0,before))
                              *(rows[i].first-rows[i-1].first);
                }
                arriving.label=QStringLiteral("cumulative arrivals; longest queue %1 at %2, total delay %3 vehicle-time")
                                   .arg(longest,0,'g',5).arg(longestAt,0,'g',5)
                                   .arg(delay,0,'g',5);
                out.series.append(arriving);
                out.series.append(departing);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("time"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("cumulative vehicles"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------------- Cohort Retention
    // What fraction of each intake is still there after so many periods, with
    // the cohorts laid over one another by AGE rather than by calendar date.
    // Aligning them that way is the whole method: on a calendar axis a cohort
    // that started later simply has a shorter line, and nothing can be compared.
    return std::nullopt;
}

} // namespace graphvis
