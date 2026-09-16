// ============================================================================
// prepareSpecCore, part 1 of 6: "4D / 5D Scatter" through "S-N Fatigue Curve".
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

std::optional<PlotSpec> QtPlotBackend::prepareEngineGroup1(const PlotSpec& in) const {

    // A SCATTER CHOSEN DIRECTLY IS MARKERS, and the backend says so itself.
    //
    // drawScatter now honours a series that asks for a line, because thirteen
    // rewrites append reference lines to it and every one of them was being
    // silently dropped. That makes the flag matter, and it exposed who was
    // setting it: PlotCanvas::buildPlotSeries clears drawLine for this engine,
    // so the application was fine and the sweep - whose fixture leaves the
    // PlotSeries default of true - drew a scatter as a line chart, identical
    // to Line Chart.
    //
    // The backend must not depend on its caller having cleared a flag. A
    // scatter the person CHOSE is markers whatever arrives; a rule appended by
    // a rewrite sets drawLine on that rule alone, and those rewrites never
    // come back through here.
    if(in.engine==QLatin1String("4D / 5D Scatter")){
        PlotSpec out=in;
        for(PlotSeries& s:out.series){ s.drawLine=false; s.drawMarkers=true; }
        return out;
    }

    // AND A BAR CHOSEN DIRECTLY IS BARS, for exactly the same reason.
    //
    // drawBar now honours a series that asks for a line, because Process
    // Capability appends two specification limits to a histogram and they were
    // being drawn as blocks. PlotSeries' default for that flag is TRUE, so a
    // caller that simply maps its columns and hands them over would get a line
    // chart under the name "Bar" - which is the shape of fault the scatter had
    // and was fixed for.
    if(in.engine==QLatin1String("Bar")){
        PlotSpec out=in;
        for(PlotSeries& s:out.series){ s.drawLine=false; s.drawMarkers=false; }
        return out;
    }

    // ------------------------------------------------------- Horizontal Bar
    // A HORIZONTAL BAR CHOSEN DIRECTLY WAS DRAWN SIDEWAYS AND FLAT.
    //
    // drawHorizontalBar reads the VALUE from x and the CATEGORY ROW from y -
    // the axes really are swapped in this chart, and that is deliberate. Three
    // rewrites hand it that shape: Global Sensitivity, Population Pyramid and
    // Silhouette Plot each emit one single-point series per bar.
    //
    // Nothing gave that shape to a Horizontal Bar the person chose from the
    // library. It arrived as an ordinary series - category in x, value in y -
    // so the painter read the category number as the bar's LENGTH and the
    // measurement as the row it belonged to. Eight measurements became eight
    // rows scattered up the plot at hairline height, each as long as its own
    // position in the table. The chart drew, which is why it survived: it was
    // a picture of the data transposed, not of nothing.
    //
    // So the ordinary shape is transposed here, once, and the axes swap with
    // it - the value axis is now x, and it is the one that gets a label.
    //
    // The three rewrites are left alone. Their series carry a single point
    // each, which is what distinguishes an already-transposed spec from one
    // that still needs it; going through this twice would put the chart back
    // the way it was.
    if(in.engine==QLatin1String("Horizontal Bar")){
        bool ordinary=false;
        for(const PlotSeries& s:in.series)
            if(qMin(s.x.size(),s.y.size())>1){ ordinary=true; break; }
        if(ordinary){
            PlotSpec out=derivedAs(in,QStringLiteral("Horizontal Bar"));
            for(const PlotSeries& s:in.series){
                const int n=qMin(s.x.size(),s.y.size());
                for(int i=0;i<n;++i){
                    if(!finite(s.x[i])||!finite(s.y[i])) continue;
                    PlotSeries bar;
                    // One legend key per SERIES, not per bar: eight bars from
                    // one column are one entry, and forty entries all saying
                    // "measured" is a legend nobody can use.
                    bar.label=(i==0)?s.label:QString();
                    bar.color=s.color;
                    bar.opacity=s.opacity;
                    bar.x={s.y[i]};   // the measurement
                    bar.y={s.x[i]};   // the category it belongs to
                    bar.drawLine=false;
                    out.series.append(bar);
                }
            }
            // The axes follow their data across.
            out.xAxis=in.yAxis;
            out.yAxis=in.xAxis;
            return out;
        }
    }

    // ----------------------------------------------------------------- ECDF
    // The empirical distribution: every observation contributes one step of
    // 1/n. Drawn as a staircase because that is what it is - joining the points
    // with straight lines would claim values between observations that were
    // never measured.
    if(in.engine==QLatin1String("ECDF")){
        PlotSpec out=derivedAs(in,QStringLiteral("Stairs"));
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<2) continue;
            std::sort(v.begin(),v.end());
            PlotSeries step;
            step.label=s.label; step.color=s.color; step.dashPattern=s.dashPattern;
            step.lineWidth=s.lineWidth; step.drawLine=true; step.drawMarkers=false;
            for(int i=0;i<v.size();++i){
                step.x.append(v[i]);
                step.y.append(double(i+1)/double(v.size()));
            }
            out.series.append(step);
        }
        out.xAxis=PlotAxis{in.yAxis.label.isEmpty()?QStringLiteral("value"):in.yAxis.label,
                           in.yAxis.log10,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("cumulative fraction"),false,0.0,1.0};
        return out;
    }

    // ------------------------------------------------------------ Q-Q Plot
    // Sample quantiles against the normal quantiles they would have if the
    // sample were normal. The 45-degree line is the hypothesis; departure from
    // it is the finding, so it is drawn rather than left to be imagined.
    if(in.engine==QLatin1String("Q-Q Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        double lo=std::numeric_limits<double>::infinity(),hi=-lo;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<3) continue;
            std::sort(v.begin(),v.end());
            const double mean=meanOf(v), sd=stdevOf(v);
            PlotSeries pts;
            pts.label=s.label; pts.color=s.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            for(int i=0;i<v.size();++i){
                // Blom's plotting position: the standard choice for a normal
                // probability plot, and unbiased for the expected order
                // statistic in a way that (i+1)/n is not.
                const double p=(double(i)+1.0-0.375)/(double(v.size())+0.25);
                const double theoretical=normalQuantile(p);
                if(!finite(theoretical)) continue;
                pts.x.append(theoretical);
                pts.y.append(sd>0.0?(v[i]-mean)/sd:0.0);
                lo=qMin(lo,qMin(theoretical,pts.y.last()));
                hi=qMax(hi,qMax(theoretical,pts.y.last()));
            }
            out.series.append(pts);
        }
        if(finite(lo)&&finite(hi)&&hi>lo){
            PlotSeries diagonal;
            diagonal.label=QStringLiteral("normal");
            diagonal.x={lo,hi}; diagonal.y={lo,hi};
            diagonal.color=in.style.gridColor;
            diagonal.lineWidth=0.9; diagonal.drawLine=true; diagonal.drawMarkers=false;
            diagonal.dashPattern={5,4};
            out.series.append(diagonal);
        }
        out.xAxis=PlotAxis{QStringLiteral("theoretical quantiles"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("standardised sample quantiles"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- KDE Density
    // A Gaussian kernel density estimate, bandwidth by Silverman's rule of
    // thumb. A histogram's shape depends on where the bin edges happen to fall;
    // this does not, which is the whole reason to prefer it.
    if(in.engine==QLatin1String("KDE Density")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<3) continue;
            std::sort(v.begin(),v.end());
            const KdeCurve kde=kdeCurve(v,256,3.0);
            if(!kde.valid) continue;
            PlotSeries curve;
            curve.label=s.label; curve.color=s.color; curve.dashPattern=s.dashPattern;
            curve.lineWidth=qMax(1.2,s.lineWidth);
            curve.drawLine=true; curve.drawMarkers=false;
            curve.x=kde.pos;
            curve.y=kde.density;
            out.series.append(curve);
        }
        out.xAxis=PlotAxis{in.yAxis.label.isEmpty()?QStringLiteral("value"):in.yAxis.label,
                           false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("density"),false,0.0,unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Bland-Altman
    // Agreement between two methods measuring the same thing: the difference
    // against the mean, with the bias and the 95% limits of agreement drawn.
    // Needs exactly two mapped columns, because that is what the method is.
    if(in.engine==QLatin1String("Bland-Altman")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const PlotSeries& a=in.series.at(0);
            const PlotSeries& b=in.series.at(1);
            const int n=qMin(a.y.size(),b.y.size());
            PlotSeries pts;
            pts.label=QStringLiteral("%1 vs %2").arg(a.label,b.label);
            pts.color=a.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            QVector<double> diffs;
            double xLo=std::numeric_limits<double>::infinity(),xHi=-xLo;
            for(int i=0;i<n;++i){
                if(!finite(a.y[i])||!finite(b.y[i])) continue;
                const double mean=(a.y[i]+b.y[i])/2.0;
                const double diff=a.y[i]-b.y[i];
                pts.x.append(mean); pts.y.append(diff);
                diffs.append(diff);
                xLo=qMin(xLo,mean); xHi=qMax(xHi,mean);
            }
            if(!diffs.isEmpty()){
                out.series.append(pts);
                const double bias=meanOf(diffs), sd=stdevOf(diffs);
                // THE NUMBERS, not just the names. The bias and the limits of
                // agreement ARE the result of a Bland-Altman analysis - they
                // are what gets quoted in the paper - and the figure drew three
                // labelled lines and left the reader to measure them off the
                // axis. "A chart that makes you get a ruler out is a chart that
                // gets read wrong" is this catalogue's own standard, stated for
                // the fitted engines; the reference rules were missed by it.
                out.series.append(horizontalRule(bias,xLo,xHi,in.style.foreground,
                                                 QStringLiteral("bias %1")
                                                     .arg(bias,0,'g',4),false));
                out.series.append(horizontalRule(bias+1.96*sd,xLo,xHi,in.style.gridColor,
                                                 QStringLiteral("+1.96 SD %1")
                                                     .arg(bias+1.96*sd,0,'g',4),true));
                out.series.append(horizontalRule(bias-1.96*sd,xLo,xHi,in.style.gridColor,
                                                 QStringLiteral("-1.96 SD %1")
                                                     .arg(bias-1.96*sd,0,'g',4),true));
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("mean of the two methods"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("difference"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ----------------------------------------------------- Control Chart
    // An individuals chart: the series in order, with the centre line and the
    // three-sigma limits. Sigma is estimated from the mean moving range rather
    // than from the standard deviation of all the points, because a process
    // that has already shifted would inflate the latter and hide the shift the
    // chart exists to reveal.
    if(in.engine==QLatin1String("Control Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<4) continue;
            PlotSeries line;
            line.label=s.label; line.color=s.color;
            line.lineWidth=qMax(1.1,s.lineWidth);
            line.drawLine=true; line.drawMarkers=true; line.markerSize=3.2;
            for(int i=0;i<v.size();++i){ line.x.append(double(i+1)); line.y.append(v[i]); }
            out.series.append(line);

            double movingRange=0.0;
            for(int i=1;i<v.size();++i) movingRange+=std::abs(v[i]-v[i-1]);
            movingRange/=double(v.size()-1);
            // d2 for a moving range of two consecutive observations.
            const double sigma=movingRange/1.128;
            const double centre=meanOf(v);
            const double xLo=1.0, xHi=double(v.size());
            // Named AND valued, for the same reason as the Bland-Altman
            // limits above: the centre line and the control limits are the
            // numbers a process engineer writes down.
            out.series.append(horizontalRule(centre,xLo,xHi,in.style.foreground,
                                             QStringLiteral("centre %1")
                                                 .arg(centre,0,'g',4),false));
            out.series.append(horizontalRule(centre+3.0*sigma,xLo,xHi,in.style.gridColor,
                                             QStringLiteral("UCL %1")
                                                 .arg(centre+3.0*sigma,0,'g',4),true));
            out.series.append(horizontalRule(centre-3.0*sigma,xLo,xHi,in.style.gridColor,
                                             QStringLiteral("LCL %1")
                                                 .arg(centre-3.0*sigma,0,'g',4),true));
            break;   // one chart, one characteristic
        }
        out.xAxis=PlotAxis{QStringLiteral("observation"),false,unsetValue(),unsetValue()};
        if(out.yAxis.label.isEmpty()&&!in.series.isEmpty()) out.yAxis.label=in.series.first().label;
        return out;
    }

    // -------------------------------------------------------- ROC Curve
    // Two mapped columns: a score and a binary label. Sweeping the threshold
    // down the sorted scores traces the curve, and the diagonal is the
    // no-information line a classifier has to beat to be worth anything.
    if(in.engine==QLatin1String("ROC Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const PlotSeries& score=in.series.at(0);
            const PlotSeries& truth=in.series.at(1);
            const int n=qMin(score.y.size(),truth.y.size());
            QVector<QPair<double,bool>> rows;
            rows.reserve(n);
            // Anything above the midpoint of the label column counts as
            // positive, so 0/1, 1/2 and true/false all behave.
            double labelLo=std::numeric_limits<double>::infinity(),labelHi=-labelLo;
            for(int i=0;i<n;++i){
                if(!finite(truth.y[i])) continue;
                labelLo=qMin(labelLo,truth.y[i]); labelHi=qMax(labelHi,truth.y[i]);
            }
            const double cut=(finite(labelLo)&&finite(labelHi))?(labelLo+labelHi)/2.0:0.5;
            for(int i=0;i<n;++i){
                if(!finite(score.y[i])||!finite(truth.y[i])) continue;
                rows.append({score.y[i],truth.y[i]>cut});
            }
            std::sort(rows.begin(),rows.end(),[](const QPair<double,bool>& a,const QPair<double,bool>& b){
                return a.first>b.first;
            });
            int positives=0,negatives=0;
            for(const auto& r:rows){ if(r.second) ++positives; else ++negatives; }
            if(positives>0&&negatives>0){
                PlotSeries curve;
                curve.label=QStringLiteral("ROC");
                curve.color=score.color;
                curve.lineWidth=qMax(1.4,score.lineWidth);
                curve.drawLine=true; curve.drawMarkers=false;
                int tp=0,fp=0;
                curve.x.append(0.0); curve.y.append(0.0);
                double auc=0.0; double prevFpr=0.0,prevTpr=0.0;
                for(const auto& r:rows){
                    if(r.second) ++tp; else ++fp;
                    const double fpr=double(fp)/double(negatives);
                    const double tpr=double(tp)/double(positives);
                    auc+=(fpr-prevFpr)*(tpr+prevTpr)/2.0;
                    prevFpr=fpr; prevTpr=tpr;
                    curve.x.append(fpr); curve.y.append(tpr);
                }
                curve.label=QStringLiteral("ROC (AUC %1)").arg(auc,0,'f',3);
                out.series.append(curve);

                PlotSeries chance;
                chance.label=QStringLiteral("chance");
                chance.x={0.0,1.0}; chance.y={0.0,1.0};
                chance.color=in.style.gridColor;
                chance.lineWidth=0.9; chance.drawLine=true; chance.drawMarkers=false;
                chance.dashPattern={5,4};
                out.series.append(chance);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("false positive rate"),false,0.0,1.0};
        out.yAxis=PlotAxis{QStringLiteral("true positive rate"),false,0.0,1.0};
        return out;
    }

    // ------------------------------------------------------ Volcano Plot
    // Effect size against significance. Two mapped columns: a fold change and
    // a p-value. Significance is plotted as -log10(p) so the interesting points
    // are at the top, and the two threshold rules are drawn because a volcano
    // plot without them is just a scatter.
    if(in.engine==QLatin1String("Volcano Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const PlotSeries& effect=in.series.at(0);
            const PlotSeries& pvalue=in.series.at(1);
            const int n=qMin(effect.y.size(),pvalue.y.size());
            PlotSeries below,above;
            below.label=QStringLiteral("not significant");
            below.color=in.style.gridColor;
            below.drawLine=false; below.drawMarkers=true; below.markerSize=3.4;
            above.label=QStringLiteral("significant");
            above.color=effect.color;
            above.drawLine=false; above.drawMarkers=true; above.markerSize=3.8;
            double xLo=std::numeric_limits<double>::infinity(),xHi=-xLo;
            // See negLogFloor: a zero is drawn a decade below the smallest
            // p-value the data actually contains, not at 1e-300.
            const double floorP=negLogFloor(pvalue.y);
            for(int i=0;i<n;++i){
                const double x=effect.y[i];
                double p=pvalue.y[i];
                if(!finite(x)||!finite(p)) continue;
                p=qBound(floorP,p,1.0);
                const double y=-std::log10(p);
                xLo=qMin(xLo,x); xHi=qMax(xHi,x);
                const bool significant=(p<0.05)&&(std::abs(x)>1.0);
                PlotSeries& target=significant?above:below;
                target.x.append(x); target.y.append(y);
            }
            if(!below.x.isEmpty()) out.series.append(below);
            if(!above.x.isEmpty()) out.series.append(above);
            if(finite(xLo)&&finite(xHi)){
                out.series.append(horizontalRule(-std::log10(0.05),xLo,xHi,in.style.gridColor,
                                                 QStringLiteral("p = 0.05"),true));
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("log2 fold change"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("-log10 p"),false,0.0,unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Pareto Front
    // Every solution as a point, with the non-dominated set joined. Assumes
    // both objectives are being minimised, which is the convention the rest of
    // the optimisation literature uses; a maximised objective is negated by the
    // person who mapped it.
    if(in.engine==QLatin1String("Pareto Front")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const PlotSeries& f1=in.series.at(0);
            const PlotSeries& f2=in.series.at(1);
            const int n=qMin(f1.y.size(),f2.y.size());
            QVector<QPointF> points;
            points.reserve(n);
            for(int i=0;i<n;++i)
                if(finite(f1.y[i])&&finite(f2.y[i])) points.append(QPointF(f1.y[i],f2.y[i]));

            PlotSeries all;
            all.label=QStringLiteral("solutions");
            all.color=in.style.gridColor;
            all.drawLine=false; all.drawMarkers=true; all.markerSize=3.2;
            for(const QPointF& pt:points){ all.x.append(pt.x()); all.y.append(pt.y()); }
            if(!all.x.isEmpty()) out.series.append(all);

            // Sort by the first objective, then sweep: a point is on the front
            // if nothing seen so far was already better on the second.
            QVector<QPointF> sorted=points;
            std::sort(sorted.begin(),sorted.end(),[](const QPointF& a,const QPointF& b){
                return a.x()!=b.x()? a.x()<b.x() : a.y()<b.y();
            });
            PlotSeries front;
            front.label=QStringLiteral("Pareto front");
            front.color=f1.color;
            front.lineWidth=qMax(1.5,f1.lineWidth);
            front.drawLine=true; front.drawMarkers=true; front.markerSize=4.2;
            double best=std::numeric_limits<double>::infinity();
            for(const QPointF& pt:sorted){
                if(pt.y()<best){
                    best=pt.y();
                    front.x.append(pt.x()); front.y.append(pt.y());
                }
            }
            if(!front.x.isEmpty()) out.series.append(front);
            out.xAxis.label=f1.label;
            out.yAxis.label=f2.label;
        }
        return out;
    }

    // -------------------------------------------------- Performance Ceiling
    // The upper envelope of what has actually been achieved: points binned
    // along x, the best value in each bin joined. It answers "how good has
    // anything been at this setting", which a scatter alone does not.
    if(in.engine==QLatin1String("Performance Ceiling")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<4) continue;
            PlotSeries cloud;
            cloud.label=s.label;
            cloud.color=in.style.gridColor;
            cloud.drawLine=false; cloud.drawMarkers=true; cloud.markerSize=3.0;
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                cloud.x.append(s.x[i]); cloud.y.append(s.y[i]);
                lo=qMin(lo,s.x[i]); hi=qMax(hi,s.x[i]);
            }
            if(cloud.x.isEmpty()||!(hi>lo)) continue;
            out.series.append(cloud);

            const int bins=qBound(8,int(std::sqrt(double(cloud.x.size()))*1.5),40);
            QVector<double> best(bins,-std::numeric_limits<double>::infinity());
            for(int i=0;i<cloud.x.size();++i){
                const int b=qBound(0,int((cloud.x[i]-lo)/(hi-lo)*bins),bins-1);
                best[b]=qMax(best[b],cloud.y[i]);
            }
            PlotSeries ceiling;
            ceiling.label=QStringLiteral("ceiling");
            ceiling.color=s.color;
            ceiling.lineWidth=qMax(1.5,s.lineWidth);
            ceiling.drawLine=true; ceiling.drawMarkers=false;
            for(int b=0;b<bins;++b){
                if(!finite(best[b])) continue;   // an empty bin is not a zero
                ceiling.x.append(lo+(hi-lo)*(double(b)+0.5)/double(bins));
                ceiling.y.append(best[b]);
            }
            if(ceiling.x.size()>=2) out.series.append(ceiling);
            break;
        }
        return out;
    }

    // ---------------------------------------------------- Global Sensitivity
    // One bar per input, sorted by influence. Horizontal because the labels are
    // variable names and reading those rotated is a needless tax.
    if(in.engine==QLatin1String("Global Sensitivity")){
        PlotSpec out=derivedAs(in,QStringLiteral("Horizontal Bar"));
        out.legendVisible=false;
        QVector<QPair<double,PlotSeries>> ranked;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.isEmpty()) continue;
            // The index is whatever the mapped column holds; if a whole column
            // was mapped rather than a single figure, its mean is the summary.
            ranked.append({std::abs(meanOf(v)),s});
        }
        std::sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){ return a.first<b.first; });
        int slot=1;
        for(const auto& entry:ranked){
            PlotSeries bar;
            bar.label=entry.second.label;
            bar.color=entry.second.color;
            // Horizontal Bar reads the value from x and the category row from
            // y. These were the other way round, which is what put every bar
            // in one band and scaled its length against the wrong axis.
            bar.x={entry.first};
            bar.y={double(slot++)};
            bar.drawLine=false;
            out.series.append(bar);
        }
        out.xAxis=PlotAxis{QStringLiteral("sensitivity index"),false,0.0,unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("input"),false,unsetValue(),unsetValue()};
        // THE INPUT NAMES, on the axis the bars are ranked up.
        //
        // The legend is off here - one key per bar would repeat the axis - so
        // without these the chart said "input" and then numbered its rows 1, 2,
        // 3. Which input is the most influential is the entire content of a
        // sensitivity chart, and it was the one thing the figure did not say.
        for(int i=0;i<out.series.size();++i){
            out.yAxis.tickValues.append(double(i+1));
            out.yAxis.tickLabels.append(out.series.at(i).label);
        }
        return out;
    }

    // ------------------------------------------------ Gompertz H2 Kinetics
    // The modified Gompertz equation is the standard model for cumulative
    // biogas from a batch fermentation:
    //
    //     H(t) = P * exp(-exp(Rm*e/P * (lambda - t) + 1))
    //
    // with P the ultimate yield, Rm the maximum production rate and lambda the
    // lag before production starts. Those three numbers are usually the point
    // of the experiment, so they are fitted here and put in the legend rather
    // than left for the reader to estimate off the curve.
    //
    // Fitted natively by Gauss-Newton on the residuals. The science add-on has
    // a fit.modified_gompertz op that does the same thing with scipy, but a
    // graph that will not draw without an optional Python install is not much
    // of a graph.
    // QStringLiteral, not QLatin1String, and the difference is the whole
    // branch. QLatin1String takes the bytes of the narrow literal and reads
    // each ONE as a Latin-1 character; the source is UTF-8, so the subscript
    // two arrives as the three bytes E2 82 82 and compares as three characters
    // that are not U+2082. The catalogue's name never matched, the ASCII
    // fallback beside it never matched either, and this entire branch was
    // unreachable - so the modified-Gompertz fit never ran. The engine still
    // drew the raw series, which is why the sweep passed it on every build:
    // it puts ink on the page, just without the fitted curve or the P, Rm and
    // lambda the experiment is usually about.
    if(in.engine==QStringLiteral("Gompertz H₂ Kinetics")
       ||in.engine==QStringLiteral("Gompertz H2 Kinetics")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            QVector<double> t,h;
            for(int i=0;i<n;++i)
                if(finite(s.x[i])&&finite(s.y[i])){ t.append(s.x[i]); h.append(s.y[i]); }
            if(t.size()<4) continue;

            PlotSeries measured;
            measured.label=s.label.isEmpty()?QStringLiteral("measured"):s.label;
            measured.color=s.color;
            measured.drawLine=false; measured.drawMarkers=true; measured.markerSize=4.2;
            measured.x=t; measured.y=h;
            out.series.append(measured);

            // Starting values taken from the data itself: the plateau, the
            // steepest observed slope, and the first time anything happened.
            double P=*std::max_element(h.begin(),h.end());
            if(!(P>0.0)) continue;
            double Rm=0.0;
            for(int i=1;i<t.size();++i){
                const double dt=t[i]-t[i-1];
                if(dt>0.0) Rm=qMax(Rm,(h[i]-h[i-1])/dt);
            }
            if(!(Rm>0.0)) Rm=P/qMax(1e-9,t.last()-t.first());
            double lag=t.first();
            for(int i=0;i<h.size();++i){ if(h[i]>0.05*P){ lag=t[i]; break; } }

            auto model=[](double tt,double p,double rm,double lam){
                const double e=2.718281828459045;
                const double inner=rm*e/qMax(1e-12,p)*(lam-tt)+1.0;
                // exp of a large positive number overflows to inf; the model
                // value there is zero, which is what the clamp expresses.
                if(inner>700.0) return 0.0;
                return p*std::exp(-std::exp(inner));
            };

            // Gauss-Newton with a numerical Jacobian and a damping term. Thirty
            // iterations is far more than this three-parameter problem needs and
            // still costs nothing at plot time.
            for(int iter=0;iter<30;++iter){
                double JtJ[3][3]={{0,0,0},{0,0,0},{0,0,0}};
                double Jtr[3]={0,0,0};
                const double step[3]={qMax(1e-6,P*1e-4),qMax(1e-6,Rm*1e-4),1e-4};
                double params[3]={P,Rm,lag};
                for(int i=0;i<t.size();++i){
                    const double residual=h[i]-model(t[i],P,Rm,lag);
                    double J[3];
                    for(int k=0;k<3;++k){
                        double bumped[3]={params[0],params[1],params[2]};
                        bumped[k]+=step[k];
                        J[k]=(model(t[i],bumped[0],bumped[1],bumped[2])
                              -model(t[i],P,Rm,lag))/step[k];
                    }
                    for(int a=0;a<3;++a){
                        Jtr[a]+=J[a]*residual;
                        for(int b=0;b<3;++b) JtJ[a][b]+=J[a]*J[b];
                    }
                }
                for(int k=0;k<3;++k) JtJ[k][k]*=1.0+1e-3;   // Levenberg damping

                // 3x3 solve by Cramer's rule; the matrix is tiny and symmetric.
                const double det=
                    JtJ[0][0]*(JtJ[1][1]*JtJ[2][2]-JtJ[1][2]*JtJ[2][1])
                   -JtJ[0][1]*(JtJ[1][0]*JtJ[2][2]-JtJ[1][2]*JtJ[2][0])
                   +JtJ[0][2]*(JtJ[1][0]*JtJ[2][1]-JtJ[1][1]*JtJ[2][0]);
                if(std::abs(det)<1e-18) break;
                auto solve=[&](int col){
                    double m[3][3];
                    for(int r=0;r<3;++r) for(int c=0;c<3;++c) m[r][c]=(c==col)?Jtr[r]:JtJ[r][c];
                    return (m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])
                           -m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])
                           +m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]))/det;
                };
                const double dP=solve(0), dRm=solve(1), dLag=solve(2);
                if(!finite(dP)||!finite(dRm)||!finite(dLag)) break;
                P+=dP; Rm+=dRm; lag+=dLag;
                P=qMax(1e-9,P); Rm=qMax(1e-12,Rm);
                if(std::abs(dP)<1e-9*qMax(1.0,P)&&std::abs(dRm)<1e-9*qMax(1.0,Rm)
                   &&std::abs(dLag)<1e-9) break;
            }

            // Did the fit actually fit?
            //
            // P and Rm are clamped from below on every iteration, so a
            // Gauss-Newton step that drives either negative - which is what
            // happens on data that is not a saturating curve - pins the
            // parameter on its floor and leaves it there. The legend then
            // printed "Rm 1e-12" in exactly the same form as a real result,
            // and those three numbers are usually the point of the experiment.
            // A reader had no way to tell a measurement from a clamp.
            //
            // Two things are reported instead. The clamp is exact and needs no
            // threshold: a parameter sitting on its floor did not come from the
            // data. And R² is given whatever happens, so a fit that converged
            // to something poor is visible rather than merely quotable.
            const bool pinned=!(P>1e-9)||!(Rm>1e-12);
            double ssRes=0.0, ssTot=0.0, meanH=0.0;
            for(const double v:h) meanH+=v;
            meanH/=double(qMax(1,h.size()));
            for(int i=0;i<t.size()&&i<h.size();++i){
                const double r=h[i]-model(t[i],P,Rm,lag);
                ssRes+=r*r;
                ssTot+=(h[i]-meanH)*(h[i]-meanH);
            }
            const double r2=(ssTot>0.0)?1.0-ssRes/ssTot
                                       :std::numeric_limits<double>::quiet_NaN();

            PlotSeries fit;
            fit.label=pinned
                ? QStringLiteral("fit did not converge (%1 pinned)")
                      .arg(!(Rm>1e-12)?QStringLiteral("Rm"):QStringLiteral("P"))
                : QStringLiteral("P %1  ·  Rm %2  ·  λ %3  ·  R² %4")
                      .arg(P,0,'g',3).arg(Rm,0,'g',3).arg(lag,0,'g',3)
                      .arg(finite(r2)?QString::number(r2,'f',3)
                                     :QStringLiteral("n/a"));
            fit.color=s.color;
            fit.lineWidth=qMax(1.6,s.lineWidth);
            fit.drawLine=true; fit.drawMarkers=false;
            const double t0=t.first(), t1=t.last();
            for(int g=0;g<200;++g){
                const double tt=t0+(t1-t0)*double(g)/199.0;
                fit.x.append(tt);
                fit.y.append(model(tt,P,Rm,lag));
            }
            out.series.append(fit);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("time");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("cumulative H₂");
        return out;
    }

    // ------------------------------------------- sCOD Degradation Profile
    // Substrate concentration against time, with removal as a percentage of the
    // starting value on the same picture. Removal is what gets reported, and
    // deriving it here means the two can never disagree.
    if(in.engine==QLatin1String("sCOD Degradation Profile")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<2) continue;
            PlotSeries conc;
            conc.label=s.label.isEmpty()?QStringLiteral("sCOD"):s.label;
            conc.color=s.color;
            conc.lineWidth=qMax(1.4,s.lineWidth);
            conc.drawLine=true; conc.drawMarkers=true; conc.markerSize=3.6;
            double first=std::numeric_limits<double>::quiet_NaN();
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                if(!finite(first)) first=s.y[i];
                conc.x.append(s.x[i]); conc.y.append(s.y[i]);
            }
            if(conc.x.size()<2) continue;
            out.series.append(conc);

            if(finite(first)&&std::abs(first)>1e-12){
                PlotSeries removal;
                removal.label=QStringLiteral("%1 removal (%)").arg(conc.label);
                removal.color=s.color;
                removal.lineWidth=1.0;
                removal.drawLine=true; removal.drawMarkers=false;
                removal.dashPattern={5,4};
                // A PERCENTAGE IS NOT A CONCENTRATION. On the concentration's
                // own axis the removal curve runs to a hundred while the
                // concentration works in milligrams per litre - or the other
                // way round - and whichever is smaller becomes a flat line
                // along the bottom. The axis then read "sCOD / removal %",
                // naming both and measuring neither.
                removal.secondaryAxis=true;
                for(int i=0;i<conc.x.size();++i){
                    removal.x.append(conc.x[i]);
                    removal.y.append((first-conc.y[i])/first*100.0);
                }
                out.series.append(removal);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("time");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("sCOD");
        out.y2Axis.label=QStringLiteral("removal (%)");
        return out;
    }

    // ------------------------------------------- VFA Concentration Profile
    // Volatile fatty acids accumulate and are consumed in sequence, so what
    // matters is the composition over time rather than any one species. Stacked
    // so the total is readable as the top of the band.
    // The stack, and the TOTAL drawn on it.
    //
    // This used to set the engine to "Stacked Lines" and two default axis
    // names and return, so the two were one figure - and a person can rename
    // an axis themselves, which is not a reason for a second catalogue entry.
    //
    // Total VFA is the number an anaerobic digester is reported on, and it is
    // the one quantity a stack does not actually show: the top of the band is
    // where it is, but there is no line to read it against and no marker on
    // the peak. Drawing it is what the entry is for.
    if(in.engine==QLatin1String("VFA Concentration Profile")){
        PlotSpec out=in;
        out.engine=QStringLiteral("Stacked Lines");
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("time");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("VFA concentration");
        // Marked NOT STACKED, which is what actually keeps it out of the
        // total it is reporting.
        //
        // This used to say that appending it after the species was enough,
        // because drawStackedLines had "already stacked them". It had not -
        // the painter walks spec.series in order and adds every one of them to
        // the running total, this line included. So the band came out at twice
        // its real height and the annotation, which is placed in data
        // coordinates at the true peak, sat halfway down the picture pointing
        // at nothing. On a shaped digester fixture the three species peak at a
        // total of 1,480 mg/L; the figure drew 2,950 and labelled it 1.48e+03.
        if(in.series.size()>=2){
            int n=std::numeric_limits<int>::max();
            for(const PlotSeries& s:in.series) n=qMin(n,qMin(s.x.size(),s.y.size()));
            if(n>=2){
                PlotSeries total;
                total.label=QStringLiteral("total VFA");
                total.color=in.style.warning;
                total.drawLine=true; total.drawMarkers=false;
                total.stacked=false;
                total.lineWidth=qMax(1.4,in.style.lineWidth*1.3);
                double peak=-std::numeric_limits<double>::infinity();
                int peakAt=0;
                for(int i=0;i<n;++i){
                    double sum=0.0;
                    for(const PlotSeries& s:in.series)
                        sum+=finite(s.y[i])?s.y[i]:0.0;
                    total.x.append(in.series.first().x[i]);
                    total.y.append(sum);
                    if(sum>peak){ peak=sum; peakAt=i; }
                }
                out.series.append(total);
                // The peak, named. A VFA profile is read for when the total
                // peaked and how high, and a reader should not have to
                // estimate either off the top of a band.
                if(finite(peak)){
                    PlotAnnotation note;
                    note.text=QStringLiteral("peak total %1").arg(formatMeasured(peak));
                    note.x=in.series.first().x[peakAt];
                    note.y=peak;
                    note.derived=true;
                    out.annotations.append(note);
                }
            }
        }
        return out;
    }

    // --------------------------------------------- Polarisation & Power Curve
    // The standard electrochemical characterisation: voltage falling with
    // current, and the power it implies. Power is computed rather than mapped,
    // because P = IV is not something the reader should have to take on trust
    // from a separate column that may or may not have been kept in step.
    if(in.engine==QLatin1String("Polarisation & Power Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<2) continue;
            PlotSeries voltage;
            voltage.label=s.label.isEmpty()?QStringLiteral("voltage"):s.label;
            voltage.color=s.color;
            voltage.lineWidth=qMax(1.4,s.lineWidth);
            voltage.drawLine=true; voltage.drawMarkers=true; voltage.markerSize=3.6;
            PlotSeries power;
            power.label=QStringLiteral("power");
            power.color=s.color;
            power.lineWidth=1.1;
            power.drawLine=true; power.drawMarkers=false;
            power.dashPattern={5,4};
            // POWER IS NOT A VOLTAGE, and it was drawn on the voltage's axis.
            //
            // P = IV, so on a cell working at a volt and tens of amps the
            // power reaches tens while the voltage stays under one. Sharing an
            // ordinate, the voltage curve - the thing a polarisation plot is
            // FOR - became a flat line along the bottom and the peak power
            // point could not be read off either curve. The axis label said
            // "voltage / power", which is an accurate description of a figure
            // nobody can use.
            power.secondaryAxis=true;
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                voltage.x.append(s.x[i]); voltage.y.append(s.y[i]);
                power.x.append(s.x[i]);   power.y.append(s.x[i]*s.y[i]);
            }
            if(voltage.x.size()<2) continue;
            out.series.append(voltage);
            out.series.append(power);
            break;   // one cell, one curve pair
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("current density");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("voltage");
        out.y2Axis.label=QStringLiteral("power");
        return out;
    }

    // ------------------------------------------------------- EIS: Nyquist
    // Impedance in the complex plane: -Z″ against Z′, so a capacitive
    // semicircle appears above the axis where everyone expects to read it.
    // Two mapped columns, real and imaginary.
    if(in.engine==QLatin1String("EIS: Nyquist")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const PlotSeries& re=in.series.at(0);
            const PlotSeries& im=in.series.at(1);
            const int n=qMin(re.y.size(),im.y.size());
            PlotSeries arc;
            arc.label=QStringLiteral("impedance");
            arc.color=re.color;
            arc.lineWidth=qMax(1.4,re.lineWidth);
            arc.drawLine=true; arc.drawMarkers=true; arc.markerSize=3.4;
            for(int i=0;i<n;++i){
                if(!finite(re.y[i])||!finite(im.y[i])) continue;
                arc.x.append(re.y[i]);
                // Negated by convention. If the column already holds -Z″ this
                // would flip it, so the sign of the majority decides.
                arc.y.append(im.y[i]);
            }
            int negative=0;
            for(double v:arc.y) if(v<0) ++negative;
            if(negative>arc.y.size()/2) for(double& v:arc.y) v=-v;
            if(!arc.x.isEmpty()) out.series.append(arc);
        }
        out.xAxis=PlotAxis{QStringLiteral("Z′ (real)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("−Z″ (imaginary)"),false,unsetValue(),unsetValue()};
        // Both axes are impedance in ohms, and the depressed SEMICIRCLE is
        // what an equivalent circuit is read off. See PlotSpec::equalAspect.
        out.equalAspect=true;
        return out;
    }

    // ---------------------------------------------------------- EIS: Bode
    // Magnitude and phase against frequency, on a log frequency axis because
    // impedance spectra span decades and a linear axis wastes most of the plot
    // on the top one.
    if(in.engine==QLatin1String("EIS: Bode")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const PlotSeries& freq=in.series.at(0);
            const int n=freq.y.size();
            for(int k=1;k<in.series.size();++k){
                const PlotSeries& channel=in.series.at(k);
                PlotSeries curve;
                curve.label=channel.label;
                curve.color=channel.color;
                curve.dashPattern=channel.dashPattern;
                curve.lineWidth=qMax(1.3,channel.lineWidth);
                curve.drawLine=true; curve.drawMarkers=true; curve.markerSize=3.2;
                // A BODE PLOT IS TWO QUANTITIES, and they have never shared
                // an axis anywhere else.
                //
                // Magnitude is an impedance in ohms and phase is an angle in
                // degrees; the first channel is the magnitude and the second
                // is the phase, which is the order every instrument reports
                // them in and the order the axis label already claimed. Drawn
                // together the phase either flattened the magnitude or was
                // flattened by it, and the axis read "|Z| / phase" - an
                // accurate description of a figure nobody can use.
                curve.secondaryAxis=(k>=2);
                const int m=qMin(n,channel.y.size());
                for(int i=0;i<m;++i){
                    if(!finite(freq.y[i])||!finite(channel.y[i])||freq.y[i]<=0) continue;
                    curve.x.append(freq.y[i]);
                    curve.y.append(channel.y[i]);
                }
                if(!curve.x.isEmpty()) out.series.append(curve);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("frequency"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{in.series.size()>1?in.series.at(1).label
                                             :QStringLiteral("|Z|"),
                           false,unsetValue(),unsetValue()};
        out.y2Axis.label=in.series.size()>2?in.series.at(2).label
                                           :QStringLiteral("phase");
        return out;
    }

    // ------------------------------------------------ 1D Marginal Responses
    // How the output moves as each input is swept on its own. The inputs have
    // different units and ranges, so each response is scaled to 0-1 over its own
    // sweep; the shape is the finding, not the magnitude.
    if(in.engine==QLatin1String("1D Marginal Responses")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<2) continue;
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            for(int i=0;i<n;++i) if(finite(s.y[i])){ lo=qMin(lo,s.y[i]); hi=qMax(hi,s.y[i]); }
            if(!finite(lo)||!finite(hi)) continue;
            const double span=hi-lo;
            PlotSeries curve;
            curve.label=s.label;
            curve.color=s.color; curve.dashPattern=s.dashPattern;
            curve.lineWidth=qMax(1.3,s.lineWidth);
            curve.drawLine=true; curve.drawMarkers=false;
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                curve.x.append(s.x[i]);
                // A flat response is drawn at the midline rather than dividing
                // by zero and vanishing.
                curve.y.append(span>0.0?(s.y[i]-lo)/span:0.5);
            }
            out.series.append(curve);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("input");
        out.yAxis=PlotAxis{QStringLiteral("scaled response"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------------------- Swarm
    // Every observation shown, displaced sideways only as far as it must be to
    // stop points overlapping. Unlike a jitter plot the offset is deterministic,
    // so the same data draws the same picture every time - which matters when
    // the picture goes in a paper.
    if(in.engine==QLatin1String("Swarm")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        out.legendVisible=in.series.size()>1;
        int slot=1;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.isEmpty()){ ++slot; continue; }
            std::sort(v.begin(),v.end());
            PlotSeries pts;
            pts.label=s.label; pts.color=s.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;

            // Points close in value share a row and are fanned out around the
            // slot centre. The tolerance is a fraction of the whole spread, so
            // it scales with the data rather than assuming units.
            const double tolerance=(v.last()-v.first())*0.012;
            int i=0;
            while(i<v.size()){
                int j=i+1;
                while(j<v.size()&&tolerance>0.0&&(v[j]-v[i])<=tolerance) ++j;
                const int count=j-i;
                for(int k=0;k<count;++k){
                    const double offset=(count==1)?0.0
                        :((double(k)-(count-1)/2.0)/qMax(1.0,double(count-1)))*0.34;
                    pts.x.append(double(slot)+offset);
                    pts.y.append(v[i+k]);
                }
                i=j;
            }
            out.series.append(pts);
            ++slot;
        }
        out.xAxis=PlotAxis{QStringLiteral("group"),false,0.4,double(slot)-0.4};
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("value");
        return out;
    }

    // ------------------------------------------------------- Violin Plot
    // The distribution's shape rather than its summary. Packed for drawViolin
    // as: x[0] = the slot, then the sample positions; y[0] unused, then the
    // density at each position. Carrying both in the existing two vectors keeps
    // PlotSeries from growing a field that only one engine would ever use.
    if(in.engine==QLatin1String("Violin Plot")||in.engine==QLatin1String("Raincloud")){
        PlotSpec out=in;
        out.series.clear();
        out.legendVisible=in.series.size()>1;
        int slot=1;
        // The observed range, tracked because the y axis has to be told it.
        // See the note where it is applied, below the loop.
        double seenLo=std::numeric_limits<double>::infinity(),seenHi=-seenLo;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<5){ ++slot; continue; }
            std::sort(v.begin(),v.end());
            seenLo=qMin(seenLo,v.first()); seenHi=qMax(seenHi,v.last());
            // No padding: a violin is drawn across the observed range, not
            // three bandwidths past either end of it.
            const KdeCurve kde=kdeCurve(v,96,0.0);
            if(!kde.valid){ ++slot; continue; }

            PlotSeries violin;
            violin.label=s.label;
            violin.color=s.color;
            violin.lineWidth=qMax(0.8,s.lineWidth);
            violin.markerSize=s.markerSize;
            // The slot marker first, then the density profile - the packing
            // drawViolin reads.
            violin.x.append(double(slot));
            violin.y.append(0.0);
            violin.x+=kde.pos;
            violin.y+=kde.density;
            // A raincloud carries the OBSERVATIONS as well, after a NaN that
            // separates them from the density profile. They are the whole
            // reason the form exists - it was invented so a reader can see the
            // measurements rather than only a smoothed curve fitted to them -
            // and a violin discards them.
            //
            // In the same series rather than a second one: the legend draws a
            // row per series, so a second series per group would put a blank
            // row under every name. Only drawRaincloud ever reads this packing;
            // Violin Plot's is unchanged, which is why the NaN is appended here
            // and not in the shared part above.
            //
            // Sorted, so the painter can take the quartiles by index instead of
            // sorting a copy on every repaint.
            if(in.engine==QLatin1String("Raincloud")){
                violin.x.append(std::numeric_limits<double>::quiet_NaN());
                violin.y.append(std::numeric_limits<double>::quiet_NaN());
                violin.x+=v;                             // already sorted above
                violin.y+=QVector<double>(v.size(),0.0);
            }
            out.series.append(violin);
            ++slot;
        }
        out.xAxis=PlotAxis{QStringLiteral("group"),false,0.4,double(slot)-0.4};
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("value");

        // The y axis has to be TOLD the range, because it cannot work it out.
        //
        // This packing puts the sample POSITIONS in `.x` and the DENSITIES in
        // `.y` - drawViolin calls toDevice(f, slot, s.x[i]), so a point's
        // height comes from `.x`. computeRange reads `.y` for the y range, so
        // the axis a violin is measured against was in units of probability
        // density while the violins themselves were drawn in units of the data.
        //
        // The catalogue fixture hid it. Its widest column tops out near 6.9 and
        // its narrowest is a p-value in 0..1, whose density peaks around 8.6 -
        // so the density range happened to CONTAIN the value range and every
        // violin landed on the picture. The only visible symptom was an axis
        // running to 8.6 with nothing drawn above 6.9, which reads as a badly
        // chosen limit rather than as the wrong quantity being measured.
        //
        // A column of pH readings between 6.9 and 7.4 has a density near 2, and
        // would have drawn its violin at y = 7 on an axis ending at 2: off the
        // top of the frame, with the plot area empty and no error anywhere.
        //
        // Set only where the person has not set a limit themselves - unlike the
        // x axis above, which is a slot index and was never theirs to choose.
        if(finite(seenLo)&&finite(seenHi)){
            const double pad=qMax((seenHi-seenLo)*0.05,std::abs(seenHi)*1e-9);
            if(isUnset(out.yAxis.min)) out.yAxis.min=seenLo-pad;
            if(isUnset(out.yAxis.max)) out.yAxis.max=seenHi+pad;
        }
        return out;
    }

    // ------------------------------------------------------- Forest Plot
    // One row per estimate. Three mapped columns - estimate, lower, upper - or
    // one column, in which case the interval is the standard error of the mean
    // and the plot still says something rather than refusing to draw.
    if(in.engine==QLatin1String("Forest Plot")){
        PlotSpec out=in;
        out.series.clear();
        out.legendVisible=false;
        int row=1;
        if(in.series.size()>=3){
            const PlotSeries& est=in.series.at(0);
            const PlotSeries& lo=in.series.at(1);
            const PlotSeries& hi=in.series.at(2);
            const int n=qMin(est.y.size(),qMin(lo.y.size(),hi.y.size()));
            for(int i=0;i<n&&i<40;++i){
                if(!finite(est.y[i])||!finite(lo.y[i])||!finite(hi.y[i])) continue;
                PlotSeries entry;
                entry.label=QStringLiteral("%1").arg(i+1);
                entry.color=est.color;
                entry.x={double(row++)};
                entry.y={est.y[i],lo.y[i],hi.y[i]};
                entry.markerSize=6.0;
                out.series.append(entry);
            }
        }else{
            for(const PlotSeries& s:in.series){
                QVector<double> v=finiteValues(s);
                if(v.size()<2) continue;
                const double mean=meanOf(v);
                const double se=stdevOf(v)/std::sqrt(double(v.size()));
                PlotSeries entry;
                entry.label=s.label;
                entry.color=s.color;
                entry.x={double(row++)};
                entry.y={mean,mean-1.96*se,mean+1.96*se};
                entry.markerSize=6.0;
                out.series.append(entry);
            }
        }
        // THE POOLED ESTIMATE, when asked for. See engineParameters for why it
        // is a choice and not a default: the arithmetic is unambiguous, and
        // where "no effect" sits is not - a ratio has none at 1, a difference
        // has none at 0, and three columns of numbers cannot say which they
        // are. Drawing that line in the wrong place puts every study on the
        // wrong side of it.
        const int measure=int(in.parameter(QStringLiteral("pooled"),0.0)+0.5);
        if(measure>0&&!out.series.isEmpty()){
            // Inverse-variance, with each study's standard error taken from the
            // half-width of its own interval. Studies whose interval has no
            // width carry no information and are skipped rather than given
            // infinite weight.
            double sumW=0.0,sumWX=0.0;
            for(const PlotSeries& e:out.series){
                if(e.y.size()<3) continue;
                const double half=(e.y[2]-e.y[1])*0.5;
                if(!finite(half)||!(std::abs(half)>1e-300)) continue;
                const double se=std::abs(half)/1.96;
                const double w=1.0/(se*se);
                if(!finite(w)) continue;
                sumW+=w; sumWX+=w*e.y[0];
            }
            if(sumW>0.0){
                const double pooled=sumWX/sumW;
                const double se=1.0/std::sqrt(sumW);
                PlotSeries summary;
                summary.label=QStringLiteral("pooled %1 (95% CI %2 to %3)")
                                  .arg(formatMeasured(pooled))
                                  .arg(formatMeasured(pooled-1.96*se))
                                  .arg(formatMeasured(pooled+1.96*se));
                summary.color=in.style.positive;
                summary.x={double(row++)};
                summary.y={pooled,pooled-1.96*se,pooled+1.96*se};
                summary.markerSize=9.0;
                out.series.append(summary);
                // The legend is off on this engine - every study is its own
                // series and a legend of forty numbered rows is noise - so the
                // pooled value is put ON the figure as a note rather than into
                // a legend nobody will see.
                out.figureNote=summary.label;
            }
        }
        // The x axis carries the ESTIMATE now, whatever column the figure was
        // mapped against - so the label is SET rather than defaulted. Left to
        // default it kept the incoming one and printed "time_h" under an axis
        // of effect sizes, which is the same mistake the group-shaped engines
        // made and were fixed for.
        out.xAxis.label=QStringLiteral("effect");
        out.yAxis=PlotAxis{QStringLiteral("study"),false,0.4,double(row)-0.4};
        return out;
    }

    // --------------------------------------------------- Correlation Matrix
    // Every pair of mapped columns, as Pearson r on a grid. Packed as the three
    // columns the field engines read: x index, y index, value.
    if(in.engine==QLatin1String("Correlation Matrix")){
        PlotSpec out=in;
        out.series.clear();
        out.legendVisible=false;
        const int k=in.series.size();
        if(k>=2){
            QVector<QVector<double>> cols;
            QStringList names;
            for(const PlotSeries& s:in.series){ cols.append(s.y); names.append(s.label); }
            PlotSeries xi,yi,vi;
            for(int a=0;a<k;++a){
                for(int b=0;b<k;++b){
                    const Moments2 m=momentsOf(cols[a],cols[b]);
                    if(m.n<2) continue;
                    xi.y.append(double(a)); yi.y.append(double(b)); vi.y.append(pearson(m));
                }
            }
            xi.label=QStringLiteral("i"); yi.label=QStringLiteral("j"); vi.label=QStringLiteral("r");
            out.series={xi,yi,vi};
            out.xAxis=PlotAxis{QStringLiteral("variable"),false,-0.5,double(k)-0.5};
            out.yAxis=PlotAxis{QStringLiteral("variable"),false,-0.5,double(k)-0.5};
            labelMatrixAxes(out,names);
        }
        return out;
    }

    // -------------------------------- 2D Heatmap / 2D Histogram / Hexbin
    // The heatmap family reads x, y and a value from the first three mapped
    // columns. A 2-D histogram has no value column - the count IS the value -
    // so one is synthesised, which is also what makes hexbin density work.
    if(in.engine==QLatin1String("2D Histogram")||in.engine==QLatin1String("Hexbin Density")){
        PlotSpec out=in;
        out.legendVisible=false;
        if(in.series.size()>=2){
            PlotSeries ones;
            ones.label=QStringLiteral("count");
            ones.y.fill(1.0,qMin(in.series.at(0).y.size(),in.series.at(1).y.size()));
            out.series={in.series.at(0),in.series.at(1),ones};
            out.xAxis.label=in.series.at(0).label;
            out.yAxis.label=in.series.at(1).label;
        }
        return out;
    }
    if(in.engine==QLatin1String("2D Heatmap")||in.engine==QLatin1String("2D Contour")){
        PlotSpec out=in;
        out.legendVisible=false;
        if(in.series.size()>=2){
            out.xAxis.label=in.series.at(0).label;
            out.yAxis.label=in.series.at(1).label;
        }
        return out;
    }

    // -------------------------------------------------- 2-D vector fields
    // The same treatment as the heatmap family above, and for the same reason:
    // series 0 and 1 are the POSITIONS, so they are what the axes describe.
    //
    // These were left out when the grid engines got it, which did not show
    // while computeRange was scaling the axes from `.x` - the labels were wrong
    // and so were the numbers, consistently. Fixing the range made the
    // mismatch visible: a divergence map came out with ticks running over the
    // signal column and "time_h" still written underneath them.
    //
    // The legend goes for the same reason it goes on a heatmap. A field is one
    // picture made from four columns, not four series; listing them down the
    // side names four things the reader cannot point at, and it sits on top of
    // the data.
    {
        static const QSet<QString> kVectorFields{
            QStringLiteral("Quiver Field"),QStringLiteral("Feather"),
            QStringLiteral("Stream Field"),QStringLiteral("Stream Particles"),
            QStringLiteral("Phase Portrait"),QStringLiteral("Flow Texture (LIC)"),
            QStringLiteral("Divergence Map"),QStringLiteral("Vorticity Map")};
        if(kVectorFields.contains(in.engine)){
            PlotSpec out=in;
            out.legendVisible=false;
            if(in.series.size()>=2){
                out.xAxis.label=in.series.at(0).label;
                out.yAxis.label=in.series.at(1).label;
            }
            return out;
        }
    }

    // ------------------------------------------------- Polar Line / Scatter
    // Angle in degrees against radius. The projection happens in drawPolar;
    // this only names the axes so the legend and any export read correctly.
    if(in.engine==QLatin1String("Polar Line")||in.engine==QLatin1String("Polar Scatter")){
        PlotSpec out=in;
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("angle (deg)");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("radius");
        return out;
    }

    // ---------------------------------------------------------------------
    // Signal transforms.
    //
    // Each of these is one operation applied to every mapped series, then drawn
    // as a line or a scatter. Expressing them as a table of transforms rather
    // than as twenty near-identical if-blocks keeps the differences visible:
    // what actually distinguishes a rolling mean from a rolling median is one
    // line, and it should look like one line.
    {
        enum class Transform { None, Derivative, Integral, CumulativeSum, RollingMean,
                               RollingMedian, MovingStd, Autocorrelation, Lag,
                               CumulativeHistogram };
        Transform how=Transform::None;
        QString yLabel;
        QString xLabel;
        QString drawAs=QStringLiteral("Line Chart");

        if(in.engine==QLatin1String("Derivative")){        how=Transform::Derivative;       yLabel=QStringLiteral("d/dx"); }
        else if(in.engine==QLatin1String("Integral")){     how=Transform::Integral;         yLabel=QStringLiteral("cumulative integral"); }
        else if(in.engine==QLatin1String("Cumulative Sum")){how=Transform::CumulativeSum;   yLabel=QStringLiteral("cumulative sum"); }
        else if(in.engine==QLatin1String("Rolling Mean")){ how=Transform::RollingMean;      yLabel=QStringLiteral("rolling mean"); }
        else if(in.engine==QLatin1String("Rolling Median")){how=Transform::RollingMedian;   yLabel=QStringLiteral("rolling median"); }
        else if(in.engine==QLatin1String("Moving Std")){   how=Transform::MovingStd;        yLabel=QStringLiteral("moving std"); }
        else if(in.engine==QLatin1String("Autocorrelation")){how=Transform::Autocorrelation;
            yLabel=QStringLiteral("autocorrelation"); xLabel=QStringLiteral("lag"); drawAs=QStringLiteral("Stem"); }
        else if(in.engine==QLatin1String("Lag Plot")){     how=Transform::Lag;
            yLabel=QStringLiteral("value at t"); xLabel=QStringLiteral("value at t−1");
            drawAs=QStringLiteral("4D / 5D Scatter"); }
        else if(in.engine==QLatin1String("Cumulative Histogram")){how=Transform::CumulativeHistogram;
            yLabel=QStringLiteral("cumulative count"); drawAs=QStringLiteral("Stairs");
            // x stops being the x COLUMN here - it becomes the measured value,
            // sorted. Every other transform in this block leaves x alone, so
            // the axis kept the original label and a cumulative count of a
            // concentration was drawn against an axis labelled "time_h".
            xLabel=in.yAxis.label.isEmpty()
                       ?(in.series.isEmpty()?QStringLiteral("value")
                                            :in.series.first().label)
                       :in.yAxis.label; }

        if(how!=Transform::None){
            PlotSpec out=derivedAs(in,drawAs);
            // A window that scales with the series: too small and a rolling
            // statistic is the original with noise, too large and it is a
            // straight line. A twentieth of the sample is the usual compromise.
            for(const PlotSeries& s:in.series){
                const int n=qMin(s.x.size(),s.y.size());
                if(n<3) continue;
                const int window=qBound(3,n/20,201);
                PlotSeries out_s;
                out_s.label=s.label; out_s.color=s.color; out_s.dashPattern=s.dashPattern;
                out_s.lineWidth=qMax(1.2,s.lineWidth);
                out_s.drawLine=(drawAs!=QLatin1String("4D / 5D Scatter"));
                out_s.drawMarkers=(drawAs==QLatin1String("4D / 5D Scatter"));
                out_s.markerSize=3.8;

                switch(how){
                case Transform::Derivative:
                    // Central differences inside, one-sided at the ends: a
                    // forward difference throughout would shift the whole curve
                    // half a sample to the left.
                    for(int i=0;i<n;++i){
                        const int a=qMax(0,i-1), b=qMin(n-1,i+1);
                        const double dx=s.x[b]-s.x[a];
                        if(!finite(dx)||std::abs(dx)<1e-15) continue;
                        out_s.x.append(s.x[i]);
                        out_s.y.append((s.y[b]-s.y[a])/dx);
                    }
                    break;
                case Transform::Integral: {
                    // Trapezoidal, which is exact for the straight segments the
                    // line chart is already drawing between the samples.
                    double acc=0.0;
                    for(int i=0;i<n;++i){
                        if(i>0&&finite(s.y[i])&&finite(s.y[i-1]))
                            acc+=(s.y[i]+s.y[i-1])/2.0*(s.x[i]-s.x[i-1]);
                        out_s.x.append(s.x[i]); out_s.y.append(acc);
                    }
                    break;
                }
                case Transform::CumulativeSum: {
                    double acc=0.0;
                    for(int i=0;i<n;++i){
                        if(finite(s.y[i])) acc+=s.y[i];
                        out_s.x.append(s.x[i]); out_s.y.append(acc);
                    }
                    break;
                }
                case Transform::RollingMean:
                case Transform::RollingMedian:
                case Transform::MovingStd: {
                    for(int i=0;i<n;++i){
                        const int lo=qMax(0,i-window/2), hi=qMin(n-1,i+window/2);
                        QVector<double> w;
                        for(int k=lo;k<=hi;++k) if(finite(s.y[k])) w.append(s.y[k]);
                        if(w.isEmpty()) continue;
                        double value=0.0;
                        if(how==Transform::RollingMean) value=meanOf(w);
                        else if(how==Transform::MovingStd) value=stdevOf(w);
                        else { std::sort(w.begin(),w.end()); value=quantileOf(w,0.5); }
                        out_s.x.append(s.x[i]); out_s.y.append(value);
                    }
                    break;
                }
                case Transform::Autocorrelation: {
                    QVector<double> v=finiteValues(s);
                    if(v.size()<8) break;
                    const double m=meanOf(v);
                    double denom=0.0;
                    for(double x:v) denom+=(x-m)*(x-m);
                    if(!(denom>0.0)) break;
                    const int maxLag=qMin(v.size()/2,60);
                    for(int lag=0;lag<=maxLag;++lag){
                        double acc=0.0;
                        for(int i=lag;i<v.size();++i) acc+=(v[i]-m)*(v[i-lag]-m);
                        out_s.x.append(double(lag));
                        out_s.y.append(acc/denom);
                    }
                    break;
                }
                case Transform::Lag: {
                    QVector<double> v=finiteValues(s);
                    for(int i=1;i<v.size();++i){ out_s.x.append(v[i-1]); out_s.y.append(v[i]); }
                    break;
                }
                case Transform::CumulativeHistogram: {
                    QVector<double> v=finiteValues(s);
                    if(v.size()<2) break;
                    std::sort(v.begin(),v.end());
                    for(int i=0;i<v.size();++i){ out_s.x.append(v[i]); out_s.y.append(double(i+1)); }
                    break;
                }
                default: break;
                }
                if(out_s.x.size()>=2) out.series.append(out_s);
            }
            if(!xLabel.isEmpty()) out.xAxis=PlotAxis{xLabel,false,unsetValue(),unsetValue()};
            out.yAxis=PlotAxis{yLabel,false,unsetValue(),unsetValue()};
            return out;
        }
    }

    // ------------------------------------------- marker and shape variants
    // These differ from an engine already ported only in how the same points
    // are marked. Saying so here is honest and one line each; giving each its
    // own draw function would be four copies of the same loop.
    // ------------------------------------------- Strip Plot / Dot Plot
    //
    // A STRIP plot is a one-dimensional scatter: every observation at its own
    // value, nothing moved. A DOT plot is Wilkinson's: values are binned, and
    // the observations in a bin are STACKED into a column, so the outline of
    // the stacks is a histogram made of countable dots. The number of dots in
    // a column is the count - that is the whole proposition, and it is what a
    // dot plot is chosen over a histogram for.
    //
    // Both used to become a plain scatter with markerSize 4.2, so the dot plot
    // had no stacks and the two were byte-identical. Keeping two catalogue
    // entries that differ only in the name is padding: a person can rename an
    // axis themselves, so an entry has to earn its place by drawing something
    // different.
    // A STRIP PLOT IS ONE COLUMN PER GROUP, not a scatter with smaller dots.
    //
    // It kept the incoming x - the time column, usually - cleared drawLine and
    // set markerSize to 4.2, which makes it 4D / 5D Scatter with one constant
    // changed. Two catalogue entries that differ in a marker size are one
    // entry and a rename, and the person choosing "Strip Plot" from a library
    // of four hundred is asking for the figure seaborn draws under that name:
    // every observation of a group at that group's position, overlaps and all.
    //
    // The jitter is what separates it from Beeswarm, which moves points until
    // none overlap and so distorts where they are. A strip plot spreads them
    // across a fixed band and lets them collide - the density IS the ink. The
    // offset is deterministic, from the point's index, because a figure that
    // redraws differently each time cannot be compared with itself.
    if(in.engine==QLatin1String("Strip Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        int slot=1;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.isEmpty()){ ++slot; continue; }
            PlotSeries pts;
            pts.label=s.label; pts.color=s.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;
            for(int i=0;i<v.size();++i){
                // A low-discrepancy sequence rather than a random number: it
                // fills the band evenly at any sample size and gives the same
                // picture every time.
                const double frac=std::fmod(double(i)*0.6180339887498949,1.0);
                pts.x.append(double(slot)+(frac-0.5)*0.32);
                pts.y.append(v[i]);
            }
            out.series.append(pts);
            ++slot;
        }
        out.xAxis=PlotAxis{QStringLiteral("group"),false,0.4,double(slot)-0.4};
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("value");
        for(const PlotSeries& g:out.series){
            if(g.x.isEmpty()||g.label.isEmpty()) continue;
            out.xAxis.tickValues.append(std::round(g.x.first()));
            out.xAxis.tickLabels.append(g.label);
        }
        return out;
    }
    if(in.engine==QLatin1String("Dot Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        out.legendVisible=in.series.size()>1;
        int slot=1;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.isEmpty()){ ++slot; continue; }
            std::sort(v.begin(),v.end());
            // Bin width by Freedman-Diaconis on the same values, so the stacks
            // are as fine as the sample supports rather than a fixed guess.
            const double iqr=v.at(qMin(v.size()-1,int(v.size()*3/4)))
                            -v.at(int(v.size()/4));
            double width=(iqr>0.0)?2.0*iqr/std::cbrt(double(v.size())):0.0;
            if(!(width>0.0)) width=qMax(1e-9,(v.last()-v.first())/24.0);

            PlotSeries dots;
            dots.label=s.label; dots.color=s.color;
            dots.drawLine=false; dots.drawMarkers=true; dots.markerSize=4.2;
            int i=0;
            while(i<v.size()){
                int j=i+1;
                while(j<v.size()&&(v[j]-v[i])<width) ++j;
                // One column per bin. The dots sit at the BIN's own position,
                // not at each value's, because a column of dots at slightly
                // different heights is not a column and cannot be counted.
                //
                // The height IS THE COUNT - one, two, three - and the y axis
                // says so. The first version stacked from a per-column slot in
                // steps of 0.06, which meant the axis read "count" over numbers
                // that were a slot index plus a fraction: a column of 212 dots
                // reached y = 13.7 and covered the four columns beside it.
                // Being able to count the dots against the axis is the entire
                // reason to draw this rather than a histogram.
                for(int k=0;k<j-i;++k){
                    dots.x.append((v[i]+v[j-1])*0.5);
                    dots.y.append(double(k)+1.0);
                }
                i=j;
            }
            if(!dots.x.isEmpty()) out.series.append(dots);
            ++slot;
        }
        // Several columns share the one count axis and are told apart by
        // colour, which is what the legend is for. Stacking them in separate
        // bands would make the height a slot again.
        out.yAxis=PlotAxis{QStringLiteral("count"),false,0.0,unsetValue()};
        // The x axis carries the VALUE being counted, whatever the x mapping
        // held - this engine does not read it. Set rather than defaulted, or
        // the inherited label puts "time_h" under an axis of measurements.
        out.xAxis=PlotAxis{in.series.size()==1&&!in.series.first().label.isEmpty()
                               ? in.series.first().label
                               : QStringLiteral("value"),
                           false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------ Beeswarm / Swarm
    //
    // Two documented ways of laying out the same points, and they produce
    // visibly different shapes.
    //
    // SWARM fans each band of near-equal values SYMMETRICALLY about the slot
    // centre: it counts how many points share a height and spreads that many
    // evenly across the width. The result is symmetric by construction, like a
    // violin drawn out of dots.
    //
    // BEESWARM places points ONE AT A TIME, each at the nearest free position
    // to the centre line. The shape is asymmetric and grows the way a real
    // swarm does - a lone point sits on the line, a crowded band bulges to
    // whichever side filled up last. That asymmetry is the signature of the
    // form and it is what the R package's default method produces.
    //
    // Both keep every point at its exact value; neither snaps to a row. An
    // earlier version of this comment claimed Swarm moved points off their
    // value, which is simply not what the code above does - it appends
    // v[i+k], the observation itself. The distinction is the PLACEMENT RULE,
    // not the heights.
    if(in.engine==QLatin1String("Beeswarm")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        out.legendVisible=in.series.size()>1;
        int slot=1;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.isEmpty()){ ++slot; continue; }
            std::sort(v.begin(),v.end());
            const double span=qMax(1e-12,v.last()-v.first());
            // Two marks collide when they are this close in value. Below that
            // they have to be separated sideways instead.
            const double reach=span*0.012;
            // Half a slot, and no further. Without a ceiling the search walks
            // outward until it finds space, and on a column with two hundred
            // points at the same height it walked to nearly two whole slots -
            // group one's swarm reached into group two's, which is not a
            // crowded swarm, it is a broken axis.
            constexpr double kStep=0.035;
            constexpr double kLimit=0.45;

            PlotSeries pts;
            pts.label=s.label; pts.color=s.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            // ONE REMEMBERED VALUE PER OFFSET, not a scan of everything
            // placed so far.
            //
            // The test was: does any point already placed within `reach` of
            // this value sit within kStep*0.9 of this offset. Every offset is
            // a multiple of kStep and consecutive ones are kStep apart, which
            // is more than kStep*0.9 - so "within kStep*0.9" means "at the
            // SAME offset", and nothing else. And because the values are
            // sorted, the only point at that offset that can still be within
            // reach is the LAST one placed there.
            //
            // So the whole backward scan collapses to one lookup. The answer
            // is identical - this is the same predicate, evaluated directly
            // instead of searched for - and the cost goes from quadratic to
            // linear: 24,000 points per column took 38.5 seconds and now take
            // a few tens of milliseconds. The scan was bounded by `reach` and
            // that felt like enough; it is not, because `reach` is a fraction
            // of the SPAN, so the number of points inside it grows with n and
            // the scan is O(n) per candidate offset per point.
            constexpr int kSteps=int(2.0*(kLimit/kStep))+4;
            QVector<double> lastAt(kSteps,-std::numeric_limits<double>::infinity());
            QVector<bool> used(kSteps,false);
            for(double value:std::as_const(v)){
                double offset=0.0;
                for(int step=0;;++step){
                    // 0, +1, -1, +2, -2 ... out from the centre line, and the
                    // FIRST free one wins - which is what makes the result
                    // asymmetric rather than a symmetric fan.
                    offset=(step==0)?0.0
                          :((step%2)?1.0:-1.0)*kStep*double((step+1)/2);
                    if(std::abs(offset)>kLimit||step>=kSteps){
                        // Full. Accept the overlap on the centre line rather
                        // than pushing into the neighbouring group: a slightly
                        // dense swarm is readable, a swarm in the wrong column
                        // is a lie about which group the point is in.
                        offset=0.0;
                        used[0]=true; lastAt[0]=value;
                        break;
                    }
                    if(!used[step]||value-lastAt[step]>reach){
                        used[step]=true; lastAt[step]=value;
                        break;
                    }
                }
                pts.x.append(double(slot)+offset);
                pts.y.append(value);
            }
            if(!pts.x.isEmpty()) out.series.append(pts);
            ++slot;
        }
        out.xAxis=PlotAxis{QStringLiteral("group"),false,0.4,double(slot)-0.4};
        // The x axis is the GROUP, not whatever column the x mapping held, so
        // the label is set rather than defaulted. Leaving the inherited one
        // put "time_h" under an axis of slot numbers.
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("value");
        return out;
    }

    // ------------------------------------------------------- Data Reduction
    //
    // The same curve with fewer points in it. A logger that wrote a sample a
    // second for a week is two hundred thousand points describing a shape that
    // needs two hundred, and the extra ones cost redraw time, file size and -
    // once the line is thicker than the spacing between them - legibility.
    //
    // Both curves are drawn: the original faint underneath, the reduced one on
    // top, and the legend says how many points each has. A reduction you cannot
    // see the error of is a reduction you should not trust, and the whole point
    // of the picture is to let the tolerance be judged by eye.
    //
    // The tolerance is a fraction of the data's own bounding diagonal rather
    // than an absolute number, because the alternative is a figure that empties
    // itself the moment the y axis is in millivolts instead of volts. Measured
    // on three periods of a noisy sine at 2,000 points, on an 800x550 plot:
    //
    //     variant     fraction   points kept   worst error on screen
    //     "Fine"        0.05%        25.4%          0.5 px
    //     default       0.25%         3.1%          2.4 px
    //     "Coarse"      1.0%          1.0%          8.9 px
    //
    // The default keeps a thirtieth of the points and moves the line by under
    // three pixels, which is why it is the default. That error is measured
    // PERPENDICULAR to the curve rather than vertically, which is also how the
    // eye reads it: on a steep segment the same two pixels amount to a large
    // change in y and are invisible.
    if(in.engine==QLatin1String("Data Reduction")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        const QString v=in.variant.trimmed();
        const double fraction=(v.compare(QLatin1String("Fine"),Qt::CaseInsensitive)==0)   ? 0.0005
                             :(v.compare(QLatin1String("Coarse"),Qt::CaseInsensitive)==0) ? 0.01
                                                                                          : 0.0025;
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            QVector<double> xs,ys;
            xs.reserve(n); ys.reserve(n);
            for(int i=0;i<n;++i)
                if(finite(s.x[i])&&finite(s.y[i])){ xs.append(s.x[i]); ys.append(s.y[i]); }
            if(xs.size()<3) continue;

            double xLo=xs[0],xHi=xs[0],yLo=ys[0],yHi=ys[0];
            for(int i=1;i<xs.size();++i){
                xLo=qMin(xLo,xs[i]); xHi=qMax(xHi,xs[i]);
                yLo=qMin(yLo,ys[i]); yHi=qMax(yHi,ys[i]);
            }
            const double dx=xHi-xLo, dy=yHi-yLo;
            const double diagonal=std::sqrt(dx*dx+dy*dy);
            if(!(diagonal>0.0)) continue;
            // Perpendicular distance is measured in the plot's own units, so x
            // and y are put on comparable footing first - otherwise a series in
            // seconds against microvolts is reduced entirely along one axis.
            const double sx=(dx>0.0)?1.0/dx:0.0, sy=(dy>0.0)?1.0/dy:0.0;
            const double tolerance=fraction*std::sqrt(2.0);

            const QVector<int> keep=douglasPeucker(xs,ys,sx,sy,tolerance);

            PlotSeries original=s;
            original.x=xs; original.y=ys;
            original.drawLine=true; original.drawMarkers=false;
            original.lineWidth=qMax(0.7,s.lineWidth*0.6);
            // Neutral, not the series colour: the whole picture is a
            // comparison, and two curves in the same hue with only a width
            // between them is not one. The original recedes; the reduced curve
            // keeps the series identity and is drawn over it.
            original.color=in.style.foreground;
            original.color.setAlphaF(0.45f);
            original.label=QStringLiteral("%1 — %2 points").arg(s.label).arg(xs.size());

            PlotSeries reduced;
            reduced.color=s.color.isValid()?s.color:in.style.warning;
            reduced.drawLine=true; reduced.drawMarkers=keep.size()<=200;
            reduced.markerSize=3.0;
            reduced.lineWidth=qMax(1.4,s.lineWidth);
            reduced.label=QStringLiteral("reduced — %1 points (%2%)")
                              .arg(keep.size())
                              .arg(100.0*double(keep.size())/double(xs.size()),0,'f',1);
            for(int i:keep){ reduced.x.append(xs[i]); reduced.y.append(ys[i]); }

            out.series.append(original);
            out.series.append(reduced);
        }
        out.xAxis=in.xAxis;
        out.yAxis=in.yAxis;
        return out;
    }

    // -------------------------------------------------------- Interpolation
    //
    // A curve through the points, evaluated on a fine grid, so a handful of
    // measurements can be read at values nobody measured. The variant picks the
    // method, and the methods differ in ways that matter:
    //
    //   "Linear"        straight between neighbours. Never overshoots, never
    //                   smooth. The safe answer.
    //   "Cubic Spline"  natural cubic spline: the smoothest curve through the
    //                   points, second derivative continuous. It CAN overshoot
    //                   - a step in the data becomes a ring around the step -
    //                   and that overshoot is real information about the fit,
    //                   not an artefact to hide.
    //   "Akima"         (the default) local, so a step stays a step: the
    //                   coefficients depend on two neighbours each side rather
    //                   than on the whole series. Much less ringing than a
    //                   spline; slightly less smooth.
    //   "Cosine"        smooth between each pair with no dependence beyond
    //                   them. Cheap, monotone between neighbours.
    //   "Nearest"       piecewise constant. For a categorical or quantised
    //                   quantity, where a value between two samples is a lie.
    //
    // The source points are drawn on top as markers, because the one question
    // asked of an interpolation is where it passed through what was measured.
    if(in.engine==QLatin1String("Interpolation")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        const InterpolationKind how=interpolationFor(in.variant);
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<2) continue;
            // Sorted and de-duplicated by x: every method here needs a
            // single-valued function of x, and a logger that repeats a
            // timestamp would otherwise divide by a zero interval.
            QVector<QPair<double,double>> pairs;
            pairs.reserve(n);
            for(int i=0;i<n;++i)
                if(finite(s.x[i])&&finite(s.y[i])) pairs.append({s.x[i],s.y[i]});
            std::sort(pairs.begin(),pairs.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){return a.first<b.first;});
            QVector<double> xs,ys;
            for(const auto& pr:pairs){
                if(!xs.isEmpty()&&!(pr.first>xs.last())){
                    // Repeated x: average, rather than pick one arbitrarily.
                    ys.last()=(ys.last()+pr.second)/2.0;
                    continue;
                }
                xs.append(pr.first); ys.append(pr.second);
            }
            if(xs.size()<2) continue;

            // Enough samples to look continuous at any figure width, and no
            // more: a thousand points is already finer than the pixels.
            const int grid=qBound(200,xs.size()*8,1000);
            PlotSeries curve;
            curve.color=s.color; curve.drawLine=true; curve.drawMarkers=false;
            curve.lineWidth=qMax(1.3,s.lineWidth);
            curve.label=QStringLiteral("%1 — %2").arg(s.label,interpolationName(how));
            // Coefficients once per series, not once per evaluated point: the
            // spline solve is O(n) and the grid is a thousand samples, so doing
            // it inside the loop would be a thousand solves for one curve.
            const Interpolator it=interpolatorFor(how,xs,ys);
            const double x0=xs.first(), x1=xs.last();
            for(int i=0;i<grid;++i){
                const double x=x0+(x1-x0)*double(i)/double(grid-1);
                const double y=interpolateAt(it,xs,ys,x);
                if(!finite(y)) continue;
                curve.x.append(x); curve.y.append(y);
            }

            PlotSeries knots;
            knots.x=xs; knots.y=ys;
            knots.color=s.color; knots.drawLine=false; knots.drawMarkers=true;
            knots.markerSize=4.5;
            knots.label=QStringLiteral("%1 — measured").arg(s.label);

            if(curve.x.size()>=2) out.series.append(curve);
            out.series.append(knots);
        }
        out.xAxis=in.xAxis;
        out.yAxis=in.yAxis;
        return out;
    }

    if(in.engine==QLatin1String("Connected Scatter")){
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        for(PlotSeries& s:out.series){ s.drawLine=true; s.drawMarkers=true; s.markerSize=4.0; }
        return out;
    }
    if(in.engine==QLatin1String("Lollipop")){
        // A LOLLIPOP IS NOT A STEM PLOT WITH A SLIGHTLY BIGGER DOT.
        //
        // This handed the stem painter a 5pt marker against the stem plot's
        // 4pt one and left the stalk at the default 1.2 - a two-and-a-half
        // pixel dot against a two pixel one, on an identically thin line. The
        // perceptual audit put the two catalogue figures 0.03 grey levels
        // apart out of 255: the same picture under two names, which is the
        // whole of the distinction between the two entries thrown away.
        //
        // The distinction is that a lollipop reads as a HEAD ON A STALK - the
        // value lives in the disc and the stalk only ties it to the baseline -
        // while a stem plot is a line whose end happens to be marked. So: a
        // stalk thick enough to see, and a head about three times the stem
        // plot's, big enough to carry the reading on its own.
        PlotSpec out=in;
        out.engine=QStringLiteral("Stem");
        for(PlotSeries& s:out.series){
            s.drawMarkers=true;
            s.markerSize=qMax(s.markerSize,11.0); s.markerSizeExplicit=true;
            s.lineWidth=qMax(s.lineWidth,3.0);    s.lineWidthExplicit=true;
        }
        return out;
    }
    if(in.engine==QLatin1String("Step Mid")){
        PlotSpec out=in;
        // The step changes half way between samples rather than at one, which
        // is the honest reading when a value is a measurement at a point rather
        // than a level that held until the next one.
        out.engine=QStringLiteral("Stairs");
        out.series.clear();
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<2) continue;
            PlotSeries mid=s;
            mid.x.clear(); mid.y.clear();
            for(int i=0;i<n;++i){
                const double half=(i==0)?(s.x[1]-s.x[0])/2.0:(s.x[i]-s.x[i-1])/2.0;
                mid.x.append(s.x[i]-half);
                mid.y.append(s.y[i]);
            }
            mid.x.append(s.x[n-1]+(s.x[n-1]-s.x[n-2])/2.0);
            mid.y.append(s.y[n-1]);
            out.series.append(mid);
        }
        return out;
    }
    // "Fill Between" keeps its own engine and its own painter. It USED to set
    // out.engine="Area" and return, which made the two entries one behaviour -
    // the same defect as Hexbin Density sharing the square-celled painter, and
    // the same symptom: the sweep's same-picture check found them in one group.
    // See drawFillBetween.
    if(in.engine==QLatin1String("Event Plot")){
        PlotSpec out=in;
        // One raster row per series: every event a tick at its own height, which
        // is how spike trains and log events are read.
        out.engine=QStringLiteral("4D / 5D Scatter");
        out.series.clear();
        int row=1;
        for(const PlotSeries& s:in.series){
            PlotSeries ticks;
            ticks.label=s.label; ticks.color=s.color;
            ticks.drawLine=false; ticks.drawMarkers=true; ticks.markerSize=3.0;
            for(int i=0;i<s.x.size();++i){
                if(!finite(s.x[i])) continue;
                ticks.x.append(s.x[i]); ticks.y.append(double(row));
            }
            if(!ticks.x.isEmpty()) out.series.append(ticks);
            ++row;
        }
        out.yAxis=PlotAxis{QStringLiteral("series"),false,0.4,double(row)-0.4};
        return out;
    }

    // ----------------------------------------------- regression diagnostics
    // Residual, calibration and probability plots all compare a measurement to
    // what a model says it should be, and all three want the reference line
    // drawn or they are just scatters.
    if(in.engine==QLatin1String("Residual Plot")||in.engine==QLatin1String("Calibration Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const PlotSeries& observed=in.series.at(0);
            const PlotSeries& predicted=in.series.at(1);
            const int n=qMin(observed.y.size(),predicted.y.size());
            const bool residual=(in.engine==QLatin1String("Residual Plot"));
            PlotSeries pts;
            pts.label=residual?QStringLiteral("residual"):QStringLiteral("observed vs predicted");
            pts.color=observed.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            for(int i=0;i<n;++i){
                if(!finite(observed.y[i])||!finite(predicted.y[i])) continue;
                pts.x.append(predicted.y[i]);
                pts.y.append(residual?(observed.y[i]-predicted.y[i]):observed.y[i]);
                lo=qMin(lo,qMin(predicted.y[i],pts.y.last()));
                hi=qMax(hi,qMax(predicted.y[i],pts.y.last()));
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                if(residual){
                    out.series.append(horizontalRule(0.0,lo,hi,in.style.foreground,
                                                     QStringLiteral("zero"),false));
                }else if(finite(lo)&&finite(hi)&&hi>lo){
                    PlotSeries ideal;
                    ideal.label=QStringLiteral("perfect agreement");
                    ideal.x={lo,hi}; ideal.y={lo,hi};
                    ideal.color=in.style.gridColor;
                    ideal.lineWidth=0.9; ideal.drawLine=true; ideal.dashPattern={5,4};
                    out.series.append(ideal);
                }
            }
            out.xAxis=PlotAxis{QStringLiteral("predicted"),false,unsetValue(),unsetValue()};
            out.yAxis=PlotAxis{residual?QStringLiteral("residual"):QStringLiteral("observed"),
                               false,unsetValue(),unsetValue()};
        }
        return out;
    }
    // A probability plot is read in PER CENT, which is the difference.
    //
    // It used to relabel itself to "Q-Q Plot" and return, so the two were one
    // engine with two names. They are not. A Q-Q plot has quantiles on both
    // axes and is read by whether the points lie on the 45-degree line. A
    // probability plot puts the CUMULATIVE PROBABILITY on one axis, spaced by
    // the quantiles of the reference distribution - so the ticks read 1, 5, 10,
    // 25, 50, 75, 90, 95, 99 per cent, unevenly spaced, and the reader takes
    // percentiles straight off the axis. Straightness still means the
    // distribution fits; what you can additionally do is read "the 95th
    // percentile is about here", which is the whole reason reliability and
    // hydrology use this form rather than the Q-Q.
    if(in.engine==QLatin1String("Probability Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        out.legendVisible=false;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<3) continue;
            std::sort(v.begin(),v.end());
            PlotSeries pts;
            pts.label=s.label; pts.color=s.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=3.8;
            for(int i=0;i<v.size();++i){
                // Blom's plotting position, which is the one a probability
                // plot is conventionally built on and is very close to
                // unbiased for a normal reference.
                const double p=(double(i)+1.0-0.375)/(double(v.size())+0.25);
                pts.x.append(v[i]);
                // The axis carries probability; the POSITION is its normal
                // quantile, which is what makes a normal sample plot straight.
                pts.y.append(normalQuantile(p));
            }
            if(!pts.x.isEmpty()) out.series.append(pts);
        }
        // The VALUE, not the x mapping: this engine sorts each column and plots
        // the column against its own cumulative probability, so an inherited
        // label describes a column that is not on the axis. It read "time_h"
        // under a scale of measurements.
        out.xAxis=PlotAxis{in.series.size()==1&&!in.series.first().label.isEmpty()
                               ? in.series.first().label
                               : QStringLiteral("value"),
                           false,unsetValue(),unsetValue()};
        // The y axis carries probability. computeFrame reads the parameter
        // below and replaces its ticks with the percentiles the form is read
        // at, each placed at its own quantile.
        out.yAxis=PlotAxis{QStringLiteral("cumulative probability (%)"),
                           false,unsetValue(),unsetValue()};
        out.parameters.insert(QStringLiteral("@probabilityAxisY"),1.0);
        return out;
    }
    if(in.engine==QLatin1String("Manhattan Plot")){
        // Genome-wide significance: -log10(p) along an ordinal axis, with the
        // 5e-8 threshold that the field uses drawn on.
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        // One floor for the whole figure, from every column at once: a
        // per-column floor would draw the same zero at a different height
        // depending on which column it happened to be in.
        QVector<double> allP;
        for(const PlotSeries& s:in.series) allP+=s.y;
        const double floorP=negLogFloor(allP);
        for(const PlotSeries& s:in.series){
            PlotSeries pts;
            pts.label=s.label; pts.color=s.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=3.0;
            const int n=qMin(s.x.size(),s.y.size());
            for(int i=0;i<n;++i){
                if(!finite(s.y[i])) continue;
                const double p=qBound(floorP,s.y[i],1.0);
                pts.x.append(finite(s.x[i])?s.x[i]:double(i));
                pts.y.append(-std::log10(p));
            }
            if(!pts.x.isEmpty()) out.series.append(pts);
        }
        if(!out.series.isEmpty()){
            double lo=out.series.first().x.first(), hi=lo;
            for(const PlotSeries& s:out.series)
                for(double x:s.x){ lo=qMin(lo,x); hi=qMax(hi,x); }
            out.series.append(horizontalRule(-std::log10(5e-8),lo,hi,in.style.gridColor,
                                             QStringLiteral("genome-wide 5e-8"),true));
        }
        out.yAxis=PlotAxis{QStringLiteral("-log10 p"),false,0.0,unsetValue()};
        return out;
    }

    // --------------------------------------------------------- 3-D variants
    // All of these are the projected cube with a different mark on it, so they
    // are named here and drawn by draw3D rather than duplicating the
    // projection, the depth sorting and the bounding box four times over.
    if(in.engine==QLatin1String("3D Bar")||in.engine==QLatin1String("3D Horizontal Bar")
       ||in.engine==QLatin1String("3D Stem")||in.engine==QLatin1String("3D Bubble")
       ||in.engine==QLatin1String("3D Swarm")||in.engine==QLatin1String("Comet 3D")
       ||in.engine==QLatin1String("3D Contour")||in.engine==QLatin1String("Surface + Contours")
       ||in.engine==QLatin1String("Ribbon")){
        PlotSpec out=in;
        if(in.series.size()>=3){
            out.xAxis.label=in.series.at(0).label;
            out.yAxis.label=in.series.at(1).label;
        }
        return out;
    }

    // ------------------------------------------------------- polar variants
    // Angle-and-radius data with different marks and one genuine difference:
    // the histogram family bins the angles first, because a wind rose is a
    // distribution of directions rather than a list of them.
    if(in.engine==QLatin1String("Polar Histogram")||in.engine==QLatin1String("Wind Rose")){
        PlotSpec out=derivedAs(in,QStringLiteral("Polar Histogram"));
        constexpr int kSectors=16;
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            QVector<double> weight(kSectors,0.0);
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])) continue;
                // Degrees, wrapped, so a bearing of 370 lands with 10.
                double angle=std::fmod(s.x[i],360.0);
                if(angle<0) angle+=360.0;
                const int sector=qBound(0,int(angle/360.0*kSectors),kSectors-1);
                // A wind rose weights by magnitude; a plain polar histogram
                // counts. Both are "how much came from this direction".
                weight[sector]+=(in.engine==QLatin1String("Wind Rose")&&finite(s.y[i]))
                                    ? std::abs(s.y[i]) : 1.0;
            }
            PlotSeries sectors;
            sectors.label=s.label; sectors.color=s.color;
            for(int k=0;k<kSectors;++k){
                sectors.x.append((double(k)+0.5)*360.0/double(kSectors));
                sectors.y.append(weight[k]);
            }
            out.series.append(sectors);
        }
        out.xAxis=PlotAxis{QStringLiteral("direction (deg)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{in.engine==QLatin1String("Wind Rose")?QStringLiteral("magnitude")
                                                               :QStringLiteral("count"),
                           false,unsetValue(),unsetValue()};
        return out;
    }
    if(in.engine==QLatin1String("Radar Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("Radar Chart"));
        // One spoke per mapped column, one closed outline per row. Values are
        // scaled per axis, because a radar chart of raw values with different
        // units compares nothing - and scaled onto a reserved inner radius, so
        // the lowest row on an axis is drawn as LEAST rather than as none. See
        // radialScale.
        constexpr double kRadarFloor=0.15;
        const int axes=in.series.size();
        if(axes>=3){
            QVector<Bounds> spans;
            for(const PlotSeries& s:in.series) spans.append(boundsOf(s.y));
            int rows=std::numeric_limits<int>::max();
            for(const PlotSeries& s:in.series) rows=qMin(rows,int(s.y.size()));
            rows=qMin(rows,8);          // more outlines than this is unreadable
            for(int r=0;r<rows;++r){
                PlotSeries outline;
                outline.label=QStringLiteral("row %1").arg(r+1);
                outline.color=in.series.at(r%axes).color;
                outline.lineWidth=1.4;
                for(int a=0;a<axes;++a){
                    outline.x.append(double(a)*360.0/double(axes));
                    outline.y.append(radialScale(spans[a],in.series.at(a).y.value(r),
                                                 kRadarFloor));
                }
                // Closed: the first spoke repeated, or the outline is a fan.
                outline.x.append(0.0);
                outline.y.append(radialScale(spans[0],in.series.at(0).y.value(r),
                                             kRadarFloor));
                out.series.append(outline);
            }
        }
        out.yAxis=PlotAxis{QStringLiteral("scaled value"),false,0.0,1.0};
        // The VARIABLE on each spoke. drawPolar labels the twelve directions in
        // degrees unless the engine names them, and a radar chart's angle is
        // not a direction: 0, 30, 60 told a reader the position of each
        // variable and not which variable it was.
        if(axes>=3)
            for(int a=0;a<axes;++a){
                out.xAxis.tickValues.append(double(a)*360.0/double(axes));
                out.xAxis.tickLabels.append(in.series.at(a).label);
            }
        return out;
    }
    if(in.engine==QLatin1String("Compass")||in.engine==QLatin1String("Polar Bubble")){
        PlotSpec out=in;
        // Compass draws each observation as a spoke from the origin. Polar
        // Bubble marks it, and SIZES the mark from a third column - which is
        // the whole difference between it and Polar Scatter, and which it did
        // not do until drawPolar was given a bubble branch. The columns pass
        // through untouched; the sizing is the painter's business.
        return out;
    }

    // ------------------------------------------------------ matrix variants
    if(in.engine==QLatin1String("Covariance Matrix")||in.engine==QLatin1String("Spy Matrix")){
        PlotSpec out=in;
        out.legendVisible=false;
        const int k=in.series.size();
        if(k>=2){
            const bool spy=(in.engine==QLatin1String("Spy Matrix"));
            QVector<QVector<double>> cols;
            for(const PlotSeries& s:in.series) cols.append(s.y);
            PlotSeries xi,yi,vi;
            for(int a=0;a<k;++a){
                for(int b=0;b<k;++b){
                    if(spy){
                        const int n=qMin(cols[a].size(),cols[b].size());
                        // The sparsity pattern: is this pair ever jointly
                        // non-zero. What a spy plot is for is structure, not
                        // magnitude.
                        double marked=0.0;
                        for(int i=0;i<n;++i)
                            if(finite(cols[a][i])&&finite(cols[b][i])
                               &&std::abs(cols[a][i])>1e-12&&std::abs(cols[b][i])>1e-12){
                                marked=1.0; break;
                            }
                        xi.y.append(double(a)); yi.y.append(double(b)); vi.y.append(marked);
                        continue;
                    }
                    const Moments2 m=momentsOf(cols[a],cols[b]);
                    if(m.n<2) continue;
                    xi.y.append(double(a)); yi.y.append(double(b));
                    vi.y.append(covarianceOf(m));
                }
            }
            xi.label=QStringLiteral("i"); yi.label=QStringLiteral("j");
            vi.label=spy?QStringLiteral("non-zero"):QStringLiteral("covariance");
            out.series={xi,yi,vi};
            out.xAxis=PlotAxis{QStringLiteral("variable"),false,-0.5,double(k)-0.5};
            out.yAxis=PlotAxis{QStringLiteral("variable"),false,-0.5,double(k)-0.5};
            QStringList names;
            for(const PlotSeries& s:in.series) names.append(s.label);
            labelMatrixAxes(out,names);
        }
        return out;
    }

    // ---------------------------------------------------------- geographic
    // Longitude east, latitude north, through a real projection.
    //
    // There is still no basemap and this still does not pretend to be one: the
    // axes are labelled with what they carry so nobody reads a country outline
    // that is not there. What changed is that the coordinates are projected
    // rather than plotted raw. Raw longitude against latitude IS a projection -
    // equirectangular - and it is the one that gets a route, a range ring and
    // the width of a country wrong everywhere except the equator. It remains
    // the default, so every figure already saved is drawn exactly as before,
    // and the catalogue variant chooses another.
    if(in.engine.startsWith(QLatin1String("Geo "))
       ||in.engine==QLatin1String("Great Circle Route")
       ||in.engine==QLatin1String("Ground Track")){
        if(in.series.size()<2) return in;
        const PlotSeries& lonSeries=in.series.at(0);
        const PlotSeries& latSeries=in.series.at(1);
        const MapProjection kind=mapProjectionFor(in.variant);
        const GeoFrame frame=geoFrameFor(kind,lonSeries.y,latSeries.y);
        const int n=qMin(lonSeries.y.size(),latSeries.y.size());

        // A route is the great circle between the points, not the straight line
        // between them: London to Tokyo over the pole is 9,600 km and the line
        // an equirectangular plot draws across Asia is 11,300. Each leg is
        // densified before projection, so the curve is the projection's own.
        QVector<double> lon,lat;
        if(in.engine==QLatin1String("Great Circle Route")){
            for(int i=0;i+1<n;++i){
                if(!finite(lonSeries.y[i])||!finite(latSeries.y[i])) continue;
                if(!finite(lonSeries.y[i+1])||!finite(latSeries.y[i+1])) continue;
                // Steps sized to the leg: a 40 km hop needs no subdivision at
                // all, a transpolar one needs plenty.
                const double metres=greatCircleMetres(lonSeries.y[i],latSeries.y[i],
                                                      lonSeries.y[i+1],latSeries.y[i+1]);
                const int steps=qBound(2,int(metres/25000.0),256);
                for(int k=0;k<steps;++k){
                    double gl=0,gp=0;
                    greatCirclePoint(lonSeries.y[i],latSeries.y[i],
                                     lonSeries.y[i+1],latSeries.y[i+1],
                                     double(k)/double(steps),gl,gp);
                    lon.append(gl); lat.append(gp);
                }
            }
            if(n>0&&finite(lonSeries.y[n-1])&&finite(latSeries.y[n-1])){
                lon.append(lonSeries.y[n-1]); lat.append(latSeries.y[n-1]);
            }
        }else{
            for(int i=0;i<n;++i){ lon.append(lonSeries.y[i]); lat.append(latSeries.y[i]); }
        }

        if(in.engine==QLatin1String("Geo Density")){
            PlotSpec out=derivedAs(in,QStringLiteral("2D Histogram"));
            PlotSeries px,py;
            px.label=projectionXLabel(frame); py.label=projectionYLabel(frame);
            for(int i=0;i<lon.size();++i){
                const QPointF q=projectPoint(frame,lon[i],lat[i]);
                if(!finite(q.x())||!finite(q.y())) continue;
                px.y.append(q.x()); py.y.append(q.y());
            }
            out.series={px,py};
            return prepareSpecCore(out);
        }

        // A BUBBLE MAP'S BUBBLES ARE THE THIRD COLUMN.
        //
        // Geo Bubble drew every point at a fixed 5.5pt - one size for the whole
        // map - so it was a Geo Scatter with larger dots, and the column it was
        // handed to size them by was read by nothing. The perceptual pass put
        // the two figures 0.09 grey levels apart, which is what "the same
        // picture" looks like when one is drawn slightly heavier.
        //
        // Marker size is a property of a SERIES, not of a point, so the honest
        // way to vary it is the way a paper map does: graduated symbols. Five
        // classes across the column's range, one series each, and the legend
        // says what each size is worth - which a per-point radius could not do
        // at all, because there would be nothing to put in the key.
        //
        // Area, not radius. A circle twice the radius is four times the ink,
        // and a reader compares the ink: sizing by the value directly makes a
        // doubling look like a quadrupling. The radius goes as the square root.
        if(in.engine==QLatin1String("Geo Bubble")&&in.series.size()>=3){
            const QVector<double>& mag=in.series.at(2).y;
            double magLo=std::numeric_limits<double>::infinity();
            double magHi=-std::numeric_limits<double>::infinity();
            const int m=qMin(n,mag.size());
            for(int i=0;i<m;++i)
                if(finite(mag[i])){ magLo=qMin(magLo,mag[i]); magHi=qMax(magHi,mag[i]); }
            if(m>0&&finite(magLo)&&finite(magHi)&&magHi>magLo){
                PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
                const int classes=5;
                const QString unit=in.series.at(2).label.isEmpty()
                                      ? QStringLiteral("value") : in.series.at(2).label;
                for(int k=0;k<classes;++k){
                    const double lo=magLo+(magHi-magLo)*double(k)/double(classes);
                    const double hi=magLo+(magHi-magLo)*double(k+1)/double(classes);
                    PlotSeries bucket;
                    bucket.label=QStringLiteral("%1 %2-%3").arg(unit)
                                     .arg(lo,0,'g',3).arg(hi,0,'g',3);
                    bucket.color=lonSeries.color;
                    bucket.drawLine=false; bucket.drawMarkers=true;
                    bucket.opacity=0.72;
                    bucket.markerSize=2.0*std::sqrt(4.0+double(k)*26.0);
                    bucket.markerSizeExplicit=true;
                    for(int i=0;i<m;++i){
                        if(!finite(mag[i])) continue;
                        // The top class takes its own upper bound; without it
                        // the single largest point falls out of every class and
                        // the biggest bubble on the map is the one missing.
                        const bool last=(k==classes-1);
                        if(!(mag[i]>=lo&&(last?mag[i]<=hi:mag[i]<hi))) continue;
                        const QPointF q=projectPoint(frame,lonSeries.y[i],latSeries.y[i]);
                        if(!finite(q.x())||!finite(q.y())) continue;
                        bucket.x.append(q.x()); bucket.y.append(q.y());
                    }
                    if(!bucket.x.isEmpty()) out.series.append(bucket);
                }
                out.xAxis=PlotAxis{projectionXLabel(frame),false,unsetValue(),unsetValue()};
                out.yAxis=PlotAxis{projectionYLabel(frame),false,unsetValue(),unsetValue()};
                if(!out.series.isEmpty()) return out;
            }
        }

        PlotSpec out=derivedAs(in,(in.engine==QLatin1String("Geo Line")
                                   ||in.engine==QLatin1String("Great Circle Route")
                                   ||in.engine==QLatin1String("Ground Track"))
                                      ? QStringLiteral("Line Chart")
                                      : QStringLiteral("4D / 5D Scatter"));
        const bool asLine=(out.engine==QLatin1String("Line Chart"));
        // A ground track crosses the antimeridian, and a polyline that does is
        // drawn straight back across the whole picture unless it is cut there.
        // The cut is a new series rather than a gap, because a series is
        // documented as already cleaned of non-finite pairs - a NaN break would
        // be relying on behaviour nothing promises.
        const bool cutAtWrap=(in.engine==QLatin1String("Ground Track"));
        PlotSeries run;
        // "position", not the longitude column's name: the series is a place,
        // and labelling it "Longitude" describes half of it.
        run.label=QStringLiteral("position");
        run.color=lonSeries.color;
        run.drawLine=asLine;
        run.drawMarkers=!asLine;
        run.markerSize=(in.engine==QLatin1String("Geo Bubble"))?5.5:3.6;
        run.lineWidth=qMax(1.2,lonSeries.lineWidth);
        double previousLon=qQNaN();
        for(int i=0;i<lon.size();++i){
            if(cutAtWrap&&finite(previousLon)&&finite(lon[i])
               &&std::abs(lon[i]-previousLon)>180.0){
                if(run.x.size()>1) out.series.append(run);
                run.x.clear(); run.y.clear();
                run.label.clear();          // one legend entry for the track
            }
            if(finite(lon[i])) previousLon=lon[i];
            const QPointF q=projectPoint(frame,lon[i],lat[i]);
            if(!finite(q.x())||!finite(q.y())) continue;
            run.x.append(q.x()); run.y.append(q.y());
        }
        if(!run.x.isEmpty()) out.series.append(run);

        // THE ROUTE SAYS WHAT IT SAVED.
        //
        // A great circle drawn on its own is a curve, and a curve is not an
        // argument: the reader cannot see what it is shorter THAN. The entry
        // exists because the obvious line - straight in longitude and latitude,
        // which is what anyone gets by plotting the two columns - is longer,
        // and on a long leg it is longer by hundreds of kilometres. So the
        // straight one is drawn beside it, dashed and faint, and both legends
        // carry their length. London to Tokyo: 9,560 km against 11,310.
        if(in.engine==QLatin1String("Great Circle Route")&&n>=2){
            double arc=0.0,flat=0.0;
            PlotSeries direct;
            direct.color=run.color;
            direct.drawLine=true; direct.drawMarkers=false;
            direct.lineWidth=1.0; direct.lineWidthExplicit=true;
            direct.opacity=0.55;
            direct.dashPattern={5.0,4.0};
            for(int i=0;i+1<n;++i){
                const double aLon=lonSeries.y[i],aLat=latSeries.y[i];
                const double bLon=lonSeries.y[i+1],bLat=latSeries.y[i+1];
                if(!finite(aLon)||!finite(aLat)||!finite(bLon)||!finite(bLat)) continue;
                arc+=greatCircleMetres(aLon,aLat,bLon,bLat);
                // The flat line's length is measured ON THE GROUND, not in
                // degrees: it is the distance actually flown by following that
                // drawn line, which is the only number comparable with the arc.
                const int steps=64;
                double pLon=aLon,pLat=aLat;
                for(int k=1;k<=steps;++k){
                    const double t=double(k)/double(steps);
                    const double qLon=aLon+(bLon-aLon)*t;
                    const double qLat=aLat+(bLat-aLat)*t;
                    flat+=greatCircleMetres(pLon,pLat,qLon,qLat);
                    const QPointF q=projectPoint(frame,qLon,qLat);
                    if(finite(q.x())&&finite(q.y())){ direct.x.append(q.x()); direct.y.append(q.y()); }
                    pLon=qLon; pLat=qLat;
                }
            }
            if(arc>0.0&&direct.x.size()>1){
                out.series.last().label=QStringLiteral("great circle, %1 km")
                                            .arg(arc/1000.0,0,'f',0);
                direct.label=QStringLiteral("straight in lon/lat, %1 km")
                                 .arg(flat/1000.0,0,'f',0);
                out.series.append(direct);
            }
        }

        out.xAxis=PlotAxis{projectionXLabel(frame),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{projectionYLabel(frame),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ terrain profile
    // Elevation along a path: longitude, latitude and height, drawn against the
    // distance actually travelled rather than against a row index. The distance
    // is the great-circle sum leg by leg, so a profile of a flight, a pipeline
    // or a transect is in kilometres on the ground.
    if(in.engine==QLatin1String("Terrain Profile")){
        PlotSpec out=derivedAs(in,QStringLiteral("Area"));
        if(in.series.size()>=3){
            const QVector<double>& lon=in.series.at(0).y;
            const QVector<double>& lat=in.series.at(1).y;
            const QVector<double>& elevation=in.series.at(2).y;
            const int n=qMin(lon.size(),qMin(lat.size(),elevation.size()));
            PlotSeries profile;
            profile.label=in.series.at(2).label.isEmpty()
                             ? QStringLiteral("elevation") : in.series.at(2).label;
            profile.color=in.series.at(2).color;
            profile.drawLine=true;
            double travelled=0.0;
            double lastLon=qQNaN(),lastLat=qQNaN();
            for(int i=0;i<n;++i){
                if(!finite(lon[i])||!finite(lat[i])||!finite(elevation[i])) continue;
                if(finite(lastLon))
                    travelled+=greatCircleMetres(lastLon,lastLat,lon[i],lat[i])/1000.0;
                lastLon=lon[i]; lastLat=lat[i];
                profile.x.append(travelled);
                profile.y.append(elevation[i]);
            }
            if(!profile.x.isEmpty()) out.series.append(profile);
        }
        out.xAxis=PlotAxis{QStringLiteral("distance along path (km)"),false,unsetValue(),unsetValue()};
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("elevation");
        return out;
    }

    // ------------------------------ hypsometric and duration curves
    // One operation: sort the values from largest to smallest and plot them
    // against the fraction of the whole that reaches at least that value. It is
    // called a hypsometric curve when the whole is an area and the values are
    // heights, and a duration curve when the whole is a year and the values are
    // a load or a flow - and it was written out twice here before that was
    // noticed.
    //
    // What each says is the same thing about a different quantity: a convex
    // curve is a young incised landscape or a peaky demand, a concave one is a
    // worn basin or a steady baseload. Neither is visible in a histogram of the
    // same numbers.
    if(in.engine==QLatin1String("Hypsometric Curve")
       ||in.engine==QLatin1String("Duration Curve")){
        const bool hypsometric=(in.engine==QLatin1String("Hypsometric Curve"));
        const double fullScale=hypsometric?1.0:100.0;
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<3) continue;
            std::sort(v.begin(),v.end(),std::greater<double>());
            PlotSeries curve;
            curve.label=s.label; curve.color=s.color; curve.drawLine=true;
            curve.lineWidth=qMax(1.2,s.lineWidth);
            for(int i=0;i<v.size();++i){
                curve.x.append(fullScale*double(i+1)/double(v.size()));
                curve.y.append(v[i]);
            }
            out.series.append(curve);
        }
        out.xAxis=PlotAxis{hypsometric?QStringLiteral("fraction of area above")
                                      :QStringLiteral("per cent of time exceeded"),
                           false,0.0,fullScale};
        if(out.yAxis.label.isEmpty())
            out.yAxis.label=hypsometric?QStringLiteral("elevation"):QStringLiteral("value");
        return out;
    }

    // ------------------------------------------- slope / aspect / hillshade
    // The three things a height grid is actually read for. All from the same
    // gridded surface and the same central differences, so they cannot disagree
    // with each other about which way the ground faces.
    //
    // Aspect is a compass bearing, and the discontinuity at north is real: a
    // slope facing 359 degrees and one facing 1 degree are neighbours on the
    // ground and at opposite ends of the colour scale. That is a property of
    // the quantity, not a fault in the drawing, and smoothing it away would be
    // the dishonest choice.
    if(in.engine==QLatin1String("Slope Map")||in.engine==QLatin1String("Aspect Map")
       ||in.engine==QLatin1String("Hillshade")){
        PlotSpec out=derivedAs(in,QStringLiteral("2D Heatmap"));
        out.legendVisible=false;
        // Resolution from the sample count, the same rule every other field
        // engine uses. Asking for a fixed grid would read a sparse survey at a
        // detail it does not have, and a dense one at less than it does.
        const ValueGrid g=gridFromSeries(in,0,GridAggregate::Mean);
        if(!g.valid||g.nx<3||g.ny<3) return out;

        ValueGrid filled=g;
        closeGaps(filled,2);
        const double dx=(filled.xHi-filled.xLo)/qMax(1,filled.nx-1);
        const double dy=(filled.yHi-filled.yLo)/qMax(1,filled.ny-1);
        PlotSeries xi,yi,vi;
        xi.label=QStringLiteral("x"); yi.label=QStringLiteral("y");
        vi.label=in.engine==QLatin1String("Slope Map") ? QStringLiteral("slope (deg)")
                : in.engine==QLatin1String("Aspect Map") ? QStringLiteral("aspect (deg from N)")
                                                         : QStringLiteral("shading");
        // Sun at 315 degrees and 45 degrees up: the convention every relief map
        // is lit by, and the one people read as terrain rather than as a hole.
        const double sunAzimuth=(360.0-315.0+90.0)*kDegToRad;
        const double sunAltitude=45.0*kDegToRad;
        // THE OUTER RING IS PART OF THE MAP.
        //
        // The loops started at 1 and stopped one short, because a central
        // difference needs a neighbour on each side - so the boundary cells got
        // no value at all, while the axes below are set to the FULL grid. The
        // figure came out as a coloured rectangle inset inside its own frame
        // with a band of empty background all the way round, which reads as a
        // map that stops short of the area it is labelled for.
        //
        // A one-sided difference at the edge is the ordinary answer. Dividing
        // by the distance actually sampled makes it one - two cells inside, one
        // at the boundary - and the same expression serves both.
        for(int j=0;j<filled.ny;++j){
            for(int i=0;i<filled.nx;++i){
                const int iA=qMax(0,i-1),iB=qMin(filled.nx-1,i+1);
                const int jA=qMax(0,j-1),jB=qMin(filled.ny-1,j+1);
                const double zL=filled.cells[j*filled.nx+iA];
                const double zR=filled.cells[j*filled.nx+iB];
                const double zD=filled.cells[jA*filled.nx+i];
                const double zU=filled.cells[jB*filled.nx+i];
                if(!finite(zL)||!finite(zR)||!finite(zD)||!finite(zU)) continue;
                const double gxv=(iB==iA)?0.0
                                 :(zR-zL)/(double(iB-iA)*qMax(1e-12,dx));
                const double gyv=(jB==jA)?0.0
                                 :(zU-zD)/(double(jB-jA)*qMax(1e-12,dy));
                double value=0.0;
                if(in.engine==QLatin1String("Slope Map")){
                    value=std::atan(std::hypot(gxv,gyv))*kRadToDeg;
                }else if(in.engine==QLatin1String("Aspect Map")){
                    if(std::abs(gxv)<1e-15&&std::abs(gyv)<1e-15) continue;  // flat has no aspect
                    // Downslope direction, as a bearing clockwise from north.
                    double bearing=std::atan2(-gxv,gyv)*kRadToDeg;
                    if(bearing<0) bearing+=360.0;
                    value=bearing;
                }else{
                    const double slope=std::atan(std::hypot(gxv,gyv));
                    double aspect=std::atan2(gyv,-gxv);
                    value=std::cos(sunAltitude)*std::cos(slope)
                         +std::sin(sunAltitude)*std::sin(slope)*std::cos(sunAzimuth-aspect);
                    value=qBound(0.0,value,1.0);
                }
                xi.y.append(filled.xLo+double(i)*dx);
                yi.y.append(filled.yLo+double(j)*dy);
                vi.y.append(value);
            }
        }
        out.series={xi,yi,vi};
        out.xAxis=PlotAxis{in.series.size()>0?in.series.at(0).label:QStringLiteral("x"),
                           false,filled.xLo,filled.xHi};
        out.yAxis=PlotAxis{in.series.size()>1?in.series.at(1).label:QStringLiteral("y"),
                           false,filled.yLo,filled.yHi};
        return out;
    }

    // =====================================================================
    // Flight and aviation
    //
    // These are the charts an aircraft is actually specified and operated by,
    // and none of them is a plot of the mapped columns as they stand: a drag
    // polar is read for the two coefficients fitted through it, a lift curve
    // for its slope and where it stops being a line, a crosswind chart for
    // components that have to be resolved from a wind and a runway heading,
    // and a weight-and-balance chart for whether the loaded point is inside the
    // envelope or outside it. Drawing the columns and leaving the reader to do
    // that arithmetic is what a general-purpose line chart already does.
    // =====================================================================

    // Payload against range: the four-cornered envelope every aircraft is sold
    // on. Filled to the axis, because the area under it is the useful part -
    // every payload/range pair inside it is a flight the aircraft can do.
    if(in.engine==QLatin1String("Payload-Range Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("Area"));
        for(const PlotSeries& s:in.series){
            PlotSeries leg=s;
            leg.drawLine=true;
            leg.drawMarkers=true;
            leg.markerSize=qMax(5.0,s.markerSize);
            leg.markerSizeExplicit=true;
            out.series.append(leg);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("range");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("payload");
        return out;
    }

    // A flight envelope is a closed shape, and a polyline that is not closed
    // reads as a boundary with a hole in it. Both of these close it and mark
    // the reference the envelope is read against: one g for the V-n diagram,
    // where the whole point is how far above and below one g the aircraft may
    // be flown.
    if(in.engine==QLatin1String("V-n Flight Envelope")
       ||in.engine==QLatin1String("Altitude-Mach Envelope")){
        const bool vn=(in.engine==QLatin1String("V-n Flight Envelope"));
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries loop;
            loop.label=s.label; loop.color=s.color; loop.drawLine=true;
            loop.drawMarkers=true; loop.markerSize=4.0;
            loop.lineWidth=qMax(1.6,s.lineWidth); loop.lineWidthExplicit=true;
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                loop.x.append(s.x[i]); loop.y.append(s.y[i]);
            }
            if(loop.x.size()<3) continue;
            // Close it, unless the data already did.
            if(!qFuzzyCompare(loop.x.first(),loop.x.last())
               ||!qFuzzyCompare(loop.y.first(),loop.y.last())){
                loop.x.append(loop.x.first());
                loop.y.append(loop.y.first());
            }
            out.series.append(loop);
        }
        if(vn&&!out.series.isEmpty()){
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            for(const PlotSeries& s:out.series)
                for(double v:s.x){ lo=qMin(lo,v); hi=qMax(hi,v); }
            if(finite(lo)&&finite(hi)&&hi>lo){
                PlotSeries level;
                level.label=QStringLiteral("1 g");
                level.color=in.style.foreground;
                level.drawLine=true; level.lineWidth=0.8; level.lineWidthExplicit=true;
                level.dashPattern={4.0,3.0};
                level.x={lo,hi}; level.y={1.0,1.0};
                out.series.append(level);
            }
        }
        if(out.xAxis.label.isEmpty())
            out.xAxis.label=vn?QStringLiteral("equivalent airspeed"):QStringLiteral("Mach number");
        if(out.yAxis.label.isEmpty())
            out.yAxis.label=vn?QStringLiteral("load factor n (g)"):QStringLiteral("altitude");
        return out;
    }

    // The drag polar, with the two numbers it exists to give: the zero-lift
    // drag and the induced-drag factor of CD = CD0 + k CL^2, fitted by least
    // squares on CL^2, and the best lift-to-drag ratio that pair implies. The
    // measured points, the fitted parabola and the tangent point are drawn
    // together so a departure from the parabola is visible rather than averaged
    // into a number.
    if(in.engine==QLatin1String("Drag Polar")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries pts;
            pts.label=s.label; pts.color=s.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            // CD = CD0 + k CL^2 is a straight line in CL^2, so that is the
            // space the fit is done in.
            QVector<double> squared,drag;
            for(int i=0;i<n;++i){
                const double cd=s.x[i],cl=s.y[i];
                if(!finite(cd)||!finite(cl)) continue;
                pts.x.append(cd); pts.y.append(cl);
                squared.append(cl*cl); drag.append(cd);
            }
            if(pts.x.isEmpty()) continue;
            out.series.append(pts);
            const LineFit fit=fitLine(squared,drag);
            const double k=fit.slope,cd0=fit.intercept;
            if(!fit.ok||!(k>0)||!(cd0>0)) continue;

            double clLo=std::numeric_limits<double>::infinity(),clHi=-clLo;
            for(double cl:pts.y){ clLo=qMin(clLo,cl); clHi=qMax(clHi,cl); }
            PlotSeries curve;
            const double clBest=std::sqrt(cd0/k);
            const double ldMax=1.0/(2.0*std::sqrt(cd0*k));
            curve.label=QStringLiteral("CD0 %1, k %2, (L/D)max %3")
                            .arg(cd0,0,'f',4).arg(k,0,'f',4).arg(ldMax,0,'f',1);
            curve.color=s.color; curve.drawLine=true; curve.drawMarkers=false;
            curve.lineWidth=1.4; curve.lineWidthExplicit=true;
            for(int i=0;i<=120;++i){
                const double cl=clLo+(clHi-clLo)*double(i)/120.0;
                curve.x.append(cd0+k*cl*cl);
                curve.y.append(cl);
            }
            out.series.append(curve);
            if(clBest>=clLo&&clBest<=clHi){
                PlotSeries best;
                best.label=QStringLiteral("best L/D at CL %1").arg(clBest,0,'f',3);
                best.color=in.style.positive;
                best.drawLine=false; best.drawMarkers=true;
                best.markerSize=8.0; best.markerSizeExplicit=true;
                best.x={cd0+k*clBest*clBest}; best.y={clBest};
                out.series.append(best);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("drag coefficient CD");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("lift coefficient CL");
        return out;
    }

    // The lift curve, with the slope of its straight part and the stall it ends
    // at. The slope is fitted only below the maximum, because fitting through
    // the stall is what makes a lift-curve slope wrong - and quietly, since the
    // fit still returns a number.
    if(in.engine==QLatin1String("Lift Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries pts;
            pts.label=s.label; pts.color=s.color;
            pts.drawLine=true; pts.drawMarkers=true; pts.markerSize=4.0;
            int peak=-1; double peakCl=-std::numeric_limits<double>::infinity();
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                pts.x.append(s.x[i]); pts.y.append(s.y[i]);
                if(s.y[i]>peakCl){ peakCl=s.y[i]; peak=pts.x.size()-1; }
            }
            if(pts.x.isEmpty()) continue;

            // Only up to the peak: the linear region is what a lift-curve
            // slope means, and fitting through the stall quietly returns a
            // smaller number that still looks like an answer.
            const LineFit fit0=fitLine(pts.x,pts.y,(peak>=0)?peak+1:-1);
            {
                const double slope=fit0.slope,intercept=fit0.intercept;
                if(fit0.ok){
                    pts.label=QStringLiteral("%1 (a %2 /deg, %3 /rad, CLmax %4)")
                                  .arg(s.label).arg(slope,0,'f',4)
                                  .arg(slope*180.0/M_PI,0,'f',3).arg(peakCl,0,'f',3);
                    PlotSeries fit;
                    fit.label=QStringLiteral("linear region");
                    fit.color=in.style.warning;
                    fit.drawLine=true; fit.drawMarkers=false;
                    fit.lineWidth=1.0; fit.lineWidthExplicit=true;
                    fit.dashPattern={5.0,3.0};
                    const double a0=pts.x.first(),a1=pts.x.at(peak>=0?peak:pts.x.size()-1);
                    fit.x={a0,a1};
                    fit.y={intercept+slope*a0,intercept+slope*a1};
                    out.series.append(pts);
                    out.series.append(fit);
                    if(peak>=0){
                        PlotSeries stall;
                        stall.label=QStringLiteral("stall");
                        stall.color=in.style.danger;
                        stall.drawLine=false; stall.drawMarkers=true;
                        stall.markerSize=8.0; stall.markerSizeExplicit=true;
                        stall.x={pts.x.at(peak)}; stall.y={pts.y.at(peak)};
                        out.series.append(stall);
                    }
                    continue;
                }
            }
            out.series.append(pts);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("angle of attack (deg)");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("lift coefficient CL");
        return out;
    }

    // Altitude against time or distance, filled, with the top of climb marked.
    // The apex is where the profile stops being a climb and starts being a
    // cruise, and finding it by eye off a line is exactly the sort of reading
    // that goes wrong on a compressed x axis.
    if(in.engine==QLatin1String("Flight Profile")){
        PlotSpec out=derivedAs(in,QStringLiteral("Area"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<2) continue;
            PlotSeries leg=s;
            leg.drawLine=true;
            out.series.append(leg);
            int apex=-1; double top=-std::numeric_limits<double>::infinity();
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                if(s.y[i]>top){ top=s.y[i]; apex=i; }
            }
            if(apex>=0){
                PlotSeries mark;
                mark.label=QStringLiteral("top of climb %1").arg(top,0,'f',0);
                mark.color=in.style.positive;
                mark.drawLine=false; mark.drawMarkers=true;
                mark.markerSize=7.0; mark.markerSizeExplicit=true;
                mark.x={s.x[apex]}; mark.y={s.y[apex]};
                out.series.append(mark);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("time or distance");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("altitude");
        return out;
    }

    // Crosswind and headwind, resolved. Three mapped columns - runway heading,
    // wind direction and wind speed - and the components computed from them,
    // because that is the arithmetic the chart exists to save and the one that
    // gets done wrong in a hurry. Sign matters and is kept: a negative
    // headwind is a tailwind and is a different decision.
    if(in.engine==QLatin1String("Runway Crosswind")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=3){
            const QVector<double>& runway=in.series.at(0).y;
            const QVector<double>& from=in.series.at(1).y;
            const QVector<double>& speed=in.series.at(2).y;
            const int n=qMin(runway.size(),qMin(from.size(),speed.size()));
            PlotSeries pts;
            pts.label=QStringLiteral("wind");
            pts.color=in.series.at(2).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            double worstCross=0.0;
            for(int i=0;i<n;++i){
                if(!finite(runway[i])||!finite(from[i])||!finite(speed[i])) continue;
                const double delta=(from[i]-runway[i])*kDegToRad;
                const double cross=speed[i]*std::sin(delta);
                const double head=speed[i]*std::cos(delta);
                pts.x.append(cross); pts.y.append(head);
                worstCross=qMax(worstCross,std::abs(cross));
            }
            if(!pts.x.isEmpty()){
                pts.label=QStringLiteral("wind (max crosswind %1)").arg(worstCross,0,'f',1);
                out.series.append(pts);
                // The line where a headwind becomes a tailwind. Everything
                // below it is a downwind landing.
                PlotSeries zero;
                zero.label=QStringLiteral("no headwind");
                zero.color=in.style.warning;
                zero.drawLine=true; zero.drawMarkers=false;
                zero.lineWidth=0.8; zero.lineWidthExplicit=true;
                zero.dashPattern={4.0,3.0};
                zero.x={-worstCross,worstCross}; zero.y={0.0,0.0};
                out.series.append(zero);
            }
        }
        if(out.xAxis.label.isEmpty())
            out.xAxis.label=QStringLiteral("crosswind component (right positive)");
        if(out.yAxis.label.isEmpty())
            out.yAxis.label=QStringLiteral("headwind component (tailwind negative)");
        return out;
    }

    // Weight and balance. The first mapped column pair is the envelope; every
    // pair after it is a loading to test against it, and each is drawn in the
    // colour of its answer. Inside or outside is a ray-crossing test on the
    // polygon rather than a judgement by eye, because a point a millimetre
    // outside the aft limit looks exactly like one a millimetre inside.
    if(in.engine==QLatin1String("Weight and Balance Envelope")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        if(!in.series.isEmpty()){
            const PlotSeries& env=in.series.at(0);
            QVector<double> ex,ey;
            const int n=qMin(env.x.size(),env.y.size());
            for(int i=0;i<n;++i){
                if(!finite(env.x[i])||!finite(env.y[i])) continue;
                ex.append(env.x[i]); ey.append(env.y[i]);
            }
            if(ex.size()>=3){
                PlotSeries loop;
                loop.label=env.label.isEmpty()?QStringLiteral("envelope"):env.label;
                loop.color=env.color; loop.drawLine=true; loop.drawMarkers=false;
                loop.lineWidth=qMax(1.6,env.lineWidth); loop.lineWidthExplicit=true;
                loop.x=ex; loop.y=ey;
                if(!qFuzzyCompare(loop.x.first(),loop.x.last())
                   ||!qFuzzyCompare(loop.y.first(),loop.y.last())){
                    loop.x.append(loop.x.first()); loop.y.append(loop.y.first());
                }
                out.series.append(loop);

                // AN INDEX OVER THE ENVELOPE, not a walk round it per loading.
                // Every loading was tested against every edge, so four fleets
                // of 24,000 loadings against an envelope of 24,000 vertices is
                // two and a half billion straddle tests - twelve seconds, and
                // the arithmetic is right, just done far too many times.
                //
                // An edge can only matter to a loading whose weight lies
                // between the edge's ends, so the edges are filed by the bands
                // of weight they cross and a loading consults only its own
                // band. An edge is filed in every band it spans, which is one
                // entry per band crossing - the whole index is built in a
                // single pass.
                //
                // The answer is unchanged, not merely close: crossings are
                // counted by parity, so visiting the same straddling edges in
                // a different order gives the same verdict, and the test
                // applied to each one is the same arithmetic as before.
                const int m=ex.size();
                double yLo=ey.first(),yHi=ey.first();
                for(double v:std::as_const(ey)){ yLo=qMin(yLo,v); yHi=qMax(yHi,v); }
                const int bands=qBound(1,int(std::sqrt(double(m)))*2,4096);
                const double bandSpan=(yHi>yLo)?(yHi-yLo)/double(bands):0.0;
                const auto bandOf=[&](double y){
                    if(!(bandSpan>0.0)||!finite(y)) return 0;
                    return qBound(0,int((y-yLo)/bandSpan),bands-1);
                };
                QVector<QVector<int>> byBand(bands);
                for(int i=0,j=m-1;i<m;j=i++){
                    const int a=bandOf(qMin(ey[i],ey[j]));
                    const int b=bandOf(qMax(ey[i],ey[j]));
                    for(int s=a;s<=b;++s) byBand[s].append(i);
                }
                const auto inside=[&](double px,double py){
                    bool contained=false;
                    for(const int i:std::as_const(byBand[bandOf(py)])){
                        const int j=(i+m-1)%m;
                        const bool straddles=((ey[i]>py)!=(ey[j]>py));
                        if(!straddles) continue;
                        const double t=(py-ey[i])/(ey[j]-ey[i]);
                        if(px<ex[i]+t*(ex[j]-ex[i])) contained=!contained;
                    }
                    return contained;
                };
                for(int k=1;k<in.series.size();++k){
                    const PlotSeries& load=in.series.at(k);
                    PlotSeries ok,bad;
                    ok.color=in.style.positive; bad.color=in.style.danger;
                    ok.drawLine=false; bad.drawLine=false;
                    ok.drawMarkers=true; bad.drawMarkers=true;
                    ok.markerSize=7.0; bad.markerSize=7.0;
                    ok.markerSizeExplicit=true; bad.markerSizeExplicit=true;
                    const int ln=qMin(load.x.size(),load.y.size());
                    for(int i=0;i<ln;++i){
                        if(!finite(load.x[i])||!finite(load.y[i])) continue;
                        PlotSeries& target=inside(load.x[i],load.y[i])?ok:bad;
                        target.x.append(load.x[i]); target.y.append(load.y[i]);
                    }
                    if(!ok.x.isEmpty()){
                        ok.label=QStringLiteral("%1 within limits (%2)")
                                     .arg(load.label).arg(ok.x.size());
                        out.series.append(ok);
                    }
                    if(!bad.x.isEmpty()){
                        bad.label=QStringLiteral("%1 OUTSIDE limits (%2)")
                                      .arg(load.label).arg(bad.x.size());
                        out.series.append(bad);
                    }
                }
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("centre of gravity");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("weight");
        return out;
    }

    // =====================================================================
    // Maintenance and reliability
    //
    // These are read for a fitted parameter, not for their shape: a Weibull
    // plot for the shape that says whether things are failing early, randomly
    // or by wear-out; a growth plot for whether the fixes are working; a CUSUM
    // for the moment a process shifted. Every one of them puts its numbers in
    // the legend, because a chart that makes you get a ruler out is a chart
    // that gets read wrong.
    // =====================================================================

    // The Weibull probability plot. Median ranks on the doubly-logarithmic
    // scale where a Weibull is a straight line, then a least-squares fit for
    // the shape and the scale.
    //
    // The shape is the whole point: below 1 the failures are early-life and a
    // burn-in is indicated; near 1 they are random and preventive replacement
    // buys nothing; above 1 they are wear-out and a life limit does. That
    // reading is not available from a histogram of the same times.
    if(in.engine==QLatin1String("Weibull Probability Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            QVector<double> t=finiteValues(s);
            // A failure at or before zero has no logarithm and is not a life.
            QVector<double> lives;
            for(double v:t) if(v>0) lives.append(v);
            if(lives.size()<3) continue;
            std::sort(lives.begin(),lives.end());
            const int n=lives.size();
            PlotSeries pts;
            pts.color=s.color; pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            for(int i=0;i<n;++i){
                // Bernard's approximation to the median rank. The plotting
                // position matters at small n, which is where reliability data
                // usually is.
                const double f=(double(i+1)-0.3)/(double(n)+0.4);
                const double x=std::log(lives[i]);
                const double y=std::log(-std::log(1.0-f));
                if(!finite(x)||!finite(y)) continue;
                pts.x.append(x); pts.y.append(y);
            }
            if(pts.x.size()<3) continue;
            {
                const LineFit line=fitLine(pts.x,pts.y);
                const double beta=line.slope;
                const double intercept=line.intercept;
                const double eta=std::exp(-intercept/qMax(1e-12,beta));
                if(line.ok&&finite(eta)&&beta>0){
                    pts.label=QStringLiteral("%1 (beta %2, eta %3, %4)")
                        .arg(s.label).arg(beta,0,'f',2).arg(eta,0,'f',3)
                        .arg(beta<0.95?QStringLiteral("early-life")
                            :beta<1.05?QStringLiteral("random")
                                      :QStringLiteral("wear-out"));
                    PlotSeries fit;
                    fit.label=QStringLiteral("Weibull fit");
                    fit.color=in.style.warning;
                    fit.drawLine=true; fit.drawMarkers=false;
                    fit.lineWidth=1.2; fit.lineWidthExplicit=true;
                    fit.x={pts.x.first(),pts.x.last()};
                    fit.y={intercept+beta*pts.x.first(),intercept+beta*pts.x.last()};
                    out.series.append(pts);
                    out.series.append(fit);
                    continue;
                }
            }
            pts.label=s.label;
            out.series.append(pts);
        }
        out.xAxis=PlotAxis{QStringLiteral("ln(life)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("ln(-ln(1-F))"),false,unsetValue(),unsetValue()};
        return out;
    }

    // Reliability growth. Cumulative failures against cumulative time, fitted
    // as N = lambda t^beta on log-log, which is the Crow-AMSAA model and the
    // Duane plot's straight line.
    //
    // Beta is the answer: below one the intervals between failures are getting
    // longer and the fixes are working, above one they are getting shorter and
    // the fleet is getting worse. The number is reported rather than left to be
    // eyeballed off a slope, because on log-log a slope of 0.8 and one of 1.2
    // look much the same.
    if(in.engine==QLatin1String("Reliability Growth")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            QVector<double> times=finiteValues(s);
            QVector<double> valid;
            for(double v:times) if(v>0) valid.append(v);
            if(valid.size()<3) continue;
            std::sort(valid.begin(),valid.end());
            PlotSeries pts;
            pts.color=s.color; pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            QVector<double> logTime,logCount;
            for(int i=0;i<valid.size();++i){
                const double x=std::log(valid[i]);
                const double y=std::log(double(i+1));
                if(!finite(x)||!finite(y)) continue;
                pts.x.append(valid[i]); pts.y.append(double(i+1));
                logTime.append(x); logCount.append(y);
            }
            if(logTime.size()<3) continue;
            {
                const LineFit line=fitLine(logTime,logCount);
                const double beta=line.slope;
                const double lambda=std::exp(line.intercept);
                if(line.ok&&finite(lambda)){
                    pts.label=QStringLiteral("%1 (beta %2, lambda %3, %4)")
                        .arg(s.label).arg(beta,0,'f',3).arg(lambda,0,'g',3)
                        .arg(beta<0.95?QStringLiteral("improving")
                            :beta>1.05?QStringLiteral("deteriorating")
                                      :QStringLiteral("steady"));
                    PlotSeries fit;
                    fit.label=QStringLiteral("Crow-AMSAA fit");
                    fit.color=in.style.warning;
                    fit.drawLine=true; fit.drawMarkers=false;
                    fit.lineWidth=1.2; fit.lineWidthExplicit=true;
                    for(int i=0;i<=60;++i){
                        const double t=valid.first()
                            +(valid.last()-valid.first())*double(i)/60.0;
                        if(!(t>0)) continue;
                        fit.x.append(t);
                        fit.y.append(lambda*std::pow(t,beta));
                    }
                    out.series.append(pts);
                    out.series.append(fit);
                    continue;
                }
            }
            pts.label=s.label;
            out.series.append(pts);
        }
        out.xAxis=PlotAxis{QStringLiteral("cumulative operating time"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("cumulative failures"),true,unsetValue(),unsetValue()};
        return out;
    }

    // Mean time between failures, as it moves. The column is failure times; the
    // intervals between them are what MTBF is the mean of, and a running mean
    // of those intervals is the only form of this chart that can show a trend
    // rather than a single number that hides one.
    if(in.engine==QLatin1String("MTBF Trend")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            QVector<double> times=finiteValues(s);
            if(times.size()<3) continue;
            std::sort(times.begin(),times.end());
            QVector<double> gaps;
            for(int i=1;i<times.size();++i) gaps.append(times[i]-times[i-1]);
            if(gaps.isEmpty()) continue;
            // A window that adapts: five points is noise on a long history and
            // all of the history on a short one.
            const int window=qBound(3,gaps.size()/6,25);
            PlotSeries cumulative,rolling;
            cumulative.label=QStringLiteral("%1 cumulative MTBF").arg(s.label);
            cumulative.color=s.color; cumulative.drawLine=true;
            rolling.label=QStringLiteral("last %1 intervals").arg(window);
            rolling.color=in.style.warning; rolling.drawLine=true;
            // Not dashed. A dash pattern over a curve of thousands of points is
            // both unreadable and slow - Qt's dash iterator runs per segment,
            // and this one series took a 36,000-point figure from 59 ms to
            // 4.7 SECONDS per repaint. Dashes are for the two-point reference
            // lines; a data curve is told apart by its colour.

            // A running sum, not a sum over the window at every step. The
            // window grows with the history - a sixth of it - so re-adding it
            // per point is quadratic, and it measured at 1,026 ms per repaint
            // on a few thousand failures. Subtracting the interval that leaves
            // and adding the one that arrives gives the same numbers in one
            // pass.
            // One point per failure is more than a trend chart can show. A
            // figure is a couple of thousand pixels wide at most, and both of
            // these are smoothed summaries - a running mean has no detail
            // between neighbouring points to lose. Striding to about two
            // thousand points was measured against the undecimated picture
            // before being kept.
            //
            // It matters because a noisy curve is expensive to draw: this one
            // series took 1,077 ms per repaint at three thousand points while
            // the smooth one beside it took 51.
            const int stride=qMax(1,int(gaps.size()/2000));
            double running=0.0,windowed=0.0;
            for(int i=0;i<gaps.size();++i){
                running+=gaps[i];
                windowed+=gaps[i];
                if(i>=window) windowed-=gaps[i-window];
                const bool keep=(i%stride==0)||(i==gaps.size()-1);
                if(keep){
                    cumulative.x.append(times[i+1]);
                    cumulative.y.append(running/double(i+1));
                }
                if(i+1>=window&&keep){
                    rolling.x.append(times[i+1]);
                    rolling.y.append(windowed/double(window));
                }
            }
            out.series.append(cumulative);
            if(!rolling.x.isEmpty()) out.series.append(rolling);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("operating time");
        out.yAxis=PlotAxis{QStringLiteral("mean time between failures"),false,unsetValue(),unsetValue()};
        return out;
    }

    // A year of daily values as a grid of weeks. The pattern a calendar shows
    // and a time series does not is the weekly one: a maintenance backlog that
    // clears every Friday, a fleet that flies at weekends. x is the week, y is
    // the day within it, and the value is the colour.
    if(in.engine==QLatin1String("Calendar Heatmap")){
        PlotSpec out=derivedAs(in,QStringLiteral("2D Heatmap"));
        out.legendVisible=false;
        if(!in.series.isEmpty()){
            const PlotSeries& s=in.series.at(0);
            const int n=qMin(s.x.size(),s.y.size());
            double first=std::numeric_limits<double>::infinity();
            for(int i=0;i<n;++i) if(finite(s.x[i])) first=qMin(first,s.x[i]);
            if(finite(first)){
                PlotSeries wx,wy,wv;
                for(int i=0;i<n;++i){
                    if(!finite(s.x[i])||!finite(s.y[i])) continue;
                    // The x column is a date in days. Anything with a fixed
                    // daily step works - a day number, a Julian day, a serial
                    // date - because only the difference is used.
                    const double days=std::floor(s.x[i]-first);
                    if(!(days>=0)) continue;
                    wx.y.append(std::floor(days/7.0));
                    wy.y.append(days-std::floor(days/7.0)*7.0);
                    wv.y.append(s.y[i]);
                }
                wx.label=QStringLiteral("week"); wy.label=QStringLiteral("day");
                wv.label=s.label;
                double weeks=0.0;
                for(double v:wx.y) weeks=qMax(weeks,v);
                out.series={wx,wy,wv};
                // Explicit, because these series carry values in .y and nothing
                // in .x: left to fit itself the frame would read the union of
                // week, weekday and value, and label the axis 0 to 1.
                out.xAxis=PlotAxis{QStringLiteral("week from first date"),false,-0.5,weeks+0.5};
                out.yAxis=PlotAxis{QStringLiteral("day within week"),false,-0.5,6.5};
                return out;
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("week from first date"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("day within week"),false,-0.5,6.5};
        return out;
    }

    // CUSUM. The tabular form, with the standard slack of half a sigma and a
    // decision interval of five: a chart that answers "when did it shift" for
    // shifts a Shewhart chart never triggers on, because a run of small
    // deviations all one way accumulates here and does not there.
    if(in.engine==QLatin1String("CUSUM Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<4) continue;
            const double mean=meanOf(v);
            const double sigma=stdevOf(v);
            if(!(sigma>0)) continue;
            const double slack=0.5*sigma,limit=5.0*sigma;
            PlotSeries up,down,upper,lower;
            up.label=QStringLiteral("%1 C+").arg(s.label);
            up.color=s.color; up.drawLine=true;
            down.label=QStringLiteral("%1 C-").arg(s.label);
            down.color=in.style.warning; down.drawLine=true;
            double cp=0,cm=0; int alarms=0;
            for(int i=0;i<v.size();++i){
                cp=qMax(0.0,cp+(v[i]-mean)-slack);
                cm=qMin(0.0,cm+(v[i]-mean)+slack);
                if(cp>limit||cm<-limit) ++alarms;
                up.x.append(double(i+1)); up.y.append(cp);
                down.x.append(double(i+1)); down.y.append(cm);
            }
            up.label=QStringLiteral("%1 C+ (%2 point%3 beyond h)")
                         .arg(s.label).arg(alarms).arg(alarms==1?QString():QStringLiteral("s"));
            upper.label=QStringLiteral("h = 5 sigma");
            upper.color=in.style.danger; upper.drawLine=true;
            upper.lineWidth=0.8; upper.lineWidthExplicit=true;
            upper.dashPattern={4.0,3.0};
            upper.x={1.0,double(v.size())}; upper.y={limit,limit};
            lower=upper; lower.label.clear();
            lower.y={-limit,-limit};
            out.series.append(up);
            out.series.append(down);
            out.series.append(upper);
            out.series.append(lower);
        }
        out.xAxis=PlotAxis{QStringLiteral("observation"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("cumulative sum of deviations"),false,unsetValue(),unsetValue()};
        return out;
    }

    // EWMA, with the limits that widen from the first point to their asymptote
    // rather than the asymptote drawn flat across the start. Early points are
    // the ones a flat limit judges wrongly, and they are the ones you have when
    // a process has just been changed.
    if(in.engine==QLatin1String("EWMA Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<4) continue;
            const double mean=meanOf(v),sigma=stdevOf(v);
            if(!(sigma>0)) continue;
            const double lambda=0.2,L=3.0;
            PlotSeries z,upper,lower,centre;
            z.label=QStringLiteral("%1 EWMA (lambda 0.2)").arg(s.label);
            z.color=s.color; z.drawLine=true; z.drawMarkers=true; z.markerSize=3.0;
            upper.label=QStringLiteral("3 sigma limits");
            upper.color=in.style.danger; upper.drawLine=true;
            upper.lineWidth=0.8; upper.lineWidthExplicit=true;
            lower=upper; lower.label.clear();
            centre.label=QStringLiteral("mean");
            centre.color=in.style.foreground; centre.drawLine=true;
            centre.lineWidth=0.6; centre.lineWidthExplicit=true;
            centre.dashPattern={4.0,4.0};
            double value=mean;
            // The centre is a constant: two points draw it exactly, and dashing
            // two points costs nothing while dashing one per observation is the
            // same cliff the MTBF rolling line fell off.
            centre.x={1.0,double(v.size())};
            centre.y={mean,mean};
            int alarms=0;
            for(int i=0;i<v.size();++i){
                value=lambda*v[i]+(1.0-lambda)*value;
                const double spread=sigma*std::sqrt((lambda/(2.0-lambda))
                                     *(1.0-std::pow(1.0-lambda,2.0*double(i+1))));
                const double hi=mean+L*spread,lo=mean-L*spread;
                if(value>hi||value<lo) ++alarms;
                z.x.append(double(i+1)); z.y.append(value);
                upper.x.append(double(i+1)); upper.y.append(hi);
                lower.x.append(double(i+1)); lower.y.append(lo);
            }
            z.label=QStringLiteral("%1 EWMA (%2 out of control)").arg(s.label).arg(alarms);
            out.series.append(z);
            out.series.append(upper);
            out.series.append(lower);
            out.series.append(centre);
        }
        out.xAxis=PlotAxis{QStringLiteral("observation"),false,unsetValue(),unsetValue()};
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("EWMA");
        return out;
    }

    // The S-N curve, with Basquin's law fitted through it: S = A N^b on log-log,
    // which is the form fatigue life is quoted in and the one that lets a life
    // be read off at a stress the test never ran at.
    if(in.engine==QLatin1String("S-N Fatigue Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries pts;
            pts.color=s.color; pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            QVector<double> logCycles,logStress;
            for(int i=0;i<n;++i){
                const double cycles=s.x[i],stress=s.y[i];
                if(!(cycles>0)||!(stress>0)) continue;
                pts.x.append(cycles); pts.y.append(stress);
                logCycles.append(std::log(cycles)); logStress.append(std::log(stress));
            }
            if(logCycles.size()<3) continue;
            {
                const LineFit line=fitLine(logCycles,logStress);
                const double b=line.slope;
                const double A=std::exp(line.intercept);
                if(line.ok&&finite(A)){
                    pts.label=QStringLiteral("%1 (S = %2 N^%3)")
                                  .arg(s.label).arg(A,0,'g',4).arg(b,0,'f',4);
                    PlotSeries fit;
                    fit.label=QStringLiteral("Basquin fit");
                    fit.color=in.style.warning;
                    fit.drawLine=true; fit.drawMarkers=false;
                    fit.lineWidth=1.2; fit.lineWidthExplicit=true;
                    double lo=std::numeric_limits<double>::infinity(),hi=-lo;
                    for(double c:pts.x){ lo=qMin(lo,c); hi=qMax(hi,c); }
                    for(int i=0;i<=60;++i){
                        const double c=std::exp(std::log(lo)
                                     +(std::log(hi)-std::log(lo))*double(i)/60.0);
                        fit.x.append(c); fit.y.append(A*std::pow(c,b));
                    }
                    out.series.append(pts);
                    out.series.append(fit);
                    continue;
                }
            }
            pts.label=s.label;
            out.series.append(pts);
        }
        out.xAxis=PlotAxis{QStringLiteral("cycles to failure N"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("stress amplitude S"),true,unsetValue(),unsetValue()};
        return out;
    }

    // Rainflow counting, ASTM E1049 four-point, on a load history.
    //
    // A fatigue life cannot be computed from a load history directly: the
    // damage is done by closed hysteresis loops, and finding them means pairing
    // a reversal with the one it closes against, which is what rainflow does
    // and what counting peaks does not. The result is the cycles by range and
    // mean, as a grid - the form a damage sum is taken over.
    return std::nullopt;
}

} // namespace graphvis
