// ============================================================================
// prepareSpecCore, part 2 of 6: "Rainflow Matrix" through "Lorenz Curve".
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

std::optional<PlotSpec> QtPlotBackend::prepareEngineGroup2(const PlotSpec& in) const {
    if(in.engine==QLatin1String("Rainflow Matrix")){
        PlotSpec out=derivedAs(in,QStringLiteral("2D Heatmap"));
        out.legendVisible=false;
        if(!in.series.isEmpty()){
            const QVector<double> raw=finiteValues(in.series.at(0));
            // Reversals only: the points where the load changes direction. The
            // straight runs between them do no damage and would otherwise be
            // counted as cycles of zero range.
            QVector<double> turns;
            for(int i=0;i<raw.size();++i){
                if(i==0||i==raw.size()-1){ turns.append(raw[i]); continue; }
                const double a=raw[i]-raw[i-1],b=raw[i+1]-raw[i];
                if((a>0&&b<0)||(a<0&&b>0)) turns.append(raw[i]);
            }
            QVector<double> ranges,means,counts;
            QVector<double> stack;
            const auto record=[&ranges,&means,&counts](double a,double b,double count){
                ranges.append(std::abs(a-b));
                means.append(0.5*(a+b));
                counts.append(count);
            };
            for(double point:turns){
                stack.append(point);
                // Four-point: the inner range closes when it is no larger than
                // the range on either side of it.
                while(stack.size()>=4){
                    const int k=stack.size();
                    const double r1=std::abs(stack[k-3]-stack[k-4]);
                    const double r2=std::abs(stack[k-2]-stack[k-3]);
                    const double r3=std::abs(stack[k-1]-stack[k-2]);
                    if(r2<=r1&&r2<=r3){
                        record(stack[k-3],stack[k-2],1.0);
                        stack.remove(k-3,2);
                    }else break;
                }
            }
            // Whatever is left never closed. Those are half cycles, and
            // dropping them would under-count the largest ranges - which are
            // the ones that do the damage.
            for(int i=0;i+1<stack.size();++i) record(stack[i],stack[i+1],0.5);

            if(!ranges.isEmpty()){
                PlotSeries rx,my,cv;
                rx.y=means; my.y=ranges; cv.y=counts;
                rx.label=QStringLiteral("mean");
                my.label=QStringLiteral("range");
                cv.label=QStringLiteral("cycles");
                out.series={rx,my,cv};
                double meanLo=std::numeric_limits<double>::infinity(),meanHi=-meanLo;
                double rangeHi=0.0;
                for(double v:means){ meanLo=qMin(meanLo,v); meanHi=qMax(meanHi,v); }
                for(double v:ranges) rangeHi=qMax(rangeHi,v);
                if(finite(meanLo)&&finite(meanHi)){
                    const double pad=qMax(1e-9,(meanHi-meanLo)*0.05);
                    out.xAxis=PlotAxis{QStringLiteral("cycle mean"),false,meanLo-pad,meanHi+pad};
                    out.yAxis=PlotAxis{QStringLiteral("cycle range"),false,0.0,rangeHi*1.05};
                    return out;
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("cycle mean"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("cycle range"),false,unsetValue(),unsetValue()};
        return out;
    }

    // =====================================================================
    // Logistics and infrastructure
    // =====================================================================

    // A stratigraphic log is read DOWN THE PAGE.
    //
    // It used to share the schedule's branch below - row, start, end, drawn as
    // a horizontal bar - so it came out as a Gantt chart with the word "depth"
    // under the horizontal axis, and the sweep found the three in one
    // same-picture group. The comment on drawHorizontalBar makes exactly this
    // argument in the other direction: a Gantt with time on the vertical axis
    // is not a Gantt, it is a puzzle. A borehole with depth across the page is
    // the same puzzle transposed, and no geologist has ever read one that way.
    //
    // So: one bed per series, x = the hole or track it belongs to, y = the
    // depth interval, drawn by the floating-BAR painter rather than the
    // floating-row one, on an inverted depth axis. Inverted by the axis flag
    // rather than by negating the depths, which would put minus signs on every
    // tick and make the axis label a lie - see PlotAxis::inverted.
    if(in.engine==QLatin1String("Borehole Log")){
        PlotSpec out=derivedAs(in,QStringLiteral("Floating Bar"));
        out.legendVisible=false;
        if(in.series.size()>=3){
            const QVector<double>& track=in.series.at(0).y;
            const QVector<double>& from=in.series.at(1).y;
            const QVector<double>& to=in.series.at(2).y;
            const int n=qMin(track.size(),qMin(from.size(),to.size()));
            double trackLo=std::numeric_limits<double>::infinity(),trackHi=-trackLo;
            for(int i=0;i<n;++i){
                if(!finite(track[i])||!finite(from[i])||!finite(to[i])) continue;
                PlotSeries bed;
                bed.label=QStringLiteral("%1").arg(track[i]);
                // One hue per bed, the way a log is ornamented by lithology.
                // Alternating rather than sequential, so two beds that meet are
                // separable without a legend a hundred-bed log cannot carry.
                bed.color=categoryColour(in,i,std::fmod(double(i)*0.17,1.0),0.45,0.9);
                bed.x={track[i]};
                bed.y={qMin(from[i],to[i]),qMax(from[i],to[i])};
                out.series.append(bed);
                trackLo=qMin(trackLo,track[i]); trackHi=qMax(trackHi,track[i]);
            }
            if(finite(trackLo)&&finite(trackHi))
                out.xAxis=PlotAxis{in.series.at(0).label.isEmpty()
                                       ?QStringLiteral("hole"):in.series.at(0).label,
                                   false,trackLo-0.6,trackHi+0.6};
        }
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("depth");
        // Deeper is further down. Only where the person has not already said
        // otherwise: a log of height above a datum reads the other way, and
        // that is their call to make.
        if(!in.yAxis.inverted) out.yAxis.inverted=true;
        return out;
    }

    // A schedule and an availability timeline share a geometry - a row per
    // thing, a bar from a start to an end - and differ in what is being
    // counted.
    //
    // They used to be one branch with one output, which made the second entry
    // a synonym: a person can rename an axis themselves, so an entry earns its
    // place by drawing something the other does not. What an availability
    // timeline adds is the ARITHMETIC. Its bars are uptime, the gaps between
    // them are downtime, and the number the figure exists to produce is the
    // percentage - per row and overall. Nobody reads an availability chart to
    // admire the bars; they read it to find out whether the machine made its
    // number. A Gantt's equivalent question is about span and slack, which is
    // a different sum and is not attempted here.
    if(in.engine==QLatin1String("Gantt Schedule")
       ||in.engine==QLatin1String("Availability Timeline")){
        PlotSpec out=derivedAs(in,QStringLiteral("Floating Row"));
        out.legendVisible=false;
        const bool availability=in.engine==QLatin1String("Availability Timeline");
        if(in.series.size()>=3){
            const QVector<double>& row=in.series.at(0).y;
            const QVector<double>& from=in.series.at(1).y;
            const QVector<double>& to=in.series.at(2).y;
            const int n=qMin(row.size(),qMin(from.size(),to.size()));

            // The window, and the covered time in it, measured before anything
            // is drawn because the backdrop below needs both.
            double rowLo=std::numeric_limits<double>::infinity(),rowHi=-rowLo;
            double spanLo=std::numeric_limits<double>::infinity(),spanHi=-spanLo;
            QMap<double,double> upPerRow;
            for(int i=0;i<n;++i){
                if(!finite(row[i])||!finite(from[i])||!finite(to[i])) continue;
                const double a=qMin(from[i],to[i]),b=qMax(from[i],to[i]);
                rowLo=qMin(rowLo,row[i]); rowHi=qMax(rowHi,row[i]);
                spanLo=qMin(spanLo,a); spanHi=qMax(spanHi,b);
                upPerRow[row[i]]+=b-a;
            }
            const double window=spanHi-spanLo;

            // THE BACKDROP, and it is the whole difference between the two.
            //
            // An availability timeline is read by its GAPS. The bars are the
            // time the thing was up; what the reader is looking for is the
            // time it was not, and a gap between two bars is only visible as a
            // gap if there is something behind it to be a gap IN. A Gantt has
            // no backdrop because the space between two tasks is not failure,
            // it is just when nothing was scheduled.
            //
            // Drawn first, so the real bars land on top of it. This also means
            // the difference survives whatever the data looks like - unlike the
            // per-row percentages below, which are only legible when there are
            // few enough rows to read.
            if(availability&&finite(window)&&window>0.0){
                for(auto it=upPerRow.constBegin();it!=upPerRow.constEnd();++it){
                    PlotSeries backdrop;
                    backdrop.label=QString();
                    backdrop.color=in.style.gridColor;
                    backdrop.opacity=0.35;
                    backdrop.y={it.key()};
                    backdrop.x={spanLo,spanHi};
                    out.series.append(backdrop);
                }
            }

            for(int i=0;i<n;++i){
                if(!finite(row[i])||!finite(from[i])||!finite(to[i])) continue;
                PlotSeries bar;
                bar.label=QStringLiteral("%1").arg(row[i]);
                // Alternating hues by row, so neighbouring rows are separable
                // without a legend - which a hundred-row schedule cannot have.
                bar.color=categoryColour(in,int(std::abs(row[i])),std::fmod(std::abs(row[i])*0.17,1.0),0.45,0.9);
                bar.y={row[i]};
                bar.x={qMin(from[i],to[i]),qMax(from[i],to[i])};
                out.series.append(bar);
            }
            if(finite(rowLo)&&finite(rowHi))
                out.yAxis=PlotAxis{in.series.at(0).label.isEmpty()
                                       ?QStringLiteral("row"):in.series.at(0).label,
                                   false,rowLo-0.6,rowHi+0.6};

            // The number the chart exists to produce. Per row: the covered time
            // over the window the WHOLE chart covers, so rows are comparable
            // with each other rather than each with itself - a machine watched
            // for an hour and up throughout is not 100% available over a week.
            if(availability&&finite(window)&&window>0.0&&!upPerRow.isEmpty()){
                double totalUp=0.0;
                for(auto it=upPerRow.constBegin();it!=upPerRow.constEnd();++it)
                    totalUp+=it.value();
                // Only when they can be read. Thirty rows of text down the
                // right-hand edge is a table badly drawn; two hundred is a grey
                // smear. The overall figure in the title survives either way.
                if(upPerRow.size()<=30){
                    for(auto it=upPerRow.constBegin();it!=upPerRow.constEnd();++it){
                        PlotAnnotation note;
                        note.text=QStringLiteral("%1%")
                                      .arg(qMin(100.0,it.value()/window*100.0),0,'f',1);
                        // Anchored at the right-hand end and offset INWARDS.
                        // The first version offset outwards by 8 points, which
                        // put every note past the edge of the plot area and
                        // straight into drawAnnotations' clip - so the figure
                        // was byte-identical to a Gantt and nothing said why.
                        note.x=spanHi;
                        note.y=it.key();
                        note.offsetX=-34.0;
                        note.offsetY=0.0;
                        note.leader=false;
                        note.derived=true;
                        out.annotations.append(note);
                    }
                }
                if(in.title.isEmpty())
                    out.title=QStringLiteral("Availability %1%")
                                  .arg(qMin(100.0,totalUp/(window*double(upPerRow.size()))*100.0),
                                       0,'f',1);
            }
        }
        // Borehole Log no longer reaches here, so the depth/time choice that
        // used to live on this line has gone with it. Left as a plain "time"
        // rather than a ternary with one dead arm, which is how a branch that
        // can never be taken survives a refactor and confuses the next reader.
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("time");
        return out;
    }

    // Open, high, low, close. Four mapped columns and one series per period,
    // which is what the candlestick painter reads.
    if(in.engine==QLatin1String("OHLC Candlestick")){
        PlotSpec out=derivedAs(in,QStringLiteral("Candlestick"));
        out.legendVisible=false;
        if(in.series.size()>=4){
            const PlotSeries& first=in.series.at(0);
            const QVector<double>& open=in.series.at(0).y;
            const QVector<double>& high=in.series.at(1).y;
            const QVector<double>& low=in.series.at(2).y;
            const QVector<double>& close=in.series.at(3).y;
            const int n=qMin(qMin(open.size(),high.size()),qMin(low.size(),close.size()));
            for(int i=0;i<n;++i){
                if(!finite(open[i])||!finite(high[i])||!finite(low[i])||!finite(close[i]))
                    continue;
                PlotSeries candle;
                // The period's own x when one was mapped, otherwise its order.
                candle.x={(i<first.x.size()&&finite(first.x[i]))?first.x[i]:double(i)};
                candle.y={open[i],high[i],low[i],close[i]};
                out.series.append(candle);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("period");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("price");
        return out;
    }

    // Flows between places, on a map. Five mapped columns - origin longitude
    // and latitude, destination longitude and latitude, and the weight - drawn
    // as great circles whose width is the flow, through the same projection
    // every other geographic engine uses.
    if(in.engine==QLatin1String("Origin-Destination Flow")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        out.legendVisible=false;
        if(in.series.size()>=4){
            const QVector<double>& oLon=in.series.at(0).y;
            const QVector<double>& oLat=in.series.at(1).y;
            const QVector<double>& dLon=in.series.at(2).y;
            const QVector<double>& dLat=in.series.at(3).y;
            const bool weighted=in.series.size()>=5;
            const QVector<double> w=weighted?in.series.at(4).y:QVector<double>();
            const int n=qMin(qMin(oLon.size(),oLat.size()),qMin(dLon.size(),dLat.size()));

            QVector<double> allLon,allLat;
            for(int i=0;i<n;++i){ allLon.append(oLon[i]); allLat.append(oLat[i]);
                                  allLon.append(dLon[i]); allLat.append(dLat[i]); }
            const GeoFrame frame=geoFrameFor(mapProjectionFor(in.variant),allLon,allLat);

            double heaviest=0.0;
            for(int i=0;i<n&&weighted;++i) if(i<w.size()&&finite(w[i]))
                heaviest=qMax(heaviest,std::abs(w[i]));
            if(!(heaviest>0)) heaviest=1.0;

            for(int i=0;i<n;++i){
                if(!finite(oLon[i])||!finite(oLat[i])||!finite(dLon[i])||!finite(dLat[i]))
                    continue;
                const double flow=(weighted&&i<w.size()&&finite(w[i]))?std::abs(w[i]):1.0;
                PlotSeries arc;
                arc.color=in.series.at(0).color;
                arc.drawLine=true; arc.drawMarkers=false;
                arc.lineWidth=qBound(0.5,4.0*flow/heaviest,5.0);
                arc.lineWidthExplicit=true;
                arc.opacity=0.85;
                const double metres=greatCircleMetres(oLon[i],oLat[i],dLon[i],dLat[i]);
                // Detail per arc, scaled by how many arcs there are. A dozen
                // routes deserve a smooth curve; three thousand of them are a
                // few pixels each, and drawing 128 points per arc cost 800 ms
                // per repaint to produce a picture indistinguishable from this
                // one. The budget is about forty thousand points in total.
                const int perArc=qBound(2,40000/qMax(1,n),128);
                const int steps=qBound(2,qMin(perArc,int(metres/50000.0)),128);
                for(int k=0;k<=steps;++k){
                    double gl=0,gp=0;
                    greatCirclePoint(oLon[i],oLat[i],dLon[i],dLat[i],
                                     double(k)/double(steps),gl,gp);
                    const QPointF q=projectPoint(frame,gl,gp);
                    if(!finite(q.x())||!finite(q.y())) continue;
                    arc.x.append(q.x()); arc.y.append(q.y());
                }
                if(arc.x.size()>1) out.series.append(arc);
            }
            out.xAxis=PlotAxis{projectionXLabel(frame),false,unsetValue(),unsetValue()};
            out.yAxis=PlotAxis{projectionYLabel(frame),false,unsetValue(),unsetValue()};
        }
        return out;
    }

    // The cumulative flow diagram. Bands stacked in the order the work moves
    // through them, so the vertical distance between two lines is how much is
    // sitting in that stage and the horizontal distance is how long it stays
    // there. Drawn largest first, because each band is filled to the baseline
    // and the later ones have to paint over the earlier.
    if(in.engine==QLatin1String("Cumulative Flow")){
        PlotSpec out=derivedAs(in,QStringLiteral("Area"));
        const int k=in.series.size();
        if(k>0){
            const int n=in.series.at(0).y.size();
            for(int band=k-1;band>=0;--band){
                PlotSeries area;
                area.label=in.series.at(band).label;
                area.color=in.series.at(band).color;
                area.drawLine=true; area.drawMarkers=false;
                area.opacity=0.85;
                for(int i=0;i<n;++i){
                    double total=0.0; bool ok=true;
                    for(int j=0;j<=band;++j){
                        if(i>=in.series.at(j).y.size()||!finite(in.series.at(j).y[i])){ ok=false; break; }
                        total+=in.series.at(j).y[i];
                    }
                    if(!ok) continue;
                    const QVector<double>& xs=in.series.at(band).x;
                    area.x.append((i<xs.size()&&finite(xs[i]))?xs[i]:double(i));
                    area.y.append(total);
                }
                if(!area.x.isEmpty()) out.series.append(area);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("time");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("cumulative count");
        return out;
    }

    // Stock over time, with the reorder points marked and the average level
    // stated. The sawtooth is read for two things - how low it gets and how
    // often it is refilled - and both are marked rather than counted by eye.
    if(in.engine==QLatin1String("Inventory Sawtooth")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries level=s;
            level.drawLine=true; level.drawMarkers=false;
            PlotSeries troughs;
            troughs.label=QStringLiteral("reorder");
            troughs.color=in.style.danger;
            troughs.drawLine=false; troughs.drawMarkers=true;
            troughs.markerSize=6.0; troughs.markerSizeExplicit=true;
            double sum=0.0; int counted=0; double lowest=std::numeric_limits<double>::infinity();
            for(int i=0;i<n;++i){
                if(!finite(s.y[i])) continue;
                sum+=s.y[i]; ++counted;
                lowest=qMin(lowest,s.y[i]);
                if(i==0||i==n-1) continue;
                if(!finite(s.y[i-1])||!finite(s.y[i+1])) continue;
                // A trough: the level falls into this point and rises out of
                // it. That is a replenishment, whatever the interval.
                if(s.y[i]<s.y[i-1]&&s.y[i+1]>s.y[i]){
                    troughs.x.append(s.x[i]); troughs.y.append(s.y[i]);
                }
            }
            if(counted>0){
                const double mean=sum/double(counted);
                level.label=QStringLiteral("%1 (mean %2, minimum %3, %4 reorders)")
                                .arg(s.label).arg(mean,0,'g',4).arg(lowest,0,'g',4)
                                .arg(troughs.x.size());
                out.series.append(level);
                if(!troughs.x.isEmpty()) out.series.append(troughs);
                PlotSeries average;
                average.label=QStringLiteral("mean level");
                average.color=in.style.warning;
                average.drawLine=true; average.lineWidth=0.8; average.lineWidthExplicit=true;
                average.dashPattern={4.0,3.0};
                average.x={s.x.first(),s.x.last()};
                average.y={mean,mean};
                out.series.append(average);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("time");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("stock on hand");
        return out;
    }

    // The fundamental diagram of traffic: flow against density, with
    // Greenshields fitted through it. The two numbers it gives - free-flow
    // speed and jam density - are what a road's capacity is computed from, and
    // the capacity itself is the apex of the parabola, which is easy to
    // mis-read off scattered points and exact from the fit.
    if(in.engine==QLatin1String("Fundamental Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<4) continue;
            PlotSeries pts;
            pts.label=s.label; pts.color=s.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            // q = vf k - (vf/kj) k^2, fitted through the origin as
            // q/k = vf - (vf/kj) k, which is a straight line in speed.
            QVector<double> densities,speeds;
            double densest=0.0;
            for(int i=0;i<n;++i){
                const double density=s.x[i],flow=s.y[i];
                if(!finite(density)||!finite(flow)||!(density>0)) continue;
                pts.x.append(density); pts.y.append(flow);
                densities.append(density); speeds.append(flow/density);
                densest=qMax(densest,density);
            }
            if(pts.x.isEmpty()) continue;
            out.series.append(pts);
            const LineFit line=fitLine(densities,speeds);
            const double slope=line.slope,freeFlow=line.intercept;
            if(!line.ok||!(freeFlow>0)||!(slope<0)) continue;
            const double jam=-freeFlow/slope;
            const double capacity=freeFlow*jam/4.0;
            PlotSeries curve;
            curve.label=QStringLiteral("Greenshields (vf %1, kj %2, capacity %3)")
                            .arg(freeFlow,0,'f',1).arg(jam,0,'f',1).arg(capacity,0,'f',0);
            curve.color=in.style.warning;
            curve.drawLine=true; curve.drawMarkers=false;
            curve.lineWidth=1.3; curve.lineWidthExplicit=true;
            for(int i=0;i<=80;++i){
                const double k=qMax(densest,jam)*double(i)/80.0;
                curve.x.append(k);
                curve.y.append(freeFlow*k+slope*k*k);
            }
            out.series.append(curve);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("density (veh/km)");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("flow (veh/h)");
        return out;
    }

    // =====================================================================
    // Chemistry, materials and the laboratory
    //
    // Every one of these is read for a number rather than for its shape - a
    // modulus, an activation energy, a pKa, a detection limit, a Km, an EC50 -
    // and every one of those numbers comes from a construction the reader is
    // otherwise expected to do with a ruler and a calculator.
    // =====================================================================

    // Stress against strain, with the three numbers a tensile test is run for:
    // the elastic modulus, the 0.2 per cent offset yield and the tensile
    // strength. The modulus is fitted over the first tenth of the strain range
    // rather than over everything, because a fit through the plastic region
    // returns a smaller modulus that still looks like an answer.
    if(in.engine==QLatin1String("Stress-Strain Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<5) continue;
            PlotSeries curve;
            curve.label=s.label; curve.color=s.color; curve.drawLine=true;
            double strainHi=0.0,peak=-std::numeric_limits<double>::infinity();
            int peakAt=-1;
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                curve.x.append(s.x[i]); curve.y.append(s.y[i]);
                strainHi=qMax(strainHi,s.x[i]);
                if(s.y[i]>peak){ peak=s.y[i]; peakAt=curve.x.size()-1; }
            }
            if(curve.x.size()<5) continue;

            // THE ELASTIC REGION IS FOUND ON THE STRESS AXIS, not by taking a
            // tenth of the strain.
            //
            // A tenth of the strain range is not the elastic region of
            // anything ductile. A tensile test that necks at 20% strain yields
            // at about 0.4%, so "below a tenth of the range" swept in every
            // point up to 2% strain - the whole of the elastic region and most
            // of the work-hardening plateau - and the line through them came
            // back at 17 GPa against a real modulus of 70. The figure printed
            // it as "E 1.686e+04": four significant figures of a number that
            // was not Young's modulus.
            //
            // Below forty per cent of the ultimate stress is the usual
            // laboratory rule, and it is a rule about the right axis. A
            // ductile metal is still elastic there whatever its ductility, and
            // a brittle one has almost no plastic range to sweep in by
            // mistake. The strain fraction stays as the fallback for a curve
            // so short or so coarse that the stress rule leaves too few
            // points to fit.
            int elastic=0;
            {
                const double knee=(peak>0)?peak*0.4:0.0;
                while(elastic<curve.x.size()&&curve.y[elastic]<=knee) ++elastic;
                if(elastic<3){
                    elastic=0;
                    while(elastic<curve.x.size()&&curve.x[elastic]<=strainHi*0.10) ++elastic;
                }
            }
            const LineFit line=fitLine(curve.x,curve.y,qMax(3,elastic));
            if(line.ok&&line.slope>0){
                // The 0.2% offset line, and where the curve crosses it - which
                // is the definition of yield for anything without a sharp one.
                const double offset=0.002;
                PlotSeries offsetLine;
                offsetLine.label=QStringLiteral("0.2% offset");
                offsetLine.color=in.style.warning;
                offsetLine.drawLine=true; offsetLine.lineWidth=0.9;
                offsetLine.lineWidthExplicit=true; offsetLine.dashPattern={5.0,3.0};
                // DRAWN ONLY AS FAR AS IT IS USED.
                //
                // Carried across the whole strain range, a 70 GPa offset line
                // reaches 14,000 MPa on a specimen that breaks at 410 - so the
                // construction line set the y axis and the curve it was drawn
                // to explain was a flat trace along the bottom of the frame.
                // The line exists to be intersected; a little past the
                // intersection it has nothing left to say.
                const double stopAt=(line.slope>0&&peak>0)
                                       ? qMin(strainHi,offset+1.15*peak/line.slope)
                                       : strainHi;
                offsetLine.x={offset,stopAt};
                offsetLine.y={line.slope*(offset-offset)+line.intercept,
                              line.slope*(stopAt-offset)+line.intercept};
                double yieldStress=qQNaN(),yieldStrain=qQNaN();
                for(int i=1;i<curve.x.size();++i){
                    const double model=line.slope*(curve.x[i]-offset)+line.intercept;
                    const double before=line.slope*(curve.x[i-1]-offset)+line.intercept;
                    if((curve.y[i-1]-before)>0&&(curve.y[i]-model)<=0){
                        yieldStrain=curve.x[i]; yieldStress=curve.y[i];
                        break;
                    }
                }
                curve.label=QStringLiteral("%1 (E %2, UTS %3%4)")
                    .arg(s.label).arg(formatMeasured(line.slope)).arg(formatMeasured(peak))
                    .arg(finite(yieldStress)
                             ?QStringLiteral(", yield %1").arg(formatMeasured(yieldStress))
                             :QString());
                out.series.append(curve);
                out.series.append(offsetLine);
                if(finite(yieldStress)){
                    PlotSeries mark;
                    mark.label=QStringLiteral("yield");
                    mark.color=in.style.positive;
                    mark.drawLine=false; mark.drawMarkers=true;
                    mark.markerSize=8.0; mark.markerSizeExplicit=true;
                    mark.x={yieldStrain}; mark.y={yieldStress};
                    out.series.append(mark);
                }
            }else{
                out.series.append(curve);
            }
            if(peakAt>=0){
                PlotSeries uts;
                uts.label=QStringLiteral("tensile strength");
                uts.color=in.style.danger;
                uts.drawLine=false; uts.drawMarkers=true;
                uts.markerSize=8.0; uts.markerSizeExplicit=true;
                uts.x={curve.x.at(peakAt)}; uts.y={curve.y.at(peakAt)};
                out.series.append(uts);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("strain");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("stress");
        return out;
    }

    // ln k against 1/T. The slope is -Ea/R, and the activation energy is the
    // reason the plot is drawn at all - so it is computed here rather than left
    // as a slope on a picture with a reciprocal axis.
    if(in.engine==QLatin1String("Arrhenius Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        constexpr double kGasConstant=8.314462618;   // J/(mol K)
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries pts;
            pts.color=s.color; pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            QVector<double> inverse,logRate;
            for(int i=0;i<n;++i){
                const double temperature=s.x[i],rate=s.y[i];
                if(!(temperature>0)||!(rate>0)) continue;
                const double x=1000.0/temperature;      // 1000/T, the usual axis
                const double y=std::log(rate);
                pts.x.append(x); pts.y.append(y);
                inverse.append(x); logRate.append(y);
            }
            if(pts.x.size()<3) continue;
            const LineFit line=fitLine(inverse,logRate);
            if(line.ok){
                // The x axis is 1000/T, so the slope is -Ea/(1000 R).
                const double activation=-line.slope*kGasConstant*1000.0;
                pts.label=QStringLiteral("%1 (Ea %2 kJ/mol, A %3)")
                              .arg(s.label).arg(activation/1000.0,0,'f',1)
                              .arg(std::exp(line.intercept),0,'g',3);
                PlotSeries fit;
                fit.label=QStringLiteral("Arrhenius fit");
                fit.color=in.style.warning;
                fit.drawLine=true; fit.lineWidth=1.2; fit.lineWidthExplicit=true;
                double lo=std::numeric_limits<double>::infinity(),hi=-lo;
                for(double v:pts.x){ lo=qMin(lo,v); hi=qMax(hi,v); }
                fit.x={lo,hi};
                fit.y={line.intercept+line.slope*lo,line.intercept+line.slope*hi};
                out.series.append(pts);
                out.series.append(fit);
                continue;
            }
            pts.label=s.label;
            out.series.append(pts);
        }
        out.xAxis=PlotAxis{QStringLiteral("1000/T (1/K)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("ln k"),false,unsetValue(),unsetValue()};
        return out;
    }

    // A titration curve, with the equivalence point taken as the steepest part
    // of the curve rather than as the middle of the visible jump, and the pKa
    // read at half that volume. Both are constructions done by hand on a
    // printout, and both are done wrong when the jump is not symmetrical.
    if(in.engine==QLatin1String("Titration Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<5) continue;
            PlotSeries curve;
            curve.label=s.label; curve.color=s.color; curve.drawLine=true;
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                curve.x.append(s.x[i]); curve.y.append(s.y[i]);
            }
            if(curve.x.size()<5) continue;
            int steepest=-1; double slope=0.0;
            for(int i=1;i<curve.x.size();++i){
                const double dx=curve.x[i]-curve.x[i-1];
                if(!(std::abs(dx)>1e-12)) continue;
                const double gradient=std::abs((curve.y[i]-curve.y[i-1])/dx);
                if(gradient>slope){ slope=gradient; steepest=i; }
            }
            out.series.append(curve);
            if(steepest>0){
                const double volume=0.5*(curve.x[steepest]+curve.x[steepest-1]);
                PlotSeries mark;
                mark.label=QStringLiteral("equivalence at %1").arg(volume,0,'g',4);
                mark.color=in.style.danger;
                mark.drawLine=false; mark.drawMarkers=true;
                mark.markerSize=8.0; mark.markerSizeExplicit=true;
                mark.x={volume}; mark.y={0.5*(curve.y[steepest]+curve.y[steepest-1])};
                out.series.append(mark);
                // Half-equivalence: for a weak acid the pH there is the pKa.
                const double half=volume*0.5;
                double pka=qQNaN();
                for(int i=1;i<curve.x.size();++i){
                    if(curve.x[i-1]<=half&&half<=curve.x[i]){
                        const double span=curve.x[i]-curve.x[i-1];
                        const double t=(std::abs(span)>1e-12)?(half-curve.x[i-1])/span:0.0;
                        pka=curve.y[i-1]+t*(curve.y[i]-curve.y[i-1]);
                        break;
                    }
                }
                if(finite(pka)){
                    PlotSeries halfMark;
                    halfMark.label=QStringLiteral("half-equivalence, pKa %1").arg(pka,0,'f',2);
                    halfMark.color=in.style.positive;
                    halfMark.drawLine=false; halfMark.drawMarkers=true;
                    halfMark.markerSize=7.0; halfMark.markerSizeExplicit=true;
                    halfMark.x={half}; halfMark.y={pka};
                    out.series.append(halfMark);
                }
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("titrant added");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("pH");
        return out;
    }

    // A calibration curve with the two limits every analytical result depends
    // on: the detection limit at 3.3 residual standard deviations over the
    // slope, and the quantitation limit at 10. Reporting a concentration below
    // the first is reporting noise, and it is done constantly because the
    // limits are not on the chart.
    if(in.engine==QLatin1String("Calibration Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries pts;
            pts.color=s.color; pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=5.0;
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                pts.x.append(s.x[i]); pts.y.append(s.y[i]);
            }
            if(pts.x.size()<3) continue;
            const LineFit line=fitLine(pts.x,pts.y);
            if(!line.ok||!(std::abs(line.slope)>0)){ pts.label=s.label; out.series.append(pts); continue; }
            double ssResidual=0.0,ssTotal=0.0,mean=0.0;
            for(double v:pts.y) mean+=v;
            mean/=double(pts.y.size());
            for(int i=0;i<pts.x.size();++i){
                const double predicted=line.intercept+line.slope*pts.x[i];
                ssResidual+=(pts.y[i]-predicted)*(pts.y[i]-predicted);
                ssTotal+=(pts.y[i]-mean)*(pts.y[i]-mean);
            }
            const int dof=pts.x.size()-2;
            const double sigma=(dof>0)?std::sqrt(ssResidual/double(dof)):0.0;
            const double r2=(ssTotal>0)?1.0-ssResidual/ssTotal:0.0;
            const double lod=3.3*sigma/std::abs(line.slope);
            const double loq=10.0*sigma/std::abs(line.slope);
            pts.label=QStringLiteral("%1 (slope %2, R2 %3, LOD %4, LOQ %5)")
                          .arg(s.label).arg(line.slope,0,'g',4).arg(r2,0,'f',4)
                          .arg(lod,0,'g',3).arg(loq,0,'g',3);
            out.series.append(pts);
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            for(double v:pts.x){ lo=qMin(lo,v); hi=qMax(hi,v); }
            PlotSeries fit;
            fit.label=QStringLiteral("least squares");
            fit.color=in.style.warning; fit.drawLine=true;
            fit.lineWidth=1.2; fit.lineWidthExplicit=true;
            fit.x={lo,hi};
            fit.y={line.intercept+line.slope*lo,line.intercept+line.slope*hi};
            out.series.append(fit);
            for(const auto& limit:{qMakePair(lod,QStringLiteral("LOD")),
                                   qMakePair(loq,QStringLiteral("LOQ"))}){
                if(!finite(limit.first)||limit.first<lo||limit.first>hi) continue;
                PlotSeries mark;
                mark.label=limit.second;
                mark.color=(limit.second==QLatin1String("LOD"))?in.style.danger:in.style.positive;
                mark.drawLine=true; mark.drawMarkers=false;
                mark.lineWidth=0.8; mark.lineWidthExplicit=true;
                mark.dashPattern={3.0,3.0};
                double yLo=std::numeric_limits<double>::infinity(),yHi=-yLo;
                for(double v:pts.y){ yLo=qMin(yLo,v); yHi=qMax(yHi,v); }
                mark.x={limit.first,limit.first};
                mark.y={yLo,yHi};
                out.series.append(mark);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("concentration");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("response");
        return out;
    }

    // Michaelis-Menten, with Vmax and Km from the Lineweaver-Burk
    // linearisation. The double-reciprocal plot is drawn as well as the
    // hyperbola, because that is where a departure from the model shows -
    // inhibition bends the reciprocal line and barely moves the hyperbola.
    if(in.engine==QLatin1String("Michaelis-Menten")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries pts;
            pts.color=s.color; pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            QVector<double> reciprocalS,reciprocalV;
            for(int i=0;i<n;++i){
                const double substrate=s.x[i],rate=s.y[i];
                if(!(substrate>0)||!(rate>0)) continue;
                pts.x.append(substrate); pts.y.append(rate);
                reciprocalS.append(1.0/substrate); reciprocalV.append(1.0/rate);
            }
            if(pts.x.size()<3) continue;
            const LineFit line=fitLine(reciprocalS,reciprocalV);
            // A NEGATIVE Km IS NOT A MICHAELIS-MENTEN FIT, and it was drawn.
            //
            // The hyperbola is v = Vmax*S/(Km + S). With Km negative the
            // denominator passes through zero inside the sampled range, and the
            // clamp that kept it from dividing by zero - qMax(1e-12, ...) -
            // turned the pole into a value of about 1e8. One column whose
            // double-reciprocal line sloped the wrong way put a spike of a
            // hundred million on a figure whose data reaches 7, and the axis
            // was then scaled to the spike: every real curve on the plot became
            // a flat line along the bottom.
            //
            // A fit whose parameters are not physical is a fit that failed. The
            // points are still drawn and the label says why, which is what the
            // Gompertz rewrite does with a fit that will not converge.
            const double vmaxTest=(line.ok&&line.intercept>0)?1.0/line.intercept:0.0;
            if(line.ok&&line.intercept>0&&!(line.slope*vmaxTest>0)){
                pts.label=QStringLiteral("%1 (no Michaelis-Menten fit: Km %2 is not positive)")
                              .arg(s.label).arg(line.slope*vmaxTest,0,'g',3);
                out.series.append(pts);
                continue;
            }
            if(line.ok&&line.intercept>0){
                const double vmax=1.0/line.intercept;
                const double km=line.slope*vmax;
                pts.label=QStringLiteral("%1 (Vmax %2, Km %3)")
                              .arg(s.label).arg(vmax,0,'g',4).arg(km,0,'g',4);
                PlotSeries curve;
                curve.label=QStringLiteral("Michaelis-Menten fit");
                curve.color=in.style.warning; curve.drawLine=true;
                curve.lineWidth=1.2; curve.lineWidthExplicit=true;
                double hi=0.0;
                for(double v:pts.x) hi=qMax(hi,v);
                for(int i=0;i<=120;++i){
                    const double substrate=hi*double(i)/120.0;
                    curve.x.append(substrate);
                    curve.y.append(vmax*substrate/qMax(1e-12,km+substrate));
                }
                out.series.append(pts);
                out.series.append(curve);
                continue;
            }
            pts.label=s.label;
            out.series.append(pts);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("substrate concentration");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("initial rate");
        return out;
    }

    // A dose-response curve with its EC50, from the Hill linearisation: the
    // logit of the normalised response against log dose is a straight line
    // whose slope is the Hill coefficient and whose root is the EC50. The
    // plateau is taken from the data rather than assumed to be one, because a
    // response normalised to a plateau that was never reached puts the EC50 in
    // the wrong place.
    if(in.engine==QLatin1String("Dose-Response Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<4) continue;
            PlotSeries pts;
            pts.color=s.color; pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            double top=-std::numeric_limits<double>::infinity();
            double bottom=std::numeric_limits<double>::infinity();
            for(int i=0;i<n;++i){
                if(!(s.x[i]>0)||!finite(s.y[i])) continue;
                pts.x.append(s.x[i]); pts.y.append(s.y[i]);
                top=qMax(top,s.y[i]); bottom=qMin(bottom,s.y[i]);
            }
            if(pts.x.size()<4||!(top>bottom)) continue;
            QVector<double> logDose,logit;
            for(int i=0;i<pts.x.size();++i){
                const double fraction=(pts.y[i]-bottom)/(top-bottom);
                if(!(fraction>0.02)||!(fraction<0.98)) continue;   // the plateaus carry no slope
                logDose.append(std::log10(pts.x[i]));
                logit.append(std::log10(fraction/(1.0-fraction)));
            }
            const LineFit line=fitLine(logDose,logit);
            if(line.ok&&std::abs(line.slope)>1e-9){
                const double ec50=std::pow(10.0,-line.intercept/line.slope);
                pts.label=QStringLiteral("%1 (EC50 %2, Hill %3)")
                              .arg(s.label).arg(ec50,0,'g',4).arg(line.slope,0,'f',2);
                PlotSeries curve;
                curve.label=QStringLiteral("Hill fit");
                curve.color=in.style.warning; curve.drawLine=true;
                curve.lineWidth=1.2; curve.lineWidthExplicit=true;
                double lo=std::numeric_limits<double>::infinity(),hi=-lo;
                for(double v:pts.x){ lo=qMin(lo,v); hi=qMax(hi,v); }
                for(int i=0;i<=120;++i){
                    const double dose=std::pow(10.0,std::log10(lo)
                                     +(std::log10(hi)-std::log10(lo))*double(i)/120.0);
                    const double ratio=std::pow(dose/qMax(1e-300,ec50),line.slope);
                    curve.x.append(dose);
                    curve.y.append(bottom+(top-bottom)*ratio/(1.0+ratio));
                }
                out.series.append(pts);
                out.series.append(curve);
                PlotSeries mark;
                mark.label=QStringLiteral("EC50");
                mark.color=in.style.positive;
                mark.drawLine=false; mark.drawMarkers=true;
                mark.markerSize=8.0; mark.markerSizeExplicit=true;
                mark.x={ec50}; mark.y={0.5*(top+bottom)};
                out.series.append(mark);
                continue;
            }
            pts.label=s.label;
            out.series.append(pts);
        }
        out.xAxis=PlotAxis{in.xAxis.label.isEmpty()?QStringLiteral("dose"):in.xAxis.label,
                           true,unsetValue(),unsetValue()};
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("response");
        return out;
    }

    // =====================================================================
    // Medicine, epidemiology and quality
    // =====================================================================

    // Kaplan-Meier. Two mapped columns - the time, and 1 for an event and 0 for
    // a censored observation - and the product-limit estimate over them, with
    // the censored observations ticked on the curve.
    //
    // An ECDF of the same times is not this: it treats a subject who left the
    // study alive as one who died, which biases survival down by exactly the
    // amount of follow-up that was lost.
    if(in.engine==QLatin1String("Kaplan-Meier Survival")){
        PlotSpec out=derivedAs(in,QStringLiteral("Stairs"));
        if(in.series.size()>=2){
            const QVector<double>& times=in.series.at(0).y;
            const QVector<double>& events=in.series.at(1).y;
            const int n=qMin(times.size(),events.size());
            QVector<QPair<double,bool>> subjects;
            for(int i=0;i<n;++i){
                if(!finite(times[i])||!finite(events[i])) continue;
                subjects.append({times[i],events[i]>=0.5});
            }
            std::sort(subjects.begin(),subjects.end(),
                      [](const QPair<double,bool>& a,const QPair<double,bool>& b){
                          return a.first<b.first; });
            if(subjects.size()>=2){
                PlotSeries curve,censored;
                curve.label=in.series.at(0).label;
                curve.color=in.series.at(0).color;
                curve.drawLine=true; curve.drawMarkers=false;
                censored.label=QStringLiteral("censored");
                censored.color=in.style.warning;
                censored.drawLine=false; censored.drawMarkers=true;
                censored.markerSize=5.0; censored.markerSizeExplicit=true;
                double survival=1.0;
                int atRisk=subjects.size();
                int deaths=0;
                double median=qQNaN();
                curve.x.append(subjects.first().first); curve.y.append(1.0);
                for(int i=0;i<subjects.size();){
                    const double t=subjects[i].first;
                    int eventsHere=0,leaving=0;
                    while(i<subjects.size()&&subjects[i].first==t){
                        if(subjects[i].second) ++eventsHere;
                        ++leaving; ++i;
                    }
                    if(eventsHere>0&&atRisk>0){
                        survival*=(1.0-double(eventsHere)/double(atRisk));
                        deaths+=eventsHere;
                        curve.x.append(t); curve.y.append(survival);
                        if(!finite(median)&&survival<=0.5) median=t;
                    }else if(leaving>0){
                        censored.x.append(t); censored.y.append(survival);
                    }
                    atRisk-=leaving;
                }
                curve.label=QStringLiteral("%1 (%2 events of %3%4)")
                    .arg(in.series.at(0).label).arg(deaths).arg(subjects.size())
                    .arg(finite(median)?QStringLiteral(", median %1").arg(median,0,'g',4)
                                       :QStringLiteral(", median not reached"));
                out.series.append(curve);
                if(!censored.x.isEmpty()) out.series.append(censored);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("time");
        out.yAxis=PlotAxis{QStringLiteral("surviving fraction"),false,0.0,1.02};
        return out;
    }

    // A funnel plot: effect against precision, with the funnel a symmetrical
    // literature would fall inside. Asymmetry at the wide end is the signal -
    // small studies reporting only large effects - and it is invisible in a
    // forest plot of the same studies.
    if(in.engine==QLatin1String("Funnel Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& effect=in.series.at(0).y;
            const QVector<double>& error=in.series.at(1).y;
            const int n=qMin(effect.size(),error.size());
            PlotSeries pts;
            pts.label=QStringLiteral("studies");
            pts.color=in.series.at(0).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=5.0;
            double weighted=0.0,weight=0.0,widest=0.0;
            for(int i=0;i<n;++i){
                if(!finite(effect[i])||!(error[i]>0)) continue;
                pts.x.append(effect[i]); pts.y.append(error[i]);
                const double w=1.0/(error[i]*error[i]);
                weighted+=effect[i]*w; weight+=w;
                widest=qMax(widest,error[i]);
            }
            if(!pts.x.isEmpty()&&weight>0){
                const double summary=weighted/weight;
                pts.label=QStringLiteral("studies (summary %1, %2 of them)")
                              .arg(summary,0,'g',4).arg(pts.x.size());
                out.series.append(pts);
                PlotSeries centre,left,right;
                centre.label=QStringLiteral("fixed-effect summary");
                centre.color=in.style.warning;
                centre.drawLine=true; centre.lineWidth=1.0; centre.lineWidthExplicit=true;
                centre.x={summary,summary}; centre.y={0.0,widest};
                left.label=QStringLiteral("95% funnel");
                left.color=in.style.foreground;
                left.drawLine=true; left.lineWidth=0.8; left.lineWidthExplicit=true;
                left.dashPattern={4.0,3.0};
                right=left; right.label.clear();
                left.x={summary-1.96*widest,summary}; left.y={widest,0.0};
                right.x={summary+1.96*widest,summary}; right.y={widest,0.0};
                out.series.append(centre);
                out.series.append(left);
                out.series.append(right);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("effect size");
        // Precision upward is the convention, and the axis is inverted by
        // giving it a maximum of zero at the top.
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("standard error");
        return out;
    }

    // X-bar and R. Subgroup means with limits from the average range, which is
    // how a control chart is actually set up: the within-subgroup range
    // estimates the process spread, and using the overall standard deviation
    // instead gives limits that are too wide to catch anything.
    if(in.engine==QLatin1String("X-bar and R Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        // Shewhart's constants, by subgroup size, from the standard tables.
        static const double kA2[11]={0,0,1.880,1.023,0.729,0.577,0.483,0.419,0.373,0.337,0.308};
        static const double kD3[11]={0,0,0.000,0.000,0.000,0.000,0.000,0.076,0.136,0.184,0.223};
        static const double kD4[11]={0,0,3.267,2.574,2.282,2.114,2.004,1.924,1.864,1.816,1.777};
        const int subgroup=qBound(2,in.series.size(),10);
        if(in.series.size()>=2){
            int rows=std::numeric_limits<int>::max();
            for(const PlotSeries& s:in.series) rows=qMin(rows,int(s.y.size()));
            PlotSeries means,ranges;
            means.label=QStringLiteral("subgroup mean");
            means.color=in.series.at(0).color;
            means.drawLine=true; means.drawMarkers=true; means.markerSize=3.5;
            // THE RANGE CHART GETS ITS OWN ORDINATE.
            //
            // A bore turned to 48 mm with a subgroup range near 0.9 put both
            // series on one axis scaled 0 to 50: the means were a flat line
            // across the top, the ranges a flat line along the bottom, and
            // neither chart could be read at all. An X-bar and R chart is
            // conventionally two stacked panels for exactly this reason - the
            // two quantities share a subgroup index and nothing else - and the
            // right-hand ordinate is what this renderer has to say so.
            ranges.label=QStringLiteral("subgroup range");
            ranges.color=in.style.warning;
            ranges.drawLine=true; ranges.drawMarkers=true; ranges.markerSize=3.0;
            ranges.secondaryAxis=true;
            double grand=0.0,averageRange=0.0; int counted=0;
            for(int r=0;r<rows;++r){
                double sum=0.0,lo=std::numeric_limits<double>::infinity(),hi=-lo;
                bool ok=true;
                for(int k=0;k<subgroup;++k){
                    const double v=in.series.at(k).y[r];
                    if(!finite(v)){ ok=false; break; }
                    sum+=v; lo=qMin(lo,v); hi=qMax(hi,v);
                }
                if(!ok) continue;
                const double mean=sum/double(subgroup);
                means.x.append(double(r+1)); means.y.append(mean);
                ranges.x.append(double(r+1)); ranges.y.append(hi-lo);
                grand+=mean; averageRange+=hi-lo; ++counted;
            }
            if(counted>0){
                grand/=double(counted); averageRange/=double(counted);
                const double a2=kA2[subgroup],d3=kD3[subgroup],d4=kD4[subgroup];
                const double upper=grand+a2*averageRange,lower=grand-a2*averageRange;
                int beyond=0;
                for(double v:means.y) if(v>upper||v<lower) ++beyond;
                means.label=QStringLiteral("subgroup mean (n %1, %2 beyond the limits)")
                                .arg(subgroup).arg(beyond);
                out.series.append(means);
                out.series.append(ranges);
                // A limit belongs on the axis of the thing it limits, which
                // is the other half of the same fault: the range limits were
                // drawn against the means' scale, so a range limit of 1.9 was
                // a line just above zero on a chart of 48 mm bores.
                const auto level=[&out,&means](const QString& label,double value,const QColor& colour,
                                               bool dashed,bool secondary){
                    if(!finite(value)||means.x.isEmpty()) return;
                    PlotSeries line;
                    line.label=label; line.color=colour;
                    line.drawLine=true; line.lineWidth=0.8; line.lineWidthExplicit=true;
                    line.secondaryAxis=secondary;
                    if(dashed) line.dashPattern={4.0,3.0};
                    line.x={means.x.first(),means.x.last()};
                    line.y={value,value};
                    out.series.append(line);
                };
                level(QStringLiteral("centre %1").arg(grand,0,'g',4),
                      grand,in.style.foreground,false,false);
                level(QStringLiteral("mean limits"),upper,in.style.danger,true,false);
                level(QString(),lower,in.style.danger,true,false);
                level(QStringLiteral("range limits"),d4*averageRange,in.style.positive,true,true);
                if(d3>0) level(QString(),d3*averageRange,in.style.positive,true,true);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("subgroup"),false,unsetValue(),unsetValue()};
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("subgroup mean");
        out.y2Axis=PlotAxis{QStringLiteral("subgroup range"),false,
                            unsetValue(),unsetValue()};
        return out;
    }

    // Process capability: the distribution against the specification, with Cp
    // and Cpk. Cp alone says the spread would fit; Cpk says whether it fits
    // where it actually sits, and a process with Cp 2.0 and Cpk 0.6 is one that
    // could be capable and is not.
    //
    // Three mapped columns: the measurements, then the lower and upper limits,
    // read from the first finite value of each.
    if(in.engine==QLatin1String("Process Capability")){
        // BINNED, which it never was.
        //
        // This set the engine to "Histogram" and returned - but a rewrite's
        // output does not come back through here, so the Histogram branch that
        // does the binning never ran and drawBar received the raw column. Two
        // hundred and forty measurements came out as two hundred and forty
        // bars at their own values: a solid block of ink in the shape of the
        // data over time, with the two specification limits somewhere inside
        // it. The chart was named for a distribution and drew a time series.
        //
        // The values go through the Histogram branch on their own - the limits
        // are not a column to be binned - and the limits are appended to what
        // comes back, as rules over the bars.
        PlotSpec out=derivedAs(in,QStringLiteral("Histogram"));
        if(!in.series.isEmpty()){
            PlotSeries values=in.series.at(0);
            const QVector<double> v=finiteValues(values);
            double lsl=qQNaN(),usl=qQNaN();
            const auto firstFinite=[](const PlotSeries& s){
                for(double x:s.y) if(finite(x)) return x;
                return qQNaN();
            };
            if(in.series.size()>=2) lsl=firstFinite(in.series.at(1));
            if(in.series.size()>=3) usl=firstFinite(in.series.at(2));
            if(v.size()>=4){
                const double mean=meanOf(v),sigma=stdevOf(v);
                if(sigma>0&&finite(lsl)&&finite(usl)&&usl>lsl){
                    const double cp=(usl-lsl)/(6.0*sigma);
                    const double cpk=qMin(usl-mean,mean-lsl)/(3.0*sigma);
                    values.label=QStringLiteral("%1 (Cp %2, Cpk %3, mean %4, sigma %5)")
                        .arg(in.series.at(0).label).arg(cp,0,'f',2).arg(cpk,0,'f',2)
                        .arg(mean,0,'g',4).arg(sigma,0,'g',3);
                }
            }
            // The measurements alone, through the Histogram branch.
            PlotSpec counted=derivedAs(in,QStringLiteral("Histogram"));
            counted.series={values};
            out=prepareSpecCore(counted);
            out.legendVisible=true;

            // The limits reach as high as the tallest bin, so each is a rule
            // ACROSS the distribution rather than a mark along the bottom of
            // it. They were drawn from 0 to 1 - which on an axis of counts
            // reaching fifty is a tick two pixels tall.
            double tallest=0.0;
            for(const PlotSeries& bin:out.series)
                for(double c:bin.y) if(finite(c)) tallest=qMax(tallest,c);
            if(!(tallest>0.0)) tallest=1.0;

            for(const auto& limit:{qMakePair(lsl,QStringLiteral("LSL")),
                                   qMakePair(usl,QStringLiteral("USL"))}){
                if(!finite(limit.first)) continue;
                PlotSeries line;
                line.label=limit.second;
                line.color=in.style.danger;
                // drawBar honours this now - see the note there. Without it
                // the rule was drawn as a bar of the same shape as the data.
                line.drawLine=true; line.drawMarkers=false;
                line.lineWidth=1.0; line.lineWidthExplicit=true;
                line.dashPattern={4.0,3.0};
                line.x={limit.first,limit.first};
                line.y={0.0,tallest};
                out.series.append(line);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("measurement");
        return out;
    }

    // =====================================================================
    // Ocean, hydrology, energy and civil works
    // =====================================================================

    // Temperature against salinity, with the density contours a water mass is
    // actually identified by. Two samples at the same density and different
    // temperatures are different water masses, and on a plot without isopycnals
    // they are two dots with nothing to say so.
    //
    // Density is sigma-t from the UNESCO one-atmosphere equation of state,
    // which is the form oceanography quotes and is good to a hundredth of a
    // kg/m3 over the range any CTD returns.
    if(in.engine==QLatin1String("T-S Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& salinity=in.series.at(0).y;
            const QVector<double>& temperature=in.series.at(1).y;
            const int n=qMin(salinity.size(),temperature.size());
            PlotSeries pts;
            pts.label=QStringLiteral("samples");
            pts.color=in.series.at(0).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            double sLo=std::numeric_limits<double>::infinity(),sHi=-sLo;
            double tLo=sLo,tHi=-sLo;
            for(int i=0;i<n;++i){
                if(!finite(salinity[i])||!finite(temperature[i])) continue;
                pts.x.append(salinity[i]); pts.y.append(temperature[i]);
                sLo=qMin(sLo,salinity[i]); sHi=qMax(sHi,salinity[i]);
                tLo=qMin(tLo,temperature[i]); tHi=qMax(tHi,temperature[i]);
            }
            if(!pts.x.isEmpty()&&finite(sLo)&&sHi>sLo&&tHi>tLo){
                out.series.append(pts);
                // sigma-t: density at one atmosphere, minus 1000.
                const auto sigmaT=[](double S,double T){
                    const double rw=999.842594+6.793952e-2*T-9.095290e-3*T*T
                                   +1.001685e-4*T*T*T-1.120083e-6*T*T*T*T+6.536332e-9*T*T*T*T*T;
                    const double A=8.24493e-1-4.0899e-3*T+7.6438e-5*T*T
                                  -8.2467e-7*T*T*T+5.3875e-9*T*T*T*T;
                    const double B=-5.72466e-3+1.0227e-4*T-1.6546e-6*T*T;
                    const double C=4.8314e-4;
                    return rw+A*S+B*S*std::sqrt(qMax(0.0,S))+C*S*S-1000.0;
                };
                double densityLo=std::numeric_limits<double>::infinity(),densityHi=-densityLo;
                for(const double sv:{sLo,sHi})
                    for(const double tv:{tLo,tHi}){
                        const double d=sigmaT(sv,tv);
                        densityLo=qMin(densityLo,d); densityHi=qMax(densityHi,d);
                    }
                // Contours on a round interval, at most a dozen of them.
                const double span=densityHi-densityLo;
                double step=0.5;
                for(const double candidate:{0.1,0.2,0.5,1.0,2.0,5.0})
                    if(span/candidate<=12.0){ step=candidate; break; }
                bool labelled=false;
                for(double level=std::ceil(densityLo/step)*step;level<=densityHi;level+=step){
                    PlotSeries isopycnal;
                    isopycnal.label=labelled?QString()
                                            :QStringLiteral("sigma-t contours, %1 apart").arg(step);
                    labelled=true;
                    isopycnal.color=in.style.gridColor;
                    isopycnal.drawLine=true; isopycnal.drawMarkers=false;
                    isopycnal.lineWidth=0.7; isopycnal.lineWidthExplicit=true;
                    // Solve for the temperature at this density for each
                    // salinity, by bisection: sigma-t falls monotonically with
                    // temperature over the oceanographic range.
                    for(int i=0;i<=60;++i){
                        const double S=sLo+(sHi-sLo)*double(i)/60.0;
                        double lo=tLo-5.0,hi=tHi+5.0;
                        if((sigmaT(S,lo)-level)*(sigmaT(S,hi)-level)>0) continue;
                        for(int k=0;k<40;++k){
                            const double mid=0.5*(lo+hi);
                            if((sigmaT(S,lo)-level)*(sigmaT(S,mid)-level)<=0) hi=mid; else lo=mid;
                        }
                        isopycnal.x.append(S); isopycnal.y.append(0.5*(lo+hi));
                    }
                    if(isopycnal.x.size()>1) out.series.append(isopycnal);
                }
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("salinity");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("temperature");
        return out;
    }

    // A profile down through the water column, or down a borehole: the property
    // on x and depth on y, increasing downward. Depth is negated so the surface
    // is at the top, which is the only orientation this is ever read in.
    if(in.engine==QLatin1String("CTD Profile")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& depth=in.series.at(0).y;
            for(int k=1;k<in.series.size();++k){
                const PlotSeries& property=in.series.at(k);
                const int n=qMin(depth.size(),property.y.size());
                PlotSeries trace;
                trace.label=property.label; trace.color=property.color;
                trace.drawLine=true; trace.drawMarkers=false;
                for(int i=0;i<n;++i){
                    if(!finite(depth[i])||!finite(property.y[i])) continue;
                    trace.x.append(property.y[i]);
                    trace.y.append(std::abs(depth[i]));
                }
                if(!trace.x.isEmpty()) out.series.append(trace);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("measured property");
        // Depth as a positive number, on an axis that runs downward. Negating
        // the data was the way to do this before PlotAxis could be inverted,
        // and it put minus signs on every tick of an axis labelled "depth".
        out.yAxis=PlotAxis{QStringLiteral("depth"),false,unsetValue(),unsetValue(),true};
        return out;
    }

    // Stage against discharge, with the power law fitted through it. That
    // relation is what turns a level gauge - which is all most rivers have -
    // into a flow record, so its two coefficients are the useful output.
    if(in.engine==QLatin1String("Rating Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries pts;
            pts.color=s.color; pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            QVector<double> logStage,logFlow;
            for(int i=0;i<n;++i){
                const double stage=s.x[i],flow=s.y[i];
                if(!(stage>0)||!(flow>0)) continue;
                pts.x.append(stage); pts.y.append(flow);
                logStage.append(std::log(stage)); logFlow.append(std::log(flow));
            }
            if(pts.x.size()<3) continue;
            const LineFit line=fitLine(logStage,logFlow);
            // A RATING CURVE RISES. Q = a*h^b describes a channel, and in a
            // channel more water stands deeper - b is positive, typically
            // between 1.5 and 2.5. Fitted to a column that falls, b comes out
            // negative and the power law goes to infinity at small stage: one
            // column produced a curve reaching 340 on a figure whose discharges
            // are all under 8, and the axis was then scaled to it so every real
            // rating on the plot was a flat line along the bottom.
            //
            // The same shape as the Michaelis-Menten pole above, and refused
            // the same way: the points stay, and the label says why there is
            // no curve rather than leaving a reader to wonder.
            if(line.ok&&!(line.slope>0)){
                pts.label=QStringLiteral("%1 (no rating fitted: discharge falls with stage, b %2)")
                              .arg(s.label).arg(line.slope,0,'f',3);
                out.series.append(pts);
                continue;
            }
            if(line.ok){
                const double coefficient=std::exp(line.intercept);
                pts.label=QStringLiteral("%1 (Q = %2 h^%3)")
                              .arg(s.label).arg(coefficient,0,'g',4).arg(line.slope,0,'f',3);
                PlotSeries fit;
                fit.label=QStringLiteral("rating");
                fit.color=in.style.warning; fit.drawLine=true;
                fit.lineWidth=1.2; fit.lineWidthExplicit=true;
                double lo=std::numeric_limits<double>::infinity(),hi=-lo;
                for(double v:pts.x){ lo=qMin(lo,v); hi=qMax(hi,v); }
                for(int i=0;i<=80;++i){
                    const double stage=lo+(hi-lo)*double(i)/80.0;
                    fit.x.append(stage);
                    fit.y.append(coefficient*std::pow(stage,line.slope));
                }
                out.series.append(pts);
                out.series.append(fit);
                continue;
            }
            pts.label=s.label;
            out.series.append(pts);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("stage");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("discharge");
        return out;
    }

    // A turbine's power curve, with the three speeds that define it found from
    // the data: where it starts generating, where it reaches rated power, and
    // where it shuts down. A warranty is written against those three numbers.
    if(in.engine==QLatin1String("Wind Power Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<4) continue;
            QVector<QPair<double,double>> points;
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                points.append({s.x[i],s.y[i]});
            }
            if(points.size()<4) continue;
            std::sort(points.begin(),points.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            PlotSeries curve;
            curve.color=s.color; curve.drawLine=true; curve.drawMarkers=true; curve.markerSize=3.0;
            double rated=-std::numeric_limits<double>::infinity();
            for(const auto& pt:points){
                curve.x.append(pt.first); curve.y.append(pt.second);
                rated=qMax(rated,pt.second);
            }
            const double threshold=rated*0.01;
            double cutIn=qQNaN(),ratedSpeed=qQNaN(),cutOut=qQNaN();
            for(const auto& pt:points){
                if(!finite(cutIn)&&pt.second>threshold) cutIn=pt.first;
                if(!finite(ratedSpeed)&&pt.second>=rated*0.99) ratedSpeed=pt.first;
            }
            for(int i=points.size()-1;i>=0;--i)
                if(points[i].second>threshold){ cutOut=points[i].first; break; }
            curve.label=QStringLiteral("%1 (cut-in %2, rated %3 at %4, cut-out %5)")
                .arg(s.label)
                .arg(finite(cutIn)?QString::number(cutIn,'g',3):QStringLiteral("-"))
                .arg(rated,0,'g',4)
                .arg(finite(ratedSpeed)?QString::number(ratedSpeed,'g',3):QStringLiteral("-"))
                .arg(finite(cutOut)?QString::number(cutOut,'g',3):QStringLiteral("-"));
            out.series.append(curve);
            PlotSeries marks;
            marks.label=QStringLiteral("cut-in, rated, cut-out");
            marks.color=in.style.positive;
            marks.drawLine=false; marks.drawMarkers=true;
            marks.markerSize=7.0; marks.markerSizeExplicit=true;
            for(const double speed:{cutIn,ratedSpeed,cutOut}){
                if(!finite(speed)) continue;
                for(const auto& pt:points)
                    if(pt.first==speed){ marks.x.append(pt.first); marks.y.append(pt.second); break; }
            }
            if(!marks.x.isEmpty()) out.series.append(marks);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("wind speed");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("power");
        return out;
    }

    // Drawdown: how far below its own running peak a series has fallen, and the
    // worst of those. A rising line with a bad month in it looks fine; the
    // drawdown of the same series shows the month.
    if(in.engine==QLatin1String("Drawdown Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Area"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries curve;
            curve.color=s.color; curve.drawLine=true; curve.drawMarkers=false;
            double peak=-std::numeric_limits<double>::infinity();
            double worst=0.0; double worstAt=qQNaN();
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                peak=qMax(peak,s.y[i]);
                // Relative where the peak is positive, absolute otherwise: a
                // per-cent drawdown of a quantity that passes through zero is
                // not a number anyone should be shown.
                const double fall=(peak>0)?(s.y[i]-peak)/peak*100.0:(s.y[i]-peak);
                curve.x.append(s.x[i]); curve.y.append(fall);
                if(fall<worst){ worst=fall; worstAt=s.x[i]; }
            }
            if(curve.x.isEmpty()) continue;
            curve.label=QStringLiteral("%1 (worst %2%3)")
                .arg(s.label).arg(worst,0,'f',1)
                .arg((peak>0)?QStringLiteral("%"):QString());
            out.series.append(curve);
            if(finite(worstAt)){
                PlotSeries mark;
                mark.label=QStringLiteral("maximum drawdown");
                mark.color=in.style.danger;
                mark.drawLine=false; mark.drawMarkers=true;
                mark.markerSize=7.0; mark.markerSizeExplicit=true;
                mark.x={worstAt}; mark.y={worst};
                out.series.append(mark);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("time");
        out.yAxis=PlotAxis{QStringLiteral("drawdown from the running peak"),false,unsetValue(),unsetValue()};
        return out;
    }

    // The mass haul diagram: cumulative cut minus fill along a chainage, with
    // the balance points where it crosses zero. Between two balance points the
    // earth moves within the job; outside them it is imported or carted away,
    // and that is the difference between a profitable job and a loss.
    if(in.engine==QLatin1String("Mass Haul Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("Area"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries curve,balance;
            curve.label=s.label; curve.color=s.color; curve.drawLine=true;
            balance.label=QStringLiteral("balance points");
            balance.color=in.style.positive;
            balance.drawLine=false; balance.drawMarkers=true;
            balance.markerSize=7.0; balance.markerSizeExplicit=true;
            double running=0.0,previous=qQNaN(),previousX=qQNaN();
            double highest=-std::numeric_limits<double>::infinity();
            double lowest=std::numeric_limits<double>::infinity();
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                running+=s.y[i];
                curve.x.append(s.x[i]); curve.y.append(running);
                highest=qMax(highest,running); lowest=qMin(lowest,running);
                if(finite(previous)&&((previous>0&&running<=0)||(previous<0&&running>=0))){
                    const double span=running-previous;
                    const double t=(std::abs(span)>1e-12)?(-previous/span):0.0;
                    balance.x.append(previousX+t*(s.x[i]-previousX));
                    balance.y.append(0.0);
                }
                previous=running; previousX=s.x[i];
            }
            if(curve.x.isEmpty()) continue;
            // UNDER THE FIGURE, because a mass haul diagram usually carries
            // one alignment and a figure with one named series draws no
            // legend - so the net volume, the largest cut and the largest fill
            // were computed and shown nowhere. Those three numbers are what
            // the diagram is drawn to produce: they decide how much material
            // has to be imported or carted away.
            curve.label=s.label;
            out.figureNote=QStringLiteral("Net %1, largest cut %2, largest fill %3, "
                                          "in the volume units of the ordinate.")
                               .arg(formatMeasured(running))
                               .arg(formatMeasured(highest)).arg(formatMeasured(lowest));
            out.series.append(curve);
            if(!balance.x.isEmpty()) out.series.append(balance);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("chainage");
        out.yAxis=PlotAxis{QStringLiteral("cumulative cut minus fill"),false,unsetValue(),unsetValue()};
        return out;
    }

    // Shear and bending moment from a distributed load along a beam: one
    // integral and then a second. They are drawn together because the pair is
    // read together - the moment peaks where the shear crosses zero, and seeing
    // that on one chart is the check that the integration is right.
    if(in.engine==QLatin1String("Shear and Moment")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries shear,moment;
            shear.label=QStringLiteral("%1 shear").arg(s.label);
            shear.color=s.color; shear.drawLine=true;
            moment.label=QStringLiteral("%1 bending moment").arg(s.label);
            moment.color=in.style.warning; moment.drawLine=true;
            // THE SUPPORTS ARE PART OF THE PROBLEM, not an afterthought.
            //
            // Integrating the load from the left end with nothing at that end
            // is a CANTILEVER, built in at x = 0 and free at the far end. It
            // is a perfectly good answer to a different question, and it was
            // being drawn under the heading "shear and moment" for a beam
            // nobody said was a cantilever: a uniform load came out with the
            // moment growing to its largest value at the free end and neither
            // diagram returning to zero. Every engineer's first check on a
            // moment diagram is that it is zero at a simple support, and this
            // one failed it everywhere.
            //
            // A single span on two supports is statically determinate, so the
            // reactions follow from the load alone - no boundary condition has
            // to be asked for. Sum of forces and sum of moments about the left
            // support give both, and the diagrams then close: shear crosses
            // zero where the moment peaks, and the moment is zero at each end,
            // which is what makes the pair checkable at a glance.
            double totalLoad=0.0,loadMoment=0.0;
            {
                double px=qQNaN(),pw=0.0;
                for(int i=0;i<n;++i){
                    if(!finite(s.x[i])||!finite(s.y[i])) continue;
                    if(finite(px)){
                        const double dx=s.x[i]-px;
                        totalLoad+=0.5*(pw+s.y[i])*dx;
                        // First moment of the load about the left support.
                        loadMoment+=0.5*(pw*px+s.y[i]*s.x[i])*dx;
                    }
                    px=s.x[i]; pw=s.y[i];
                }
            }
            double spanLo=qQNaN(),spanHi=qQNaN();
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                if(!finite(spanLo)) spanLo=s.x[i];
                spanHi=s.x[i];
            }
            const double span=(finite(spanLo)&&finite(spanHi))?(spanHi-spanLo):0.0;
            double reactionA=0.0;
            if(span>1e-12){
                // Moments about the left support: R_B * span + loadMoment = 0,
                // measuring the load's arm from that support.
                const double reactionB=-(loadMoment-totalLoad*spanLo)/span;
                reactionA=-totalLoad-reactionB;
            }
            double v=reactionA,m=0.0,previousX=qQNaN(),previousLoad=0.0;
            double previousShear=reactionA;
            double peakMoment=0.0,peakAt=qQNaN();
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                if(finite(previousX)){
                    const double dx=s.x[i]-previousX;
                    v+=0.5*(previousLoad+s.y[i])*dx;      // trapezoidal, both ways
                    m+=0.5*(previousShear+v)*dx;
                }
                shear.x.append(s.x[i]); shear.y.append(v);
                moment.x.append(s.x[i]); moment.y.append(m);
                if(std::abs(m)>std::abs(peakMoment)){ peakMoment=m; peakAt=s.x[i]; }
                previousX=s.x[i]; previousLoad=s.y[i]; previousShear=v;
            }
            if(shear.x.isEmpty()) continue;
            moment.label=QStringLiteral("%1 bending moment (peak %2 at %3)")
                .arg(s.label).arg(peakMoment,0,'g',4)
                .arg(finite(peakAt)?QString::number(peakAt,'g',4):QStringLiteral("-"));
            out.series.append(shear);
            out.series.append(moment);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("position along the beam");
        out.yAxis=PlotAxis{QStringLiteral("shear and moment (simply supported)"),
                           false,unsetValue(),unsetValue()};
        return out;
    }

    // An eye diagram: the signal cut into symbol-length pieces and laid on top
    // of each other. What it shows is the opening in the middle - how much
    // noise and timing error the link can take before a bit is read wrongly -
    // and that is invisible in the signal drawn end to end.
    //
    // The symbol period comes from the second mapped column's first value, or
    // from the dominant period in the signal when only one column is mapped.
    if(in.engine==QLatin1String("Eye Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(!in.series.isEmpty()){
            const PlotSeries& s=in.series.at(0);
            const int n=qMin(s.x.size(),s.y.size());
            double period=qQNaN();
            if(in.series.size()>=2)
                for(double v:in.series.at(1).y) if(finite(v)&&v>0){ period=v; break; }
            if(!finite(period)&&n>2){
                // Zero crossings of the centred signal: the median gap between
                // them is one half-symbol, so twice it is the period.
                double mean=0.0; int counted=0;
                for(int i=0;i<n;++i) if(finite(s.y[i])){ mean+=s.y[i]; ++counted; }
                if(counted>0){
                    mean/=double(counted);
                    QVector<double> gaps; double last=qQNaN();
                    for(int i=1;i<n;++i){
                        if(!finite(s.y[i])||!finite(s.y[i-1])) continue;
                        if((s.y[i-1]-mean)*(s.y[i]-mean)<0){
                            if(finite(last)) gaps.append(s.x[i]-last);
                            last=s.x[i];
                        }
                    }
                    if(!gaps.isEmpty()){
                        std::sort(gaps.begin(),gaps.end());
                        period=2.0*gaps[gaps.size()/2];
                    }
                }
            }
            if(finite(period)&&period>0&&n>2){
                const double start=s.x.first();
                PlotSeries trace;
                trace.color=s.color; trace.drawLine=true; trace.drawMarkers=false;
                trace.opacity=0.35;
                trace.lineWidth=0.8; trace.lineWidthExplicit=true;
                int sweeps=0;
                double previousPhase=-1.0;
                for(int i=0;i<n;++i){
                    if(!finite(s.x[i])||!finite(s.y[i])) continue;
                    // Two symbols wide, which is how an eye is drawn: the
                    // opening sits in the middle with a transition either side.
                    const double phase=std::fmod(s.x[i]-start,2.0*period);
                    if(phase<previousPhase){
                        if(trace.x.size()>1){ out.series.append(trace); ++sweeps; }
                        trace.x.clear(); trace.y.clear();
                        trace.label.clear();
                    }
                    previousPhase=phase;
                    trace.x.append(phase); trace.y.append(s.y[i]);
                }
                if(trace.x.size()>1){ out.series.append(trace); ++sweeps; }
                // Every sweep after the first is unlabelled on purpose - an
                // eye of eighty traces must not be an eighty-row legend - so
                // naming the first one put the sweep count and the symbol
                // period in the only legend row there was, and a legend of
                // one row is suppressed as a caption. How many sweeps are
                // overlaid, and on what symbol period, is how a reader knows
                // what the eye is an eye OF.
                if(!out.series.isEmpty()) out.series[0].label=s.label;
                out.figureNote=QStringLiteral("%1 sweeps overlaid on a symbol period "
                                              "of %2.").arg(sweeps).arg(formatMeasured(period));
                out.legendVisible=true;
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("time within two symbols"),false,unsetValue(),unsetValue()};
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("amplitude");
        return out;
    }

    // A stereonet: dip direction and dip, on the equal-area projection that
    // structural geology counts orientations on. Drawn as a polar scatter with
    // the radius computed - equal-area means a cluster of readings covers the
    // same area of the net wherever it sits, which is the whole reason to count
    // on this projection rather than on a plain polar plot.
    if(in.engine==QLatin1String("Stereonet")){
        PlotSpec out=derivedAs(in,QStringLiteral("Polar Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& azimuth=in.series.at(0).y;
            const QVector<double>& dip=in.series.at(1).y;
            const int n=qMin(azimuth.size(),dip.size());
            PlotSeries poles;
            poles.label=QStringLiteral("poles to planes");
            poles.color=in.series.at(0).color;
            poles.drawLine=false; poles.drawMarkers=true; poles.markerSize=4.5;
            for(int i=0;i<n;++i){
                if(!finite(azimuth[i])||!finite(dip[i])) continue;
                const double plunge=qBound(0.0,90.0-dip[i],90.0);
                // Schmidt: r = R sqrt(2) sin((90 - plunge)/2), with R = 1.
                const double r=std::sqrt(2.0)*std::sin((90.0-plunge)*kDegToRad/2.0);
                // The pole to a plane lies opposite its dip direction.
                poles.x.append(std::fmod(azimuth[i]+180.0,360.0));
                poles.y.append(r);
            }
            if(!poles.x.isEmpty()) out.series.append(poles);
        }
        out.xAxis=PlotAxis{QStringLiteral("dip direction (deg)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("equal-area radius"),false,0.0,std::sqrt(2.0)};
        return out;
    }

    // A psychrometric chart: dry-bulb temperature against humidity ratio, with
    // the relative-humidity curves that make it readable drawn as part of the
    // figure rather than as a background image. Saturation pressure is the
    // Magnus form, which is within a tenth of a per cent over the range a
    // building is conditioned in.
    if(in.engine==QLatin1String("Psychrometric Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        const auto saturation=[](double t){       // kPa
            return 0.61094*std::exp(17.625*t/(t+243.04));
        };
        const auto ratio=[&saturation](double t,double rh){   // kg water / kg dry air
            const double pressure=101.325;
            const double vapour=rh*saturation(t);
            return 0.62198*vapour/qMax(1e-9,pressure-vapour);
        };
        double tLo=0.0,tHi=50.0;
        if(in.series.size()>=2){
            const QVector<double>& dry=in.series.at(0).y;
            const QVector<double>& humidity=in.series.at(1).y;
            const int n=qMin(dry.size(),humidity.size());
            PlotSeries pts;
            pts.label=QStringLiteral("states");
            pts.color=in.series.at(0).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=5.0;
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            for(int i=0;i<n;++i){
                if(!finite(dry[i])||!finite(humidity[i])) continue;
                // A humidity column above 1.5 is read as a percentage.
                const double rh=(humidity[i]>1.5)?humidity[i]/100.0:humidity[i];
                pts.x.append(dry[i]);
                pts.y.append(ratio(dry[i],qBound(0.0,rh,1.0))*1000.0);   // g/kg
                lo=qMin(lo,dry[i]); hi=qMax(hi,dry[i]);
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                tLo=std::floor(lo-2.0); tHi=std::ceil(hi+2.0);
            }
        }
        for(const int rh:{10,20,30,40,50,60,70,80,90,100}){
            PlotSeries line;
            line.label=(rh==100)?QStringLiteral("saturation")
                                :(rh==50?QStringLiteral("relative humidity, 10% steps"):QString());
            line.color=(rh==100)?in.style.foreground:in.style.gridColor;
            line.drawLine=true; line.drawMarkers=false;
            line.lineWidth=(rh==100)?1.1:0.6; line.lineWidthExplicit=true;
            for(int i=0;i<=60;++i){
                const double t=tLo+(tHi-tLo)*double(i)/60.0;
                line.x.append(t);
                line.y.append(ratio(t,double(rh)/100.0)*1000.0);
            }
            out.series.append(line);
        }
        out.xAxis=PlotAxis{QStringLiteral("dry-bulb temperature (degC)"),false,tLo,tHi};
        out.yAxis=PlotAxis{QStringLiteral("humidity ratio (g/kg dry air)"),false,0.0,unsetValue()};
        return out;
    }

    // =====================================================================
    // The gaps a comparison against LabPlot and sci-draw turned up
    // =====================================================================

    // A dendrogram. The multivariate engine already computes the hierarchical
    // clustering; nothing could draw the tree it produces, so the one output
    // that says WHERE to cut - the height at which the merges suddenly get
    // expensive - had no picture.
    //
    // Two mapped columns: the merge heights, and the size of each merged
    // cluster. Drawn as a linkage tree with the leaves in merge order.
    if(in.engine==QLatin1String("Dendrogram")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        out.legendVisible=false;
        if(!in.series.isEmpty()){
            QVector<double> heights=finiteValues(in.series.at(0));
            if(heights.size()>=2){
                if(heights.size()>200) heights.resize(200);
                // The leaves sit at unit spacing; each merge joins the two
                // nearest unjoined positions and takes their midpoint, which
                // reproduces the shape of a linkage tree from the heights
                // alone.
                QVector<double> positions;
                for(int i=0;i<heights.size()+1;++i) positions.append(double(i));
                QVector<double> live=positions;
                QVector<double> base(live.size(),0.0);
                for(int m=0;m<heights.size()&&live.size()>=2;++m){
                    // Merge the closest pair, which for unit-spaced leaves is
                    // the pair with the smallest gap.
                    int at=0; double closest=std::numeric_limits<double>::infinity();
                    for(int i=0;i+1<live.size();++i){
                        const double gap=live[i+1]-live[i];
                        if(gap<closest){ closest=gap; at=i; }
                    }
                    const double left=live[at],right=live[at+1];
                    const double top=heights[m];
                    const double leftBase=base[at],rightBase=base[at+1];
                    PlotSeries link;
                    link.color=in.series.at(0).color;
                    link.drawLine=true; link.drawMarkers=false;
                    link.lineWidth=1.1; link.lineWidthExplicit=true;
                    link.x={left,left,right,right};
                    link.y={leftBase,top,top,rightBase};
                    out.series.append(link);
                    live[at]=0.5*(left+right);
                    base[at]=top;
                    live.remove(at+1); base.remove(at+1);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("observation"),false,unsetValue(),unsetValue()};
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("merge distance");
        return out;
    }

    // Precision against recall, with the average precision that summarises it.
    // On an unbalanced problem this is the curve that tells the truth: a ROC
    // can look excellent while almost every positive prediction is wrong,
    // because the false-positive rate is diluted by a huge negative class.
    //
    // Two mapped columns: the score, and the true label as 1 or 0.
    if(in.engine==QLatin1String("Precision-Recall Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& score=in.series.at(0).y;
            const QVector<double>& truth=in.series.at(1).y;
            const int n=qMin(score.size(),truth.size());
            QVector<QPair<double,bool>> rows;
            int positives=0;
            for(int i=0;i<n;++i){
                if(!finite(score[i])||!finite(truth[i])) continue;
                const bool positive=truth[i]>=0.5;
                rows.append({score[i],positive});
                if(positive) ++positives;
            }
            if(positives>0&&rows.size()>1){
                std::sort(rows.begin(),rows.end(),
                          [](const QPair<double,bool>& a,const QPair<double,bool>& b){
                              return a.first>b.first; });
                PlotSeries curve;
                curve.color=in.series.at(0).color;
                curve.drawLine=true; curve.drawMarkers=false;
                int truePositives=0,called=0;
                double average=0.0,previousRecall=0.0;
                for(const auto& row:rows){
                    ++called;
                    if(row.second) ++truePositives;
                    const double precision=double(truePositives)/double(called);
                    const double recall=double(truePositives)/double(positives);
                    // Average precision: the precision at each point weighted
                    // by the recall it gained, which is the area under this
                    // curve without interpolating between the points.
                    average+=precision*(recall-previousRecall);
                    previousRecall=recall;
                    curve.x.append(recall); curve.y.append(precision);
                }
                curve.label=QStringLiteral("%1 (average precision %2)")
                                .arg(in.series.at(0).label).arg(average,0,'f',3);
                out.series.append(curve);
                // The line a coin-flip classifier sits on: the prevalence.
                PlotSeries chance;
                chance.label=QStringLiteral("chance (%1 positive)")
                                 .arg(double(positives)/double(rows.size()),0,'f',3);
                chance.color=in.style.warning;
                chance.drawLine=true; chance.lineWidth=0.8; chance.lineWidthExplicit=true;
                chance.dashPattern={4.0,3.0};
                const double prevalence=double(positives)/double(rows.size());
                chance.x={0.0,1.0}; chance.y={prevalence,prevalence};
                out.series.append(chance);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("recall"),false,0.0,1.0};
        out.yAxis=PlotAxis{QStringLiteral("precision"),false,0.0,1.02};
        return out;
    }

    // A confusion matrix, as a grid with the counts on it. Two mapped columns -
    // the true class and the predicted one - and the accuracy in the title
    // line, because a matrix without it invites reading the diagonal and
    // guessing.
    if(in.engine==QLatin1String("Confusion Matrix")){
        PlotSpec out=derivedAs(in,QStringLiteral("2D Heatmap"));
        out.legendVisible=false;
        if(in.series.size()>=2){
            const QVector<double>& truth=in.series.at(0).y;
            const QVector<double>& predicted=in.series.at(1).y;
            const int n=qMin(truth.size(),predicted.size());
            // STOP AS SOON AS THERE ARE TOO MANY CLASSES.
            //
            // The cap - twenty, because a confusion matrix of ten thousand
            // classes is not a confusion matrix - was applied after the whole
            // class list had been collected, and collecting it asked
            // `QVector::contains` once per value, which is a linear scan. On a
            // continuous column mapped by mistake that is 576 million
            // comparisons to reach a verdict of "too many", the same verdict a
            // set and an early exit reach in one pass.
            //
            // Same shape as the mosaic plot's guard, which had the same fault.
            constexpr int kMaxClasses=20;
            QVector<double> classes;
            {
                QSet<double> seen;
                for(int i=0;i<n&&seen.size()<=kMaxClasses;++i){
                    for(const double v:{truth[i],predicted[i]}){
                        if(!finite(v)) continue;
                        if(seen.contains(v)) continue;
                        seen.insert(v);
                        classes.append(v);
                    }
                }
            }
            std::sort(classes.begin(),classes.end());
            if(!classes.isEmpty()&&classes.size()<=kMaxClasses){
                QVector<double> cells(classes.size()*classes.size(),0.0);
                int correct=0,total=0;
                for(int i=0;i<n;++i){
                    if(!finite(truth[i])||!finite(predicted[i])) continue;
                    const int r=classes.indexOf(truth[i]);
                    const int c=classes.indexOf(predicted[i]);
                    if(r<0||c<0) continue;
                    cells[r*classes.size()+c]+=1.0;
                    if(r==c) ++correct;
                    ++total;
                }
                PlotSeries xi,yi,vi;
                for(int r=0;r<classes.size();++r)
                    for(int c=0;c<classes.size();++c){
                        xi.y.append(double(c)); yi.y.append(double(r));
                        vi.y.append(cells[r*classes.size()+c]);
                    }
                xi.label=QStringLiteral("predicted");
                yi.label=QStringLiteral("true");
                vi.label=total>0
                    ? QStringLiteral("count (accuracy %1)").arg(double(correct)/double(total),0,'f',3)
                    : QStringLiteral("count");
                out.series={xi,yi,vi};
                out.xAxis=PlotAxis{QStringLiteral("predicted class"),false,
                                   -0.5,double(classes.size())-0.5};
                out.yAxis=PlotAxis{QStringLiteral("true class"),false,
                                   -0.5,double(classes.size())-0.5};
                // The CLASSES, at the positions their cells occupy.
                //
                // The cells are indexed 0..n-1 but the classes they stand for
                // are whatever was in the data, so the tick has to carry both:
                // the index to place it at, and the class value to write. Left
                // to the ordinary chooser this axis got 0.5, 1.5 and 2.5 -
                // labels for classes that do not exist.
                // EXACTLY ONE CELL PER (true, predicted) PAIR.
                //
                // Without this the grid resolution was inferred from the sample
                // count and floored at twelve, so a four-class table was drawn
                // as a twelve-by-twelve field: the diagonal smeared, and cells
                // showing counts that are in no row of the matrix. A confusion
                // matrix has n by n cells and no other number is meaningful.
                out.style.fieldResolution=classes.size();
                for(int k=0;k<classes.size();++k){
                    const QString name=QString::number(classes.at(k),'g',6);
                    out.xAxis.tickValues.append(double(k));
                    out.xAxis.tickLabels.append(name);
                    out.yAxis.tickValues.append(double(k));
                    out.yAxis.tickLabels.append(name);
                }
                return out;
            }
        }
        return out;
    }

    // An MA plot: the log ratio between two conditions against their average
    // intensity. The shape is the point - a ratio that drifts with intensity is
    // a normalisation problem, not biology - and it is invisible in a plot of
    // one condition against the other.
    if(in.engine==QLatin1String("MA Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& a=in.series.at(0).y;
            const QVector<double>& b=in.series.at(1).y;
            const int n=qMin(a.size(),b.size());
            PlotSeries pts;
            pts.color=in.series.at(0).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=3.2;
            double sum=0.0; int counted=0;
            for(int i=0;i<n;++i){
                if(!(a[i]>0)||!(b[i]>0)) continue;      // a log needs a positive
                const double la=std::log2(a[i]),lb=std::log2(b[i]);
                pts.x.append(0.5*(la+lb));              // A, the average
                pts.y.append(la-lb);                    // M, the log ratio
                sum+=la-lb; ++counted;
            }
            if(counted>0){
                const double bias=sum/double(counted);
                pts.label=QStringLiteral("%1 vs %2 (median shift %3)")
                              .arg(in.series.at(0).label,in.series.at(1).label)
                              .arg(bias,0,'f',3);
                out.series.append(pts);
                PlotSeries zero;
                zero.label=QStringLiteral("no change");
                zero.color=in.style.warning;
                zero.drawLine=true; zero.lineWidth=0.9; zero.lineWidthExplicit=true;
                zero.dashPattern={4.0,3.0};
                double lo=std::numeric_limits<double>::infinity(),hi=-lo;
                for(double v:pts.x){ lo=qMin(lo,v); hi=qMax(hi,v); }
                zero.x={lo,hi}; zero.y={0.0,0.0};
                out.series.append(zero);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("A, mean log2 intensity"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("M, log2 ratio"),false,unsetValue(),unsetValue()};
        return out;
    }

    // The four attribute control charts. They differ only in what is counted
    // and how the limits are derived, so they are one rewrite with a table
    // rather than four near-identical ones:
    //
    //   p   the fraction defective, limits from the binomial, varying with n
    //   np  the number defective, constant n
    //   c   the count of defects per unit
    //   u   the count of defects per unit of opportunity, varying opportunity
    //
    // Mapped columns: the count, and for p, np and u the sample size beside it.
    if(in.engine==QLatin1String("p-Chart")||in.engine==QLatin1String("np-Chart")
       ||in.engine==QLatin1String("c-Chart")||in.engine==QLatin1String("u-Chart")){
        const bool proportion=(in.engine==QLatin1String("p-Chart"));
        const bool number=(in.engine==QLatin1String("np-Chart"));
        const bool perUnit=(in.engine==QLatin1String("u-Chart"));
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(!in.series.isEmpty()){
            const QVector<double>& counts=in.series.at(0).y;
            const QVector<double> sizes=(in.series.size()>=2)?in.series.at(1).y:QVector<double>();
            const int n=counts.size();
            PlotSeries values,upper,lower,centre;
            values.color=in.series.at(0).color;
            values.drawLine=true; values.drawMarkers=true; values.markerSize=3.5;
            upper.label=QStringLiteral("control limits");
            upper.color=in.style.danger;
            upper.drawLine=true; upper.lineWidth=0.9; upper.lineWidthExplicit=true;
            lower=upper; lower.label.clear();
            // Bare on purpose, unlike the Bland-Altman and control-chart rules
            // above: this chart already states its centre value in the series
            // label ("centre %3" below), so repeating it on the rule would say
            // the same number twice in one legend.
            centre.label=QStringLiteral("centre");
            centre.color=in.style.foreground;
            centre.drawLine=true; centre.lineWidth=0.7; centre.lineWidthExplicit=true;
            centre.dashPattern={4.0,4.0};

            double totalCount=0.0,totalSize=0.0; int used=0;
            for(int i=0;i<n;++i){
                if(!finite(counts[i])||counts[i]<0) continue;
                const double size=(i<sizes.size()&&finite(sizes[i])&&sizes[i]>0)?sizes[i]:1.0;
                totalCount+=counts[i]; totalSize+=size; ++used;
            }
            if(used>0&&totalSize>0){
                // The centre line: a pooled proportion for p and np, a mean
                // count for c, a pooled rate for u. Pooling rather than
                // averaging the per-sample values is what makes the limits
                // right when the samples are different sizes.
                const double meanSize=totalSize/double(used);
                const double centreValue=
                    proportion ? totalCount/totalSize
                  : number     ? totalCount/double(used)
                  : perUnit    ? totalCount/totalSize
                               : totalCount/double(used);
                int beyond=0;
                for(int i=0;i<n;++i){
                    if(!finite(counts[i])||counts[i]<0) continue;
                    const double size=(i<sizes.size()&&finite(sizes[i])&&sizes[i]>0)?sizes[i]:1.0;
                    const double value=
                        proportion ? counts[i]/size
                      : perUnit    ? counts[i]/size
                                   : counts[i];
                    double spread=0.0;
                    if(proportion)      spread=std::sqrt(qMax(0.0,centreValue*(1.0-centreValue)/size));
                    else if(number)     spread=std::sqrt(qMax(0.0,centreValue*(1.0-centreValue/qMax(1.0,meanSize))));
                    else if(perUnit)    spread=std::sqrt(qMax(0.0,centreValue/size));
                    else                spread=std::sqrt(qMax(0.0,centreValue));
                    const double hi=centreValue+3.0*spread;
                    // A count cannot be negative, and neither can a rate.
                    const double lo=qMax(0.0,centreValue-3.0*spread);
                    if(value>hi||value<lo) ++beyond;
                    values.x.append(double(i+1)); values.y.append(value);
                    upper.x.append(double(i+1)); upper.y.append(hi);
                    lower.x.append(double(i+1)); lower.y.append(lo);
                    centre.x.append(double(i+1)); centre.y.append(centreValue);
                }
                values.label=QStringLiteral("%1 (%2, centre %3, %4 out of control)")
                    .arg(in.series.at(0).label)
                    .arg(in.engine).arg(centreValue,0,'g',4).arg(beyond);
                out.series.append(values);
                out.series.append(upper);
                out.series.append(lower);
                out.series.append(centre);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("sample"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{proportion?QStringLiteral("fraction defective")
                          :number    ?QStringLiteral("number defective")
                          :perUnit   ?QStringLiteral("defects per unit")
                                     :QStringLiteral("defects per sample"),
                           false,unsetValue(),unsetValue()};
        return out;
    }

    // A rug: one tick per observation along the axis. It costs almost no ink
    // and it puts the actual sample back under a smoothed curve, which is the
    // one thing a density estimate hides - a beautiful KDE over eleven points
    // looks exactly like one over eleven thousand.
    if(in.engine==QLatin1String("Rug Plot")){
        // ONE ROW PER COLUMN, NAMED.
        //
        // The rows used to be stacked at whatever three per cent of each
        // column's own range came to, accumulated - so the y axis was labelled
        // "series" over numbers like 0.09 and 0.27, which are neither series
        // nor anything else. A rug's vertical position carries no quantity at
        // all: it is a row, and a row's only useful property is whose it is.
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        int slotIndex=0;
        constexpr double kRugHeight=0.55;
        for(const PlotSeries& s:in.series){
            const QVector<double> v=finiteValues(s);
            if(v.isEmpty()) continue;
            const double slot=double(slotIndex);
            const double height=kRugHeight;
            PlotSeries ticks;
            ticks.label=QStringLiteral("%1 (%2 observations)").arg(s.label).arg(v.size());
            ticks.color=s.color;
            ticks.drawLine=false; ticks.drawMarkers=true;
            ticks.markerSize=2.0; ticks.markerSizeExplicit=true;
            // A tick is two marks a hair apart rather than a drawn segment, so
            // ten thousand of them cost what ten thousand points cost and not
            // what ten thousand polylines cost.
            for(double value:v){
                ticks.x.append(value); ticks.y.append(slot);
                ticks.x.append(value); ticks.y.append(slot+height);
            }
            out.series.append(ticks);
            out.yAxis.tickValues.append(slot+height*0.5);
            out.yAxis.tickLabels.append(s.label);
            ++slotIndex;
        }
        // x is the OBSERVED VALUE of each mapped column, not the column the
        // figure was mapped against - so the label is set rather than
        // defaulted, or it keeps the incoming one and reads "time_h" under an
        // axis of concentrations. One column can give its own name; several
        // have no single name between them.
        out.xAxis.label=(in.series.size()==1&&!in.series.first().label.isEmpty())
                            ?in.series.first().label
                            :QStringLiteral("value");
        {
            // Kept, rather than assigned over: the tick labels above were
            // appended to this axis and a fresh PlotAxis would drop them.
            const QVector<double> values=out.yAxis.tickValues;
            const QStringList names=out.yAxis.tickLabels;
            out.yAxis=PlotAxis{QStringLiteral("series"),false,
                               -0.35,double(qMax(1,slotIndex))-1.0+kRugHeight+0.35};
            out.yAxis.tickValues=values;
            out.yAxis.tickLabels=names;
        }
        return out;
    }

    // ------------------------------------------- Power Spectral Density
    // Periodogram of each mapped series, Hann-windowed, on log-log axes because
    // that is where power laws are straight lines and where the useful part of
    // a spectrum lives.
    if(in.engine==QLatin1String("Power Spectral Density")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<16) continue;
            // Sample interval from the mapped x column; 1.0 if it is not usable,
            // in which case the frequency axis is in cycles per sample and the
            // label says so.
            double dt=1.0;
            bool haveTime=false;
            if(s.x.size()>=2&&finite(s.x[0])&&finite(s.x[1])){
                const double d=(s.x[s.x.size()-1]-s.x[0])/double(s.x.size()-1);
                if(finite(d)&&std::abs(d)>1e-15){ dt=std::abs(d); haveTime=true; }
            }
            const int n=nextPowerOfTwo(v.size());
            QVector<double> re(n,0.0),im(n,0.0);
            const double mean=meanOf(v);
            // The catalogue variant picks the taper; Hann when nothing is asked
            // for, which is what this engine always used.
            const SpectralWindow taper=spectralWindowFor(in.variant);
            double windowPower=0.0;
            for(int i=0;i<v.size();++i){
                const double w=spectralWindow(taper,i,v.size());
                re[i]=(v[i]-mean)*w;      // remove the DC term or it swamps the plot
                windowPower+=w*w;
            }
            if(!(windowPower>0.0)) continue;
            fftInPlace(re,im);
            PlotSeries psd;
            psd.label=s.label; psd.color=s.color; psd.dashPattern=s.dashPattern;
            psd.lineWidth=qMax(1.2,s.lineWidth);
            psd.drawLine=true; psd.drawMarkers=false;
            const double scale=2.0*dt/windowPower;
            double peak=0.0,fPeak=0.0;
            for(int k=1;k<n/2;++k){
                const double power=(re[k]*re[k]+im[k]*im[k])*scale;
                if(!(power>0.0)) continue;
                const double f=double(k)/(double(n)*dt);
                psd.x.append(f);
                psd.y.append(power);
                if(power>peak){ peak=power; fPeak=f; }
            }
            if(psd.x.size()>=2){
                out.series.append(psd);
                // THE PEAK, MARKED AND NAMED - the same idiom the Lomb-Scargle
                // periodogram beside it already uses, and for the same reason.
                //
                // A spectrum exists to answer "at what frequency", and this one
                // answered it by drawing a hump and leaving the reader to
                // estimate the abscissa by eye. The shaped fixture makes it
                // concrete: two unmistakable peaks at 6.25 Hz and 12.5 Hz, and
                // neither number anywhere on the figure.
                //
                // A SECOND named series rather than text appended to the first,
                // because a figure with one named series draws no legend at all
                // - so a label-only fix would have been invisible, which is the
                // fault this codebase found thirteen times in one pass. The
                // mark also settles WHICH hump is meant when several are
                // comparable, which the number alone does not.
                if(fPeak>0.0){
                    PlotSeries mark;
                    mark.label=haveTime
                        ? QStringLiteral("peak %1 Hz").arg(formatMeasured(fPeak))
                        : QStringLiteral("peak %1 cycles/sample").arg(formatMeasured(fPeak));
                    mark.color=in.style.warning;
                    mark.drawLine=false; mark.drawMarkers=true;
                    mark.markerSize=8.0; mark.markerSizeExplicit=true;
                    mark.x={fPeak}; mark.y={peak};
                    out.series.append(mark);
                }
            }
            out.xAxis=PlotAxis{haveTime?QStringLiteral("frequency"):QStringLiteral("cycles per sample"),
                               true,unsetValue(),unsetValue()};
        }
        out.yAxis=PlotAxis{QStringLiteral("power spectral density"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Spectrogram
    // Short-time Fourier transform of the first mapped series, as a heatmap of
    // time against frequency. Packed into the three columns the field engines
    // read, so the existing heatmap draws it.
    if(in.engine==QLatin1String("Spectrogram")){
        PlotSpec out=in;
        out.engine=QStringLiteral("2D Heatmap");
        out.legendVisible=false;
        out.series.clear();
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<64) continue;
            double dt=1.0;
            if(s.x.size()>=2&&finite(s.x[0])&&finite(s.x[1])){
                const double d=(s.x[s.x.size()-1]-s.x[0])/double(s.x.size()-1);
                if(finite(d)&&std::abs(d)>1e-15) dt=std::abs(d);
            }
            // A window of a sixteenth of the record, half-overlapped: enough
            // frames to see something change, enough samples per frame to
            // resolve what changed.
            const int window=qBound(32,nextPowerOfTwo(v.size()/16),512);
            const int hop=qMax(1,window/2);
            const SpectralWindow taper=spectralWindowFor(in.variant);
            PlotSeries tx,fy,pv;
            for(int start=0;start+window<=v.size();start+=hop){
                QVector<double> re(window,0.0),im(window,0.0);
                double mean=0.0;
                for(int i=0;i<window;++i) mean+=v[start+i];
                mean/=double(window);
                for(int i=0;i<window;++i) re[i]=(v[start+i]-mean)*spectralWindow(taper,i,window);
                fftInPlace(re,im);
                const double centre=(double(start)+window/2.0)*dt;
                for(int k=1;k<window/2;++k){
                    const double power=re[k]*re[k]+im[k]*im[k];
                    tx.y.append(centre);
                    fy.y.append(double(k)/(double(window)*dt));
                    // Decibels: a linear spectrogram shows the loudest frame
                    // and nothing else.
                    pv.y.append(10.0*std::log10(qMax(1e-18,power)));
                }
            }
            tx.label=QStringLiteral("time"); fy.label=QStringLiteral("frequency");
            pv.label=QStringLiteral("dB");
            if(!tx.y.isEmpty()) out.series={tx,fy,pv};
            break;              // one signal, one spectrogram
        }
        out.xAxis=PlotAxis{QStringLiteral("time"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("frequency"),false,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------------------- Cross Correlation
    if(in.engine==QLatin1String("Cross Correlation")){
        PlotSpec out=derivedAs(in,QStringLiteral("Stem"));
        if(in.series.size()>=2){
            QVector<double> a=finiteValues(in.series.at(0));
            QVector<double> b=finiteValues(in.series.at(1));
            const int n=qMin(a.size(),b.size());
            if(n>=8){
                const double ma=meanOf(a), mb=meanOf(b);
                double da=0.0,db=0.0;
                for(int i=0;i<n;++i){ da+=(a[i]-ma)*(a[i]-ma); db+=(b[i]-mb)*(b[i]-mb); }
                const double denom=std::sqrt(da*db);
                if(denom>0.0){
                    PlotSeries xc;
                    xc.label=QStringLiteral("%1 x %2").arg(in.series.at(0).label,in.series.at(1).label);
                    xc.color=in.series.at(0).color;
                    const int maxLag=qMin(n/2,60);
                    // WHERE THE PEAK IS, which is the answer a cross
                    // correlation exists to give and which this engine drew
                    // without ever saying. The correlogram was labelled "a x b"
                    // and the reader was left to find the tallest stem by eye
                    // and read its lag off the axis - on a 121-stem plot, with
                    // the neighbouring stems within a few per cent of it.
                    //
                    // The lag of the strongest correlation IS the result: it is
                    // the delay between the two records. Reporting it is the
                    // same standard the fitted engines in this catalogue are
                    // already held to.
                    double peak=0.0; int peakLag=0; bool havePeak=false;
                    for(int lag=-maxLag;lag<=maxLag;++lag){
                        double acc=0.0;
                        for(int i=0;i<n;++i){
                            const int j=i+lag;
                            if(j<0||j>=n) continue;
                            acc+=(a[i]-ma)*(b[j]-mb);
                        }
                        const double r=acc/denom;
                        xc.x.append(double(lag));
                        xc.y.append(r);
                        // By magnitude: a strong ANTI-correlation at a lag is
                        // as much a finding as a positive one, and taking the
                        // maximum rather than the largest absolute value would
                        // report a weak positive peak over a strong negative.
                        // A tie keeps the smaller lag, so the answer does not
                        // depend on the order the lags happen to be walked.
                        if(!havePeak||std::abs(r)>std::abs(peak)+1e-12){
                            peak=r; peakLag=lag; havePeak=true;
                        }
                    }
                    out.series.append(xc);
                    // MARKED, NOT JUST NAMED. Putting the peak into the
                    // correlogram's own label achieved nothing: this figure has
                    // ONE named series, and a legend of one row is suppressed
                    // as a caption - so the number was written somewhere the
                    // reader never sees. A second named series both carries the
                    // number into the legend and puts a mark on the stem it
                    // describes, which is the idiom the elbow's knee and the
                    // beam caustic's waist already use.
                    if(havePeak){
                        PlotSeries mark;
                        mark.label=QStringLiteral("strongest at lag %1, r %2")
                                       .arg(peakLag).arg(peak,0,'g',3);
                        mark.color=in.style.warning;
                        mark.drawLine=false; mark.drawMarkers=true;
                        mark.markerSize=8.0; mark.markerSizeExplicit=true;
                        mark.x={double(peakLag)}; mark.y={peak};
                        out.series.append(mark);
                    }
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("lag"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("cross correlation"),false,-1.0,1.0};
        return out;
    }

    // ---------------------------------------------------- Parallel Coordinates
    // One vertical axis per mapped column, one polyline per row. Each axis is
    // scaled to its own range, because the whole point is to compare the shape
    // of a row across variables whose units have nothing to do with each other.
    if(in.engine==QLatin1String("Parallel Coordinates")||in.engine==QLatin1String("Andrews Curves")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        out.legendVisible=false;
        const int axes=in.series.size();
        if(axes>=2){
            QVector<Bounds> spans;
            for(const PlotSeries& s:in.series) spans.append(boundsOf(s.y));
            int rows=std::numeric_limits<int>::max();
            for(const PlotSeries& s:in.series) rows=qMin(rows,int(s.y.size()));
            // A hundred polylines is already a smear; beyond that the plot is
            // ink rather than information.
            rows=qMin(rows,120);
            const bool andrews=(in.engine==QLatin1String("Andrews Curves"));
            for(int r=0;r<rows;++r){
                PlotSeries line;
                line.label=QString();
                line.color=in.series.at(r%axes).color;
                line.lineWidth=0.8;
                line.opacity=0.55;
                line.drawLine=true; line.drawMarkers=false;
                if(andrews){
                    // Andrews: each row becomes a Fourier series evaluated over
                    // -pi..pi, so similar rows trace similar curves. The
                    // classic definition, with the first term at 1/sqrt(2).
                    constexpr int kSamples=96;
                    for(int i=0;i<kSamples;++i){
                        const double t=-3.14159265358979323846
                                      +2.0*3.14159265358979323846*double(i)/double(kSamples-1);
                        double value=spans[0].scale(in.series.at(0).y.value(r))/std::sqrt(2.0);
                        for(int a=1;a<axes;++a){
                            const double coeff=spans[a].scale(in.series.at(a).y.value(r));
                            value+=(a%2==1)? coeff*std::sin(((a+1)/2)*t)
                                           : coeff*std::cos((a/2)*t);
                        }
                        line.x.append(t); line.y.append(value);
                    }
                }else{
                    for(int a=0;a<axes;++a){
                        line.x.append(double(a));
                        line.y.append(spans[a].scale(in.series.at(a).y.value(r)));
                    }
                }
                if(line.x.size()>=2) out.series.append(line);
            }
            if(!andrews){
                out.xAxis=PlotAxis{QStringLiteral("variable"),false,-0.2,double(axes)-0.8};
                out.yAxis=PlotAxis{QStringLiteral("scaled value"),false,0.0,1.0};
                // THE VARIABLE ON EACH AXIS.
                //
                // The x positions are 0, 1, 2 ... one per mapped column, and
                // the ordinary tick chooser labelled them 0.0, 0.5, 1.0 - half
                // of them at positions where no axis exists. The legend is off
                // here, because one key per ROW would be a hundred and twenty
                // keys, so those numbers were the only thing naming the axes
                // and they named none of them. A parallel-coordinates plot is
                // read by following a line across NAMED axes; without the names
                // it is a shape with no subject.
                for(int a=0;a<axes;++a){
                    out.xAxis.tickValues.append(double(a));
                    out.xAxis.tickLabels.append(in.series.at(a).label);
                }
            }else{
                out.xAxis=PlotAxis{QStringLiteral("t"),false,unsetValue(),unsetValue()};
                out.yAxis=PlotAxis{QStringLiteral("f(t)"),false,unsetValue(),unsetValue()};
            }
        }
        return out;
    }

    // ---------------------------------------------------------- Slope Graph
    // Two states, one line per subject. It exists to show which things went up
    // and which went down, so the two columns are the only x positions there
    // are and the line between them is the whole message.
    if(in.engine==QLatin1String("Slope Graph")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const PlotSeries& before=in.series.at(0);
            const PlotSeries& after=in.series.at(1);
            const int n=qMin(before.y.size(),after.y.size());
            for(int i=0;i<n&&i<60;++i){
                if(!finite(before.y[i])||!finite(after.y[i])) continue;
                PlotSeries slope;
                slope.label=QString();
                // Rising and falling in different colours: the direction is the
                // finding and it should not need tracing by eye.
                slope.color=(after.y[i]>=before.y[i])?in.style.positive:in.style.danger;
                slope.lineWidth=1.1;
                slope.drawLine=true; slope.drawMarkers=true; slope.markerSize=4.0;
                slope.x={0.0,1.0};
                slope.y={before.y[i],after.y[i]};
                out.series.append(slope);
            }
        }
        out.legendVisible=false;
        out.xAxis=PlotAxis{QStringLiteral("state"),false,-0.15,1.15};
        // TWO COLUMNS, AND THEY ARE THE TWO MAPPED COLUMNS.
        //
        // There are exactly two x positions in a slope graph and the ordinary
        // tick chooser put six numbers along the axis - 0.0, 0.2, 0.4 and so on
        // - at positions where nothing is drawn. The legend is off, because one
        // key per subject would be sixty keys, so the reader had no way at all
        // to tell which end was "before".
        if(in.series.size()>=2){
            out.xAxis.tickValues={0.0,1.0};
            out.xAxis.tickLabels={in.series.at(0).label,in.series.at(1).label};
        }
        return out;
    }

    // ------------------------------------------------------------ Waterfall
    // Each value as a step from where the last one left off, so a running total
    // is built and broken down in the same picture. The final bar is the total
    // itself, from zero.
    if(in.engine==QLatin1String("Waterfall")){
        PlotSpec out=in;
        // Was "Error Bar", which reads only series[0] as values and series[1]
        // as their uncertainty - so forty one-point step series collapsed into
        // a single drawn point. A floating bar is what a waterfall step is.
        out.engine=QStringLiteral("Floating Bar");
        out.series.clear();
        out.legendVisible=false;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.isEmpty()) continue;
            double running=0.0;
            for(int i=0;i<v.size()&&i<40;++i){
                PlotSeries step;
                step.label=QString();
                step.color=(v[i]>=0)?in.style.positive:in.style.danger;
                // Carried as {centre, low, high} the way the error bar draw
                // already reads, so a floating bar needs no new geometry.
                const double lo=qMin(running,running+v[i]);
                const double hi=qMax(running,running+v[i]);
                step.x={double(i+1)};
                step.y={lo,hi};
                out.series.append(step);
                running+=v[i];
            }
            PlotSeries total;
            total.label=QStringLiteral("total");
            total.color=in.style.foreground;
            const double lo=qMin(0.0,running), hi=qMax(0.0,running);
            total.x={double(qMin(v.size(),40)+1)};
            total.y={lo,hi};
            out.series.append(total);
            break;
        }
        out.xAxis=PlotAxis{QStringLiteral("step"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------- Confidence Ellipse
    // The scatter with the covariance ellipse that contains 95% of a bivariate
    // normal with the same moments. Drawn from the eigenvectors, which is where
    // the orientation and the two radii come from.
    if(in.engine==QLatin1String("Confidence Ellipse")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const PlotSeries& xs=in.series.at(0);
            const PlotSeries& ys=in.series.at(1);
            const int n=qMin(xs.y.size(),ys.y.size());
            PlotSeries pts;
            pts.label=QStringLiteral("observations");
            pts.color=xs.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=3.6;
            QVector<double> vx,vy;
            for(int i=0;i<n;++i){
                if(!finite(xs.y[i])||!finite(ys.y[i])) continue;
                pts.x.append(xs.y[i]); pts.y.append(ys.y[i]);
                vx.append(xs.y[i]); vy.append(ys.y[i]);
            }
            if(vx.size()>=3){
                out.series.append(pts);
                const Moments2 m=momentsOf(vx,vy);
                const double mx=m.mx, my=m.my;
                const double d=double(m.n-1);
                const double sxx=m.sxx/d, syy=m.syy/d, sxy=m.sxy/d;
                // Eigenvalues of a symmetric 2x2, in closed form.
                const double tr=sxx+syy, det=sxx*syy-sxy*sxy;
                const double disc=std::sqrt(qMax(0.0,tr*tr/4.0-det));
                const double l1=tr/2.0+disc, l2=tr/2.0-disc;
                const double angle=(std::abs(sxy)<1e-15)?0.0:std::atan2(l1-sxx,sxy);
                // chi-square with two degrees of freedom, 95%.
                const double k=std::sqrt(5.991);
                PlotSeries ellipse;
                ellipse.label=QStringLiteral("95% ellipse");
                ellipse.color=in.style.warning;
                ellipse.lineWidth=1.3;
                ellipse.drawLine=true; ellipse.drawMarkers=false;
                for(int i=0;i<=96;++i){
                    const double t=2.0*3.14159265358979323846*double(i)/96.0;
                    const double ex=k*std::sqrt(qMax(0.0,l1))*std::cos(t);
                    const double ey=k*std::sqrt(qMax(0.0,l2))*std::sin(t);
                    ellipse.x.append(mx+ex*std::cos(angle)-ey*std::sin(angle));
                    ellipse.y.append(my+ex*std::sin(angle)+ey*std::cos(angle));
                }
                out.series.append(ellipse);
            }
            out.xAxis.label=xs.label;
            out.yAxis.label=ys.label;
        }
        return out;
    }

    // ------------------------------------------------------------ Ridgeline
    // Stacked densities, each on its own baseline. It reads as a set of
    // distributions changing across groups, which overlaying them on one axis
    // does not.
    if(in.engine==QLatin1String("Ridgeline")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        int row=0;
        // The offset is a fraction of the tallest ridge, so the overlap looks
        // the same whatever the densities happen to be.
        QVector<PlotSeries> curves;
        double tallest=0.0;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<5) continue;
            std::sort(v.begin(),v.end());
            const KdeCurve kde=kdeCurve(v,128,3.0);
            if(!kde.valid) continue;
            PlotSeries curve;
            curve.label=s.label; curve.color=s.color;
            curve.lineWidth=1.2; curve.drawLine=true; curve.drawMarkers=false;
            curve.x=kde.pos;
            curve.y=kde.density;
            tallest=qMax(tallest,kde.peak);
            curves.append(curve);
        }
        for(PlotSeries& curve:curves){
            const double offset=double(row)*tallest*0.55;
            for(double& y:curve.y) y+=offset;
            out.series.append(curve);
            ++row;
        }
        out.yAxis=PlotAxis{QStringLiteral("density, offset per group"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Population Pyramid
    // Two groups back to back on a shared category axis, one drawn negative.
    if(in.engine==QLatin1String("Population Pyramid")){
        PlotSpec out=derivedAs(in,QStringLiteral("Horizontal Bar"));
        if(in.series.size()>=2){
            for(int side=0;side<2;++side){
                const PlotSeries& s=in.series.at(side);
                for(int i=0;i<s.y.size()&&i<40;++i){
                    if(!finite(s.y[i])) continue;
                    PlotSeries bar;
                    bar.label=(i==0)?s.label:QString();
                    bar.color=s.color;
                    // The left-hand group is negated so the two grow apart from
                    // a shared spine, which is what makes it a pyramid. Value
                    // on x, category row on y - the order Horizontal Bar reads.
                    bar.x={(side==0)?-std::abs(s.y[i]):std::abs(s.y[i])};
                    bar.y={double(i+1)};
                    out.series.append(bar);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("count"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("group"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------------- Ternary Scatter
    // Three components summing to a whole, projected onto the triangle. Each
    // row is normalised first: compositions that do not sum to one are the
    // normal case, not an error.
    if(in.engine==QLatin1String("Ternary Scatter")){
        PlotSpec out=in;
        // Line Chart, not Scatter: the scatter draw only marks points, and the
        // triangle that makes the projection legible is a line. The line chart
        // honours both flags per series, so one target draws both.
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
        out.legendVisible=false;
        if(in.series.size()>=3){
            const PlotSeries& a=in.series.at(0);
            const PlotSeries& b=in.series.at(1);
            const PlotSeries& c=in.series.at(2);
            const int n=qMin(a.y.size(),qMin(b.y.size(),c.y.size()));
            PlotSeries pts;
            pts.label=QStringLiteral("composition");
            pts.color=a.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            for(int i=0;i<n;++i){
                if(!finite(a.y[i])||!finite(b.y[i])||!finite(c.y[i])) continue;
                const double total=std::abs(a.y[i])+std::abs(b.y[i])+std::abs(c.y[i]);
                if(!(total>0.0)) continue;
                const double pa=std::abs(a.y[i])/total;
                const double pc=std::abs(c.y[i])/total;
                // Standard barycentric placement on an equilateral triangle.
                pts.x.append(pa+pc/2.0);
                pts.y.append(pc*std::sqrt(3.0)/2.0);
            }
            if(!pts.x.isEmpty()) out.series.append(pts);

            // AND THE THREE CORNERS ARE NAMED.
            //
            // The composition names were put in the triangle's legend label -
            // on a figure whose legend is switched off two lines above, so
            // they appeared nowhere at all. A ternary diagram without its
            // corners named is a scatter of dots in a triangle: the reader can
            // see that a point is near one vertex and has no way to learn
            // which of the three components that vertex is.
            //
            // Annotations rather than a legend, because a corner label belongs
            // AT the corner - the whole reading is "how close to this vertex",
            // and a key in the top right makes the reader hold three names in
            // their head while they look.
            {
                const auto corner=[&out](double x,double y,const QString& text,
                                         double dx,double dy){
                    if(text.isEmpty()) return;
                    PlotAnnotation note;
                    note.x=x; note.y=y; note.text=text;
                    note.offsetX=dx; note.offsetY=dy; note.leader=false;
                    note.derived=true;
                    out.annotations.append(note);
                };
                // Offsets in points: negative Y is up. The corner labels sit
                // OUTSIDE the triangle, which is where the room had to be made
                // for them - the axis limits below were -0.06 to 1.06, tight
                // enough that a label placed outside fell past the clip
                // rectangle and was not drawn at all. The first version of
                // this put three annotations on the figure and none of them
                // appeared, which looks exactly like the feature not existing.
                corner(0.0,0.0,b.label,-10.0,14.0);                 // lower left
                corner(1.0,0.0,a.label,-10.0,14.0);                 // lower right
                corner(0.5,std::sqrt(3.0)/2.0,c.label,-12.0,-6.0);  // apex
            }

            // The triangle itself, so the projection is legible as one.
            PlotSeries frame;
            frame.label=QStringLiteral("%1 / %2 / %3").arg(a.label,b.label,c.label);
            frame.color=in.style.gridColor;
            frame.lineWidth=0.9; frame.drawLine=true; frame.drawMarkers=false;
            frame.x={0.0,1.0,0.5,0.0};
            frame.y={0.0,0.0,std::sqrt(3.0)/2.0,0.0};
            out.series.append(frame);
        }
        // Room for the corner names, outside the triangle on all three sides.
        out.xAxis=PlotAxis{QString(),false,-0.14,1.14};
        out.yAxis=PlotAxis{QString(),false,-0.16,1.00};
        // See PlotSpec::framed. The three axes of a ternary diagram run along
        // the triangle's own edges; a rectangular frame around it is a second
        // set of axes measuring numbers that are in no part of the reading.
        out.framed=false;
        return out;
    }

    // --------------------------------------------------------------- Patch
    // A closed filled polygon from the mapped coordinates: a region rather than
    // a trace. Used for boundaries, footprints and shaded domains.
    if(in.engine==QLatin1String("Patch")){
        PlotSpec out=derivedAs(in,QStringLiteral("Area"));
        for(const PlotSeries& s:in.series){
            PlotSeries patch=s;
            // Closed explicitly, or the fill runs to the baseline instead of
            // back to where the outline started.
            if(patch.x.size()>=3){
                patch.x.append(patch.x.first());
                patch.y.append(patch.y.first());
            }
            patch.opacity=0.55;
            out.series.append(patch);
        }
        return out;
    }

    // ------------------------------------------------- Comet / Animated Line
    // Both animate in GraphVis 17. A still figure cannot animate, so what is
    // drawn is the state these settle into: the whole trace, with the head
    // marked. Saying that plainly is better than drawing a static plot and
    // calling it an animation.
    // Two ways of showing that a path has an ORDER, in a figure that cannot
    // move.
    //
    // Both used to produce a half-transparent trail with a marker on the end,
    // so they were one figure with two names. Neither name is wrong, but they
    // answer different questions, and a still frame can serve both:
    //
    //   a COMET is where the thing is NOW - one bright head, and a trail that
    //   fades behind it so the recent past is distinguishable from the distant
    //   past. The reader wants the current position and the direction it came
    //   from.
    //
    //   an ANIMATED LINE is the whole sequence - it is a play button in the
    //   interactive program, and on paper what has to survive is WHEN. So it
    //   keeps the line at full strength and marks it at intervals, each mark
    //   labelled with its position in the record, which is the information the
    //   animation would have carried in time.
    if(in.engine==QLatin1String("Comet")||in.engine==QLatin1String("Animated Line")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        const bool comet=in.engine==QLatin1String("Comet");
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<2) continue;

            if(comet){
                // The fade, in segments. One series at half opacity is a trail
                // of uniform age, which shows the path and not the recency -
                // and recency is the only thing the head is meaningful against.
                constexpr int kSegments=6;
                for(int seg=0;seg<kSegments;++seg){
                    const int from=int(qint64(seg)*n/kSegments);
                    const int to=qMin(n,int(qint64(seg+1)*n/kSegments)+1);
                    if(to-from<2) continue;
                    PlotSeries piece;
                    piece.label=(seg==kSegments-1)?s.label:QString();
                    piece.color=s.color;
                    piece.lineWidth=s.lineWidth;
                    piece.drawLine=true; piece.drawMarkers=false;
                    piece.opacity=0.12+0.78*(double(seg)+1.0)/double(kSegments);
                    for(int i=from;i<to;++i){ piece.x.append(s.x[i]); piece.y.append(s.y[i]); }
                    out.series.append(piece);
                }
                PlotSeries head;
                head.label=QStringLiteral("%1 (now)").arg(s.label);
                head.color=s.color;
                head.drawLine=false; head.drawMarkers=true; head.markerSize=7.0;
                head.x={s.x[n-1]}; head.y={s.y[n-1]};
                out.series.append(head);
            }else{
                PlotSeries path=s;
                path.drawLine=true; path.drawMarkers=false;
                out.series.append(path);

                // Ten steps along the record, each one named. Ten because the
                // marks have to be countable at a glance and legible at 89 mm;
                // a mark per sample is the line again.
                constexpr int kSteps=10;
                PlotSeries marks;
                marks.label=QStringLiteral("%1 (steps)").arg(s.label);
                marks.color=s.color;
                marks.drawLine=false; marks.drawMarkers=true; marks.markerSize=5.0;
                for(int step=0;step<=kSteps;++step){
                    const int i=qBound(0,int(qint64(step)*(n-1)/kSteps),n-1);
                    if(!finite(s.x[i])||!finite(s.y[i])) continue;
                    marks.x.append(s.x[i]); marks.y.append(s.y[i]);
                    // Only every other one carries a number, or the labels
                    // collide on a path that doubles back on itself.
                    if(step%2) continue;
                    PlotAnnotation note;
                    note.text=QString::number(i+1);
                    note.x=s.x[i]; note.y=s.y[i];
                    note.offsetX=7.0; note.offsetY=-9.0;
                    note.leader=false;
                    note.color=s.color;
                    note.derived=true;
                    out.annotations.append(note);
                }
                if(!marks.x.isEmpty()) out.series.append(marks);
            }
        }
        return out;
    }

    // --------------------------------------------- Scatter + Marginals / Plot Matrix
    // Both are small multiples, and this backend draws one figure. What it can
    // do honestly is the panel that carries the information: the joint scatter
    // for the first, the first pair for the second, each with the correlation
    // stated so the number that a matrix is scanned for is not lost.
    // A PLOT MATRIX IS A MATRIX NOW.
    //
    // It used to share the branch below with Scatter + Marginals and come out
    // as a single scatter of the first pair, on the reasoning that "an n x n
    // grid of panels is n^2 figures and there is one frame". That was true of
    // the frame machinery and not of the canvas: drawScatterMarginals already
    // subdivides the plot area to put a distribution along each edge, and a
    // matrix is the same idea carried to a grid. drawPlotMatrix draws its own
    // panels, so it keeps its engine name and skips the rectangular frame.
    //
    // The correlation it used to compute and show nowhere is now in the upper
    // triangle, one number per pair, which is what a matrix is scanned for.
    if(in.engine==QLatin1String("Plot Matrix")) return in;

    if(in.engine==QLatin1String("Scatter + Marginals")){
        PlotSpec out=derivedAs(in,QStringLiteral("Scatter + Marginals"));
        if(in.series.size()>=2){
            const PlotSeries& xs=in.series.at(0);
            const PlotSeries& ys=in.series.at(1);
            const int n=qMin(xs.y.size(),ys.y.size());
            PlotSeries pts;
            pts.color=xs.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=3.8;
            QVector<double> vx,vy;
            for(int i=0;i<n;++i){
                if(!finite(xs.y[i])||!finite(ys.y[i])) continue;
                pts.x.append(xs.y[i]); pts.y.append(ys.y[i]);
                vx.append(xs.y[i]); vy.append(ys.y[i]);
            }
            const double r=(vx.size()>=3)?pearson(momentsOf(vx,vy)):0.0;
            pts.label=QStringLiteral("r = %1").arg(r,0,'f',3);
            if(!pts.x.isEmpty()) out.series.append(pts);
            out.xAxis.label=xs.label;
            out.yAxis.label=ys.label;
            // THE CORRELATION HAS TO REACH THE READER.
            //
            // It was put in the series label - and the legend draws nothing
            // for a figure with one named series, because a legend of one row
            // is a caption the axes already carry. So the number that is the
            // entire reason this panel stands in for a matrix was computed and
            // then shown nowhere, and the figure was a scatter under a
            // different title.
            //
            // The note under the figure is where a thing like this belongs: it
            // states what the panel IS, which matters more for a plot matrix
            // than for anything else here - a reader who asked for a matrix
            // and got one panel is owed the sentence saying so.
            // The correlation belongs under the figure, not in the series
            // label: a legend of one named series is not drawn, so the number
            // was being computed and shown nowhere.
            if(vx.size()>=3)
                out.figureNote=QStringLiteral("Pearson r = %1.").arg(r,0,'f',3);
        }
        return out;
    }

    // -------------------------------------------------- Function and Implicit
    // These plot a formula rather than a dataset. The expression is compiled
    // once and evaluated over a grid; a default is used when nothing has been
    // typed, so the entry draws something recognisable instead of an empty
    // frame the first time it is opened.
    if(in.engine.startsWith(QLatin1String("Function"))
       ||in.engine.startsWith(QLatin1String("Implicit"))){
        PlotSpec out=in;
        const bool parametric=in.engine==QLatin1String("Function 3D Parametric");
        // Implicit Surface has left this set: it is a zero level set in three
        // dimensions now, not a height field, and it returns above.
        const bool surface=in.engine==QLatin1String("Function Surface")
                         ||in.engine==QLatin1String("Function Mesh");
        const bool contour=in.engine==QLatin1String("Function Contour")
                         ||in.engine==QLatin1String("Implicit Function");

        QString text=in.expression.trimmed();
        if(text.isEmpty()){
            if(parametric)     text=QStringLiteral("cos(t); sin(t); t/6");
            else if(surface||contour) text=QStringLiteral("sin(x)*cos(y)");
            else               text=QStringLiteral("sin(x)*exp(-x/6)");
        }

        // The domain follows the mapped data when there is any, so a formula
        // can be overlaid on a measurement rather than plotted beside it.
        double lo=-10.0, hi=10.0;
        if(!in.series.isEmpty()){
            const Bounds span=boundsOf(in.series.first().y);
            if(span.hi>span.lo){ lo=span.lo; hi=span.hi; }
        }

        out.series.clear();
        out.legendVisible=false;

        if(parametric){
            const QStringList partsText=text.split(QLatin1Char(';'));
            Expression fx,fy,fz;
            const QStringList vars{QStringLiteral("t")};
            if(partsText.size()>=3&&fx.compile(partsText.at(0),vars)
               &&fy.compile(partsText.at(1),vars)&&fz.compile(partsText.at(2),vars)){
                PlotSeries px,py,pz;
                px.label=QStringLiteral("x(t)"); py.label=QStringLiteral("y(t)");
                pz.label=QStringLiteral("z(t)");
                px.y.reserve(600); py.y.reserve(600); pz.y.reserve(600);
                QVector<double> at(1);
                for(int i=0;i<600;++i){
                    at[0]=lo+(hi-lo)*double(i)/599.0;
                    px.y.append(fx.evaluate(at));
                    py.y.append(fy.evaluate(at));
                    pz.y.append(fz.evaluate(at));
                }
                out.series={px,py,pz};
                out.engine=QStringLiteral("3D Line");
            }else{
                out.title=QStringLiteral("%1 — needs three formulas separated by ; (x; y; z)")
                              .arg(in.engine);
            }
            return out;
        }

        // An implicit SURFACE is the zero level set in three dimensions, and
        // until there was a marching-tetrahedra routine there was no honest way
        // to draw one - so it projected z = f(x,y) instead and came out
        // identical to Function Surface. The sweep reported the pair every run
        // and the finding written against it said "needs marching cubes over a
        // volume". There is now one, verified against a sphere.
        //
        // Sampled here and handed to the Isosurface painter as four columns,
        // rather than given a painter of its own: the reconstruction, the
        // depth sort and the projection are all written once and this is the
        // same job with the field coming from a formula instead of a file.
        if(in.engine==QLatin1String("Implicit Surface")){
            QString formula=in.expression.trimmed();
            // Three variables, so the default has to mention all three - the
            // shared default sin(x)*cos(y) has no z in it and its zero set is a
            // pair of planes, which is a poor advertisement for the form.
            const bool defaulted=formula.isEmpty();
            if(defaulted) formula=QStringLiteral("x^2+y^2+z^2-1");

            // The DEFAULT formula brings its own box, and only the default.
            //
            // lo and hi above are the range of the first mapped column, applied
            // to all three of x, y and z. For a formula someone typed over
            // their own measurement that is the point - the comment above says
            // so. For the default it is a region of space chosen by an
            // unrelated column: the sweep fixture's first column runs 2.11 to
            // 5.24, so the unit sphere was sampled in the cube [2.11, 5.24]^3,
            // which it does not reach. The engine then correctly reported that
            // the formula has no zero in range and drew nothing, and the sweep
            // correctly failed the run for an engine that draws nothing.
            //
            // Both halves of that were right and the default was still useless.
            // A default exists so the entry "draws something recognisable
            // instead of an empty frame the first time it is opened", and half
            // a default - a formula with no domain to show it in - does not.
            // Only this engine needs it: a zero LEVEL SET depends on where the
            // box is, while sin(x)*cos(y) oscillates through zero in almost any
            // box, so the other function engines are left alone.
            double boxLo=lo,boxHi=hi;
            if(defaulted){ boxLo=-2.0; boxHi=2.0; }
            Expression f;
            const QStringList vars{QStringLiteral("x"),QStringLiteral("y"),
                                   QStringLiteral("z")};
            if(!f.compile(formula,vars)){
                PlotSpec bad=in;
                bad.title=QStringLiteral("%1 — %2").arg(in.engine,f.error());
                return bad;
            }
            PlotSpec sampled=derivedAs(in,QStringLiteral("Isosurface"));
            sampled.legendVisible=false;
            constexpr int kSide=26;
            PlotSeries sx,sy,sz,sv;
            sx.label=QStringLiteral("x"); sy.label=QStringLiteral("y");
            sz.label=QStringLiteral("z"); sv.label=formula;
            const int total=kSide*kSide*kSide;
            sx.y.reserve(total); sy.y.reserve(total);
            sz.y.reserve(total); sv.y.reserve(total);
            QVector<double> at(3);
            double seenLo=std::numeric_limits<double>::infinity(),seenHi=-seenLo;
            for(int k=0;k<kSide;++k){
                for(int j=0;j<kSide;++j){
                    for(int i=0;i<kSide;++i){
                        const double x=boxLo+(boxHi-boxLo)*double(i)/double(kSide-1);
                        const double y=boxLo+(boxHi-boxLo)*double(j)/double(kSide-1);
                        const double z=boxLo+(boxHi-boxLo)*double(k)/double(kSide-1);
                        at[0]=x; at[1]=y; at[2]=z;
                        const double v=f.evaluate(at);
                        if(!finite(v)) continue;
                        sx.y.append(x); sy.y.append(y); sz.y.append(z); sv.y.append(v);
                        seenLo=qMin(seenLo,v); seenHi=qMax(seenHi,v);
                    }
                }
            }
            sampled.series={sx,sy,sz,sv};
            // The painter takes its level as a FRACTION of the field's range,
            // so zero has to be expressed in those terms. A formula that never
            // changes sign has no zero set, and says so rather than drawing an
            // empty cube.
            if(finite(seenLo)&&finite(seenHi)&&seenHi>seenLo
               &&seenLo<=0.0&&seenHi>=0.0){
                sampled.parameters.insert(QStringLiteral("isoLevel"),
                                      (0.0-seenLo)/(seenHi-seenLo));
            }else{
                sampled.title=QStringLiteral("%1 — %2 has no zero between %3 and %4, "
                                         "so there is no surface to draw")
                              .arg(in.engine,formula)
                              .arg(boxLo,0,'g',3).arg(boxHi,0,'g',3);
                // And then DRAW NOTHING, or the sentence is contradicted by
                // the picture underneath it.
                //
                // Without this the painter fell back to its default level -
                // the middle of the field's range - and drew that surface
                // while the title said there was none. The first gallery run
                // showed exactly that: "x^2+y^2+z^2-1 has no zero between 2.11
                // and 5.24, so there is no surface to draw", over a surface.
                // A figure that contradicts its own caption is worse than
                // either half alone, because now neither can be trusted.
                sampled.series.clear();
            }
            return sampled;
        }

        if(surface||contour){
            Expression f;
            const QStringList vars{QStringLiteral("x"),QStringLiteral("y")};
            if(!f.compile(text,vars)){
                out.title=QStringLiteral("%1 — %2").arg(in.engine,f.error());
                return out;
            }
            const bool implicit=in.engine.startsWith(QLatin1String("Implicit"));
            constexpr int kGrid=90;
            PlotSeries gx,gy,gv;
            gx.label=QStringLiteral("x"); gy.label=QStringLiteral("y");
            gv.label=text;
            gx.y.reserve(kGrid*kGrid); gy.y.reserve(kGrid*kGrid); gv.y.reserve(kGrid*kGrid);
            // Hoisted: `f.evaluate({x,y})` brace-constructs - and therefore
            // heap-allocates - a two-element QVector for every one of the 8100
            // grid points.
            QVector<double> at(2);
            for(int j=0;j<kGrid;++j){
                for(int i=0;i<kGrid;++i){
                    const double x=lo+(hi-lo)*double(i)/double(kGrid-1);
                    const double y=lo+(hi-lo)*double(j)/double(kGrid-1);
                    at[0]=x; at[1]=y;
                    const double v=f.evaluate(at);
                    if(!finite(v)) continue;
                    gx.y.append(x); gy.y.append(y); gv.y.append(v);
                }
            }
            out.series={gx,gy,gv};
            out.engine=(in.engine==QLatin1String("Function Mesh"))    ? QStringLiteral("3D Mesh")
                      :(in.engine==QLatin1String("Function Surface")) ? QStringLiteral("3D Topography / Surface")
                                                                     : QStringLiteral("2D Contour");

            // An implicit plot is the ZERO LEVEL SET - the curve where
            // f(x, y) = 0 - and nothing else. That is the whole definition:
            // x^2 + y^2 - 1 = 0 is a circle, and the level at 0.4 is a
            // different circle that the reader did not ask for.
            //
            // The old comment here said "an implicit curve is the zero level
            // set, which is exactly what the contour engine draws", and that
            // was wrong about drawContour: it draws TEN evenly spaced levels
            // between the data's minimum and maximum. Zero is one of them only
            // by luck, and the other nine are curves of a function nobody
            // asked about. The variable that would have carried the
            // distinction was computed one line above and discarded with
            // Q_UNUSED(implicit), so the intention is on the record - it just
            // never reached the painter.
            //
            // Carried as a parameter rather than by keeping the engine name:
            // the name is what puts an engine into the colour-map set, the
            // grid set, the column-shaped-axes set and the axis-labelling
            // branch, and a fifth entry in each of those to move one boolean
            // is four more places to forget. The "@" marks it as internal -
            // set by a rewrite, read by a painter, never declared to the
            // interface as a control.
            if(implicit&&out.engine==QLatin1String("2D Contour")){
                out.parameters.insert(QStringLiteral("@zeroLevelOnly"),1.0);
                // A formula with no zero in the domain draws an empty frame,
                // and an empty frame with no explanation is the single most
                // confusing thing this program can do. Said here, where the
                // values are already in hand, in the same place a formula that
                // will not compile says so.
                bool anyBelow=false,anyAbove=false;
                for(double v:std::as_const(gv.y)){
                    if(v<0.0) anyBelow=true;
                    else if(v>0.0) anyAbove=true;
                    if(anyBelow&&anyAbove) break;
                }
                if(!(anyBelow&&anyAbove))
                    out.title=QStringLiteral("%1 — %2 has no zero between %3 and %4, "
                                             "so there is no curve to draw")
                                  .arg(in.engine,text)
                                  .arg(lo,0,'g',3).arg(hi,0,'g',3);
            }
            out.xAxis=PlotAxis{QStringLiteral("x"),false,unsetValue(),unsetValue()};
            out.yAxis=PlotAxis{QStringLiteral("y"),false,unsetValue(),unsetValue()};
            return out;
        }

        // Function Plot: y = f(x).
        Expression f;
        if(!f.compile(text,{QStringLiteral("x")})){
            out.title=QStringLiteral("%1 — %2").arg(in.engine,f.error());
            out.engine=QStringLiteral("Line Chart");
            return out;
        }
        PlotSeries curve;
        // The formula, which is this figure's entire content. It reaches the
        // reader through the ORDINATE, which is set to the same text a few
        // lines below - so unlike the other engines whose one named series
        // carried a computed number, nothing here was lost, and a note under
        // the figure would only repeat the axis.
        curve.label=text;
        curve.color=in.series.isEmpty()?QColor(0x66,0x9d,0xdb):in.series.first().color;
        curve.lineWidth=1.5;
        curve.drawLine=true; curve.drawMarkers=false;
        curve.x.reserve(800); curve.y.reserve(800);
        QVector<double> at(1);
        for(int i=0;i<800;++i){
            const double x=lo+(hi-lo)*double(i)/799.0;
            at[0]=x;
            const double y=f.evaluate(at);
            // A pole leaves a gap rather than a vertical stroke across the plot.
            if(!finite(y)) continue;
            curve.x.append(x); curve.y.append(y);
        }
        out.series.append(curve);
        out.engine=QStringLiteral("Line Chart");
        out.xAxis=PlotAxis{QStringLiteral("x"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{text,false,unsetValue(),unsetValue()};
        return out;
    }

    if(in.engine==QLatin1String("Histogram")){
        PlotSpec out=in;
        out.series.clear();
        // ONE column needs no legend - the axis already names it. SEVERAL do,
        // and the legend was off for both: five columns binned into five
        // colours with nothing anywhere saying which colour was which column.
        out.legendVisible=in.series.size()>1;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<2) continue;
            std::sort(v.begin(),v.end());
            const double lo=v.first(), hi=v.last();
            if(!(hi>lo)) continue;
            // Freedman-Diaconis, falling back to Sturges for tiny samples.
            // quantileOf, not v[int(n*0.25)]: the uninterpolated form used here
            // is a different and more biased estimator, so the bin width
            // disagreed with the IQR the box plot draws for the same column.
            const double q1=quantileOf(v,0.25), q3=quantileOf(v,0.75);
            const double iqr=q3-q1;
            int bins;
            if(iqr>0){
                const double width=2.0*iqr/std::cbrt(double(v.size()));
                bins=int(std::ceil((hi-lo)/qMax(width,1e-12)));
            }else{
                bins=int(std::ceil(std::log2(double(v.size()))+1.0));
            }
            bins=qBound(5,bins,120);
            const double width=(hi-lo)/bins;
            QVector<double> counts(bins,0.0);
            for(double x:v){
                int b=int((x-lo)/width);
                counts[qBound(0,b,bins-1)]+=1.0;
            }
            PlotSeries bar;
            bar.label=s.label;
            bar.color=s.color;
            bar.drawLine=false;
            // Overlaid histograms are read THROUGH each other - see drawBar,
            // which stops dividing the bin between them. Opaque, the last
            // column drawn would be the only one visible.
            bar.opacity=(in.series.size()>1)?0.55:1.0;
            for(int i=0;i<bins;++i){ bar.x.append(lo+width*(i+0.5)); bar.y.append(counts[i]); }
            out.series.append(bar);
        }
        out.xAxis=PlotAxis{in.yAxis.label.isEmpty()?in.series.isEmpty()?QString():in.series.first().label:in.yAxis.label,false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("count"),false,0.0,unsetValue()};
        return out;
    }

    if(in.engine==QLatin1String("Box Plot")){
        // One box per mapped series, placed at x = 1..N. The five summary
        // values are carried in y so the draw step needs no extra structure.
        PlotSpec out=in;
        out.series.clear();
        out.legendVisible=false;
        int slot=1;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<5){ ++slot; continue; }
            std::sort(v.begin(),v.end());
            const double q1=quantileOf(v,0.25), med=quantileOf(v,0.5), q3=quantileOf(v,0.75);
            const double iqr=q3-q1;
            double lo=q1-1.5*iqr, hi=q3+1.5*iqr;
            // Whiskers reach the furthest observation inside the fences.
            double wlo=v.first(), whi=v.last();
            for(double x:v){ if(x>=lo){ wlo=x; break; } }
            for(int i=v.size()-1;i>=0;--i){ if(v[i]<=hi){ whi=v[i]; break; } }
            PlotSeries box;
            box.label=s.label;
            box.color=s.color;
            box.x={double(slot)};
            box.y={wlo,q1,med,q3,whi};
            out.series.append(box);
            ++slot;
        }
        out.xAxis=PlotAxis{QStringLiteral("series"),false,0.5,double(slot)-0.5};
        out.yAxis=PlotAxis{in.yAxis.label,false,unsetValue(),unsetValue()};
        // THE COLUMN NAMES, under the boxes they belong to.
        //
        // The legend is off here - one key per box would repeat the axis - so
        // the boxes had no names at all and the axis counted them 1, 2, 3. A
        // box plot is chosen to compare NAMED groups; which distribution is
        // which is not a detail of it.
        //
        // Read from each box's own slot rather than from its position in the
        // list, because a column with fewer than five values is skipped and
        // its slot is still consumed.
        for(const PlotSeries& box:out.series){
            if(box.x.isEmpty()||box.label.isEmpty()) continue;
            out.xAxis.tickValues.append(box.x.first());
            out.xAxis.tickLabels.append(box.label);
        }
        return out;
    }

    // =====================================================================
    // Batch 1 of the catalogue expansion: statistics, model evaluation,
    // meta-analysis, survival and reliability.
    //
    // Every one is a rewrite rather than a painter, for the reason the rest of
    // this file is: each is a transformation of the data followed by geometry
    // that already exists and is already tested. A Lorenz curve is a line, a
    // silhouette plot is a horizontal bar chart, a caterpillar plot is a forest
    // plot with its rows sorted. Writing a bespoke painter for each is how a
    // plotting library ends up with forty subtly different ways to draw a line.
    // =====================================================================

    // ------------------------------------------------------- Lorenz Curve
    // Cumulative share of the total against cumulative share of the
    // population, with the line of perfect equality. The area between them is
    // the Gini coefficient, which is reported in the label because it is the
    // number anybody drawing this actually wants.
    if(in.engine==QLatin1String("Lorenz Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            // Negative values have no cumulative share: a Lorenz curve of
            // incomes that include debts is not defined, and silently treating
            // a debt as a small income would invent a distribution.
            v.erase(std::remove_if(v.begin(),v.end(),[](double d){ return d<0.0; }),v.end());
            if(v.size()<2) continue;
            std::sort(v.begin(),v.end());
            double total=0.0;
            for(double d:v) total+=d;
            if(!(total>0.0)) continue;
            PlotSeries curve;
            curve.color=s.color;
            curve.lineWidth=qMax(1.2,in.style.lineWidth);
            curve.x.append(0.0); curve.y.append(0.0);
            double running=0.0, gini=0.0, prevX=0.0, prevY=0.0;
            for(int i=0;i<v.size();++i){
                running+=v[i];
                const double px=double(i+1)/double(v.size());
                const double py=running/total;
                // Trapezoid under the curve, accumulated as we go, so the Gini
                // costs nothing beyond the walk we are already doing.
                gini+=(px-prevX)*(py+prevY)/2.0;
                prevX=px; prevY=py;
                curve.x.append(px); curve.y.append(py);
            }
            curve.label=QStringLiteral("%1 (Gini %2)")
                            .arg(s.label).arg(1.0-2.0*gini,0,'f',3);
            out.series.append(curve);
        }
        if(!out.series.isEmpty()){
            PlotSeries equality;
            equality.label=QStringLiteral("perfect equality");
            equality.x={0.0,1.0}; equality.y={0.0,1.0};
            equality.color=in.style.gridColor;
            equality.dashPattern={5,4};
            out.series.append(equality);
        }
        out.xAxis=PlotAxis{QStringLiteral("cumulative share of population"),false,0.0,1.0};
        out.yAxis=PlotAxis{QStringLiteral("cumulative share of total"),false,0.0,1.0};
        return out;
    }

    // ------------------------------------------------- Concentration Curve
    // The same construction, but ranked by a SECOND column rather than by the
    // outcome itself: the cumulative share of spending against the cumulative
    // share of people ranked by income. Above the diagonal the outcome favours
    // the poor, below it the rich - which is the whole reading of the figure
    // and is invisible on a Lorenz curve of the outcome alone.
    return std::nullopt;
}

} // namespace graphvis
