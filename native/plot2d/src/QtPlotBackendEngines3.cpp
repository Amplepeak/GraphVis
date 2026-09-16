// ============================================================================
// prepareSpecCore, part 3 of 6: "Concentration Curve" through "Substrate Inhibition Curve".
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

std::optional<PlotSpec> QtPlotBackend::prepareEngineGroup3(const PlotSpec& in) const {
    if(in.engine==QLatin1String("Concentration Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& outcome=in.series.at(0).y;
            const QVector<double>& rank=in.series.at(1).y;
            const int n=qMin(outcome.size(),rank.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i)
                if(finite(outcome[i])&&finite(rank[i])&&outcome[i]>=0.0)
                    rows.append({rank[i],outcome[i]});
            if(rows.size()>=2){
                std::sort(rows.begin(),rows.end(),
                          [](const QPair<double,double>& a,const QPair<double,double>& b){
                              return a.first<b.first;
                          });
                double total=0.0;
                for(const auto& r:rows) total+=r.second;
                if(total>0.0){
                    PlotSeries curve;
                    curve.label=QStringLiteral("%1 by %2")
                                    .arg(in.series.at(0).label,in.series.at(1).label);
                    curve.color=in.series.at(0).color;
                    curve.lineWidth=qMax(1.2,in.style.lineWidth);
                    curve.x.append(0.0); curve.y.append(0.0);
                    double running=0.0;
                    for(int i=0;i<rows.size();++i){
                        running+=rows[i].second;
                        curve.x.append(double(i+1)/double(rows.size()));
                        curve.y.append(running/total);
                    }
                    out.series.append(curve);
                    PlotSeries equality;
                    equality.label=QStringLiteral("proportionate");
                    equality.x={0.0,1.0}; equality.y={0.0,1.0};
                    equality.color=in.style.gridColor;
                    equality.dashPattern={5,4};
                    out.series.append(equality);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("cumulative share of population, ranked"),
                           false,0.0,1.0};
        out.yAxis=PlotAxis{QStringLiteral("cumulative share of outcome"),false,0.0,1.0};
        return out;
    }

    // ------------------------------------------------ Rank-Abundance Curve
    // Value against its rank, commonest first. A steep curve is a community
    // dominated by a few species; a shallow one is even. Drawn on a log y axis,
    // which is where the shape is legible.
    if(in.engine==QLatin1String("Rank-Abundance Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            v.erase(std::remove_if(v.begin(),v.end(),[](double d){ return d<=0.0; }),v.end());
            if(v.size()<2) continue;
            std::sort(v.begin(),v.end(),std::greater<double>());
            PlotSeries curve;
            curve.label=s.label;
            curve.color=s.color;
            curve.drawMarkers=v.size()<=60;
            curve.markerSize=3.5;
            for(int i=0;i<v.size();++i){ curve.x.append(double(i+1)); curve.y.append(v[i]); }
            out.series.append(curve);
        }
        out.xAxis=PlotAxis{QStringLiteral("rank"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("abundance"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------- Scree Plot
    // Eigenvalues, or any decreasing measure of explained variance, against
    // component number, with the cumulative percentage over the top. The elbow
    // is read off the first; where the second crosses 80 or 90% is read off the
    // second, and drawing only one of them is why people argue about how many
    // components to keep.
    if(in.engine==QLatin1String("Scree Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            v.erase(std::remove_if(v.begin(),v.end(),[](double d){ return d<0.0; }),v.end());
            if(v.size()<2) continue;
            std::sort(v.begin(),v.end(),std::greater<double>());
            double total=0.0;
            for(double d:v) total+=d;
            PlotSeries eig;
            eig.label=QStringLiteral("%1 — eigenvalue").arg(s.label);
            eig.color=s.color;
            eig.drawMarkers=true; eig.markerSize=4.0;
            for(int i=0;i<v.size()&&i<40;++i){ eig.x.append(double(i+1)); eig.y.append(v[i]); }
            out.series.append(eig);
            if(total>0.0){
                PlotSeries cum;
                cum.label=QStringLiteral("cumulative %");
                cum.color=in.style.warning;
                cum.dashPattern={5,3};
                // ON ITS OWN AXIS, as a share, rather than scaled onto the
                // eigenvalue's.
                //
                // The scaling was a workaround for there being no second
                // ordinate: the running share was multiplied by the largest
                // eigenvalue so that it fitted, and the y axis had to carry
                // the apology - "dashed: cumulative share, scaled". The number
                // a scree plot is read for is "how much variance do the first
                // k components explain", and it could not be read off the axis
                // at all. There is a right-hand ordinate now.
                double running=0.0;
                cum.secondaryAxis=true;
                for(int i=0;i<v.size()&&i<40;++i){
                    running+=v[i];
                    cum.x.append(double(i+1));
                    cum.y.append(running/total*100.0);
                }
                out.series.append(cum);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("component"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("eigenvalue"),false,0.0,unsetValue()};
        out.y2Axis=PlotAxis{QStringLiteral("cumulative share (%)"),false,0.0,100.0};
        return out;
    }

    // ------------------------------------------------- Prediction Error Plot
    // Observed against predicted, with the identity line and the least-squares
    // fit. The gap between the two lines IS the bias, and R-squared is on the
    // legend because a scatter without it invites optimism.
    if(in.engine==QLatin1String("Prediction Error Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& actual=in.series.at(0).y;
            const QVector<double>& predicted=in.series.at(1).y;
            const int n=qMin(actual.size(),predicted.size());
            PlotSeries pts;
            pts.color=in.series.at(0).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=3.6;
            QVector<double> ax,py;
            for(int i=0;i<n;++i){
                if(!finite(actual[i])||!finite(predicted[i])) continue;
                pts.x.append(actual[i]); pts.y.append(predicted[i]);
                ax.append(actual[i]); py.append(predicted[i]);
            }
            if(!pts.x.isEmpty()){
                const Moments2 m=momentsOf(ax,py);
                const double r=pearson(m);
                pts.label=QStringLiteral("R² = %1").arg(r*r,0,'f',3);
                out.series.append(pts);

                const Bounds b=boundsOf(ax);
                if(b.valid){
                    PlotSeries identity;
                    identity.label=QStringLiteral("identity");
                    identity.x={b.lo,b.hi}; identity.y={b.lo,b.hi};
                    identity.color=in.style.gridColor;
                    identity.dashPattern={5,4};
                    out.series.append(identity);
                    if(m.n>=2&&m.sxx>1e-15){
                        const double slope=m.sxy/m.sxx;
                        const double intercept=m.my-slope*m.mx;
                        PlotSeries fit;
                        fit.label=QStringLiteral("best fit");
                        fit.x={b.lo,b.hi};
                        fit.y={intercept+slope*b.lo,intercept+slope*b.hi};
                        fit.color=in.style.warning;
                        fit.lineWidth=qMax(1.2,in.style.lineWidth);
                        out.series.append(fit);
                    }
                }
            }
            out.xAxis=PlotAxis{in.series.at(0).label.isEmpty()?QStringLiteral("observed")
                                                              :in.series.at(0).label,
                               false,unsetValue(),unsetValue()};
            out.yAxis=PlotAxis{in.series.at(1).label.isEmpty()?QStringLiteral("predicted")
                                                              :in.series.at(1).label,
                               false,unsetValue(),unsetValue()};
        }
        return out;
    }

    // ----------------------------------- Learning Curve / Validation Curve
    // Two scores against a swept quantity - training-set size for one, a
    // hyperparameter for the other - so the gap between them can be read. A
    // training score that stays high while the validation score falls away is
    // overfitting, and it is invisible if only one of the two is drawn.
    if(in.engine==QLatin1String("Learning Curve")
       ||in.engine==QLatin1String("Validation Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        const bool learning=(in.engine==QLatin1String("Learning Curve"));
        if(in.series.size()>=2){
            static const char* kNames[2]={"training","validation"};
            for(int k=0;k<2;++k){
                const PlotSeries& s=in.series.at(k);
                PlotSeries line;
                line.label=s.label.isEmpty()?QLatin1String(kNames[k]):s.label;
                line.color=s.color;
                line.lineWidth=qMax(1.2,in.style.lineWidth);
                line.drawMarkers=s.y.size()<=80;
                line.markerSize=3.6;
                const int n=qMin(s.x.size(),s.y.size());
                for(int i=0;i<n;++i){
                    if(!finite(s.x[i])||!finite(s.y[i])) continue;
                    line.x.append(s.x[i]); line.y.append(s.y[i]);
                }
                if(!line.x.isEmpty()) out.series.append(line);
            }

            // WHAT EACH OF THE TWO IS ACTUALLY READ FOR.
            //
            // Both drew the same two lines and differed only in the x axis
            // label, which is two catalogue entries and one figure. They are
            // not the same question:
            //
            //   a LEARNING curve asks whether more data would help, and is
            //   read at the point where the validation score stops climbing;
            //
            //   a VALIDATION curve asks which value of the hyperparameter is
            //   best, and is read at the peak of the validation score.
            //
            // The gap between the two curves at that point is the other half
            // of either reading - a model that scores 0.99 on its training set
            // and 0.72 on held-out data is overfitting, and the number that
            // says so is the difference, not either curve alone.
            if(out.series.size()>=2){
                const PlotSeries& train=out.series.at(0);
                const PlotSeries& valid=out.series.at(1);
                const int n=qMin(train.y.size(),valid.y.size());
                int mark=-1;
                if(n>=3){
                    if(learning){
                        // The first point past which the validation score
                        // gains less than 2% of its total rise per step - the
                        // plateau, which is where more data stops paying.
                        double lo=valid.y.first(),hi=valid.y.first();
                        for(double v:valid.y){ lo=qMin(lo,v); hi=qMax(hi,v); }
                        const double rise=hi-lo;
                        if(rise>0.0)
                            for(int i=1;i<n;++i)
                                if(valid.y[i]-valid.y[i-1]<rise*0.02){ mark=i; break; }
                    }else{
                        double best=-std::numeric_limits<double>::infinity();
                        for(int i=0;i<n;++i)
                            if(finite(valid.y[i])&&valid.y[i]>best){ best=valid.y[i]; mark=i; }
                    }
                }
                if(mark>=0&&mark<n){
                    const double gap=train.y[mark]-valid.y[mark];
                    PlotSeries at;
                    at.label=learning
                        ? QStringLiteral("plateau at %1; gap %2")
                              .arg(valid.x[mark],0,'g',4).arg(gap,0,'g',3)
                        : QStringLiteral("best at %1, score %2; gap %3")
                              .arg(valid.x[mark],0,'g',4).arg(valid.y[mark],0,'g',4)
                              .arg(gap,0,'g',3);
                    at.color=in.style.positive;
                    at.drawLine=false; at.drawMarkers=true;
                    at.markerSize=8.0; at.markerSizeExplicit=true;
                    at.x={valid.x[mark]}; at.y={valid.y[mark]};
                    out.series.append(at);
                }
            }
        }
        out.xAxis=PlotAxis{learning?QStringLiteral("training examples")
                                   :QStringLiteral("hyperparameter"),
                           false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("score"),false,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------------- Discrimination Threshold
    // Precision, recall, F1 and the queue rate, all against the decision
    // threshold. A classifier is chosen at a threshold, not at a point on a
    // curve, and this is the figure that says which threshold - the ROC curve
    // deliberately hides it.
    if(in.engine==QLatin1String("Discrimination Threshold")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& score=in.series.at(0).y;
            const QVector<double>& truth=in.series.at(1).y;
            const int n=qMin(score.size(),truth.size());
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            double tLo=lo,tHi=-lo;
            for(int i=0;i<n;++i){
                if(!finite(score[i])||!finite(truth[i])) continue;
                lo=qMin(lo,score[i]); hi=qMax(hi,score[i]);
                tLo=qMin(tLo,truth[i]); tHi=qMax(tHi,truth[i]);
            }
            const double cut=(finite(tLo)&&finite(tHi))?(tLo+tHi)/2.0:0.5;
            if(finite(lo)&&hi>lo){
                PlotSeries precision,recall,f1,queue;
                precision.label=QStringLiteral("precision");
                recall.label=QStringLiteral("recall");
                f1.label=QStringLiteral("F1");
                queue.label=QStringLiteral("queue rate");
                precision.color=in.style.positive;
                recall.color=in.series.at(0).color;
                f1.color=in.style.warning;
                queue.color=in.style.gridColor;
                queue.dashPattern={4,3};
                constexpr int kSteps=60;
                for(int s=0;s<=kSteps;++s){
                    const double th=lo+(hi-lo)*double(s)/double(kSteps);
                    int tp=0,fp=0,fn=0,flagged=0,total=0;
                    for(int i=0;i<n;++i){
                        if(!finite(score[i])||!finite(truth[i])) continue;
                        ++total;
                        const bool positive=truth[i]>cut;
                        const bool called=score[i]>=th;
                        if(called) ++flagged;
                        if(called&&positive) ++tp;
                        else if(called&&!positive) ++fp;
                        else if(!called&&positive) ++fn;
                    }
                    if(total==0) continue;
                    // A threshold that flags nothing has no precision - not a
                    // precision of zero, and not of one. Left as a gap.
                    const double p=(tp+fp>0)?double(tp)/double(tp+fp)
                                            :std::numeric_limits<double>::quiet_NaN();
                    const double r=(tp+fn>0)?double(tp)/double(tp+fn)
                                            :std::numeric_limits<double>::quiet_NaN();
                    precision.x.append(th); precision.y.append(p);
                    recall.x.append(th);    recall.y.append(r);
                    f1.x.append(th);
                    f1.y.append((finite(p)&&finite(r)&&p+r>0.0)?2.0*p*r/(p+r)
                                                               :std::numeric_limits<double>::quiet_NaN());
                    queue.x.append(th);     queue.y.append(double(flagged)/double(total));
                }
                out.series.append(precision);
                out.series.append(recall);
                out.series.append(f1);
                out.series.append(queue);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("decision threshold"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("rate"),false,0.0,1.0};
        return out;
    }

    // ----------------------------------------------------- Silhouette Plot
    // One bar per observation, sorted within its cluster, with the overall mean
    // marked. A cluster whose bars are short or negative is one whose members
    // are closer to a different cluster than to their own - which no measure of
    // total inertia will tell you.
    if(in.engine==QLatin1String("Silhouette Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Horizontal Bar"));
        out.legendVisible=false;
        if(in.series.size()>=2){
            const QVector<double>& cluster=in.series.at(0).y;
            const QVector<double>& score=in.series.at(1).y;
            const int n=qMin(cluster.size(),score.size());
            QVector<QPair<double,double>> rows;
            double sum=0.0; int used=0;
            for(int i=0;i<n;++i){
                if(!finite(cluster[i])||!finite(score[i])) continue;
                rows.append({cluster[i],score[i]});
                sum+=score[i]; ++used;
            }
            if(used>0){
                // Cluster ascending, and within a cluster the widest bar first,
                // which is the shape the figure is recognised by.
                std::sort(rows.begin(),rows.end(),
                          [](const QPair<double,double>& a,const QPair<double,double>& b){
                              if(a.first!=b.first) return a.first<b.first;
                              return a.second>b.second;
                          });
                // 400 bars is already a solid block; beyond that they are
                // thinner than a pixel and the picture stops changing.
                const int stride=qMax(1,int(rows.size()/400));
                int slot=1;
                double last=rows.first().first;
                for(int i=0;i<rows.size();i+=stride){
                    if(rows[i].first!=last){ ++slot; last=rows[i].first; }
                    PlotSeries bar;
                    bar.label=(i==0)?QStringLiteral("silhouette"):QString();
                    bar.color=categoryColour(in,int(std::abs(rows[i].first)),std::fmod(std::abs(rows[i].first)*0.17,1.0),0.45,0.9);
                    bar.x={rows[i].second};
                    bar.y={double(slot)};
                    out.series.append(bar);
                    ++slot;
                }
                out.series.append(horizontalRule(0.0,sum/double(used),sum/double(used),
                                                 in.style.foreground,
                                                 QStringLiteral("mean %1")
                                                     .arg(sum/double(used),0,'f',3),true));
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("silhouette coefficient"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("observation, by cluster"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------- Elbow Plot
    // A clustering score against the number of clusters, with the knee marked
    // by maximum distance from the chord between the first and last points -
    // the standard construction, and better than eyeballing it because two
    // people eyeballing the same curve disagree.
    if(in.engine==QLatin1String("Elbow Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            PlotSeries line;
            line.label=s.label;
            line.color=s.color;
            line.lineWidth=qMax(1.2,in.style.lineWidth);
            line.drawMarkers=true; line.markerSize=4.0;
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                line.x.append(s.x[i]); line.y.append(s.y[i]);
            }
            if(line.x.size()<3){ if(!line.x.isEmpty()) out.series.append(line); continue; }
            out.series.append(line);

            const double x0=line.x.first(),y0=line.y.first();
            const double x1=line.x.last(), y1=line.y.last();
            const double dx=x1-x0, dy=y1-y0;
            const double len=std::hypot(dx,dy);
            if(!(len>0.0)) continue;
            int best=-1; double bestD=-1.0;
            for(int i=1;i<line.x.size()-1;++i){
                const double d=std::abs(dy*line.x[i]-dx*line.y[i]+x1*y0-y1*x0)/len;
                if(d>bestD){ bestD=d; best=i; }
            }
            if(best>0){
                PlotSeries knee;
                knee.label=QStringLiteral("knee at %1").arg(line.x[best],0,'g',4);
                knee.x={line.x[best]}; knee.y={line.y[best]};
                knee.color=in.style.warning;
                knee.drawLine=false; knee.drawMarkers=true; knee.markerSize=8.0;
                out.series.append(knee);
            }
            break;   // one curve's knee; several would be a different figure
        }
        out.xAxis=PlotAxis{QStringLiteral("clusters"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("score"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------ Radial (Galbraith) Plot
    // Standardised effect against precision. Every study is plotted at the same
    // vertical scale in standard errors, so a small imprecise study cannot look
    // as convincing as a large precise one - which is exactly what it does on a
    // forest plot, where both get a row of the same height.
    if(in.engine==QLatin1String("Radial Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& effect=in.series.at(0).y;
            const QVector<double>& se=in.series.at(1).y;
            const int n=qMin(effect.size(),se.size());
            PlotSeries pts;
            pts.label=QStringLiteral("studies");
            pts.color=in.series.at(0).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;
            double wSum=0.0,weSum=0.0,xHi=0.0;
            for(int i=0;i<n;++i){
                if(!finite(effect[i])||!finite(se[i])||!(se[i]>0.0)) continue;
                const double precision=1.0/se[i];
                pts.x.append(precision);
                pts.y.append(effect[i]*precision);
                const double w=precision*precision;
                wSum+=w; weSum+=w*effect[i];
                xHi=qMax(xHi,precision);
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                if(wSum>0.0&&xHi>0.0){
                    // The pooled effect is the SLOPE through the origin, not a
                    // horizontal line: that is what makes the plot radial.
                    const double pooled=weSum/wSum;
                    PlotSeries ray;
                    ray.label=QStringLiteral("pooled %1").arg(pooled,0,'f',3);
                    ray.x={0.0,xHi}; ray.y={0.0,pooled*xHi};
                    ray.color=in.style.warning;
                    ray.lineWidth=qMax(1.2,in.style.lineWidth);
                    out.series.append(ray);
                    for(int k=-2;k<=2;k+=2){
                        if(k==0) continue;
                        PlotSeries band;
                        band.label=QStringLiteral("%1%2 SE").arg(k>0?"+":"").arg(k);
                        band.x={0.0,xHi}; band.y={double(k),pooled*xHi+double(k)};
                        band.color=in.style.gridColor;
                        band.dashPattern={4,3};
                        out.series.append(band);
                    }
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("precision  (1 / standard error)"),
                           false,0.0,unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("standardised effect  (effect / SE)"),
                           false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------------- L'Abbé Plot
    // The event rate in the treated arm against the rate in the control arm,
    // one point per study, against the line of no effect. Points scattered
    // across the diagonal rather than sitting parallel to it are heterogeneity
    // you can see, which a pooled estimate and an I-squared cannot show you.
    // QStringLiteral for the same reason as the Gompertz branch above: the
    // accented e is two UTF-8 bytes, and QLatin1String would read them as two
    // separate characters, so the name never matched and this never ran.
    if(in.engine==QStringLiteral("L'Abbé Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& control=in.series.at(0).y;
            const QVector<double>& treated=in.series.at(1).y;
            const int n=qMin(control.size(),treated.size());
            PlotSeries pts;
            pts.label=QStringLiteral("studies");
            pts.color=in.series.at(0).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            for(int i=0;i<n;++i){
                if(!finite(control[i])||!finite(treated[i])) continue;
                pts.x.append(control[i]); pts.y.append(treated[i]);
                lo=qMin(lo,qMin(control[i],treated[i]));
                hi=qMax(hi,qMax(control[i],treated[i]));
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                if(finite(lo)&&hi>lo){
                    PlotSeries same;
                    same.label=QStringLiteral("no difference");
                    same.x={lo,hi}; same.y={lo,hi};
                    same.color=in.style.gridColor;
                    same.dashPattern={5,4};
                    out.series.append(same);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("control arm"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("treated arm"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------- Caterpillar Plot
    // A forest plot with its rows sorted by estimate, so the shape of the whole
    // body of evidence is visible rather than the order the studies happened to
    // be entered in. Derived onto Forest Plot, which already draws an estimate
    // with an interval.
    if(in.engine==QLatin1String("Caterpillar Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Forest Plot"));
        if(in.series.size()>=3){
            const QVector<double>& est=in.series.at(0).y;
            const QVector<double>& lo=in.series.at(1).y;
            const QVector<double>& hi=in.series.at(2).y;
            const int n=qMin(est.size(),qMin(lo.size(),hi.size()));
            QVector<int> order;
            for(int i=0;i<n;++i)
                if(finite(est[i])&&finite(lo[i])&&finite(hi[i])) order.append(i);
            std::sort(order.begin(),order.end(),
                      [&est](int a,int b){ return est[a]<est[b]; });
            PlotSeries e,l,h;
            e.label=in.series.at(0).label; e.color=in.series.at(0).color;
            l.label=in.series.at(1).label; h.label=in.series.at(2).label;
            for(int i:order){ e.y.append(est[i]); l.y.append(lo[i]); h.y.append(hi[i]); }
            for(int i=0;i<order.size();++i){ e.x.append(double(i)); l.x.append(double(i)); h.x.append(double(i)); }
            if(!e.y.isEmpty()){ out.series.append(e); out.series.append(l); out.series.append(h); }
        }
        // BACK THROUGH THE FOREST BRANCH, which is the only thing that knows
        // how drawForest wants its input.
        //
        // This returned three full-length COLUMNS with the engine set to
        // Forest Plot and stopped there - but a rewrite's output does not come
        // back through here, so the painter received the columns raw. It reads
        // x as a single row position and y as {estimate, low, high}, so it drew
        // three bars all on row zero out of the first three studies and
        // discarded the rest. The figure was a handful of marks in a corner of
        // an empty plot, and the sorting the engine exists for was invisible.
        //
        // Recursing hands the sorted columns to the Forest branch, which packs
        // one single-point series per row exactly as it does for a forest plot.
        // Same packing, one copy of it.
        return prepareSpecCore(out);
    }

    // ---------------------------------------- Cumulative Meta-Analysis Plot
    // The pooled estimate recomputed as each study is added, so it can be seen
    // when the answer stopped changing. A conclusion that was already stable
    // after four of forty studies is a different finding from one that only
    // settled at the fortieth, and a plain forest plot shows neither.
    if(in.engine==QLatin1String("Cumulative Meta-Analysis")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& effect=in.series.at(0).y;
            const QVector<double>& variance=in.series.at(1).y;
            const int n=qMin(effect.size(),variance.size());
            PlotSeries pooled,upper,lower;
            pooled.label=QStringLiteral("pooled estimate");
            pooled.color=in.series.at(0).color;
            pooled.lineWidth=qMax(1.4,in.style.lineWidth);
            pooled.drawMarkers=n<=80; pooled.markerSize=3.6;
            upper.label=QStringLiteral("95% CI");
            lower.label=QString();
            upper.color=lower.color=in.style.gridColor;
            upper.dashPattern=lower.dashPattern={4,3};
            double wSum=0.0,weSum=0.0;
            int step=0;
            for(int i=0;i<n;++i){
                if(!finite(effect[i])||!finite(variance[i])||!(variance[i]>0.0)) continue;
                // Inverse-variance weighting, which is the fixed-effect pooled
                // estimate. A random-effects version needs tau-squared and is a
                // different engine rather than a silent variation on this one.
                const double w=1.0/variance[i];
                wSum+=w; weSum+=w*effect[i];
                ++step;
                const double mu=weSum/wSum;
                const double se=std::sqrt(1.0/wSum);
                pooled.x.append(double(step)); pooled.y.append(mu);
                upper.x.append(double(step));  upper.y.append(mu+1.96*se);
                lower.x.append(double(step));  lower.y.append(mu-1.96*se);
            }
            if(!pooled.x.isEmpty()){
                out.series.append(pooled);
                out.series.append(upper);
                out.series.append(lower);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("studies included"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("pooled effect"),false,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------- Nelson-Aalen Cumulative Hazard
    // The cumulative hazard rather than the survival probability. The same
    // information as a Kaplan-Meier curve, plotted so that a constant hazard is
    // a straight line - which is the reason to draw it, because departure from
    // straight is what a survival curve makes hard to judge.
    if(in.engine==QLatin1String("Nelson-Aalen Cumulative Hazard")
       ||in.engine==QLatin1String("Cumulative Incidence")){
        const bool incidence=(in.engine==QLatin1String("Cumulative Incidence"));
        PlotSpec out=derivedAs(in,QStringLiteral("Stairs"));
        if(in.series.size()>=2){
            const QVector<double>& time=in.series.at(0).y;
            const QVector<double>& event=in.series.at(1).y;
            const int n=qMin(time.size(),event.size());
            QVector<QPair<double,bool>> rows;
            double eLo=std::numeric_limits<double>::infinity(),eHi=-eLo;
            for(int i=0;i<n;++i){
                if(!finite(time[i])||!finite(event[i])) continue;
                eLo=qMin(eLo,event[i]); eHi=qMax(eHi,event[i]);
            }
            // Anything above the midpoint of the event column counts as an
            // event, so 0/1, 1/2 and true/false all behave - the same rule the
            // ROC engine uses, deliberately.
            const double cut=(finite(eLo)&&finite(eHi))?(eLo+eHi)/2.0:0.5;
            for(int i=0;i<n;++i){
                if(!finite(time[i])||!finite(event[i])) continue;
                rows.append({time[i],event[i]>cut});
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,bool>& a,const QPair<double,bool>& b){
                          return a.first<b.first;
                      });
            if(rows.size()>=2){
                PlotSeries curve;
                curve.label=incidence?QStringLiteral("cumulative incidence")
                                     :QStringLiteral("cumulative hazard");
                curve.color=in.series.at(0).color;
                curve.lineWidth=qMax(1.3,in.style.lineWidth);
                int atRisk=rows.size();
                double cumulativeHazard=0.0, survival=1.0;
                curve.x.append(rows.first().first);
                curve.y.append(0.0);
                int i=0;
                while(i<rows.size()){
                    const double t=rows[i].first;
                    int events=0,tied=0;
                    while(i+tied<rows.size()&&rows[i+tied].first==t){
                        if(rows[i+tied].second) ++events;
                        ++tied;
                    }
                    if(events>0&&atRisk>0){
                        cumulativeHazard+=double(events)/double(atRisk);
                        survival*=(1.0-double(events)/double(atRisk));
                        curve.x.append(t);
                        curve.y.append(incidence?(1.0-survival):cumulativeHazard);
                    }
                    atRisk-=tied;
                    i+=tied;
                }
                if(curve.x.size()>=2) out.series.append(curve);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("time"),false,unsetValue(),unsetValue()};
        out.yAxis=incidence
            ? PlotAxis{QStringLiteral("cumulative incidence"),false,0.0,unsetValue()}
            : PlotAxis{QStringLiteral("cumulative hazard"),false,0.0,unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Decision Curve
    // Net benefit against the threshold probability, with treat-all and
    // treat-none for comparison. A model is only worth using where its curve is
    // above BOTH of those, and a model with a fine AUC can fail that over the
    // whole range of thresholds anyone would actually use.
    if(in.engine==QLatin1String("Decision Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& prob=in.series.at(0).y;
            const QVector<double>& truth=in.series.at(1).y;
            const int n=qMin(prob.size(),truth.size());
            double tLo=std::numeric_limits<double>::infinity(),tHi=-tLo;
            double pLo=tLo,pHi=-tLo;
            for(int i=0;i<n;++i){
                if(!finite(prob[i])||!finite(truth[i])) continue;
                tLo=qMin(tLo,truth[i]); tHi=qMax(tHi,truth[i]);
                pLo=qMin(pLo,prob[i]);  pHi=qMax(pHi,prob[i]);
            }
            const double cut=(finite(tLo)&&finite(tHi))?(tLo+tHi)/2.0:0.5;
            // The score need not already be a probability; rescaled onto 0..1
            // so a threshold probability means what it says.
            const double span=(finite(pLo)&&pHi>pLo)?(pHi-pLo):1.0;
            int positives=0,total=0;
            for(int i=0;i<n;++i){
                if(!finite(prob[i])||!finite(truth[i])) continue;
                ++total; if(truth[i]>cut) ++positives;
            }
            if(total>0){
                PlotSeries model,all,none;
                model.label=QStringLiteral("model");
                model.color=in.series.at(0).color;
                model.lineWidth=qMax(1.3,in.style.lineWidth);
                all.label=QStringLiteral("treat all");
                all.color=in.style.warning; all.dashPattern={5,3};
                none.label=QStringLiteral("treat none");
                none.color=in.style.gridColor;
                const double prevalence=double(positives)/double(total);
                constexpr int kSteps=60;
                for(int s=1;s<kSteps;++s){
                    const double pt=double(s)/double(kSteps);
                    int tp=0,fp=0;
                    for(int i=0;i<n;++i){
                        if(!finite(prob[i])||!finite(truth[i])) continue;
                        const double scaled=(prob[i]-pLo)/span;
                        if(scaled<pt) continue;
                        if(truth[i]>cut) ++tp; else ++fp;
                    }
                    const double w=pt/(1.0-pt);
                    model.x.append(pt);
                    model.y.append(double(tp)/double(total)-double(fp)/double(total)*w);
                    all.x.append(pt);
                    all.y.append(prevalence-(1.0-prevalence)*w);
                    none.x.append(pt);
                    none.y.append(0.0);
                }
                out.series.append(model);
                out.series.append(all);
                out.series.append(none);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("threshold probability"),false,0.0,1.0};
        out.yAxis=PlotAxis{QStringLiteral("net benefit"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Bathtub Curve
    // The hazard rate against age, binned from a column of lifetimes. Infant
    // mortality falling, a flat useful life, and wear-out rising - and the
    // point of drawing it is to find out which of the three regions your fleet
    // is actually in, which a mean time between failures cannot tell you.
    if(in.engine==QLatin1String("Bathtub Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            v.erase(std::remove_if(v.begin(),v.end(),[](double d){ return d<0.0; }),v.end());
            if(v.size()<8) continue;
            std::sort(v.begin(),v.end());
            const double lo=v.first(), hi=v.last();
            if(!(hi>lo)) continue;
            const int bins=qBound(6,int(std::sqrt(double(v.size()))),40);
            const double width=(hi-lo)/bins;
            QVector<int> failed(bins,0);
            for(double d:v) failed[qBound(0,int((d-lo)/width),bins-1)]+=1;
            PlotSeries hazard;
            hazard.label=QStringLiteral("%1 — hazard").arg(s.label);
            hazard.color=s.color;
            hazard.lineWidth=qMax(1.3,in.style.lineWidth);
            hazard.drawMarkers=true; hazard.markerSize=3.4;
            int survivors=v.size();
            for(int b=0;b<bins;++b){
                if(survivors<=0) break;
                // Hazard is failures in the interval divided by those still at
                // risk at its start, not by the original population: the second
                // is a failure DENSITY and slopes down even when the hazard is
                // flat, which is how a bathtub curve gets drawn upside down.
                hazard.x.append(lo+width*(b+0.5));
                hazard.y.append(double(failed[b])/double(survivors)/width);
                survivors-=failed[b];
            }
            if(!hazard.x.isEmpty()) out.series.append(hazard);
        }
        out.xAxis=PlotAxis{QStringLiteral("age"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("hazard rate"),false,0.0,unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Sequence Logo
    // Nothing to transform - the columns are already the counts - but the y
    // axis has to be set here rather than left to the data. The frame is built
    // from the series' values, which are counts, so without this the axis would
    // run from zero to the largest count and the stacks, which are measured in
    // BITS, would occupy a sliver at the bottom of it. The ceiling is log2 of
    // the alphabet size, which is the most any position can carry.
    if(in.engine==QLatin1String("Sequence Logo")){
        PlotSpec out=in;
        const int symbols=qMax(2,int(in.series.size()));
        out.legendVisible=true;
        // The x range is padded by half a position at each end. A stack is
        // drawn CENTRED on its position, so with the frame running exactly from
        // the first position to the last, the first and last stacks had half a
        // letter outside the frame and clipped away.
        double lo=unsetValue(),hi=unsetValue();
        if(!in.series.isEmpty()){
            QVector<double> centres;
            for(double x:in.series.at(0).x) if(finite(x)) centres.append(x);
            if(!centres.isEmpty()){
                const Bounds across=boundsOf(centres);
                const double slot=slotWidthFrom(centres,1.0,
                                                qMax(1.0,across.hi-across.lo));
                lo=across.lo-slot*0.6;
                hi=across.hi+slot*0.6;
            }
        }
        out.xAxis=PlotAxis{in.xAxis.label.isEmpty()?QStringLiteral("position")
                                                   :in.xAxis.label,
                           false,lo,hi};
        out.yAxis=PlotAxis{QStringLiteral("information (bits)"),false,
                           0.0,std::log2(double(symbols))};
        return out;
    }

    // -------------------------------------------------------- Dalitz Plot
    // The columns pass through untouched - the painter reads the two, or six,
    // it was given - but the axes have to be set here. The frame is built from
    // every series, and in the six-column form four of them are masses: left
    // to itself it would size the picture to include a column of 1.865s
    // alongside invariant masses of a few tenths, and the events would occupy
    // a corner of it.
    if(in.engine==QLatin1String("Dalitz Plot")){
        PlotSpec out=in;
        for(PlotSeries& s:out.series){ s.drawLine=false; s.drawMarkers=false; }
        out.legendVisible=false;
        double xLo=unsetValue(),xHi=unsetValue(),yLo=unsetValue(),yHi=unsetValue();
        if(in.series.size()>=2){
            const Bounds across=boundsOf(in.series.at(0).y);
            const Bounds up=boundsOf(in.series.at(1).y);
            // Padded by a twentieth, so the boundary the painter draws is not
            // pressed against the frame - it usually reaches further than the
            // events do, since the corners of the region are the least
            // populated part of phase space.
            const double padX=qMax(1e-12,(across.hi-across.lo)*0.05);
            const double padY=qMax(1e-12,(up.hi-up.lo)*0.05);
            xLo=across.lo-padX; xHi=across.hi+padX;
            yLo=up.lo-padY;     yHi=up.hi+padY;
        }
        out.xAxis=PlotAxis{in.xAxis.label.isEmpty()?QStringLiteral("m2(12)")
                                                   :in.xAxis.label,
                           false,xLo,xHi};
        out.yAxis=PlotAxis{in.yAxis.label.isEmpty()?QStringLiteral("m2(23)")
                                                   :in.yAxis.label,
                           false,yLo,yHi};
        return out;
    }

    // ---------------------------------------------------- Pourbaix Diagram
    // The boundaries pass through untouched; only the axes are named, because a
    // Pourbaix diagram with unlabelled axes is indistinguishable from any other
    // pair of lines and the two water lines the engine draws are quoted against
    // the standard hydrogen electrode specifically.
    if(in.engine==QLatin1String("Pourbaix Diagram")){
        PlotSpec out=in;
        for(PlotSeries& s:out.series){
            s.drawLine=true;
            s.drawMarkers=s.y.size()<=40;
            s.lineWidth=qMax(1.2,in.style.lineWidth);
        }
        out.xAxis=PlotAxis{in.xAxis.label.isEmpty()?QStringLiteral("pH")
                                                   :in.xAxis.label,
                           false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{in.yAxis.label.isEmpty()
                               ?QStringLiteral("E (V vs SHE)"):in.yAxis.label,
                           false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------ Tripartite Response Spectrum
    // The engine keeps its own name here rather than deriving into another,
    // because the rewrite has nothing to transform: the two columns are already
    // period and pseudo-velocity. What it does is FORCE both axes logarithmic,
    // which is not a preference. The displacement and acceleration families are
    // straight lines only on log-log, and a tripartite grid drawn on linear
    // axes is four curved families that cannot be read - so this is a property
    // of the chart rather than a setting to be left to whoever opens it.
    if(in.engine==QLatin1String("Tripartite Response Spectrum")){
        PlotSpec out=in;
        for(PlotSeries& s:out.series){
            s.drawLine=true;
            s.drawMarkers=s.y.size()<=60;
            s.lineWidth=qMax(1.3,in.style.lineWidth);
        }
        out.xAxis=PlotAxis{in.xAxis.label.isEmpty()?QStringLiteral("period")
                                                   :in.xAxis.label,
                           true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{in.yAxis.label.isEmpty()
                               ?QStringLiteral("pseudo-velocity"):in.yAxis.label,
                           true,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------- Duane Plot
    // Cumulative mean time between failures against cumulative operating time,
    // on log-log axes, where reliability growth is a straight line and its
    // slope is the growth rate. Crow-AMSAA in its original graphical form.
    if(in.engine==QLatin1String("Duane Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            PlotSeries pts;
            pts.label=s.label;
            pts.color=s.color;
            pts.drawLine=true; pts.drawMarkers=n<=80; pts.markerSize=3.6;
            QVector<double> lx,ly;
            for(int i=0;i<n;++i){
                const double t=s.x[i];
                const double failures=s.y[i];
                if(!finite(t)||!finite(failures)||!(t>0.0)||!(failures>0.0)) continue;
                const double mtbf=t/failures;
                pts.x.append(t); pts.y.append(mtbf);
                lx.append(std::log10(t)); ly.append(std::log10(mtbf));
            }
            if(pts.x.isEmpty()) continue;
            out.series.append(pts);
            const Moments2 m=momentsOf(lx,ly);
            if(m.n>=2&&m.sxx>1e-15){
                const double slope=m.sxy/m.sxx;
                const double intercept=m.my-slope*m.mx;
                const Bounds b=boundsOf(lx);
                PlotSeries fit;
                fit.label=QStringLiteral("growth slope %1").arg(slope,0,'f',3);
                fit.x={std::pow(10.0,b.lo),std::pow(10.0,b.hi)};
                fit.y={std::pow(10.0,intercept+slope*b.lo),
                       std::pow(10.0,intercept+slope*b.hi)};
                fit.color=in.style.warning;
                fit.lineWidth=qMax(1.2,in.style.lineWidth);
                out.series.append(fit);
            }
            break;
        }
        out.xAxis=PlotAxis{QStringLiteral("cumulative operating time"),true,
                           unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("cumulative MTBF"),true,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------------- Mean Cumulative Function
    // The average number of repairs a unit has had by a given age, across a
    // fleet. The right tool for repairable systems, where a survival curve is
    // simply the wrong model: a pump that has been fixed four times has not
    // died four times.
    if(in.engine==QLatin1String("Mean Cumulative Function")){
        PlotSpec out=derivedAs(in,QStringLiteral("Stairs"));
        if(in.series.size()>=2){
            const QVector<double>& age=in.series.at(0).y;
            const QVector<double>& unit=in.series.at(1).y;
            const int n=qMin(age.size(),unit.size());
            QSet<double> units;
            QVector<double> ages;
            for(int i=0;i<n;++i){
                if(!finite(age[i])||!finite(unit[i])) continue;
                units.insert(unit[i]);
                ages.append(age[i]);
            }
            const int fleet=qMax(1,units.size());
            std::sort(ages.begin(),ages.end());
            if(ages.size()>=2){
                PlotSeries mcf;
                // The fleet size is what the function is a mean OVER, so a
                // reader cannot interpret the ordinate without it - and it was
                // in the only named series' label, which is not drawn.
                mcf.label=QStringLiteral("mean cumulative function");
                out.figureNote=QStringLiteral("Mean over %1 unit%2, so the ordinate is "
                                              "repairs per unit.")
                                   .arg(fleet).arg(fleet==1?QString():QStringLiteral("s"));
                mcf.color=in.series.at(0).color;
                mcf.lineWidth=qMax(1.3,in.style.lineWidth);
                for(int i=0;i<ages.size();++i){
                    mcf.x.append(ages[i]);
                    mcf.y.append(double(i+1)/double(fleet));
                }
                out.series.append(mcf);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("age"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("mean cumulative repairs per unit"),
                           false,0.0,unsetValue()};
        return out;
    }
    // =====================================================================
    // Batch 2: engineering. Thermo-fluids, materials, fatigue, rotating
    // machinery, geotechnics, power and control.
    //
    // Several of these draw a REFERENCE OVERLAY computed from a formula
    // alongside the measured data - Moody's roughness family, Casagrande's
    // A-line, the Goodman envelope, the ITIC tolerance curve. That overlay is
    // the whole reason the figure exists: a friction factor on its own is a
    // number, and on the Moody chart it is a number you can see the regime of.
    // =====================================================================

    // -------------------------------------------------------- Moody Diagram
    // Darcy friction factor against Reynolds number, over the family of
    // relative-roughness curves. The measured points are yours; the curves are
    // Colebrook-White, solved by fixed-point iteration, and the laminar line is
    // 64/Re exactly.
    if(in.engine==QLatin1String("Moody Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        // Laminar, up to the critical Reynolds number.
        {
            PlotSeries laminar;
            laminar.label=QStringLiteral("laminar  64/Re");
            laminar.color=in.style.foreground;
            laminar.lineWidth=qMax(1.2,in.style.lineWidth);
            for(double re=640.0;re<=2300.0;re*=1.08){
                laminar.x.append(re); laminar.y.append(64.0/re);
            }
            out.series.append(laminar);
        }
        // Turbulent, one curve per relative roughness. Colebrook-White is
        // implicit in f, so it is iterated: five passes from the Swamee-Jain
        // explicit approximation is converged to well past plotting precision.
        static const double kRoughness[]={0.0,1e-5,1e-4,5e-4,1e-3,4e-3,1e-2,3e-2,5e-2};
        int idx=0;
        for(double eps:kRoughness){
            PlotSeries curve;
            curve.label=(eps==0.0)?QStringLiteral("smooth")
                                  :QStringLiteral("ε/D = %1").arg(eps,0,'g',2);
            curve.color=colourmaps::sample(colourMapFor(in.style.colourMap),
                                           double(idx)/8.0);
            curve.lineWidth=qMax(0.8,in.style.lineWidth*0.8);
            ++idx;
            for(double re=4000.0;re<=1e8;re*=1.12){
                // Swamee-Jain as the starting guess.
                double f=0.25/std::pow(std::log10(eps/3.7+5.74/std::pow(re,0.9)),2.0);
                for(int it=0;it<5;++it){
                    const double rhs=-2.0*std::log10(eps/3.7+2.51/(re*std::sqrt(f)));
                    f=1.0/(rhs*rhs);
                }
                if(finite(f)&&f>0.0){ curve.x.append(re); curve.y.append(f); }
            }
            out.series.append(curve);
        }
        // The measurements, over the top.
        if(in.series.size()>=2){
            const QVector<double>& re=in.series.at(0).y;
            const QVector<double>& f=in.series.at(1).y;
            const int n=qMin(re.size(),f.size());
            PlotSeries pts;
            pts.label=QStringLiteral("measured");
            pts.color=in.style.warning;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            for(int i=0;i<n;++i)
                if(finite(re[i])&&finite(f[i])&&re[i]>0.0&&f[i]>0.0){
                    pts.x.append(re[i]); pts.y.append(f[i]);
                }
            if(!pts.x.isEmpty()) out.series.append(pts);
        }
        out.xAxis=PlotAxis{QStringLiteral("Reynolds number"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("Darcy friction factor"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------- Pump Performance Curve
    // Head against flow, with the system resistance curve over it. The duty
    // point is where they cross, and it is marked - reading it off by eye is
    // how a pump ends up specified for a flow it never runs at.
    if(in.engine==QLatin1String("Pump Performance Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const PlotSeries& flow=in.series.at(0);
            const PlotSeries& head=in.series.at(1);
            const int n=qMin(flow.y.size(),head.y.size());
            QVector<QPair<double,double>> curve;
            for(int i=0;i<n;++i)
                if(finite(flow.y[i])&&finite(head.y[i])&&flow.y[i]>=0.0)
                    curve.append({flow.y[i],head.y[i]});
            std::sort(curve.begin(),curve.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first;
                      });
            if(curve.size()>=2){
                PlotSeries pump;
                pump.label=QStringLiteral("pump head");
                pump.color=in.series.at(1).color;
                pump.lineWidth=qMax(1.4,in.style.lineWidth);
                double qMaxFlow=0.0,hAtZero=curve.first().second;
                for(const auto& c:curve){ pump.x.append(c.first); pump.y.append(c.second); qMaxFlow=qMax(qMaxFlow,c.first); }
                out.series.append(pump);

                // System resistance: static lift plus a loss rising with the
                // square of flow. Fitted so it meets the pump curve near the
                // middle of its range, which is where a duty point belongs.
                const double hMid=curve.at(curve.size()/2).second;
                const double qMid=qMax(1e-9,curve.at(curve.size()/2).first);
                const double staticHead=hAtZero*0.35;
                const double k=(hMid-staticHead)/(qMid*qMid);
                PlotSeries system;
                system.label=QStringLiteral("system resistance");
                system.color=in.style.warning;
                system.dashPattern={5,3};
                for(int i=0;i<=40;++i){
                    const double q=qMaxFlow*double(i)/40.0;
                    system.x.append(q); system.y.append(staticHead+k*q*q);
                }
                out.series.append(system);

                // Where they cross.
                double bestQ=0.0,bestH=0.0,bestGap=std::numeric_limits<double>::infinity();
                for(const auto& c:curve){
                    const double gap=std::abs(c.second-(staticHead+k*c.first*c.first));
                    if(gap<bestGap){ bestGap=gap; bestQ=c.first; bestH=c.second; }
                }
                if(finite(bestQ)){
                    PlotSeries duty;
                    duty.label=QStringLiteral("duty point  %1 @ %2").arg(bestQ,0,'g',4).arg(bestH,0,'g',4);
                    duty.x={bestQ}; duty.y={bestH};
                    duty.color=in.style.positive;
                    duty.drawLine=false; duty.drawMarkers=true; duty.markerSize=9.0;
                    out.series.append(duty);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("flow"),false,0.0,unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("head"),false,0.0,unsetValue()};
        return out;
    }

    // --------------------------------------------------------- Stribeck Curve
    // Friction coefficient against the Hersey number, showing the boundary,
    // mixed and hydrodynamic regimes. The minimum is the operating point a
    // bearing is designed around, and which side of it you are on decides
    // whether more speed helps or hurts.
    if(in.engine==QLatin1String("Stribeck Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& hersey=in.series.at(0).y;
            const QVector<double>& mu=in.series.at(1).y;
            const int n=qMin(hersey.size(),mu.size());
            QVector<QPair<double,double>> pts;
            for(int i=0;i<n;++i)
                if(finite(hersey[i])&&finite(mu[i])&&hersey[i]>0.0)
                    pts.append({hersey[i],mu[i]});
            std::sort(pts.begin(),pts.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first;
                      });
            if(pts.size()>=3){
                PlotSeries curve;
                curve.label=QStringLiteral("friction");
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.3,in.style.lineWidth);
                curve.drawMarkers=pts.size()<=80; curve.markerSize=3.4;
                int lowest=0;
                for(int i=0;i<pts.size();++i){
                    curve.x.append(pts[i].first); curve.y.append(pts[i].second);
                    if(pts[i].second<pts[lowest].second) lowest=i;
                }
                out.series.append(curve);
                PlotSeries minimum;
                minimum.label=QStringLiteral("regime change");
                minimum.x={pts[lowest].first}; minimum.y={pts[lowest].second};
                minimum.color=in.style.warning;
                minimum.drawLine=false; minimum.drawMarkers=true; minimum.markerSize=9.0;
                out.series.append(minimum);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("Hersey number  (η·N / P)"),true,
                           unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("coefficient of friction"),false,0.0,unsetValue()};
        return out;
    }

    // -------------------------------------------------------- Haigh Diagram
    // Alternating stress against mean stress, with the Goodman, Gerber and
    // Soderberg envelopes. A load point inside all three is safe on every
    // criterion; between them is where the choice of criterion decides the
    // answer, which is exactly what the figure is for.
    if(in.engine==QLatin1String("Haigh Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& mean=in.series.at(0).y;
            const QVector<double>& alt=in.series.at(1).y;
            const int n=qMin(mean.size(),alt.size());
            PlotSeries pts;
            pts.label=QStringLiteral("load cases");
            pts.color=in.series.at(0).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;
            double uts=0.0, se=0.0;
            for(int i=0;i<n;++i){
                if(!finite(mean[i])||!finite(alt[i])) continue;
                pts.x.append(mean[i]); pts.y.append(alt[i]);
                uts=qMax(uts,mean[i]); se=qMax(se,alt[i]);
            }
            if(!pts.x.isEmpty()&&uts>0.0&&se>0.0){
                out.series.append(pts);
                // The envelopes are anchored on the data's own extremes,
                // because the material's ultimate and endurance limits are not
                // in the columns. Marked as inferred in the labels so nobody
                // reads them as the material's real allowables.
                const double su=uts*1.15, sa=se*1.15, sy=su*0.75;
                PlotSeries goodman,gerber,soderberg;
                goodman.label=QStringLiteral("Goodman (inferred Su)");
                gerber.label=QStringLiteral("Gerber (inferred Su)");
                soderberg.label=QStringLiteral("Soderberg (inferred Sy)");
                goodman.color=in.style.danger;
                gerber.color=in.style.warning;
                soderberg.color=in.style.positive;
                gerber.dashPattern={5,3};
                soderberg.dashPattern={2,3};
                for(int i=0;i<=50;++i){
                    const double sm=su*double(i)/50.0;
                    goodman.x.append(sm);   goodman.y.append(sa*(1.0-sm/su));
                    gerber.x.append(sm);    gerber.y.append(sa*(1.0-(sm/su)*(sm/su)));
                    if(sm<=sy){ soderberg.x.append(sm); soderberg.y.append(sa*(1.0-sm/sy)); }
                }
                out.series.append(goodman);
                out.series.append(gerber);
                out.series.append(soderberg);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("mean stress"),false,0.0,unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("alternating stress"),false,0.0,unsetValue()};
        return out;
    }

    // ------------------------------------------------ Crack Growth Rate Curve
    // da/dN against the stress-intensity range on log-log axes. Region II is a
    // straight line - that is Paris' law - and its slope m and intercept C are
    // fitted and reported, because they are what the figure is measured for.
    if(in.engine==QLatin1String("Crack Growth Rate")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& dk=in.series.at(0).y;
            const QVector<double>& dadn=in.series.at(1).y;
            const int n=qMin(dk.size(),dadn.size());
            PlotSeries pts;
            pts.label=QStringLiteral("measured");
            pts.color=in.series.at(0).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=3.8;
            QVector<double> lx,ly;
            for(int i=0;i<n;++i){
                if(!finite(dk[i])||!finite(dadn[i])||!(dk[i]>0.0)||!(dadn[i]>0.0)) continue;
                pts.x.append(dk[i]); pts.y.append(dadn[i]);
                lx.append(std::log10(dk[i])); ly.append(std::log10(dadn[i]));
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                // Fitted over the middle 80% of the range: regions I and III
                // curve away from the line and including them drags the Paris
                // exponent toward a value that describes neither.
                QVector<double> sorted=lx;
                std::sort(sorted.begin(),sorted.end());
                const double lo=quantileOf(sorted,0.10), hi=quantileOf(sorted,0.90);
                QVector<double> fx,fy;
                for(int i=0;i<lx.size();++i)
                    if(lx[i]>=lo&&lx[i]<=hi){ fx.append(lx[i]); fy.append(ly[i]); }
                const Moments2 m=momentsOf(fx,fy);
                if(m.n>=2&&m.sxx>1e-15){
                    const double slope=m.sxy/m.sxx;
                    const double intercept=m.my-slope*m.mx;
                    PlotSeries paris;
                    paris.label=QStringLiteral("Paris  m = %1,  C = %2")
                                    .arg(slope,0,'f',2).arg(std::pow(10.0,intercept),0,'e',2);
                    paris.x={std::pow(10.0,lo),std::pow(10.0,hi)};
                    paris.y={std::pow(10.0,intercept+slope*lo),
                             std::pow(10.0,intercept+slope*hi)};
                    paris.color=in.style.warning;
                    paris.lineWidth=qMax(1.3,in.style.lineWidth);
                    out.series.append(paris);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("ΔK  (stress-intensity range)"),true,
                           unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("da/dN  (crack growth per cycle)"),true,
                           unsetValue(),unsetValue()};
        return out;
    }

    // ----------------------------------------------------- Strain-Life Curve
    // Total strain amplitude against reversals to failure, with the elastic and
    // plastic components drawn separately. Where the two cross is the transition
    // life, and which side of it a component sits on decides whether the design
    // is stress-driven or strain-driven.
    if(in.engine==QLatin1String("Strain-Life Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& reversals=in.series.at(0).y;
            const QVector<double>& strain=in.series.at(1).y;
            const int n=qMin(reversals.size(),strain.size());
            PlotSeries pts;
            pts.label=QStringLiteral("total strain");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            QVector<double> lx,ly;
            for(int i=0;i<n;++i){
                if(!finite(reversals[i])||!finite(strain[i])) continue;
                if(!(reversals[i]>0.0)||!(strain[i]>0.0)) continue;
                pts.x.append(reversals[i]); pts.y.append(strain[i]);
                lx.append(std::log10(reversals[i])); ly.append(std::log10(strain[i]));
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                const Moments2 m=momentsOf(lx,ly);
                if(m.n>=2&&m.sxx>1e-15){
                    const double slope=m.sxy/m.sxx;
                    const double intercept=m.my-slope*m.mx;
                    const Bounds b=boundsOf(lx);
                    PlotSeries fit;
                    fit.label=QStringLiteral("fitted exponent %1").arg(slope,0,'f',3);
                    fit.x={std::pow(10.0,b.lo),std::pow(10.0,b.hi)};
                    fit.y={std::pow(10.0,intercept+slope*b.lo),
                           std::pow(10.0,intercept+slope*b.hi)};
                    fit.color=in.style.warning;
                    fit.lineWidth=qMax(1.3,in.style.lineWidth);
                    out.series.append(fit);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("reversals to failure  2Nf"),true,
                           unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("strain amplitude"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------ Larson-Miller Master Curve
    // Stress against the Larson-Miller parameter, which collapses creep-rupture
    // tests at many temperatures onto one curve. That collapse is the point: it
    // is how a 10,000-hour life at one temperature is inferred from a 1,000-hour
    // test at another.
    if(in.engine==QLatin1String("Larson-Miller Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=3){
            const QVector<double>& temperature=in.series.at(0).y;   // absolute
            const QVector<double>& hours=in.series.at(1).y;
            const QVector<double>& stress=in.series.at(2).y;
            const int n=qMin(temperature.size(),qMin(hours.size(),stress.size()));
            PlotSeries pts;
            // C = 20 is the conventional value for steels and is stated on the
            // label rather than hidden, because the parameter is meaningless
            // without knowing which constant produced it.
            constexpr double kC=20.0;
            pts.label=QStringLiteral("rupture data  (C = 20)");
            pts.color=in.series.at(2).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;
            QVector<double> px,py;
            for(int i=0;i<n;++i){
                if(!finite(temperature[i])||!finite(hours[i])||!finite(stress[i])) continue;
                if(!(temperature[i]>0.0)||!(hours[i]>0.0)||!(stress[i]>0.0)) continue;
                const double lmp=temperature[i]*(kC+std::log10(hours[i]))/1000.0;
                pts.x.append(lmp); pts.y.append(stress[i]);
                px.append(lmp); py.append(std::log10(stress[i]));
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                const Moments2 m=momentsOf(px,py);
                if(m.n>=2&&m.sxx>1e-15){
                    const double slope=m.sxy/m.sxx;
                    const double intercept=m.my-slope*m.mx;
                    const Bounds b=boundsOf(px);
                    PlotSeries master;
                    master.label=QStringLiteral("master curve");
                    master.x={b.lo,b.hi};
                    master.y={std::pow(10.0,intercept+slope*b.lo),
                              std::pow(10.0,intercept+slope*b.hi)};
                    master.color=in.style.warning;
                    master.lineWidth=qMax(1.3,in.style.lineWidth);
                    out.series.append(master);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("Larson-Miller parameter  T(20 + log t)/1000"),
                           false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("stress"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------- Campbell Diagram
    // Natural frequencies against shaft speed, with the engine-order rays over
    // them. Every crossing is a potential resonance, and the crossings are what
    // a critical-speed list is derived from.
    if(in.engine==QLatin1String("Campbell Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        double speedMax=0.0,freqMax=0.0;
        if(in.series.size()>=2){
            const QVector<double>& speed=in.series.at(0).y;
            for(int k=1;k<in.series.size();++k){
                const QVector<double>& freq=in.series.at(k).y;
                const int n=qMin(speed.size(),freq.size());
                PlotSeries mode;
                mode.label=in.series.at(k).label.isEmpty()
                               ? QStringLiteral("mode %1").arg(k) : in.series.at(k).label;
                mode.color=in.series.at(k).color;
                mode.lineWidth=qMax(1.3,in.style.lineWidth);
                QVector<QPair<double,double>> rows;
                for(int i=0;i<n;++i)
                    if(finite(speed[i])&&finite(freq[i])&&speed[i]>=0.0)
                        rows.append({speed[i],freq[i]});
                std::sort(rows.begin(),rows.end(),
                          [](const QPair<double,double>& a,const QPair<double,double>& b){
                              return a.first<b.first;
                          });
                for(const auto& r:rows){
                    mode.x.append(r.first); mode.y.append(r.second);
                    speedMax=qMax(speedMax,r.first); freqMax=qMax(freqMax,r.second);
                }
                if(!mode.x.isEmpty()) out.series.append(mode);
            }
        }
        // Engine orders: frequency = order x rev/s. Only the orders that reach
        // the drawn frequency range, so a 16x ray does not compress everything
        // else into the bottom of the figure.
        if(speedMax>0.0&&freqMax>0.0){
            for(int order:{1,2,3,4,6,8}){
                const double atMax=double(order)*speedMax/60.0;
                if(atMax<freqMax*0.15) continue;
                PlotSeries ray;
                ray.label=QStringLiteral("%1×").arg(order);
                ray.x={0.0,speedMax};
                ray.y={0.0,atMax};
                ray.color=in.style.gridColor;
                ray.dashPattern={4,3};
                out.series.append(ray);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("shaft speed  (rev/min)"),false,0.0,unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("frequency  (Hz)"),false,0.0,unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Shaft Orbit Plot
    // The journal centre's path within one or more revolutions, from a pair of
    // proximity probes at right angles. The SHAPE is the diagnosis - an ellipse
    // is unbalance, a figure of eight is a rub, an inward loop is oil whirl -
    // and none of that is visible in either probe's time trace alone.
    if(in.engine==QLatin1String("Shaft Orbit")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& px=in.series.at(0).y;
            const QVector<double>& py=in.series.at(1).y;
            const int n=qMin(px.size(),py.size());
            PlotSeries orbit;
            orbit.label=QStringLiteral("orbit");
            orbit.color=in.series.at(0).color;
            orbit.lineWidth=qMax(1.2,in.style.lineWidth);
            for(int i=0;i<n;++i){
                if(!finite(px[i])||!finite(py[i])) continue;
                orbit.x.append(px[i]); orbit.y.append(py[i]);
            }
            if(!orbit.x.isEmpty()){
                out.series.append(orbit);
                // The start, so the direction of precession can be read. Whether
                // the orbit runs with the shaft or against it is the difference
                // between unbalance and a serious instability.
                PlotSeries start;
                start.label=QStringLiteral("start");
                start.x={orbit.x.first()}; start.y={orbit.y.first()};
                start.color=in.style.positive;
                start.drawLine=false; start.drawMarkers=true; start.markerSize=8.0;
                out.series.append(start);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("probe X displacement"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("probe Y displacement"),false,unsetValue(),unsetValue()};
        // Two probes measuring the same displacement at right angles. The
        // SHAPE of the orbit names the fault - an ellipse is misalignment, a
        // circle is unbalance - so a frame that stretches one axis renames it.
        out.equalAspect=true;
        return out;
    }

    // ---------------------------------------------------- Plasticity Chart
    // Plasticity index against liquid limit, over Casagrande's A-line and
    // U-line. Which zone a sample falls in IS its USCS classification, so the
    // lines are not decoration - they are the answer.
    if(in.engine==QLatin1String("Plasticity Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& ll=in.series.at(0).y;
            const QVector<double>& pi=in.series.at(1).y;
            const int n=qMin(ll.size(),pi.size());
            PlotSeries pts;
            pts.label=QStringLiteral("samples");
            pts.color=in.series.at(0).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            for(int i=0;i<n;++i)
                if(finite(ll[i])&&finite(pi[i])){ pts.x.append(ll[i]); pts.y.append(pi[i]); }
            if(!pts.x.isEmpty()) out.series.append(pts);
        }
        // A-line: PI = 0.73 (LL - 20), from LL = 20. U-line: PI = 0.9 (LL - 8),
        // the empirical upper bound no natural soil has been found above.
        PlotSeries aLine,uLine,ll50;
        aLine.label=QStringLiteral("A-line  PI = 0.73(LL − 20)");
        uLine.label=QStringLiteral("U-line  PI = 0.9(LL − 8)");
        aLine.color=in.style.foreground;
        uLine.color=in.style.danger;
        uLine.dashPattern={5,3};
        aLine.lineWidth=uLine.lineWidth=qMax(1.0,in.style.lineWidth);
        for(double x=20.0;x<=100.0;x+=2.0){ aLine.x.append(x); aLine.y.append(0.73*(x-20.0)); }
        for(double x=8.0;x<=100.0;x+=2.0){ uLine.x.append(x); uLine.y.append(0.9*(x-8.0)); }
        // LL = 50 divides low from high plasticity, which is the other half of
        // the classification.
        ll50.label=QStringLiteral("LL = 50");
        ll50.x={50.0,50.0}; ll50.y={0.0,60.0};
        ll50.color=in.style.gridColor;
        ll50.dashPattern={3,3};
        out.series.append(aLine);
        out.series.append(uLine);
        out.series.append(ll50);
        out.xAxis=PlotAxis{QStringLiteral("liquid limit  LL (%)"),false,0.0,unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("plasticity index  PI (%)"),false,0.0,unsetValue()};
        return out;
    }

    // ---------------------------------------------- Particle Size Distribution
    // Percent finer against grain size on a REVERSED log axis, which is the
    // convention: coarse on the left, fine on the right. D10, D30 and D60 are
    // marked because the uniformity and curvature coefficients come from them.
    if(in.engine==QLatin1String("Particle Size Distribution")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& size=in.series.at(0).y;
            const QVector<double>& finer=in.series.at(1).y;
            const int n=qMin(size.size(),finer.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i)
                if(finite(size[i])&&finite(finer[i])&&size[i]>0.0)
                    rows.append({size[i],finer[i]});
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first;
                      });
            if(rows.size()>=2){
                PlotSeries curve;
                curve.label=QStringLiteral("grading");
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.4,in.style.lineWidth);
                curve.drawMarkers=rows.size()<=60; curve.markerSize=3.6;
                for(const auto& r:rows){ curve.x.append(r.first); curve.y.append(r.second); }
                out.series.append(curve);

                // D10, D30, D60 by linear interpolation in log-size, which is
                // the space the curve is read in.
                const auto dAt=[&rows](double pct)->double{
                    for(int i=1;i<rows.size();++i){
                        if(rows[i-1].second<=pct&&rows[i].second>=pct){
                            const double span=rows[i].second-rows[i-1].second;
                            if(!(std::abs(span)>1e-12)) return rows[i].first;
                            const double t=(pct-rows[i-1].second)/span;
                            return std::pow(10.0,std::log10(rows[i-1].first)
                                                +t*(std::log10(rows[i].first)
                                                   -std::log10(rows[i-1].first)));
                        }
                    }
                    return std::numeric_limits<double>::quiet_NaN();
                };
                const double d10=dAt(10.0),d30=dAt(30.0),d60=dAt(60.0);
                if(finite(d10)&&finite(d60)&&d10>0.0){
                    PlotSeries marks;
                    marks.label=QStringLiteral("D10 %1, D60 %2, Cu %3")
                                    .arg(d10,0,'g',3).arg(d60,0,'g',3).arg(d60/d10,0,'f',1);
                    marks.x={d10,d30,d60};
                    marks.y={10.0,30.0,60.0};
                    marks.color=in.style.warning;
                    marks.drawLine=false; marks.drawMarkers=true; marks.markerSize=7.0;
                    out.series.append(marks);
                }
            }
        }
        // Reversed, per the convention. PlotAxis::inverted exists precisely so
        // this does not have to be done by negating the data, which puts minus
        // signs on the ticks.
        out.xAxis=PlotAxis{QStringLiteral("grain size (mm)"),true,unsetValue(),unsetValue(),true};
        out.yAxis=PlotAxis{QStringLiteral("percent finer (%)"),false,0.0,100.0};
        return out;
    }

    // ----------------------------------------------------- Nichols Chart
    // Open-loop gain in decibels against phase. Gain and phase margins are read
    // off the crossings directly - one figure where a Bode plot needs two, and
    // the closed-loop behaviour is a position on this plane rather than an
    // inference from a pair of curves.
    if(in.engine==QLatin1String("Nichols Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& phase=in.series.at(0).y;
            const QVector<double>& gain=in.series.at(1).y;
            const int n=qMin(phase.size(),gain.size());
            PlotSeries locus;
            locus.label=QStringLiteral("open loop");
            locus.color=in.series.at(1).color;
            locus.lineWidth=qMax(1.4,in.style.lineWidth);
            for(int i=0;i<n;++i){
                if(!finite(phase[i])||!finite(gain[i])) continue;
                locus.x.append(phase[i]); locus.y.append(gain[i]);
            }
            if(!locus.x.isEmpty()) out.series.append(locus);
        }
        // The critical point: -180 degrees, 0 dB. Everything on this chart is
        // read as a distance from it.
        PlotSeries critical;
        critical.label=QStringLiteral("−180°, 0 dB");
        critical.x={-180.0}; critical.y={0.0};
        critical.color=in.style.danger;
        critical.drawLine=false; critical.drawMarkers=true; critical.markerSize=9.0;
        out.series.append(critical);
        out.series.append(horizontalRule(0.0,-360.0,0.0,in.style.gridColor,
                                         QStringLiteral("0 dB"),true));
        out.xAxis=PlotAxis{QStringLiteral("open-loop phase (deg)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("open-loop gain (dB)"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Pole-Zero Map
    // Poles and zeros on the complex plane, with the stability boundary. For a
    // continuous system that is the imaginary axis; anything to the right of it
    // is unstable, and that is the entire reading of the figure.
    if(in.engine==QLatin1String("Pole-Zero Map")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        double reach=1.0;
        if(in.series.size()>=2){
            const QVector<double>& re=in.series.at(0).y;
            const QVector<double>& im=in.series.at(1).y;
            const int n=qMin(re.size(),im.size());
            PlotSeries poles;
            poles.label=QStringLiteral("poles");
            poles.color=in.series.at(0).color;
            poles.drawLine=false; poles.drawMarkers=true; poles.markerSize=7.0;
            for(int i=0;i<n;++i){
                if(!finite(re[i])||!finite(im[i])) continue;
                poles.x.append(re[i]); poles.y.append(im[i]);
                reach=qMax(reach,qMax(std::abs(re[i]),std::abs(im[i])));
            }
            if(!poles.x.isEmpty()) out.series.append(poles);
        }
        PlotSeries axis;
        axis.label=QStringLiteral("stability boundary");
        axis.x={0.0,0.0}; axis.y={-reach*1.2,reach*1.2};
        axis.color=in.style.danger;
        axis.dashPattern={5,3};
        out.series.append(axis);
        out.xAxis=PlotAxis{QStringLiteral("real"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("imaginary"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- P-V Nose Curve
    // Bus voltage against transferred power. The nose is the point of voltage
    // collapse, and the distance to it along the power axis is the loading
    // margin - which is the number a system operator is actually watching.
    if(in.engine==QLatin1String("P-V Nose Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& power=in.series.at(0).y;
            const QVector<double>& voltage=in.series.at(1).y;
            const int n=qMin(power.size(),voltage.size());
            PlotSeries curve;
            curve.label=QStringLiteral("P-V");
            curve.color=in.series.at(1).color;
            curve.lineWidth=qMax(1.4,in.style.lineWidth);
            int nose=-1; double best=-std::numeric_limits<double>::infinity();
            for(int i=0;i<n;++i){
                if(!finite(power[i])||!finite(voltage[i])) continue;
                curve.x.append(power[i]); curve.y.append(voltage[i]);
                if(power[i]>best){ best=power[i]; nose=curve.x.size()-1; }
            }
            if(!curve.x.isEmpty()){
                out.series.append(curve);
                if(nose>=0){
                    PlotSeries tip;
                    tip.label=QStringLiteral("collapse at P = %1").arg(best,0,'g',4);
                    tip.x={curve.x[nose]}; tip.y={curve.y[nose]};
                    tip.color=in.style.danger;
                    tip.drawLine=false; tip.drawMarkers=true; tip.markerSize=9.0;
                    out.series.append(tip);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("transferred power"),false,0.0,unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("bus voltage"),false,0.0,unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Airfoil Cp Plot
    // Pressure coefficient along the chord, with the y axis INVERTED, which is
    // the convention: suction is plotted upward because that is where the lift
    // comes from. The area between the two surfaces is the sectional lift.
    if(in.engine==QLatin1String("Airfoil Cp Distribution")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& chord=in.series.at(0).y;
            for(int k=1;k<in.series.size();++k){
                const QVector<double>& cp=in.series.at(k).y;
                const int n=qMin(chord.size(),cp.size());
                PlotSeries surface;
                surface.label=in.series.at(k).label.isEmpty()
                                  ? QStringLiteral("surface %1").arg(k) : in.series.at(k).label;
                surface.color=in.series.at(k).color;
                surface.lineWidth=qMax(1.3,in.style.lineWidth);
                for(int i=0;i<n;++i){
                    if(!finite(chord[i])||!finite(cp[i])) continue;
                    surface.x.append(chord[i]); surface.y.append(cp[i]);
                }
                if(!surface.x.isEmpty()) out.series.append(surface);
            }
        }
        out.series.append(horizontalRule(0.0,0.0,1.0,in.style.gridColor,
                                         QStringLiteral("Cp = 0"),true));
        out.xAxis=PlotAxis{QStringLiteral("x / c"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("pressure coefficient  Cp"),false,
                           unsetValue(),unsetValue(),true};
        return out;
    }
    // =====================================================================
    // Batch 3: chemistry, earth science, physics and medicine.
    //
    // Four of these are LINEARISATIONS - Lineweaver-Burk, Eadie-Hofstee,
    // Hanes-Woolf and Scatchard. They plot the same measurements as an
    // existing engine on rearranged axes, and the reason there are several is
    // that each puts the experimental error in a different place: a
    // double-reciprocal plot magnifies the error on the smallest velocities
    // enormously, which is precisely why the other two exist. They are
    // different pictures of one dataset, and which one is right depends on
    // where the uncertainty is.
    // =====================================================================

    // ------------------------------------- Enzyme kinetics linearisations
    if(in.engine==QLatin1String("Lineweaver-Burk Plot")
       ||in.engine==QLatin1String("Eadie-Hofstee Plot")
       ||in.engine==QLatin1String("Hanes-Woolf Plot")){
        const bool burk  =(in.engine==QLatin1String("Lineweaver-Burk Plot"));
        const bool hofstee=(in.engine==QLatin1String("Eadie-Hofstee Plot"));
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& substrate=in.series.at(0).y;
            const QVector<double>& velocity=in.series.at(1).y;
            const int n=qMin(substrate.size(),velocity.size());
            PlotSeries pts;
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.4;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                const double s=substrate[i], v=velocity[i];
                if(!finite(s)||!finite(v)) continue;
                // Every one of the three divides by something, and a zero
                // concentration or a zero rate is a measurement rather than a
                // mistake - so those rows are dropped rather than turned into
                // an infinity that would own the axis.
                double x,y;
                if(burk){        if(!(s>0.0)||!(v>0.0)) continue; x=1.0/s;  y=1.0/v; }
                else if(hofstee){ if(!(s>0.0))          continue; x=v/s;    y=v;     }
                else {            if(!(v>0.0))          continue; x=s;      y=s/v;   }
                pts.x.append(x); pts.y.append(y);
                fx.append(x); fy.append(y);
            }
            if(!pts.x.isEmpty()){
                const Moments2 m=momentsOf(fx,fy);
                double vmax=0.0,km=0.0;
                bool ok=false;
                if(m.n>=2&&m.sxx>1e-15){
                    const double slope=m.sxy/m.sxx;
                    const double intercept=m.my-slope*m.mx;
                    // Vmax and Km read off differently from each rearrangement.
                    // Written out per plot rather than converted, because the
                    // conversion is the step people get wrong.
                    if(burk&&std::abs(intercept)>1e-15){ vmax=1.0/intercept; km=slope*vmax; ok=true; }
                    else if(hofstee){ vmax=intercept; km=-slope; ok=true; }
                    else if(std::abs(slope)>1e-15){ vmax=1.0/slope; km=intercept*vmax; ok=true; }

                    const Bounds b=boundsOf(fx);
                    if(b.valid){
                        PlotSeries fit;
                        fit.label=ok?QStringLiteral("Vmax %1, Km %2").arg(vmax,0,'g',3).arg(km,0,'g',3)
                                    :QStringLiteral("least squares");
                        fit.x={b.lo,b.hi};
                        fit.y={intercept+slope*b.lo,intercept+slope*b.hi};
                        fit.color=in.style.warning;
                        fit.lineWidth=qMax(1.3,in.style.lineWidth);
                        out.series.append(fit);
                    }
                }
                pts.label=QStringLiteral("measurements");
                out.series.append(pts);
            }
        }
        if(burk){
            out.xAxis=PlotAxis{QStringLiteral("1 / [S]"),false,unsetValue(),unsetValue()};
            out.yAxis=PlotAxis{QStringLiteral("1 / v"),false,unsetValue(),unsetValue()};
        }else if(hofstee){
            out.xAxis=PlotAxis{QStringLiteral("v / [S]"),false,unsetValue(),unsetValue()};
            out.yAxis=PlotAxis{QStringLiteral("v"),false,unsetValue(),unsetValue()};
        }else{
            out.xAxis=PlotAxis{QStringLiteral("[S]"),false,unsetValue(),unsetValue()};
            out.yAxis=PlotAxis{QStringLiteral("[S] / v"),false,unsetValue(),unsetValue()};
        }
        return out;
    }

    // ------------------------------------------------------- Scatchard Plot
    // Bound over free against bound, for a saturation binding experiment. The
    // slope is -1/Kd and the x intercept is the number of sites; a curve rather
    // than a line means more than one class of site, which is the finding the
    // plot exists to make visible.
    if(in.engine==QLatin1String("Scatchard Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& bound=in.series.at(0).y;
            const QVector<double>& freeLigand=in.series.at(1).y;
            const int n=qMin(bound.size(),freeLigand.size());
            PlotSeries pts;
            pts.label=QStringLiteral("binding");
            pts.color=in.series.at(0).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.4;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(bound[i])||!finite(freeLigand[i])||!(freeLigand[i]>0.0)) continue;
                pts.x.append(bound[i]); pts.y.append(bound[i]/freeLigand[i]);
                fx.append(bound[i]); fy.append(bound[i]/freeLigand[i]);
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                const Moments2 m=momentsOf(fx,fy);
                if(m.n>=2&&m.sxx>1e-15){
                    const double slope=m.sxy/m.sxx;
                    const double intercept=m.my-slope*m.mx;
                    const Bounds b=boundsOf(fx);
                    PlotSeries fit;
                    fit.label=(std::abs(slope)>1e-15)
                        ? QStringLiteral("Kd = %1,  Bmax = %2")
                              .arg(-1.0/slope,0,'g',3).arg(-intercept/slope,0,'g',3)
                        : QStringLiteral("least squares");
                    fit.x={b.lo,b.hi};
                    fit.y={intercept+slope*b.lo,intercept+slope*b.hi};
                    fit.color=in.style.warning;
                    fit.lineWidth=qMax(1.3,in.style.lineWidth);
                    out.series.append(fit);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("bound"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("bound / free"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ van Deemter Plot
    // Plate height against mobile-phase velocity, with the A + B/u + Cu fit and
    // its minimum. That minimum is the velocity a column should be run at, and
    // reading it off by eye from a scatter is how a separation ends up run at
    // twice the plate height it needed to be.
    if(in.engine==QLatin1String("van Deemter Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& u=in.series.at(0).y;
            const QVector<double>& h=in.series.at(1).y;
            const int n=qMin(u.size(),h.size());
            PlotSeries pts;
            pts.label=QStringLiteral("plate height");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.4;
            // H = A + B/u + Cu is linear in its three coefficients, so it is
            // solved as a 3x3 normal-equation system rather than iterated.
            double s[3][4]={};
            int used=0;
            for(int i=0;i<n;++i){
                if(!finite(u[i])||!finite(h[i])||!(u[i]>0.0)) continue;
                pts.x.append(u[i]); pts.y.append(h[i]);
                const double basis[3]={1.0,1.0/u[i],u[i]};
                for(int r=0;r<3;++r){
                    for(int c=0;c<3;++c) s[r][c]+=basis[r]*basis[c];
                    s[r][3]+=basis[r]*h[i];
                }
                ++used;
            }
            if(!pts.x.isEmpty()) out.series.append(pts);
            if(used>=4){
                // Gauss-Jordan with partial pivoting; three unknowns, so the
                // cost is irrelevant and the stability is not.
                bool ok=true;
                for(int col=0;col<3&&ok;++col){
                    int pivot=col;
                    for(int r=col+1;r<3;++r)
                        if(std::abs(s[r][col])>std::abs(s[pivot][col])) pivot=r;
                    if(std::abs(s[pivot][col])<1e-12){ ok=false; break; }
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
                    const double A=s[0][3],B=s[1][3],C=s[2][3];
                    const Bounds b=boundsOf(pts.x);
                    PlotSeries fit;
                    fit.label=QStringLiteral("A %1, B %2, C %3").arg(A,0,'g',3).arg(B,0,'g',3).arg(C,0,'g',3);
                    fit.color=in.style.warning;
                    fit.lineWidth=qMax(1.3,in.style.lineWidth);
                    for(int i=0;i<=60;++i){
                        const double uu=b.lo+(b.hi-b.lo)*double(i)/60.0;
                        if(!(uu>0.0)) continue;
                        fit.x.append(uu); fit.y.append(A+B/uu+C*uu);
                    }
                    out.series.append(fit);
                    if(B>0.0&&C>0.0){
                        const double uOpt=std::sqrt(B/C);
                        PlotSeries best;
                        best.label=QStringLiteral("optimum u = %1").arg(uOpt,0,'g',3);
                        best.x={uOpt}; best.y={A+2.0*std::sqrt(B*C)};
                        best.color=in.style.positive;
                        best.drawLine=false; best.drawMarkers=true; best.markerSize=9.0;
                        out.series.append(best);
                    }
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("linear velocity  u"),false,0.0,unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("plate height  H"),false,0.0,unsetValue()};
        return out;
    }

    // ------------------------------------------------------------- BET Plot
    // The BET transform against relative pressure, over the linear region. The
    // surface area comes from the slope and intercept together, and the fit is
    // taken over 0.05 to 0.35 because that is the range the BET model is valid
    // in - fitting the whole isotherm gives a number with no meaning.
    if(in.engine==QLatin1String("BET Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& rel=in.series.at(0).y;
            const QVector<double>& q=in.series.at(1).y;
            const int n=qMin(rel.size(),q.size());
            PlotSeries pts,inRange;
            pts.label=QStringLiteral("all points");
            pts.color=in.style.gridColor;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=3.4;
            inRange.label=QStringLiteral("BET region 0.05–0.35");
            inRange.color=in.series.at(1).color;
            inRange.drawLine=false; inRange.drawMarkers=true; inRange.markerSize=4.6;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                const double p=rel[i];
                if(!finite(p)||!finite(q[i])||!(p>0.0)||!(p<1.0)||!(q[i]>0.0)) continue;
                const double transform=1.0/(q[i]*(1.0/p-1.0));
                pts.x.append(p); pts.y.append(transform);
                if(p>=0.05&&p<=0.35){
                    inRange.x.append(p); inRange.y.append(transform);
                    fx.append(p); fy.append(transform);
                }
            }
            if(!pts.x.isEmpty()) out.series.append(pts);
            if(!inRange.x.isEmpty()) out.series.append(inRange);
            const Moments2 m=momentsOf(fx,fy);
            if(m.n>=2&&m.sxx>1e-15){
                const double slope=m.sxy/m.sxx;
                const double intercept=m.my-slope*m.mx;
                PlotSeries fit;
                const double denom=slope+intercept;
                fit.label=(std::abs(denom)>1e-15)
                    ? QStringLiteral("monolayer capacity %1").arg(1.0/denom,0,'g',4)
                    : QStringLiteral("least squares");
                fit.x={0.05,0.35};
                fit.y={intercept+slope*0.05,intercept+slope*0.35};
                fit.color=in.style.warning;
                fit.lineWidth=qMax(1.3,in.style.lineWidth);
                out.series.append(fit);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("relative pressure  P/P₀"),false,0.0,unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("1 / [Q(P₀/P − 1)]"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------------- Job Plot
    // The observable against the mole fraction of one partner. Where the
    // maximum falls gives the stoichiometry of the complex - 0.5 for a 1:1,
    // 0.33 for a 1:2 - and it is read off the position, not the height.
    if(in.engine==QLatin1String("Job Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& fraction=in.series.at(0).y;
            const QVector<double>& signal=in.series.at(1).y;
            const int n=qMin(fraction.size(),signal.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i)
                if(finite(fraction[i])&&finite(signal[i])) rows.append({fraction[i],signal[i]});
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first;
                      });
            if(!rows.isEmpty()){
                PlotSeries curve;
                curve.label=QStringLiteral("continuous variation");
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.3,in.style.lineWidth);
                curve.drawMarkers=true; curve.markerSize=4.2;
                int peak=0;
                for(int i=0;i<rows.size();++i){
                    curve.x.append(rows[i].first); curve.y.append(rows[i].second);
                    if(rows[i].second>rows[peak].second) peak=i;
                }
                out.series.append(curve);
                PlotSeries mark;
                mark.label=QStringLiteral("maximum at χ = %1").arg(rows[peak].first,0,'f',3);
                mark.x={rows[peak].first}; mark.y={rows[peak].second};
                mark.color=in.style.warning;
                mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=9.0;
                out.series.append(mark);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("mole fraction"),false,0.0,1.0};
        out.yAxis=PlotAxis{QStringLiteral("corrected signal"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------- Gutenberg-Richter Frequency
    // Log cumulative event count against magnitude. The slope is the b-value,
    // and the point where the data bends away from the line is the magnitude of
    // completeness - below which the catalogue is missing events rather than
    // the earth producing fewer.
    if(in.engine==QLatin1String("Gutenberg-Richter Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        for(const PlotSeries& s:in.series){
            QVector<double> mag=finiteValues(s);
            if(mag.size()<8) continue;
            std::sort(mag.begin(),mag.end());
            PlotSeries cumulative;
            cumulative.label=QStringLiteral("cumulative count");
            cumulative.color=s.color;
            cumulative.drawLine=false; cumulative.drawMarkers=true; cumulative.markerSize=3.8;
            QVector<double> fx,fy;
            const double lo=mag.first(), hi=mag.last();
            if(!(hi>lo)) continue;
            for(int b=0;b<=30;++b){
                const double m=lo+(hi-lo)*double(b)/30.0;
                // N(>=m), which is what "cumulative" means here - the count of
                // events at least this large, not the count in a bin.
                int count=0;
                for(double d:mag) if(d>=m) ++count;
                if(count<1) continue;
                cumulative.x.append(m); cumulative.y.append(double(count));
                fx.append(m); fy.append(std::log10(double(count)));
            }
            if(cumulative.x.isEmpty()) continue;
            out.series.append(cumulative);
            const Moments2 mm=momentsOf(fx,fy);
            if(mm.n>=2&&mm.sxx>1e-15){
                const double slope=mm.sxy/mm.sxx;
                const double intercept=mm.my-slope*mm.mx;
                PlotSeries fit;
                fit.label=QStringLiteral("b = %1").arg(-slope,0,'f',2);
                fit.x={lo,hi};
                fit.y={std::pow(10.0,intercept+slope*lo),std::pow(10.0,intercept+slope*hi)};
                fit.color=in.style.warning;
                fit.lineWidth=qMax(1.3,in.style.lineWidth);
                out.series.append(fit);
            }
            break;
        }
        out.xAxis=PlotAxis{QStringLiteral("magnitude"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("cumulative number of events"),true,
                           unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------- Double-Mass Curve
    // Cumulative total at one station against the cumulative total at a
    // reference. A straight line means the record is homogeneous; a change of
    // slope is the date something about the station changed, and that is the
    // only way to find it without the station's own history.
    if(in.engine==QLatin1String("Double-Mass Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& station=in.series.at(0).y;
            const QVector<double>& reference=in.series.at(1).y;
            const int n=qMin(station.size(),reference.size());
            PlotSeries curve;
            curve.label=QStringLiteral("cumulative");
            curve.color=in.series.at(0).color;
            curve.lineWidth=qMax(1.3,in.style.lineWidth);
            curve.drawMarkers=n<=80; curve.markerSize=3.4;
            double a=0.0,b=0.0;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(station[i])||!finite(reference[i])) continue;
                a+=station[i]; b+=reference[i];
                curve.x.append(b); curve.y.append(a);
                fx.append(b); fy.append(a);
            }
            if(!curve.x.isEmpty()){
                out.series.append(curve);
                const Moments2 m=momentsOf(fx,fy);
                if(m.n>=2&&m.sxx>1e-15){
                    const double slope=m.sxy/m.sxx;
                    const double intercept=m.my-slope*m.mx;
                    const Bounds bb=boundsOf(fx);
                    PlotSeries trend;
                    trend.label=QStringLiteral("overall slope %1").arg(slope,0,'f',3);
                    trend.x={bb.lo,bb.hi};
                    trend.y={intercept+slope*bb.lo,intercept+slope*bb.hi};
                    trend.color=in.style.gridColor;
                    trend.dashPattern={5,3};
                    out.series.append(trend);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("cumulative reference"),false,0.0,unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("cumulative station"),false,0.0,unsetValue()};
        return out;
    }

    // -------------------------------------------------------------- Hodograph
    // Wind u against v, traced with height. The shape is the wind shear - a
    // curved hodograph is directional shear, a long straight one is speed shear
    // - and neither is visible in a profile of speed against height.
    if(in.engine==QLatin1String("Hodograph")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& u=in.series.at(0).y;
            const QVector<double>& v=in.series.at(1).y;
            const int n=qMin(u.size(),v.size());
            PlotSeries trace;
            trace.label=QStringLiteral("wind with height");
            trace.color=in.series.at(0).color;
            trace.lineWidth=qMax(1.4,in.style.lineWidth);
            double reach=1.0;
            for(int i=0;i<n;++i){
                if(!finite(u[i])||!finite(v[i])) continue;
                trace.x.append(u[i]); trace.y.append(v[i]);
                reach=qMax(reach,std::hypot(u[i],v[i]));
            }
            if(!trace.x.isEmpty()){
                out.series.append(trace);
                PlotSeries ground;
                ground.label=QStringLiteral("surface");
                ground.x={trace.x.first()}; ground.y={trace.y.first()};
                ground.color=in.style.positive;
                ground.drawLine=false; ground.drawMarkers=true; ground.markerSize=8.0;
                out.series.append(ground);
                // Speed rings, so a magnitude can be read off a plot whose
                // whole point is direction.
                for(int ring=1;ring<=3;++ring){
                    PlotSeries circle;
                    circle.label=QStringLiteral("%1").arg(reach*double(ring)/3.0,0,'f',0);
                    circle.color=in.style.gridColor;
                    circle.dashPattern={3,3};
                    const double r=reach*double(ring)/3.0;
                    for(int a=0;a<=72;++a){
                        const double th=double(a)/72.0*2.0*M_PI;
                        circle.x.append(r*std::cos(th)); circle.y.append(r*std::sin(th));
                    }
                    out.series.append(circle);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("u  (west-east)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("v  (south-north)"),false,unsetValue(),unsetValue()};
        // Two components of one wind vector. The curvature of the trace is
        // read as veering or backing, which a stretched axis invents.
        out.equalAspect=true;
        return out;
    }

    // ------------------------------------------------------ Flow-Volume Loop
    // Expiratory and inspiratory flow against volume, as a closed loop. The
    // SHAPE is the diagnosis - a scooped expiratory limb is obstruction, a
    // flattened one is a fixed upper-airway lesion - and none of it is visible
    // in a volume-time trace.
    if(in.engine==QLatin1String("Flow-Volume Loop")
       ||in.engine==QLatin1String("Pressure-Volume Loop")){
        const bool spiro=(in.engine==QLatin1String("Flow-Volume Loop"));
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& volume=in.series.at(0).y;
            const QVector<double>& other=in.series.at(1).y;
            const int n=qMin(volume.size(),other.size());
            PlotSeries loop;
            loop.label=spiro?QStringLiteral("manoeuvre"):QStringLiteral("cardiac cycle");
            loop.color=in.series.at(1).color;
            loop.lineWidth=qMax(1.4,in.style.lineWidth);
            for(int i=0;i<n;++i){
                if(!finite(volume[i])||!finite(other[i])) continue;
                loop.x.append(volume[i]); loop.y.append(other[i]);
            }
            // Closed, because it is a loop: leaving the last point unjoined to
            // the first draws an open curve that reads as an incomplete cycle.
            if(loop.x.size()>=3){
                loop.x.append(loop.x.first()); loop.y.append(loop.y.first());
                out.series.append(loop);
                PlotSeries start;
                start.label=QStringLiteral("start");
                start.x={loop.x.first()}; start.y={loop.y.first()};
                start.color=in.style.positive;
                start.drawLine=false; start.drawMarkers=true; start.markerSize=8.0;
                out.series.append(start);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("volume"),false,unsetValue(),unsetValue()};
        out.yAxis=spiro?PlotAxis{QStringLiteral("flow"),false,unsetValue(),unsetValue()}
                       :PlotAxis{QStringLiteral("pressure"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------- Allan Deviation
    // Overlapping Allan deviation against averaging time, on log-log axes, with
    // the noise-slope guides. Which slope a region follows names the noise
    // process - white, flicker, random walk - and the minimum is where an
    // oscillator is at its best.
    if(in.engine==QLatin1String("Allan Deviation")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        for(const PlotSeries& s:in.series){
            const QVector<double> y=finiteValues(s);
            if(y.size()<16) continue;
            PlotSeries dev;
            dev.label=QStringLiteral("σ(τ)");
            dev.color=s.color;
            dev.lineWidth=qMax(1.3,in.style.lineWidth);
            dev.drawMarkers=true; dev.markerSize=3.6;
            double lowest=std::numeric_limits<double>::infinity(),atTau=1.0;
            // The standard overlapping estimator, over octave-spaced averaging
            // factors: sigma^2(m) = mean of squared successive differences of
            // the m-sample averages, halved.
            for(int m=1;m<=y.size()/4;m*=2){
                const int groups=y.size()/m;
                if(groups<3) break;
                QVector<double> avg;
                avg.reserve(groups);
                for(int g=0;g<groups;++g){
                    double sum=0.0;
                    for(int k=0;k<m;++k) sum+=y[g*m+k];
                    avg.append(sum/double(m));
                }
                double acc=0.0;
                for(int i=1;i<avg.size();++i){ const double d=avg[i]-avg[i-1]; acc+=d*d; }
                const double variance=acc/(2.0*double(avg.size()-1));
                if(!(variance>0.0)||!finite(variance)) continue;
                const double sigma=std::sqrt(variance);
                dev.x.append(double(m)); dev.y.append(sigma);
                if(sigma<lowest){ lowest=sigma; atTau=double(m); }
            }
            if(dev.x.size()<2) continue;
            out.series.append(dev);
            PlotSeries best;
            best.label=QStringLiteral("minimum at τ = %1").arg(atTau,0,'g',3);
            best.x={atTau}; best.y={lowest};
            best.color=in.style.warning;
            best.drawLine=false; best.drawMarkers=true; best.markerSize=8.0;
            out.series.append(best);
            break;
        }
        out.xAxis=PlotAxis{QStringLiteral("averaging factor  τ"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("Allan deviation  σ(τ)"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Paschen Curve
    // Breakdown voltage against pressure times gap, on log axes, with the
    // Paschen minimum. Below the minimum the voltage RISES as the gap shrinks,
    // which is the counter-intuitive result the curve exists to show and the
    // reason small gaps are not automatically safer.
    if(in.engine==QLatin1String("Paschen Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& pd=in.series.at(0).y;
            const QVector<double>& vb=in.series.at(1).y;
            const int n=qMin(pd.size(),vb.size());
            PlotSeries pts;
            pts.label=QStringLiteral("measured");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;
            for(int i=0;i<n;++i)
                if(finite(pd[i])&&finite(vb[i])&&pd[i]>0.0&&vb[i]>0.0){
                    pts.x.append(pd[i]); pts.y.append(vb[i]);
                }
            if(!pts.x.isEmpty()) out.series.append(pts);
        }
        // Paschen's law for air: A = 15 (cm·Torr)^-1, B = 365 V/(cm·Torr),
        // secondary emission coefficient 0.01. Labelled as air so nobody reads
        // it as the curve for their own gas.
        {
            constexpr double A=15.0, B=365.0, gamma=0.01;
            const double denom=std::log(1.0+1.0/gamma);
            PlotSeries law;
            law.label=QStringLiteral("Paschen's law, air");
            law.color=in.style.warning;
            law.lineWidth=qMax(1.2,in.style.lineWidth);
            double lowest=std::numeric_limits<double>::infinity(),atPd=1.0;
            for(double x=0.05;x<=1000.0;x*=1.12){
                const double inner=std::log(A*x/denom);
                if(!(inner>0.0)) continue;
                const double v=B*x/inner;
                if(!finite(v)||v<=0.0) continue;
                law.x.append(x); law.y.append(v);
                if(v<lowest){ lowest=v; atPd=x; }
            }
            if(!law.x.isEmpty()){
                out.series.append(law);
                PlotSeries minimum;
                minimum.label=QStringLiteral("minimum %1 V at pd = %2")
                                  .arg(lowest,0,'f',0).arg(atPd,0,'g',3);
                minimum.x={atPd}; minimum.y={lowest};
                minimum.color=in.style.positive;
                minimum.drawLine=false; minimum.drawMarkers=true; minimum.markerSize=8.0;
                out.series.append(minimum);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("pressure × gap  (Torr·cm)"),true,
                           unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("breakdown voltage  (V)"),true,
                           unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------- Phase-Folded Light Curve
    // Flux against phase after folding on a trial period, drawn over two cycles
    // so a feature at the join is not cut in half. A period that is right makes
    // the scatter collapse onto a curve; one that is wrong leaves a cloud, and
    // that collapse is the entire test.
    if(in.engine==QLatin1String("Phase-Folded Light Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& time=in.series.at(0).y;
            const QVector<double>& flux=in.series.at(1).y;
            const int n=qMin(time.size(),flux.size());
            // The period comes from the third column if there is one; otherwise
            // the span of the data, which folds it into a single cycle and is
            // at least an honest default rather than a guess at a real period.
            double period=0.0;
            if(in.series.size()>=3){
                const QVector<double> p=finiteValues(in.series.at(2));
                if(!p.isEmpty()) period=std::abs(p.first());
            }
            if(!(period>0.0)){
                const Bounds b=boundsOf(time);
                period=b.valid?(b.hi-b.lo):1.0;
            }
            if(period>0.0){
                PlotSeries folded;
                // The period is the whole result: a light curve folded on
                // the wrong period is noise, and folded on the right one is a
                // shape. It was written into the only named series, whose
                // legend row is suppressed as a caption, so the figure never
                // said what it had been folded on.
                folded.label=QStringLiteral("flux");
                out.figureNote=QStringLiteral("Folded on a period of %1.")
                                   .arg(formatMeasured(period,5));
                folded.color=in.series.at(1).color;
                folded.drawLine=false; folded.drawMarkers=true; folded.markerSize=3.2;
                for(int i=0;i<n;++i){
                    if(!finite(time[i])||!finite(flux[i])) continue;
                    double phase=std::fmod(time[i],period)/period;
                    if(phase<0.0) phase+=1.0;
                    folded.x.append(phase);       folded.y.append(flux[i]);
                    folded.x.append(phase+1.0);   folded.y.append(flux[i]);
                }
                if(!folded.x.isEmpty()) out.series.append(folded);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("phase"),false,0.0,2.0};
        out.yAxis=PlotAxis{QStringLiteral("flux"),false,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------------------------- O−C Diagram
    // Observed minus computed event time against epoch. A flat line means the
    // ephemeris is right; a slope means the period is slightly wrong; curvature
    // means it is changing. Three different findings from one straight-looking
    // plot, and none visible in the event times themselves.
    if(in.engine==QLatin1String("O-C Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& epoch=in.series.at(0).y;
            const QVector<double>& observed=in.series.at(1).y;
            const int n=qMin(epoch.size(),observed.size());
            QVector<double> ex,oy;
            for(int i=0;i<n;++i){
                if(!finite(epoch[i])||!finite(observed[i])) continue;
                ex.append(epoch[i]); oy.append(observed[i]);
            }
            // The linear ephemeris is fitted rather than assumed: T = T0 + P·E.
            // What is left over IS the O−C, so the residual is the figure.
            const Moments2 m=momentsOf(ex,oy);
            if(m.n>=2&&m.sxx>1e-15){
                const double period=m.sxy/m.sxx;
                const double epochZero=m.my-period*m.mx;
                PlotSeries oc;
                oc.label=QStringLiteral("O−C   (T₀ %1, P %2)")
                             .arg(formatMeasured(epochZero,9))
                             .arg(formatMeasured(period,6));
                oc.color=in.series.at(1).color;
                oc.drawLine=false; oc.drawMarkers=true; oc.markerSize=4.2;
                for(int i=0;i<ex.size();++i){
                    oc.x.append(ex[i]);
                    oc.y.append(oy[i]-(epochZero+period*ex[i]));
                }
                out.series.append(oc);
                const Bounds b=boundsOf(ex);
                if(b.valid)
                    out.series.append(horizontalRule(0.0,b.lo,b.hi,in.style.gridColor,
                                                     QStringLiteral("ephemeris"),true));
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("epoch"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("observed − computed"),false,unsetValue(),unsetValue()};
        return out;
    }
    // =====================================================================
    // Batch 4: model interpretation, regression diagnostics, economics and
    // decision analysis.
    // =====================================================================

    // ------------------------------- Partial Dependence / ICE / ALE Plot
    // What the model predicts as one feature is varied, everything else held
    // where it was. The partial dependence is the average of those curves; the
    // ICE plot draws them individually, because an average can be flat while
    // every individual curve is steep in opposite directions - and that is a
    // finding, not noise.
    if(in.engine==QLatin1String("Partial Dependence Plot")
       ||in.engine==QLatin1String("ICE Plot")){
        const bool ice=(in.engine==QLatin1String("ICE Plot"));
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        out.legendVisible=!ice;
        if(in.series.size()>=2){
            const QVector<double>& feature=in.series.at(0).y;
            const QVector<double>& prediction=in.series.at(1).y;
            // An id column splits the cloud into individual curves. Without one
            // there is a single relationship to draw, and an ICE plot of one
            // curve is a partial dependence plot - said in the label rather
            // than drawn as though it were something else.
            const bool grouped=ice&&in.series.size()>=3;
            const QVector<double> id=grouped?in.series.at(2).y:QVector<double>();
            const int n=qMin(feature.size(),prediction.size());

            if(grouped){
                QHash<double,QVector<QPair<double,double>>> curves;
                for(int i=0;i<n&&i<id.size();++i){
                    if(!finite(feature[i])||!finite(prediction[i])||!finite(id[i])) continue;
                    curves[id[i]].append({feature[i],prediction[i]});
                }
                // Fifty curves is already a smear; beyond that the plot is ink.
                int drawn=0;
                QList<double> keys=curves.keys();
                std::sort(keys.begin(),keys.end());
                for(double k:keys){
                    if(drawn>=50) break;
                    QVector<QPair<double,double>> c=curves.value(k);
                    if(c.size()<2) continue;
                    std::sort(c.begin(),c.end(),
                              [](const QPair<double,double>& a,const QPair<double,double>& b){
                                  return a.first<b.first;
                              });
                    PlotSeries line;
                    line.label=QString();
                    line.color=in.style.gridColor;
                    line.lineWidth=0.6;
                    line.opacity=0.55;
                    for(const auto& pt:c){ line.x.append(pt.first); line.y.append(pt.second); }
                    out.series.append(line);
                    ++drawn;
                }
            }

            // The average, binned over the feature, drawn bold over the top.
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i)
                if(finite(feature[i])&&finite(prediction[i]))
                    rows.append({feature[i],prediction[i]});
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first;
                      });
            if(rows.size()>=4){
                const int bins=qBound(6,int(std::sqrt(double(rows.size()))),40);
                const double lo=rows.first().first, hi=rows.last().first;
                PlotSeries average;
                average.label=ice?QStringLiteral("average (partial dependence)")
                                 :QStringLiteral("partial dependence");
                average.color=in.series.at(1).color;
                average.lineWidth=qMax(1.6,in.style.lineWidth*1.4);
                if(hi>lo){
                    for(int b=0;b<bins;++b){
                        const double a=lo+(hi-lo)*double(b)/bins;
                        const double z=lo+(hi-lo)*double(b+1)/bins;
                        double sum=0.0; int count=0;
                        for(const auto& r:rows){
                            if(r.first<a) continue;
                            if(r.first>z) break;
                            sum+=r.second; ++count;
                        }
                        if(count==0) continue;
                        average.x.append((a+z)/2.0);
                        average.y.append(sum/double(count));
                    }
                    if(!average.x.isEmpty()) out.series.append(average);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("feature value"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("predicted response"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Influence Plot
    // Studentised residual against leverage, with marker area following Cook's
    // distance. An outlier with low leverage is a bad point; a high-leverage
    // point with a small residual is a point the model has bent itself to fit.
    // Those are different problems and the scatter separates them.
    if(in.engine==QLatin1String("Influence Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& leverage=in.series.at(0).y;
            const QVector<double>& residual=in.series.at(1).y;
            const int n=qMin(leverage.size(),residual.size());
            PlotSeries pts;
            pts.label=QStringLiteral("observations");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            double meanH=0.0; int used=0;
            for(int i=0;i<n;++i){
                if(!finite(leverage[i])||!finite(residual[i])) continue;
                pts.x.append(leverage[i]); pts.y.append(residual[i]);
                meanH+=leverage[i]; ++used;
            }
            if(used>0){
                out.series.append(pts);
                meanH/=double(used);
                // The conventional cut-offs: twice the mean leverage, and
                // residuals beyond plus or minus two.
                out.series.append(horizontalRule(2.0,0.0,pts.x.isEmpty()?1.0:*std::max_element(pts.x.begin(),pts.x.end()),
                                                 in.style.warning,QStringLiteral("±2 σ"),true));
                out.series.append(horizontalRule(-2.0,0.0,pts.x.isEmpty()?1.0:*std::max_element(pts.x.begin(),pts.x.end()),
                                                 in.style.warning,QString(),true));
                PlotSeries hBar;
                hBar.label=QStringLiteral("2× mean leverage");
                const double yLo=*std::min_element(pts.y.begin(),pts.y.end());
                const double yHi=*std::max_element(pts.y.begin(),pts.y.end());
                hBar.x={2.0*meanH,2.0*meanH}; hBar.y={yLo,yHi};
                hBar.color=in.style.danger;
                hBar.dashPattern={5,3};
                out.series.append(hBar);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("leverage"),false,0.0,unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("studentised residual"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------- Added-Variable Plot
    // The residuals of y after regressing on the other predictors, against the
    // residuals of the predictor of interest after the same. The slope of this
    // scatter IS that predictor's coefficient in the full model, which is what
    // makes it the honest picture of one variable's contribution.
    if(in.engine==QLatin1String("Added-Variable Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=3){
            const QVector<double>& y=in.series.at(0).y;
            const QVector<double>& focus=in.series.at(1).y;
            const QVector<double>& other=in.series.at(2).y;
            const int n=qMin(y.size(),qMin(focus.size(),other.size()));
            QVector<double> yy,ff,oo;
            for(int i=0;i<n;++i){
                if(!finite(y[i])||!finite(focus[i])||!finite(other[i])) continue;
                yy.append(y[i]); ff.append(focus[i]); oo.append(other[i]);
            }
            const Moments2 my=momentsOf(oo,yy);
            const Moments2 mf=momentsOf(oo,ff);
            if(my.n>=3&&my.sxx>1e-15&&mf.sxx>1e-15){
                const double by=my.sxy/my.sxx, ay=my.my-by*my.mx;
                const double bf=mf.sxy/mf.sxx, af=mf.my-bf*mf.mx;
                PlotSeries pts;
                pts.color=in.series.at(1).color;
                pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
                QVector<double> rx,ry;
                for(int i=0;i<yy.size();++i){
                    const double resY=yy[i]-(ay+by*oo[i]);
                    const double resF=ff[i]-(af+bf*oo[i]);
                    pts.x.append(resF); pts.y.append(resY);
                    rx.append(resF); ry.append(resY);
                }
                const Moments2 mr=momentsOf(rx,ry);
                const double slope=(mr.sxx>1e-15)?mr.sxy/mr.sxx:0.0;
                pts.label=QStringLiteral("partial slope = %1").arg(slope,0,'g',4);
                out.series.append(pts);
                const Bounds b=boundsOf(rx);
                if(b.valid){
                    PlotSeries fit;
                    fit.label=QStringLiteral("partial regression");
                    fit.x={b.lo,b.hi};
                    fit.y={slope*b.lo,slope*b.hi};   // both residuals have zero mean
                    fit.color=in.style.warning;
                    fit.lineWidth=qMax(1.3,in.style.lineWidth);
                    out.series.append(fit);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("residual of the predictor"),false,
                           unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("residual of the response"),false,
                           unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Interaction Plot
    // Cell means joined across one factor, one line per level of a second.
    // Parallel lines mean no interaction; lines that cross or diverge mean the
    // effect of one factor depends on the other, which no table of main effects
    // will tell you.
    if(in.engine==QLatin1String("Interaction Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=3){
            const QVector<double>& factorA=in.series.at(0).y;
            const QVector<double>& factorB=in.series.at(1).y;
            const QVector<double>& response=in.series.at(2).y;
            const int n=qMin(factorA.size(),qMin(factorB.size(),response.size()));
            // Cell means, keyed by the pair. A hash of pairs rather than a
            // dense matrix, because the levels are values in a column and need
            // not be 0..k.
            QHash<double,QHash<double,QPair<double,int>>> cells;
            for(int i=0;i<n;++i){
                if(!finite(factorA[i])||!finite(factorB[i])||!finite(response[i])) continue;
                auto& cell=cells[factorB[i]][factorA[i]];
                cell.first+=response[i]; cell.second+=1;
            }
            QList<double> levelsB=cells.keys();
            std::sort(levelsB.begin(),levelsB.end());
            // Hue-cycled rather than the shared palette: this file does not
            // include ColourVision, and the same construction is already used
            // for Gantt rows and swimmer lanes for the same reason.
            int idx=0;
            for(double b:levelsB){
                if(idx>=12) break;           // beyond a dozen lines it is a mesh
                QList<double> levelsA=cells[b].keys();
                std::sort(levelsA.begin(),levelsA.end());
                PlotSeries line;
                line.label=QStringLiteral("%1 = %2")
                               .arg(in.series.at(1).label.isEmpty()?QStringLiteral("B")
                                                                   :in.series.at(1).label)
                               .arg(b,0,'g',4);
                line.color=categoryColour(in,idx,std::fmod(double(idx)*0.17+0.05,1.0),0.55,0.92);
                line.lineWidth=qMax(1.3,in.style.lineWidth);
                line.drawMarkers=true; line.markerSize=4.5;
                for(double a:levelsA){
                    const auto cell=cells[b][a];
                    if(cell.second==0) continue;
                    line.x.append(a);
                    line.y.append(cell.first/double(cell.second));
                }
                if(line.x.size()>=1){ out.series.append(line); ++idx; }
            }
        }
        out.xAxis=PlotAxis{in.series.isEmpty()?QStringLiteral("factor A")
                                              :in.series.at(0).label,
                           false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("cell mean"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Tornado Diagram
    // Each input's low-to-high swing around the base case, sorted longest
    // first. The funnel shape is the point: the bars at the top are the inputs
    // worth measuring better, and everything below them is noise in the
    // sensitivity study.
    if(in.engine==QLatin1String("Tornado Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("Floating Row"));
        out.legendVisible=false;
        if(in.series.size()>=2){
            const QVector<double>& low=in.series.at(0).y;
            const QVector<double>& high=in.series.at(1).y;
            const int n=qMin(low.size(),high.size());
            QVector<int> order;
            for(int i=0;i<n;++i)
                if(finite(low[i])&&finite(high[i])) order.append(i);
            // Widest swing at the TOP, which with y increasing upward means
            // sorting ascending and letting the last row sit highest.
            std::sort(order.begin(),order.end(),[&low,&high](int a,int b){
                return std::abs(high[a]-low[a])<std::abs(high[b]-low[b]);
            });
            double base=0.0; int used=0;
            for(int i:order){ base+=(low[i]+high[i])/2.0; ++used; }
            if(used>0) base/=double(used);
            int slot=1;
            for(int i:order){
                PlotSeries bar;
                bar.label=QString();
                // Coloured by which direction the swing runs, because "this
                // input pushes the answer up" is half the reading.
                bar.color=(high[i]>=low[i])?in.style.positive:in.style.danger;
                bar.x={qMin(low[i],high[i]),qMax(low[i],high[i])};
                bar.y={double(slot)};
                out.series.append(bar);
                ++slot;
            }
            if(used>0){
                PlotSeries baseline;
                // Named plainly, with the VALUE under the figure. The bars
                // carry no label, so this was the only named series and its
                // legend row was suppressed as a caption - which left a
                // tornado whose dashed vertical line was unexplained and
                // whose base case was computed and never shown.
                baseline.label=QStringLiteral("base case");
                out.figureNote=QStringLiteral("Base case %1; bars span the outcome "
                                              "as each input moves over its range.")
                                   .arg(formatMeasured(base));
                baseline.x={base,base};
                baseline.y={0.0,double(slot)};
                baseline.color=in.style.foreground;
                baseline.dashPattern={5,3};
                out.series.append(baseline);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("outcome"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("input, by influence"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------- Efficient Frontier
    // Expected return against risk, with the frontier drawn through the
    // portfolios nothing dominates. A point below the frontier is one you could
    // improve without taking more risk, and the hull is what makes that
    // visible rather than arguable.
    if(in.engine==QLatin1String("Efficient Frontier")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& risk=in.series.at(0).y;
            const QVector<double>& ret=in.series.at(1).y;
            const int n=qMin(risk.size(),ret.size());
            PlotSeries cloud;
            cloud.label=QStringLiteral("portfolios");
            cloud.color=in.style.gridColor;
            cloud.drawLine=false; cloud.drawMarkers=true; cloud.markerSize=3.2;
            QVector<QPair<double,double>> pts;
            for(int i=0;i<n;++i){
                if(!finite(risk[i])||!finite(ret[i])) continue;
                cloud.x.append(risk[i]); cloud.y.append(ret[i]);
                pts.append({risk[i],ret[i]});
            }
            if(!pts.isEmpty()){
                out.series.append(cloud);
                // The frontier: sweeping left to right, keep a point only if no
                // point to its left has a return at least as high. That is the
                // non-dominated set, which is what "efficient" means.
                std::sort(pts.begin(),pts.end(),
                          [](const QPair<double,double>& a,const QPair<double,double>& b){
                              if(a.first!=b.first) return a.first<b.first;
                              return a.second>b.second;
                          });
                PlotSeries frontier;
                frontier.label=QStringLiteral("efficient frontier");
                frontier.color=in.style.positive;
                frontier.lineWidth=qMax(1.5,in.style.lineWidth);
                frontier.drawMarkers=true; frontier.markerSize=4.2;
                double best=-std::numeric_limits<double>::infinity();
                for(const auto& p:pts){
                    if(p.second<=best) continue;
                    best=p.second;
                    frontier.x.append(p.first); frontier.y.append(p.second);
                }
                if(frontier.x.size()>=2) out.series.append(frontier);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("risk  (standard deviation)"),false,0.0,unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("expected return"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------------- Fan Chart
    // A forecast as nested probability bands widening into the future, over the
    // history. Drawn this way because a single forecast line invites being read
    // as a prediction, and the whole content of a forecast is its uncertainty.
    if(in.engine==QLatin1String("Fan Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<8) continue;
            QVector<double> x,y;
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                x.append(s.x[i]); y.append(s.y[i]);
            }
            if(x.size()<8) continue;
            PlotSeries history;
            history.label=s.label.isEmpty()?QStringLiteral("history"):s.label;
            history.color=s.color;
            history.lineWidth=qMax(1.4,in.style.lineWidth);
            history.x=x; history.y=y;
            out.series.append(history);

            // The fan continues the last trend, and its width grows with the
            // square root of the horizon - the spread of a random walk, which
            // is the honest default when no model has said otherwise.
            const int look=qMax(4,x.size()/8);
            const double slope=(y.last()-y.at(y.size()-look))
                              /qMax(1e-12,x.last()-x.at(x.size()-look));
            double sd=0.0;
            for(int i=x.size()-look;i<x.size()-1;++i){
                const double step=y[i+1]-y[i];
                sd+=step*step;
            }
            sd=std::sqrt(sd/qMax(1,look-1));
            const double dx=(x.last()-x.first())/double(x.size()-1);
            const int horizon=qMax(6,x.size()/4);
            static const double kBands[]={0.5,1.0,1.5,2.0};
            for(double k:kBands){
                PlotSeries upper,lower;
                upper.label=(k==0.5)?QStringLiteral("forecast bands"):QString();
                lower.label=QString();
                upper.color=lower.color=in.style.warning;
                upper.opacity=lower.opacity=qBound(0.25,1.0-k*0.3,0.9);
                upper.dashPattern=lower.dashPattern={4,3};
                for(int h=0;h<=horizon;++h){
                    const double xx=x.last()+dx*h;
                    const double centre=y.last()+slope*dx*h;
                    const double spread=k*sd*std::sqrt(double(h));
                    upper.x.append(xx); upper.y.append(centre+spread);
                    lower.x.append(xx); lower.y.append(centre-spread);
                }
                out.series.append(upper);
                out.series.append(lower);
            }
            break;
        }
        out.xAxis=PlotAxis{QStringLiteral("time"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("value"),false,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------------------------- Snail Trail
    // Trailing risk against trailing return, joined in time order. It curls,
    // and the direction it curls in is the story: a manager drifting up and
    // left is improving, down and right is taking more risk for less.
    if(in.engine==QLatin1String("Snail Trail")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& risk=in.series.at(0).y;
            const QVector<double>& ret=in.series.at(1).y;
            const int n=qMin(risk.size(),ret.size());
            PlotSeries trail;
            trail.label=QStringLiteral("rolling window");
            trail.color=in.series.at(1).color;
            trail.lineWidth=qMax(1.3,in.style.lineWidth);
            trail.drawMarkers=n<=60; trail.markerSize=3.4;
            for(int i=0;i<n;++i){
                if(!finite(risk[i])||!finite(ret[i])) continue;
                trail.x.append(risk[i]); trail.y.append(ret[i]);
            }
            if(!trail.x.isEmpty()){
                out.series.append(trail);
                PlotSeries latest;
                latest.label=QStringLiteral("latest");
                latest.x={trail.x.last()}; latest.y={trail.y.last()};
                latest.color=in.style.positive;
                latest.drawLine=false; latest.drawMarkers=true; latest.markerSize=9.0;
                out.series.append(latest);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("trailing risk"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("trailing return"),false,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------------- Cost-Effectiveness Plane
    // Incremental cost against incremental effect, with the quadrant axes and
    // the willingness-to-pay ray. Points below the ray are worth buying at that
    // threshold; the cloud's spread across quadrants is the decision
    // uncertainty, and a point estimate hides all of it.
    if(in.engine==QLatin1String("Cost-Effectiveness Plane")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& effect=in.series.at(0).y;
            const QVector<double>& cost=in.series.at(1).y;
            const int n=qMin(effect.size(),cost.size());
            PlotSeries cloud;
            cloud.color=in.series.at(1).color;
            cloud.drawLine=false; cloud.drawMarkers=true; cloud.markerSize=3.2;
            double eLo=0.0,eHi=0.0,cLo=0.0,cHi=0.0;
            int below=0,total=0;
            double slope=0.0;
            {
                // The willingness-to-pay ray is set to the mean cost per unit
                // effect, so the figure always has a reference even when nobody
                // has stated a threshold. Named in the label as inferred.
                double sumE=0.0,sumC=0.0;
                for(int i=0;i<n;++i){
                    if(!finite(effect[i])||!finite(cost[i])) continue;
                    sumE+=effect[i]; sumC+=cost[i];
                }
                if(std::abs(sumE)>1e-12) slope=sumC/sumE;
            }
            for(int i=0;i<n;++i){
                if(!finite(effect[i])||!finite(cost[i])) continue;
                cloud.x.append(effect[i]); cloud.y.append(cost[i]);
                eLo=qMin(eLo,effect[i]); eHi=qMax(eHi,effect[i]);
                cLo=qMin(cLo,cost[i]);   cHi=qMax(cHi,cost[i]);
                ++total;
                if(cost[i]<slope*effect[i]) ++below;
            }
            if(total>0){
                cloud.label=QStringLiteral("%1% below the threshold")
                                .arg(100.0*double(below)/double(total),0,'f',0);
                out.series.append(cloud);
                PlotSeries zeroCost,zeroEffect,ray;
                zeroCost.label=QString();
                zeroCost.x={eLo,eHi}; zeroCost.y={0.0,0.0};
                zeroCost.color=in.style.gridColor;
                zeroEffect.label=QString();
                zeroEffect.x={0.0,0.0}; zeroEffect.y={cLo,cHi};
                zeroEffect.color=in.style.gridColor;
                ray.label=QStringLiteral("threshold %1 per unit effect")
                              .arg(formatMeasured(slope));
                ray.x={eLo,eHi}; ray.y={slope*eLo,slope*eHi};
                ray.color=in.style.warning;
                ray.dashPattern={5,3};
                out.series.append(zeroCost);
                out.series.append(zeroEffect);
                out.series.append(ray);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("incremental effect"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("incremental cost"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------- Cost-Effectiveness Acceptability Curve
    // The probability an option is cost-effective, against what a decision
    // maker is willing to pay. The answer to "is this worth it" is a curve
    // rather than a yes, and this is that curve.
    if(in.engine==QLatin1String("Acceptability Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& effect=in.series.at(0).y;
            const QVector<double>& cost=in.series.at(1).y;
            const int n=qMin(effect.size(),cost.size());
            double maxRatio=0.0; int valid=0;
            for(int i=0;i<n;++i){
                if(!finite(effect[i])||!finite(cost[i])) continue;
                ++valid;
                if(std::abs(effect[i])>1e-12)
                    maxRatio=qMax(maxRatio,std::abs(cost[i]/effect[i]));
            }
            if(valid>0&&maxRatio>0.0){
                PlotSeries curve;
                curve.label=QStringLiteral("probability cost-effective");
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.4,in.style.lineWidth);
                constexpr int kSteps=60;
                for(int s=0;s<=kSteps;++s){
                    const double wtp=maxRatio*2.0*double(s)/double(kSteps);
                    int accept=0;
                    for(int i=0;i<n;++i){
                        if(!finite(effect[i])||!finite(cost[i])) continue;
                        // Net monetary benefit positive: effect x WTP exceeds
                        // cost. The formulation that behaves when the effect is
                        // negative, where the ratio does not.
                        if(effect[i]*wtp-cost[i]>0.0) ++accept;
                    }
                    curve.x.append(wtp);
                    curve.y.append(double(accept)/double(valid));
                }
                out.series.append(curve);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("willingness to pay per unit effect"),
                           false,0.0,unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("probability cost-effective"),false,0.0,1.0};
        return out;
    }

    // ------------------------------------------------------- Lasagna Plot
    // A subject-by-time matrix as a raster, one row per subject. The
    // alternative for repeated measures is a spaghetti plot, which stops being
    // readable at about twenty subjects; this stays readable at hundreds
    // because it does not rely on telling one line from another.
    if(in.engine==QLatin1String("Lasagna Plot")){
        // Straight onto the heat grid: series 0 is x, 1 is y, 2 is the value,
        // which is exactly time, subject and measurement.
        PlotSpec out=derivedAs(in,QStringLiteral("2D Heatmap"));
        if(in.series.size()>=3){
            out.series.append(in.series.at(1));   // time on x
            out.series.append(in.series.at(0));   // subject on y
            out.series.append(in.series.at(2));
            out.xAxis=PlotAxis{in.series.at(1).label.isEmpty()?QStringLiteral("time")
                                                              :in.series.at(1).label,
                               false,unsetValue(),unsetValue()};
            out.yAxis=PlotAxis{in.series.at(0).label.isEmpty()?QStringLiteral("subject")
                                                              :in.series.at(0).label,
                               false,unsetValue(),unsetValue()};
        }
        return out;
    }

    // -------------------------------------------------------- Swimmer Plot
    // One horizontal lane per subject: a bar from start to stop, with a marker
    // where the study ended. A Gantt chart of people, and the standard way an
    // oncology trial's individual courses are shown.
    if(in.engine==QLatin1String("Swimmer Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Floating Row"));
        out.legendVisible=false;
        if(in.series.size()>=3){
            const QVector<double>& subject=in.series.at(0).y;
            const QVector<double>& start=in.series.at(1).y;
            const QVector<double>& stop=in.series.at(2).y;
            const int n=qMin(subject.size(),qMin(start.size(),stop.size()));
            QVector<int> order;
            for(int i=0;i<n;++i)
                if(finite(subject[i])&&finite(start[i])&&finite(stop[i])) order.append(i);
            // Longest at the top, which is what makes a swimmer plot readable
            // as a distribution of durations rather than a list.
            std::sort(order.begin(),order.end(),[&start,&stop](int a,int b){
                return (stop[a]-start[a])<(stop[b]-start[b]);
            });
            int slot=1;
            for(int i:order){
                PlotSeries lane;
                lane.label=QString();
                lane.color=categoryColour(in,int(std::abs(subject[i])),std::fmod(std::abs(subject[i])*0.17,1.0),0.45,0.9);
                lane.x={qMin(start[i],stop[i]),qMax(start[i],stop[i])};
                lane.y={double(slot)};
                out.series.append(lane);
                ++slot;
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("time on study"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("subject, by duration"),false,unsetValue(),unsetValue()};
        return out;
    }
    // =====================================================================
    // Batch 5: electrochemistry, bioprocess kinetics, energy, and structures.
    //
    // The first seven are the plots an electrochemist actually draws, and they
    // exist for the same reason the enzyme-kinetics linearisations do: each
    // rearrangement reads a different constant off a straight line, and which
    // one is trustworthy depends on where the measurement error sits. Levich
    // and Koutecky-Levich are the clearest case - the first is a line through
    // the origin whose slope is the diffusion-limited transport, the second is
    // its reciprocal, drawn precisely so the INTERCEPT can be read, because
    // that intercept is the kinetic current the first plot cannot show at all.
    // =====================================================================

    // ------------------------------------------------------------ Tafel Plot
    // Overpotential against log current density. The straight part of each
    // branch has slope b (volts per decade of current) and extrapolates back to
    // the exchange current density at zero overpotential.
    //
    // The two branches are fitted SEPARATELY. An anodic and a cathodic Tafel
    // slope are different numbers - they are the two transfer coefficients -
    // and a single fit through both would report the average of two quantities
    // that were never meant to be averaged.
    if(in.engine==QLatin1String("Tafel Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& eta=in.series.at(0).y;
            const QVector<double>& current=in.series.at(1).y;
            const int n=qMin(eta.size(),current.size());

            // The measured points, both branches, on |j| so a cathodic current
            // recorded as negative still lands on a log axis.
            PlotSeries pts;
            pts.label=QStringLiteral("measured");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;

            // Fitted in log space on each side of the origin, and only outside
            // the linear-polarisation region: within about 50 mV of the
            // equilibrium potential both reactions run and the plot curves, so
            // fitting there measures the curvature rather than the slope.
            QVector<double> anLog,anEta,caLog,caEta;
            for(int i=0;i<n;++i){
                const double e=eta[i], j=current[i];
                if(!finite(e)||!finite(j)) continue;
                const double mag=std::abs(j);
                if(!(mag>0.0)) continue;          // log of zero is not a point
                pts.x.append(mag); pts.y.append(e);
                if(std::abs(e)<0.05) continue;
                if(e>0.0){ anLog.append(std::log10(mag)); anEta.append(e); }
                else     { caLog.append(std::log10(mag)); caEta.append(e); }
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                const auto branch=[&out,&in](const QVector<double>& lg,
                                             const QVector<double>& e,
                                             const QString& name){
                    if(lg.size()<3) return;
                    const LineFit f=fitLine(lg,e);
                    if(!f.ok||std::abs(f.slope)<1e-12) return;
                    // eta = intercept + slope*log10|j|, so log10 j0 is where
                    // eta crosses zero.
                    const double j0=std::pow(10.0,-f.intercept/f.slope);
                    const Bounds b=boundsOf(lg);
                    if(!b.valid) return;
                    PlotSeries fit;
                    fit.label=QStringLiteral("%1: %2 mV/dec, j0 %3")
                                  .arg(name)
                                  .arg(f.slope*1000.0,0,'f',1)
                                  .arg(j0,0,'g',3);
                    // Drawn back to the exchange current so the extrapolation
                    // that produced j0 is visible rather than asserted.
                    const double lo=qMin(b.lo,std::log10(qMax(j0,1e-300)));
                    fit.x={std::pow(10.0,lo),std::pow(10.0,b.hi)};
                    fit.y={f.intercept+f.slope*lo,f.intercept+f.slope*b.hi};
                    fit.color=in.style.warning;
                    fit.lineWidth=qMax(1.3,in.style.lineWidth);
                    out.series.append(fit);
                };
                branch(anLog,anEta,QStringLiteral("anodic"));
                branch(caLog,caEta,QStringLiteral("cathodic"));
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("|current density|"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("overpotential (V)"),false,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------------------- Cyclic Voltammogram
    // Current against potential, traced in ACQUISITION ORDER. Sorting by
    // potential would be fatal here: the forward and reverse sweeps pass
    // through the same potentials at different currents, and that separation is
    // the entire measurement. Sorted, the loop collapses into a single line and
    // the peaks it exists to show disappear.
    if(in.engine==QLatin1String("Cyclic Voltammogram")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& potential=in.series.at(0).y;
            const QVector<double>& current=in.series.at(1).y;
            const int n=qMin(potential.size(),current.size());
            PlotSeries loop;
            loop.label=QStringLiteral("voltammogram");
            loop.color=in.series.at(1).color;
            loop.lineWidth=qMax(1.2,in.style.lineWidth);
            int peakA=-1,peakC=-1;
            for(int i=0;i<n;++i){
                if(!finite(potential[i])||!finite(current[i])) continue;
                loop.x.append(potential[i]); loop.y.append(current[i]);
                if(peakA<0||current[i]>current[peakA]) peakA=i;
                if(peakC<0||current[i]<current[peakC]) peakC=i;
            }
            if(!loop.x.isEmpty()){
                out.series.append(loop);
                if(peakA>=0&&peakC>=0){
                    PlotSeries marks;
                    // The two peaks and what they say about reversibility. A
                    // 59 mV separation at 25 C is a one-electron reversible
                    // couple; the number is reported rather than judged,
                    // because whether it is reversible depends on the scan
                    // rate and the couple, which this plot does not know.
                    const double dE=std::abs(potential[peakA]-potential[peakC]);
                    const double ratio=std::abs(current[peakC])>1e-300
                                       ?std::abs(current[peakA]/current[peakC]):0.0;
                    marks.label=QStringLiteral("dEp %1 mV, ipa/ipc %2")
                                    .arg(dE*1000.0,0,'f',0).arg(ratio,0,'f',2);
                    marks.drawLine=false; marks.drawMarkers=true; marks.markerSize=6.5;
                    marks.color=in.style.warning;
                    marks.x={potential[peakA],potential[peakC]};
                    marks.y={current[peakA],current[peakC]};
                    out.series.append(marks);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("potential (V)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("current"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------- Levich / Koutecky-Levich / Randles-Sevcik Plots
    // Three straight lines from rotating-disc and sweep voltammetry, sharing
    // one code path because they share one shape: a transformed abscissa, a
    // least-squares line, and a constant read off it.
    //
    // Levich and Randles-Sevcik are lines THROUGH THE ORIGIN - at zero rotation
    // or zero scan rate there is no current - so they are fitted with the
    // intercept fixed at zero. Fitting a free intercept would produce a
    // slightly better residual and a slope that no longer means what the
    // Levich equation says it means.
    if(in.engine==QLatin1String("Levich Plot")
       ||in.engine==QLatin1String("Koutecky-Levich Plot")
       ||in.engine==QLatin1String("Randles-Sevcik Plot")){
        const bool koutecky=(in.engine==QLatin1String("Koutecky-Levich Plot"));
        const bool randles=(in.engine==QLatin1String("Randles-Sevcik Plot"));
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& rate=in.series.at(0).y;     // omega, or scan rate
            const QVector<double>& current=in.series.at(1).y;
            const int n=qMin(rate.size(),current.size());
            PlotSeries pts;
            pts.label=QStringLiteral("measured");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.4;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                const double w=rate[i], j=current[i];
                if(!finite(w)||!finite(j)||!(w>0.0)) continue;
                const double root=std::sqrt(w);
                double x,y;
                if(koutecky){ if(!(std::abs(j)>0.0)) continue; x=1.0/root; y=1.0/j; }
                else        { x=root; y=j; }
                pts.x.append(x); pts.y.append(y);
                fx.append(x); fy.append(y);
            }
            if(fx.size()>=2){
                out.series.append(pts);
                const Bounds b=boundsOf(fx);
                PlotSeries fit;
                fit.color=in.style.warning;
                fit.lineWidth=qMax(1.3,in.style.lineWidth);
                if(koutecky){
                    // Free intercept, and the intercept is the point: 1/i_k,
                    // the current the reaction would pass if transport were
                    // infinitely fast.
                    const LineFit f=fitLine(fx,fy);
                    if(f.ok&&b.valid){
                        const double ik=std::abs(f.intercept)>1e-15?1.0/f.intercept:0.0;
                        fit.label=QStringLiteral("kinetic current %1, slope %2")
                                      .arg(formatMeasured(ik)).arg(formatMeasured(f.slope));
                        // Drawn back to the axis, because reading an intercept
                        // off a line that stops short of it is guesswork.
                        const double lo=qMin(0.0,b.lo);
                        fit.x={lo,b.hi};
                        fit.y={f.intercept+f.slope*lo,f.intercept+f.slope*b.hi};
                        out.series.append(fit);
                    }
                }else{
                    double sxy=0.0,sxx=0.0;
                    for(int i=0;i<fx.size();++i){ sxy+=fx[i]*fy[i]; sxx+=fx[i]*fx[i]; }
                    if(sxx>1e-300&&b.valid){
                        const double slope=sxy/sxx;
                        fit.label=randles
                            ?QStringLiteral("ip / sqrt(v) = %1").arg(slope,0,'g',4)
                            :QStringLiteral("Levich slope %1").arg(slope,0,'g',4);
                        fit.x={0.0,b.hi};
                        fit.y={0.0,slope*b.hi};
                        out.series.append(fit);
                    }
                }
            }
        }
        if(koutecky){
            out.xAxis=PlotAxis{QStringLiteral("1 / sqrt(rotation rate)"),false,unsetValue(),unsetValue()};
            out.yAxis=PlotAxis{QStringLiteral("1 / current"),false,unsetValue(),unsetValue()};
        }else if(randles){
            out.xAxis=PlotAxis{QStringLiteral("sqrt(scan rate)"),false,unsetValue(),unsetValue()};
            out.yAxis=PlotAxis{QStringLiteral("peak current"),false,unsetValue(),unsetValue()};
        }else{
            out.xAxis=PlotAxis{QStringLiteral("sqrt(rotation rate)"),false,unsetValue(),unsetValue()};
            out.yAxis=PlotAxis{QStringLiteral("limiting current"),false,unsetValue(),unsetValue()};
        }
        return out;
    }

    // ------------------------- Monod Growth / Substrate Inhibition Curves
    // Specific growth rate against substrate concentration. Monod saturates;
    // Haldane rises, peaks and falls again because the substrate that feeds the
    // culture also poisons it above some concentration.
    //
    // Both are fitted through the Hanes-Woolf rearrangement S/mu, which is
    // linear in (1, S) for Monod and in (1, S, S^2) for Haldane - so the same
    // three-column normal equations solve either, and the well-conditioned
    // rearrangement is used rather than the double-reciprocal one.
    if(in.engine==QLatin1String("Monod Growth Curve")
       ||in.engine==QLatin1String("Substrate Inhibition Curve")){
        const bool haldane=(in.engine==QLatin1String("Substrate Inhibition Curve"));
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& substrate=in.series.at(0).y;
            const QVector<double>& growth=in.series.at(1).y;
            const int n=qMin(substrate.size(),growth.size());
            PlotSeries pts;
            pts.label=QStringLiteral("measured");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.4;
            QVector<double> ss,ww;
            for(int i=0;i<n;++i){
                const double s=substrate[i], u=growth[i];
                if(!finite(s)||!finite(u)||!(s>0.0)||!(u>0.0)) continue;
                pts.x.append(s); pts.y.append(u);
                ss.append(s); ww.append(s/u);        // Hanes-Woolf ordinate
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                // Normal equations for w = c0 + c1*S (+ c2*S^2), by Cramer's
                // rule on the 2x2 or 3x3. Small and explicit beats pulling in a
                // solver for a system this size.
                const int terms=haldane?3:2;
                double A[3][3]={{0,0,0},{0,0,0},{0,0,0}}, rhs[3]={0,0,0};
                for(int i=0;i<ss.size();++i){
                    double basis[3]={1.0,ss[i],ss[i]*ss[i]};
                    for(int r=0;r<terms;++r){
                        for(int c=0;c<terms;++c) A[r][c]+=basis[r]*basis[c];
                        rhs[r]+=basis[r]*ww[i];
                    }
                }
                double c[3]={0,0,0};
                bool solved=false;
                if(ss.size()>=terms){
                    if(terms==2){
                        const double det=A[0][0]*A[1][1]-A[0][1]*A[1][0];
                        if(std::abs(det)>1e-300){
                            c[0]=(rhs[0]*A[1][1]-A[0][1]*rhs[1])/det;
                            c[1]=(A[0][0]*rhs[1]-rhs[0]*A[1][0])/det;
                            solved=true;
                        }
                    }else{
                        const auto det3=[](const double m[3][3]){
                            return m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])
                                  -m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])
                                  +m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
                        };
                        const double det=det3(A);
                        if(std::abs(det)>1e-300){
                            for(int k=0;k<3;++k){
                                double M[3][3];
                                for(int r=0;r<3;++r)
                                    for(int cc=0;cc<3;++cc) M[r][cc]=(cc==k)?rhs[r]:A[r][cc];
                                c[k]=det3(M)/det;
                            }
                            solved=true;
                        }
                    }
                }
                // S/mu = Ks/mumax + S/mumax + S^2/(mumax*Ki)
                if(solved&&c[1]>1e-300){
                    const double umax=1.0/c[1];
                    const double ks=c[0]*umax;
                    // Ki = 1/(umax*c2), NOT umax/c2. The Hanes-Woolf
                    // coefficient on S^2 is 1/(umax*Ki), so the wrong
                    // rearrangement is out by umax squared - and it fails
                    // quietly, because it still returns a plausible
                    // positive number and still draws a curve through the
                    // points. Noiseless Haldane data with Ki 300 reported
                    // 60.8 until this was checked against the constants it
                    // was generated from.
                    const double ki=(haldane&&c[2]>1e-300)?1.0/(umax*c[2]):0.0;
                    const Bounds b=boundsOf(ss);
                    if(b.valid&&ks>=0.0){
                        PlotSeries curve;
                        curve.label=haldane
                            ?QStringLiteral("umax %1, Ks %2, Ki %3")
                                 .arg(umax,0,'g',3).arg(ks,0,'g',3).arg(ki,0,'g',3)
                            :QStringLiteral("umax %1, Ks %2")
                                 .arg(umax,0,'g',3).arg(ks,0,'g',3);
                        curve.color=in.style.warning;
                        curve.lineWidth=qMax(1.3,in.style.lineWidth);
                        for(int k=0;k<=120;++k){
                            const double s=b.lo+(b.hi-b.lo)*double(k)/120.0;
                            double denom=ks+s;
                            if(haldane&&ki>0.0) denom+=s*s/ki;
                            if(!(denom>1e-300)) continue;
                            curve.x.append(s); curve.y.append(umax*s/denom);
                        }
                        if(curve.x.size()>=2) out.series.append(curve);
                        // Where an inhibited culture grows fastest. This is the
                        // number the plot is drawn to find - a feed above it
                        // makes the culture slower, not faster - and it is
                        // nowhere on a Monod curve, which has no maximum.
                        if(haldane&&ki>0.0&&ks>0.0){
                            const double sOpt=std::sqrt(ks*ki);
                            const double uOpt=umax*sOpt/(ks+sOpt+sOpt*sOpt/ki);
                            PlotSeries mark;
                            mark.label=QStringLiteral("optimum S %1").arg(sOpt,0,'g',3);
                            mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.5;
                            mark.color=in.style.warning;
                            mark.x={sOpt}; mark.y={uOpt};
                            out.series.append(mark);
                        }
                    }
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("substrate concentration"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("specific growth rate"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ----------------------------------------------------------- Ragone Plot
    // Energy density against power density, both logarithmic, because the
    // devices being compared differ by four orders of magnitude in one and
    // three in the other. The diagonals are lines of constant discharge time:
    // energy over power IS a time, so a device's position between two diagonals
    // says how long it can sustain its rated power, which is the comparison the
    // plot exists to make.
    return std::nullopt;
}

} // namespace graphvis
