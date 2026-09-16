// ============================================================================
// prepareSpecCore, part 4 of 6: "Ragone Plot" through "Nyquist Stability Plot".
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

std::optional<PlotSpec> QtPlotBackend::prepareEngineGroup4(const PlotSpec& in) const {
    if(in.engine==QLatin1String("Ragone Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& power=in.series.at(0).y;
            const QVector<double>& energy=in.series.at(1).y;
            const QVector<double> group=in.series.size()>=3?in.series.at(2).y:QVector<double>();
            const int n=qMin(power.size(),energy.size());

            QHash<double,PlotSeries> families;
            QList<double> order;
            double pLo=0,pHi=0,eLo=0,eHi=0; bool any=false;
            for(int i=0;i<n;++i){
                const double p=power[i], e=energy[i];
                if(!finite(p)||!finite(e)||!(p>0.0)||!(e>0.0)) continue;
                const double key=(i<group.size()&&finite(group[i]))?group[i]:0.0;
                if(!families.contains(key)){
                    PlotSeries s;
                    s.label=group.isEmpty()?QStringLiteral("devices")
                                           :QStringLiteral("group %1").arg(key,0,'g',4);
                    s.color=group.isEmpty()?in.series.at(1).color
                                          :categoryColour(in,int(std::abs(key)),std::fmod(std::abs(key)*0.17,1.0),0.62,0.92);
                    s.drawLine=false; s.drawMarkers=true; s.markerSize=5.0;
                    families.insert(key,s);
                    order.append(key);
                }
                families[key].x.append(p);
                families[key].y.append(e);
                if(!any){ pLo=pHi=p; eLo=eHi=e; any=true; }
                else { pLo=qMin(pLo,p); pHi=qMax(pHi,p); eLo=qMin(eLo,e); eHi=qMax(eHi,e); }
            }
            if(any){
                // The diagonals first, so the devices are drawn over them.
                const double tLo=std::floor(std::log10(eLo/pHi));
                const double tHi=std::ceil(std::log10(eHi/pLo));
                for(double t=tLo;t<=tHi&&t-tLo<8.0;t+=1.0){
                    const double hours=std::pow(10.0,t);
                    PlotSeries iso;
                    iso.label=QString();
                    iso.color=in.style.gridColor;
                    iso.lineWidth=0.8;
                    iso.opacity=0.7;
                    iso.x={pLo,pHi};
                    iso.y={pLo*hours,pHi*hours};
                    out.series.append(iso);
                }
                std::sort(order.begin(),order.end());
                for(double k:order) out.series.append(families.value(k));
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("specific power"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("specific energy"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------------- I-V Curve
    // Current against voltage for a photovoltaic device, with the
    // maximum-power rectangle drawn on it.
    //
    // The power curve is deliberately NOT drawn on this axis. Power and current
    // are different units, and putting both on one unlabelled ordinate - which
    // is how the chart usually appears - means one of the two curves is being
    // read against a scale that is not its own. The rectangle carries the same
    // information honestly: its corner is the maximum power point, its area is
    // Pmax, and the fill factor is that area over the Voc-Isc rectangle.
    if(in.engine==QLatin1String("I-V Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& voltage=in.series.at(0).y;
            const QVector<double>& current=in.series.at(1).y;
            const int n=qMin(voltage.size(),current.size());
            QVector<QPair<double,double>> iv;
            for(int i=0;i<n;++i)
                if(finite(voltage[i])&&finite(current[i]))
                    iv.append({voltage[i],current[i]});
            std::sort(iv.begin(),iv.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first;
                      });
            if(iv.size()>=2){
                PlotSeries curve;
                curve.label=QStringLiteral("I-V");
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.3,in.style.lineWidth);
                double pmax=0.0,vmp=0.0,imp=0.0,isc=0.0,voc=0.0;
                for(const auto& pt:iv){
                    curve.x.append(pt.first); curve.y.append(pt.second);
                    const double p=pt.first*pt.second;
                    if(p>pmax){ pmax=p; vmp=pt.first; imp=pt.second; }
                }
                out.series.append(curve);
                // Isc is the current at the smallest voltage measured and Voc
                // the voltage at the smallest current - taken from the data
                // rather than extrapolated, because an extrapolated Voc on a
                // sweep that stopped early is a number nobody measured.
                isc=iv.first().second;
                voc=iv.last().first;
                for(const auto& pt:iv) if(std::abs(pt.second)<std::abs(isc)*1e-3){ voc=pt.first; break; }
                if(pmax>0.0&&voc>0.0&&isc>0.0){
                    const double ff=pmax/(voc*isc);
                    PlotSeries box;
                    box.label=QStringLiteral("Pmax %1, FF %2, Voc %3, Isc %4")
                                  .arg(pmax,0,'g',3).arg(ff,0,'f',3)
                                  .arg(voc,0,'g',3).arg(isc,0,'g',3);
                    box.color=in.style.warning;
                    box.lineWidth=1.0;
                    box.x={0.0,vmp,vmp,0.0,0.0};
                    box.y={imp,imp,0.0,0.0,imp};
                    out.series.append(box);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("voltage (V)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("current (A)"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------- Degree-Day Signature
    // Metered energy against outside temperature, with the change-point model
    // fitted: flat above the balance point, sloping below it. The balance-point
    // temperature is where the building stops needing heat, and it is a
    // property of the building rather than of the weather - which is why this
    // plot, and not a time series of consumption, is what shows whether an
    // energy-conservation measure worked.
    if(in.engine==QLatin1String("Degree-Day Signature")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& temperature=in.series.at(0).y;
            const QVector<double>& energy=in.series.at(1).y;
            const int n=qMin(temperature.size(),energy.size());
            PlotSeries pts;
            pts.label=QStringLiteral("metered");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;
            QVector<double> tx,ty;
            for(int i=0;i<n;++i){
                if(!finite(temperature[i])||!finite(energy[i])) continue;
                pts.x.append(temperature[i]); pts.y.append(energy[i]);
                tx.append(temperature[i]); ty.append(energy[i]);
            }
            if(tx.size()>=4){
                out.series.append(pts);
                // The breakpoint is found by search rather than by algebra: for
                // each candidate balance point the model is linear in its two
                // free parameters, so the search is over ONE dimension and each
                // step is a closed-form least squares. Trying to solve for all
                // three at once is a non-convex problem that lands in a
                // different local minimum depending on where it starts.
                const Bounds b=boundsOf(tx);
                double bestSse=std::numeric_limits<double>::infinity();
                double bestTau=0.0,bestBase=0.0,bestSlope=0.0;
                const int steps=60;
                for(int k=1;k<steps;++k){
                    const double tau=b.lo+(b.hi-b.lo)*double(k)/double(steps);
                    QVector<double> hx,hy;
                    for(int i=0;i<tx.size();++i){ hx.append(qMax(0.0,tau-tx[i])); hy.append(ty[i]); }
                    const LineFit f=fitLine(hx,hy);
                    if(!f.ok) continue;
                    double sse=0.0;
                    for(int i=0;i<hx.size();++i){
                        const double r=hy[i]-(f.intercept+f.slope*hx[i]);
                        sse+=r*r;
                    }
                    if(sse<bestSse){ bestSse=sse; bestTau=tau; bestBase=f.intercept; bestSlope=f.slope; }
                }
                if(std::isfinite(bestSse)&&b.valid){
                    PlotSeries model;
                    model.label=QStringLiteral("balance point %1, slope %2 per degree")
                                    .arg(bestTau,0,'f',1).arg(bestSlope,0,'g',3);
                    model.color=in.style.warning;
                    model.lineWidth=qMax(1.3,in.style.lineWidth);
                    model.x={b.lo,bestTau,b.hi};
                    model.y={bestBase+bestSlope*qMax(0.0,bestTau-b.lo),bestBase,bestBase};
                    out.series.append(model);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("outside temperature"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("energy use"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------- Abatement Cost Curve
    // Measures ranked cheapest first, each drawn as a block whose WIDTH is how
    // much it saves and whose HEIGHT is what it costs per unit saved. The width
    // is the whole point: a cheap measure that abates almost nothing is a
    // narrow block, and a bar chart of cost per tonne makes it look identical
    // to one that abates a third of the total.
    //
    // Blocks below the axis pay for themselves.
    if(in.engine==QLatin1String("Abatement Cost Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        out.legendVisible=false;
        if(in.series.size()>=2){
            const QVector<double>& quantity=in.series.at(0).y;
            const QVector<double>& cost=in.series.at(1).y;
            const int n=qMin(quantity.size(),cost.size());
            QVector<QPair<double,double>> measures;   // (cost, quantity)
            for(int i=0;i<n;++i){
                if(!finite(quantity[i])||!finite(cost[i])||!(quantity[i]>0.0)) continue;
                measures.append({cost[i],quantity[i]});
            }
            std::sort(measures.begin(),measures.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first;
                      });
            double x=0.0;
            for(int i=0;i<measures.size()&&i<200;++i){
                const double c=measures[i].first, w=measures[i].second;
                PlotSeries block;
                block.label=QString();
                block.color=c<0.0?in.style.positive:in.style.warning;
                block.lineWidth=1.0;
                // A cost curve is read by AREA - width is the abatement
                // available and height is what it costs - so a hollow block
                // hides the quantity the chart exists for.
                block.fillClosed=true;
                block.x={x,x,x+w,x+w,x};
                block.y={0.0,c,c,0.0,0.0};
                out.series.append(block);
                x+=w;
            }
            if(x>0.0){
                PlotSeries axis;
                axis.label=QString();
                axis.color=in.style.gridColor;
                axis.lineWidth=1.0;
                axis.x={0.0,x}; axis.y={0.0,0.0};
                out.series.append(axis);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("cumulative abatement"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("cost per unit abated"),false,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------------------------- Mohr's Circle
    // A stress state as a circle on the (normal, shear) plane: the centre is
    // the mean stress, the radius is the maximum shear, and the two points
    // where the circle crosses the axis are the principal stresses. The
    // diameter joins the two faces the state was measured on, and its angle to
    // the axis is twice the angle to the principal directions.
    if(in.engine==QLatin1String("Mohr's Circle")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=3){
            const QVector<double>& sx=in.series.at(0).y;
            const QVector<double>& sy=in.series.at(1).y;
            const QVector<double>& txy=in.series.at(2).y;
            const int n=qMin(sx.size(),qMin(sy.size(),txy.size()));
            // One circle per row, capped: a dozen stress states on one plane is
            // a comparison; a hundred is a disc.
            for(int i=0;i<n&&i<12;++i){
                if(!finite(sx[i])||!finite(sy[i])||!finite(txy[i])) continue;
                const double centre=(sx[i]+sy[i])/2.0;
                const double half=(sx[i]-sy[i])/2.0;
                const double radius=std::sqrt(half*half+txy[i]*txy[i]);
                if(!(radius>0.0)) continue;
                const double s1=centre+radius, s2=centre-radius;
                const double theta=0.5*std::atan2(txy[i],half)*180.0/M_PI;

                PlotSeries circle;
                // The principal stresses go UNDER the figure. Only the first
                // circle was ever named, so on a single stress state - the
                // ordinary case - this was the one named series on the figure
                // and its legend row was suppressed as a caption. A Mohr's
                // circle is drawn to read sigma-1, sigma-2, tau-max and the
                // angle off; all four were computed and none reached the page.
                circle.label=(n==1||i==0)?QStringLiteral("stress state"):QString();
                if(i==0)
                    out.figureNote=(n>1)
                        ? QStringLiteral("First state: s1 %1, s2 %2, tmax %3, "
                                         "principal plane at %4 deg.")
                              .arg(formatMeasured(s1)).arg(formatMeasured(s2))
                              .arg(formatMeasured(radius)).arg(theta,0,'f',1)
                        : QStringLiteral("s1 %1, s2 %2, tmax %3, principal plane "
                                         "at %4 deg.")
                              .arg(formatMeasured(s1)).arg(formatMeasured(s2))
                              .arg(formatMeasured(radius)).arg(theta,0,'f',1);
                circle.color=(n==1)?in.series.at(0).color
                                  :categoryColour(in,i,std::fmod(double(i)*0.13,1.0),0.6,0.92);
                circle.lineWidth=qMax(1.2,in.style.lineWidth);
                for(int k=0;k<=180;++k){
                    const double a=2.0*M_PI*double(k)/180.0;
                    circle.x.append(centre+radius*std::cos(a));
                    circle.y.append(radius*std::sin(a));
                }
                out.series.append(circle);

                // The measured diameter, and the two principal points.
                PlotSeries diameter;
                diameter.label=QString();
                diameter.color=circle.color;
                diameter.lineWidth=0.9;
                diameter.opacity=0.85;
                diameter.x={sx[i],sy[i]};
                diameter.y={txy[i],-txy[i]};
                out.series.append(diameter);

                PlotSeries principal;
                principal.label=QString();
                principal.color=in.style.warning;
                principal.drawLine=false; principal.drawMarkers=true; principal.markerSize=5.0;
                principal.x={s1,s2}; principal.y={0.0,0.0};
                out.series.append(principal);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("normal stress"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("shear stress"),false,unsetValue(),unsetValue()};
        // Both axes are STRESS in the same unit and the radius is the reading:
        // see PlotSpec::equalAspect. Drawn to its own range on each axis this
        // came out as an ellipse.
        out.equalAspect=true;
        return out;
    }

    // --------------------------------------------- P-M Interaction Diagram
    // The axial-load and bending-moment pairs a section can carry, as a closed
    // envelope. Anything inside is safe; anything outside is not; and the
    // characteristic bulge means a column can carry MORE moment under some
    // compression than under none, which is the fact the diagram exists to
    // make obvious.
    if(in.engine==QLatin1String("P-M Interaction Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& moment=in.series.at(0).y;
            const QVector<double>& axial=in.series.at(1).y;
            const int n=qMin(moment.size(),axial.size());
            QVector<QPair<double,double>> pts;
            double cx=0.0,cy=0.0;
            for(int i=0;i<n;++i){
                if(!finite(moment[i])||!finite(axial[i])) continue;
                pts.append({moment[i],axial[i]});
                cx+=moment[i]; cy+=axial[i];
            }
            if(pts.size()>=3){
                cx/=double(pts.size()); cy/=double(pts.size());
                // Ordered by angle about the centroid, so a capacity table
                // written in any order still closes into an envelope rather
                // than a scribble.
                std::sort(pts.begin(),pts.end(),
                          [cx,cy](const QPair<double,double>& a,const QPair<double,double>& b){
                              return std::atan2(a.second-cy,a.first-cx)
                                   < std::atan2(b.second-cy,b.first-cx);
                          });
                PlotSeries envelope;
                envelope.color=in.series.at(1).color;
                envelope.lineWidth=qMax(1.3,in.style.lineWidth);
                int balanced=0;
                for(int i=0;i<pts.size();++i){
                    envelope.x.append(pts[i].first);
                    envelope.y.append(pts[i].second);
                    if(pts[i].first>pts[balanced].first) balanced=i;
                }
                envelope.x.append(pts.first().first);
                envelope.y.append(pts.first().second);
                // THE NAME BELONGS ON THE MARK, not on the envelope. The
                // balanced point was named in the envelope's label while the
                // marker that actually sits on it carried none - so the figure
                // had one named series, its legend row was suppressed as a
                // caption, and neither the words nor the dot said which point
                // was which. Naming the mark instead gives the legend two rows,
                // so it is drawn, and ties the number to the dot it describes.
                envelope.label=QStringLiteral("interaction envelope");
                out.series.append(envelope);

                PlotSeries mark;
                mark.label=QStringLiteral("balanced point: M %1 at P %2")
                               .arg(formatMeasured(pts[balanced].first))
                               .arg(formatMeasured(pts[balanced].second));
                mark.color=in.style.warning;
                mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.0;
                mark.x={pts[balanced].first}; mark.y={pts[balanced].second};
                out.series.append(mark);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("moment"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("axial load"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------- Pushover Capacity Curve
    // Base shear against roof displacement, with the equal-area bilinear
    // idealisation over it. The ductility ratio it yields - ultimate over yield
    // displacement - is the number the whole nonlinear analysis was run to
    // produce, and it cannot be read off the curve by eye.
    if(in.engine==QLatin1String("Pushover Capacity Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& displacement=in.series.at(0).y;
            const QVector<double>& shear=in.series.at(1).y;
            const int n=qMin(displacement.size(),shear.size());
            QVector<QPair<double,double>> curve;
            for(int i=0;i<n;++i)
                if(finite(displacement[i])&&finite(shear[i]))
                    curve.append({displacement[i],shear[i]});
            std::sort(curve.begin(),curve.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first;
                      });
            if(curve.size()>=3){
                PlotSeries line;
                line.label=QStringLiteral("capacity");
                line.color=in.series.at(1).color;
                line.lineWidth=qMax(1.3,in.style.lineWidth);
                double vmax=0.0,dAtMax=0.0;
                for(const auto& pt:curve){
                    line.x.append(pt.first); line.y.append(pt.second);
                    if(pt.second>vmax){ vmax=pt.second; dAtMax=pt.first; }
                }
                out.series.append(line);
                (void)dAtMax;
                // Initial stiffness through the 0.6*Vmax point, as the codes
                // define it, rather than through the first two samples - which
                // would make the answer depend on how finely the analysis
                // happened to be stepped.
                double d60=0.0;
                for(int i=1;i<curve.size();++i){
                    if(curve[i].second>=0.6*vmax){
                        const double y0=curve[i-1].second, y1=curve[i].second;
                        const double t=(y1>y0)?(0.6*vmax-y0)/(y1-y0):0.0;
                        d60=curve[i-1].first+t*(curve[i].first-curve[i-1].first);
                        break;
                    }
                }
                const double du=curve.last().first;
                if(d60>0.0&&vmax>0.0&&du>d60){
                    const double k=0.6*vmax/d60;
                    // Equal areas: the bilinear curve encloses the same energy
                    // as the real one, which fixes the yield shear.
                    double area=0.0;
                    for(int i=1;i<curve.size();++i)
                        area+=0.5*(curve[i].second+curve[i-1].second)
                                 *(curve[i].first-curve[i-1].first);
                    // area = Vy*du - Vy^2/(2k)  =>  quadratic in Vy.
                    const double disc=du*du-2.0*area/k;
                    double vy=vmax;
                    if(disc>=0.0) vy=k*(du-std::sqrt(disc));
                    const double dy=(k>0.0)?vy/k:0.0;
                    if(dy>0.0){
                        PlotSeries bilinear;
                        bilinear.label=QStringLiteral("bilinear: Vy %1, dy %2, ductility %3")
                                           .arg(vy,0,'g',4).arg(dy,0,'g',3)
                                           .arg(du/dy,0,'f',2);
                        bilinear.color=in.style.warning;
                        bilinear.lineWidth=qMax(1.2,in.style.lineWidth);
                        bilinear.x={0.0,dy,du};
                        bilinear.y={0.0,vy,vy};
                        out.series.append(bilinear);
                    }
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("roof displacement"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("base shear"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Response Spectrum
    // Peak response against period, one curve per damping ratio, on a
    // logarithmic period axis - because the interesting structures span from a
    // tenth of a second to ten seconds and a linear axis crushes the short end
    // where most buildings sit.
    if(in.engine==QLatin1String("Response Spectrum")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& period=in.series.at(0).y;
            // THE PEAKS GO UNDER THE FIGURE, one clause per damping curve.
            //
            // They used to be appended to each curve's own legend row, which
            // works while there are several curves and fails on the ordinary
            // case of one: a figure with a single named series draws no legend
            // at all, so the peak spectral acceleration and the period it
            // falls at - which is what a design spectrum is read for - were
            // computed and shown nowhere. Collected here they are drawn
            // whether there is one damping curve or five, and the legend is
            // left carrying the damping names it is actually for.
            QStringList peaks;
            for(int s=1;s<in.series.size();++s){
                const PlotSeries& src=in.series.at(s);
                QVector<QPair<double,double>> pts;
                const int n=qMin(period.size(),src.y.size());
                for(int i=0;i<n;++i){
                    if(!finite(period[i])||!finite(src.y[i])||!(period[i]>0.0)) continue;
                    pts.append({period[i],src.y[i]});
                }
                std::sort(pts.begin(),pts.end(),
                          [](const QPair<double,double>& a,const QPair<double,double>& b){
                              return a.first<b.first;
                          });
                if(pts.size()<2) continue;
                PlotSeries curve;
                curve.label=src.label.isEmpty()?QStringLiteral("damping %1").arg(s):src.label;
                curve.color=src.color;
                curve.lineWidth=qMax(1.2,in.style.lineWidth);
                double peak=0.0,tPeak=0.0;
                for(const auto& pt:pts){
                    curve.x.append(pt.first); curve.y.append(pt.second);
                    if(pt.second>peak){ peak=pt.second; tPeak=pt.first; }
                }
                if(peak>0.0)
                    peaks.append(QStringLiteral("%1 peaks at %2, T %3 s")
                                     .arg(curve.label).arg(formatMeasured(peak))
                                     .arg(formatMeasured(tPeak)));
                out.series.append(curve);
            }
            if(!peaks.isEmpty())
                out.figureNote=peaks.join(QStringLiteral("; "))+QLatin1Char('.');
        }
        out.xAxis=PlotAxis{QStringLiteral("period (s)"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("spectral response"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------- Consolidation Curve
    // Dial reading against log time for one load increment, with Casagrande's
    // construction: the tangent at the steepest point and the secondary-
    // compression asymptote meet at 100% primary consolidation, and t50 is read
    // halfway between that and the corrected zero.
    //
    // The ordinate descends because settlement does. An axis drawn the other
    // way up is a picture of a building rising out of the ground.
    if(in.engine==QLatin1String("Consolidation Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& time=in.series.at(0).y;
            const QVector<double>& settlement=in.series.at(1).y;
            const int n=qMin(time.size(),settlement.size());
            QVector<QPair<double,double>> pts;      // (log10 t, settlement)
            for(int i=0;i<n;++i){
                if(!finite(time[i])||!finite(settlement[i])||!(time[i]>0.0)) continue;
                pts.append({std::log10(time[i]),settlement[i]});
            }
            std::sort(pts.begin(),pts.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first;
                      });
            if(pts.size()>=4){
                PlotSeries curve;
                curve.label=QStringLiteral("consolidation");
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.3,in.style.lineWidth);
                for(const auto& pt:pts){
                    curve.x.append(std::pow(10.0,pt.first));
                    curve.y.append(pt.second);
                }
                out.series.append(curve);

                // Steepest segment in log time - the middle of primary
                // consolidation - and the last quarter of the record, which is
                // secondary compression.
                int steep=1; double best=0.0;
                for(int i=1;i<pts.size();++i){
                    const double dx=pts[i].first-pts[i-1].first;
                    if(!(dx>1e-12)) continue;
                    const double slope=std::abs((pts[i].second-pts[i-1].second)/dx);
                    if(slope>best){ best=slope; steep=i; }
                }
                const int tailFrom=qMax(1,pts.size()*3/4);
                QVector<double> tx,ty;
                for(int i=tailFrom;i<pts.size();++i){ tx.append(pts[i].first); ty.append(pts[i].second); }
                const LineFit tail=fitLine(tx,ty);
                const double primarySlope=(pts[steep].second-pts[steep-1].second)
                                          /(pts[steep].first-pts[steep-1].first);
                const double primaryB=pts[steep].second-primarySlope*pts[steep].first;
                if(tail.ok&&std::abs(primarySlope-tail.slope)>1e-12){
                    const double xi=(tail.intercept-primaryB)/(primarySlope-tail.slope);
                    const double d100=primaryB+primarySlope*xi;
                    const double d0=pts.first().second;
                    const double d50=(d0+d100)/2.0;
                    // t50 by walking the curve to the half-settlement, which is
                    // where the coefficient of consolidation comes from.
                    double t50=0.0;
                    for(int i=1;i<pts.size();++i){
                        const double a=pts[i-1].second, b2=pts[i].second;
                        if((a-d50)*(b2-d50)<=0.0&&std::abs(b2-a)>1e-15){
                            const double t=(d50-a)/(b2-a);
                            t50=std::pow(10.0,pts[i-1].first+t*(pts[i].first-pts[i-1].first));
                            break;
                        }
                    }
                    if(t50>0.0){
                        PlotSeries mark;
                        mark.label=QStringLiteral("t50 %1, d100 %2")
                                       .arg(t50,0,'g',3).arg(d100,0,'g',4);
                        mark.color=in.style.warning;
                        mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.0;
                        mark.x={t50}; mark.y={d50};
                        out.series.append(mark);
                    }
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("time (log)"),true,unsetValue(),unsetValue()};
        // Settlement downwards, by the axis flag rather than by negating the
        // readings - which would put minus signs on every tick and make the
        // axis label a lie.
        out.yAxis=PlotAxis{QStringLiteral("settlement"),false,
                           unsetValue(),unsetValue(),true};
        return out;
    }

    // ------------------------------------------- Lomb-Scargle Periodogram
    // Power against frequency for UNEVENLY sampled data - which is the whole
    // reason it exists. An FFT periodogram needs a constant sampling interval,
    // and astronomical, ecological and instrument records almost never have
    // one; resampling them onto a grid first invents data and smears the very
    // peak being looked for.
    if(in.engine==QLatin1String("Lomb-Scargle Periodogram")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& time=in.series.at(0).y;
            const QVector<double>& value=in.series.at(1).y;
            const int n=qMin(time.size(),value.size());
            QVector<double> t,v;
            for(int i=0;i<n;++i){
                if(!finite(time[i])||!finite(value[i])) continue;
                t.append(time[i]); v.append(value[i]);
            }
            if(t.size()>=8){
                double mean=0.0;
                for(double x:v) mean+=x;
                mean/=double(v.size());
                double variance=0.0;
                for(double x:v) variance+=(x-mean)*(x-mean);
                variance/=double(v.size()-1);

                const Bounds span=boundsOf(t);
                const double baseline=span.hi-span.lo;
                if(baseline>0.0&&variance>0.0){
                    // Frequencies from one cycle over the record to the
                    // pseudo-Nyquist of the MEDIAN spacing. Using the mean
                    // spacing instead would let one long gap halve the
                    // frequency range that gets searched.
                    QVector<double> gaps;
                    QVector<double> sorted=t;
                    std::sort(sorted.begin(),sorted.end());
                    for(int i=1;i<sorted.size();++i)
                        if(sorted[i]>sorted[i-1]) gaps.append(sorted[i]-sorted[i-1]);
                    std::sort(gaps.begin(),gaps.end());
                    const double median=gaps.isEmpty()?baseline/double(t.size())
                                                      :gaps[gaps.size()/2];
                    const double fLo=1.0/baseline;
                    const double fHi=(median>0.0)?0.5/median:fLo*double(t.size());
                    const int bins=qBound(64,int(t.size())*10,2000);
                    PlotSeries power;
                    power.label=QStringLiteral("power");
                    power.color=in.series.at(1).color;
                    power.lineWidth=qMax(1.1,in.style.lineWidth);
                    double peak=0.0,fPeak=0.0;
                    for(int b=0;b<bins;++b){
                        const double f=fLo+(fHi-fLo)*double(b)/double(bins-1);
                        if(!(f>0.0)) continue;
                        const double w=2.0*M_PI*f;
                        // The time offset tau is what makes this estimator
                        // invariant to where the clock was started; without it
                        // the power depends on the arbitrary time origin.
                        double ss=0.0,cc=0.0;
                        for(double x:t){ ss+=std::sin(2.0*w*x); cc+=std::cos(2.0*w*x); }
                        const double tau=std::atan2(ss,cc)/(2.0*w);
                        double sc=0.0,cs=0.0,s2=0.0,c2=0.0;
                        for(int i=0;i<t.size();++i){
                            const double a=w*(t[i]-tau);
                            const double cosA=std::cos(a), sinA=std::sin(a);
                            const double d=v[i]-mean;
                            cs+=d*cosA; sc+=d*sinA;
                            c2+=cosA*cosA; s2+=sinA*sinA;
                        }
                        double p=0.0;
                        if(c2>1e-300) p+=cs*cs/c2;
                        if(s2>1e-300) p+=sc*sc/s2;
                        p/=(2.0*variance);
                        power.x.append(f); power.y.append(p);
                        if(p>peak){ peak=p; fPeak=f; }
                    }
                    if(!power.x.isEmpty()){
                        power.label=QStringLiteral("power");
                        out.series.append(power);
                        // MARKED, NOT JUST NAMED - the idiom the cross
                        // correlation's strongest lag and the elbow's knee
                        // already use. The peak period was written into the
                        // periodogram's own label, and a figure with one named
                        // series draws no legend, so the one number a
                        // periodogram exists to give was shown nowhere. A
                        // second named series carries it into the legend AND
                        // puts a mark on the peak it refers to, which on a
                        // spectrum with several comparable humps is the half
                        // that settles which one is meant.
                        if(fPeak>0.0){
                            PlotSeries mark;
                            mark.label=QStringLiteral("peak period %1 (power %2)")
                                           .arg(formatMeasured(1.0/fPeak)).arg(peak,0,'f',2);
                            mark.color=in.style.warning;
                            mark.drawLine=false; mark.drawMarkers=true;
                            mark.markerSize=8.0; mark.markerSizeExplicit=true;
                            mark.x={fPeak}; mark.y={peak};
                            out.series.append(mark);
                        }
                    }
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("frequency"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("normalised power"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ----------------------------------------------------------- Waffle Chart
    // A hundred squares, coloured in proportion. It replaces a pie chart for
    // the case a pie is worst at - reading a value rather than comparing two -
    // because counting squares is exact and judging the angle of a wedge is
    // not.
    if(in.engine==QLatin1String("Waffle Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        // One category per ROW of a single column, which is how a pie chart
        // reads its input and what this replaces. Reading one category per
        // COLUMN was tried first and is wrong for the ordinary case: a single
        // mapped column then became one category holding a hundred squares,
        // which is a picture of the number one.
        QVector<double> shares;
        if(!in.series.isEmpty())
            for(double v:in.series.first().y)
                if(finite(v)&&v>0.0) shares.append(v);
        double grand=0.0;
        for(double x:shares) grand+=x;
        if(grand>0.0){
            const int cells=100, cols=10;
            // Largest-remainder apportionment, so the squares add to exactly a
            // hundred. Rounding each share independently gives 99 or 101 and
            // the chart stops being readable as a percentage - which is the one
            // thing it is for.
            QVector<int> quota(shares.size(),0);
            QVector<double> remainder(shares.size(),0.0);
            int assigned=0;
            for(int i=0;i<shares.size();++i){
                const double exact=shares[i]/grand*double(cells);
                quota[i]=int(std::floor(exact));
                remainder[i]=exact-quota[i];
                assigned+=quota[i];
            }
            while(assigned<cells&&!remainder.isEmpty()){
                int best=0;
                for(int i=1;i<remainder.size();++i) if(remainder[i]>remainder[best]) best=i;
                if(remainder[best]<0.0) break;      // every remainder spent
                quota[best]+=1; remainder[best]=-1.0; ++assigned;
            }
            const QString stem=in.series.first().label.isEmpty()
                               ?QStringLiteral("category"):in.series.first().label;
            int cell=0;
            for(int i=0;i<quota.size();++i){
                PlotSeries block;
                block.label=QStringLiteral("%1 %2  -  %3%").arg(stem).arg(i+1).arg(quota[i]);
                block.color=categoryColour(in,i,std::fmod(double(i)*0.137,1.0),0.55,0.93);
                block.drawLine=false; block.drawMarkers=true; block.markerSize=11.0;
                block.markerSizeExplicit=true;
                for(int k=0;k<quota[i]&&cell<cells;++k,++cell){
                    block.x.append(double(cell%cols));
                    // Filled from the bottom up, so the first category is the
                    // bottom row rather than the top - the direction a stacked
                    // proportion is read in.
                    block.y.append(double(9-cell/cols));
                }
                if(!block.x.isEmpty()) out.series.append(block);
            }
        }
        out.xAxis=PlotAxis{QString(),false,-1.0,10.0};
        out.yAxis=PlotAxis{QString(),false,-1.0,10.0};
        out.style.gridVisible=false;
        out.style.scaleLabelsVisible=false;
        // AND NO FRAME EITHER. Both of these lay their marks out on a grid of
        // POSITIONS - one glyph per row, one square per unit of a hundred -
        // and the numbers on that grid are the positions themselves. Turning
        // off the gridlines and the scale labels left the box and its ticks,
        // which is an axis reading 0, 2, 4 under a row of glyphs. See
        // PlotSpec::framed.
        out.framed=false;
        return out;
    }

    // =====================================================================
    // Batch 6: regression diagnostics, signal structure, multivariate views,
    // and the two electrochemistry plots the catalogue still lacked.
    // =====================================================================

    // ----------------------------------------- Regression diagnostic family
    // Cook's distance, scale-location and the partial residual plot are three
    // of the four panels R prints when you plot() a linear model, and the
    // catalogue had the fourth (Residual Plot) and none of these. Each reads a
    // fitted value and a residual and asks a different question of them:
    // which points are moving the fit, whether the spread grows with the fit,
    // and whether one predictor's relationship is really linear.
    if(in.engine==QLatin1String("Cook's Distance Plot")
       ||in.engine==QLatin1String("Scale-Location Plot")){
        const bool cooks=(in.engine==QLatin1String("Cook's Distance Plot"));
        PlotSpec out=derivedAs(in,cooks?QStringLiteral("Stem")
                                       :QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& fitted=in.series.at(0).y;
            const QVector<double>& residual=in.series.at(1).y;
            const int n=qMin(fitted.size(),residual.size());
            // The residual standard error, which both panels are scaled by. Two
            // parameters are spent on an intercept and a slope, so n-2.
            double sse=0.0; int used=0;
            for(int i=0;i<n;++i){
                if(!finite(residual[i])) continue;
                sse+=residual[i]*residual[i]; ++used;
            }
            const double sigma=(used>2)?std::sqrt(sse/double(used-2)):0.0;
            PlotSeries pts;
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;
            if(cooks){ pts.drawLine=true; pts.drawMarkers=true; }
            for(int i=0;i<n;++i){
                if(!finite(fitted[i])||!finite(residual[i])||!(sigma>0.0)) continue;
                const double standardised=residual[i]/sigma;
                if(cooks){
                    // Cook's distance with the leverage unavailable is the
                    // squared standardised residual over the parameter count -
                    // the part of it that a residual alone can honestly carry.
                    // Named on the axis so it is not mistaken for the full
                    // statistic, which needs the hat matrix.
                    pts.x.append(double(i));
                    pts.y.append(standardised*standardised/2.0);
                }else{
                    pts.x.append(fitted[i]);
                    pts.y.append(std::sqrt(std::abs(standardised)));
                }
            }
            if(!pts.x.isEmpty()){
                pts.label=cooks?QStringLiteral("influence")
                               :QStringLiteral("sqrt |standardised residual|");
                out.series.append(pts);
                if(cooks){
                    // 4/n is the usual screening threshold. A rule of thumb
                    // drawn as a rule of thumb, in the legend.
                    const double cut=(used>0)?4.0/double(used):0.0;
                    out.series.append(horizontalRule(cut,0.0,double(qMax(1,n-1)),
                                                     in.style.warning,
                                                     QStringLiteral("4/n = %1").arg(cut,0,'g',3),
                                                     true));
                }else{
                    // The trend in the spread. Flat means constant variance,
                    // which is the assumption this panel exists to check.
                    const LineFit f=fitLine(pts.x,pts.y);
                    const Bounds b=boundsOf(pts.x);
                    if(f.ok&&b.valid){
                        PlotSeries trend;
                        trend.label=QStringLiteral("spread trend, slope %1").arg(f.slope,0,'g',3);
                        trend.color=in.style.warning;
                        trend.lineWidth=qMax(1.3,in.style.lineWidth);
                        trend.x={b.lo,b.hi};
                        trend.y={f.intercept+f.slope*b.lo,f.intercept+f.slope*b.hi};
                        out.series.append(trend);
                    }
                }
            }
        }
        if(cooks){
            out.xAxis=PlotAxis{QStringLiteral("observation"),false,unsetValue(),unsetValue()};
            out.yAxis=PlotAxis{QStringLiteral("influence (residual component)"),false,
                               unsetValue(),unsetValue()};
        }else{
            out.xAxis=PlotAxis{QStringLiteral("fitted value"),false,unsetValue(),unsetValue()};
            out.yAxis=PlotAxis{QStringLiteral("sqrt |standardised residual|"),false,
                               unsetValue(),unsetValue()};
        }
        return out;
    }

    // ------------------------------------------------ Partial Residual Plot
    // The residual with one predictor's fitted contribution ADDED BACK, drawn
    // against that predictor. A straight line means the linear term is right;
    // a curve is the plot telling you the model needs a quadratic or a log,
    // which an ordinary residual plot smears across every predictor at once.
    if(in.engine==QLatin1String("Partial Residual Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& predictor=in.series.at(0).y;
            const QVector<double>& response=in.series.at(1).y;
            const int n=qMin(predictor.size(),response.size());
            const LineFit f=fitLine(predictor,response);
            if(f.ok){
                PlotSeries pts;
                pts.label=QStringLiteral("partial residual");
                pts.color=in.series.at(1).color;
                pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;
                for(int i=0;i<n;++i){
                    if(!finite(predictor[i])||!finite(response[i])) continue;
                    const double residual=response[i]-(f.intercept+f.slope*predictor[i]);
                    pts.x.append(predictor[i]);
                    pts.y.append(residual+f.slope*predictor[i]);
                }
                if(!pts.x.isEmpty()){
                    out.series.append(pts);
                    const Bounds b=boundsOf(pts.x);
                    if(b.valid){
                        PlotSeries line;
                        line.label=QStringLiteral("linear term, slope %1").arg(f.slope,0,'g',4);
                        line.color=in.style.warning;
                        line.lineWidth=qMax(1.3,in.style.lineWidth);
                        line.x={b.lo,b.hi};
                        line.y={f.slope*b.lo,f.slope*b.hi};
                        out.series.append(line);
                    }
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("predictor"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("partial residual"),false,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------- Savitzky-Golay / LOWESS Trend
    // Two ways to draw a smooth line through noisy measurements, and they fail
    // differently, which is why both are here.
    //
    // Savitzky-Golay fits a polynomial over a sliding window of FIXED WIDTH, so
    // it preserves the height and width of a peak where a moving average
    // flattens it - the reason it is the standard smoother in spectroscopy and
    // chromatography. LOWESS fits over a fixed FRACTION of the sample with
    // distance weights, so it adapts to uneven sampling and follows a trend
    // that changes character along the record.
    if(in.engine==QLatin1String("Savitzky-Golay Smoothing")
       ||in.engine==QLatin1String("LOWESS Trend")){
        const bool golay=(in.engine==QLatin1String("Savitzky-Golay Smoothing"));
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            QVector<QPair<double,double>> rows;
            const QVector<double>& xs=in.series.at(0).y;
            const QVector<double>& ys=in.series.at(1).y;
            const int n=qMin(xs.size(),ys.size());
            for(int i=0;i<n;++i)
                if(finite(xs[i])&&finite(ys[i])) rows.append({xs[i],ys[i]});
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first;
                      });
            if(rows.size()>=5){
                PlotSeries raw;
                raw.label=QStringLiteral("measured");
                raw.color=in.style.gridColor;
                raw.drawLine=false; raw.drawMarkers=true; raw.markerSize=3.0;
                for(const auto& r:rows){ raw.x.append(r.first); raw.y.append(r.second); }
                out.series.append(raw);

                PlotSeries smooth;
                smooth.color=in.series.at(1).color;
                smooth.lineWidth=qMax(1.5,in.style.lineWidth*1.3);
                const int m=rows.size();
                if(golay){
                    // Quadratic over a window of about a twentieth of the
                    // record, at least 5 points and always odd - a window with
                    // no centre point has no value to write back to.
                    int half=qBound(2,m/40,50);
                    smooth.label=QStringLiteral("Savitzky-Golay, window %1").arg(2*half+1);
                    for(int i=0;i<m;++i){
                        const int lo=qMax(0,i-half), hi=qMin(m-1,i+half);
                        double A[9]={0,0,0,0,0,0,0,0,0}, b[3]={0,0,0};
                        for(int j=lo;j<=hi;++j){
                            const double u=rows[j].first-rows[i].first;
                            const double basis[3]={1.0,u,u*u};
                            for(int r=0;r<3;++r){
                                for(int c=0;c<3;++c) A[r*3+c]+=basis[r]*basis[c];
                                b[r]+=basis[r]*rows[j].second;
                            }
                        }
                        // Cramer's rule on the 3x3. The basis is centred on
                        // the point being smoothed, so the answer is simply
                        // the constant term and no evaluation step can get
                        // the centring wrong.
                        const auto det3=[](const double e[9]){
                            return e[0]*(e[4]*e[8]-e[5]*e[7])
                                  -e[1]*(e[3]*e[8]-e[5]*e[6])
                                  +e[2]*(e[3]*e[7]-e[4]*e[6]);
                        };
                        double value=rows[i].second;
                        const double det=det3(A);
                        if(std::abs(det)>1e-300){
                            double M0[9];
                            for(int r=0;r<3;++r)
                                for(int c=0;c<3;++c) M0[r*3+c]=(c==0)?b[r]:A[r*3+c];
                            value=det3(M0)/det;
                        }
                        smooth.x.append(rows[i].first);
                        smooth.y.append(value);
                    }
                }else{
                    const int span=qBound(3,int(m*0.25),m);
                    smooth.label=QStringLiteral("LOWESS, %1% span").arg(int(100.0*span/m));
                    // A SMOOTHER EVALUATED AT MORE POINTS THAN THE FIGURE HAS
                    // PIXELS is computing values that land on top of one
                    // another. LOWESS fits over a fixed FRACTION of the sample,
                    // so unlike the Savitzky-Golay window above - which is
                    // capped at 50 either side - its window grows with the
                    // record, and the whole smoother is quadratic: 3.3 seconds
                    // at 24,000 readings, with nothing to show for it. A local
                    // regression is smooth by construction, so between two fits
                    // a thousandth of the record apart there is nothing for the
                    // curve to do but go straight.
                    //
                    // So the fit is evaluated at a bounded number of ANCHORS
                    // and the readings between them are carried on the straight
                    // line joining their neighbours - which is Cleveland's own
                    // `delta`, expressed as a count rather than as a distance.
                    // The cap is far above any plot width, so the drawn curve
                    // does not change; and below it every reading is still an
                    // anchor, so for anything of ordinary size nothing about
                    // the old behaviour changes at all.
                    constexpr int kMaxFits=2000;
                    const int stride=qMax(1,(m+kMaxFits-1)/kMaxFits);
                    const auto fitAt=[&](int i){
                        // The span nearest points, and Cleveland's tricube
                        // weight over the distance to the furthest of them.
                        const int lo=qBound(0,i-span/2,qMax(0,m-span));
                        const int hi=qMin(m-1,lo+span-1);
                        const double h=qMax(1e-300,
                            qMax(std::abs(rows[i].first-rows[lo].first),
                                 std::abs(rows[hi].first-rows[i].first)));
                        double sw=0,swx=0,swy=0,swxx=0,swxy=0;
                        for(int j=lo;j<=hi;++j){
                            const double d=std::abs(rows[j].first-rows[i].first)/h;
                            const double t=qMax(0.0,1.0-d*d*d);
                            const double w=t*t*t;
                            if(!(w>0.0)) continue;
                            const double x=rows[j].first, y=rows[j].second;
                            sw+=w; swx+=w*x; swy+=w*y; swxx+=w*x*x; swxy+=w*x*y;
                        }
                        double value=rows[i].second;
                        const double den=sw*swxx-swx*swx;
                        if(std::abs(den)>1e-300){
                            const double slope=(sw*swxy-swx*swy)/den;
                            const double intercept=(swy-slope*swx)/sw;
                            value=intercept+slope*rows[i].first;
                        }
                        return value;
                    };
                    QVector<int> anchors;
                    anchors.reserve(m/stride+2);
                    for(int i=0;i<m;i+=stride) anchors.append(i);
                    if(anchors.isEmpty()||anchors.last()!=m-1) anchors.append(m-1);
                    QVector<double> fitted(anchors.size(),0.0);
                    for(int k=0;k<anchors.size();++k) fitted[k]=fitAt(anchors[k]);
                    for(int k=0;k+1<anchors.size();++k){
                        const int a=anchors[k],b=anchors[k+1];
                        const double xa=rows[a].first,xb=rows[b].first;
                        for(int i=a;i<b;++i){
                            const double t=(xb>xa)?(rows[i].first-xa)/(xb-xa):0.0;
                            smooth.x.append(rows[i].first);
                            smooth.y.append(fitted[k]+t*(fitted[k+1]-fitted[k]));
                        }
                    }
                    smooth.x.append(rows[m-1].first);
                    smooth.y.append(fitted.last());
                }
                out.series.append(smooth);
            }
        }
        return out;
    }

    // ---------------------------------------- Partial Autocorrelation (PACF)
    // The correlation at each lag with the shorter lags' effect REMOVED. An
    // ordinary autocorrelation of a trending series is large at every lag
    // because lag 1 is large and every later lag inherits it; the partial
    // version is what actually tells you the order of an autoregressive
    // process, by Durbin-Levinson on the autocorrelations.
    if(in.engine==QLatin1String("Partial Autocorrelation")){
        PlotSpec out=derivedAs(in,QStringLiteral("Stem"));
        const QVector<double>& raw=in.series.size()>=2?in.series.at(1).y
                                                      :(in.series.isEmpty()?QVector<double>()
                                                                           :in.series.at(0).y);
        QVector<double> v;
        for(double q:raw) if(finite(q)) v.append(q);
        const int n=v.size();
        if(n>=8){
            double mean=0.0;
            for(double q:v) mean+=q;
            mean/=double(n);
            const int maxLag=qBound(4,n/4,60);
            QVector<double> r(maxLag+1,0.0);
            double c0=0.0;
            for(int i=0;i<n;++i) c0+=(v[i]-mean)*(v[i]-mean);
            c0/=double(n);
            for(int k=0;k<=maxLag;++k){
                double c=0.0;
                for(int i=0;i+k<n;++i) c+=(v[i]-mean)*(v[i+k]-mean);
                r[k]=(c0>1e-300)?(c/double(n))/c0:0.0;
            }
            // Durbin-Levinson: phi_kk is the partial autocorrelation at lag k.
            QVector<double> phi(maxLag+1,0.0), prev(maxLag+1,0.0);
            PlotSeries pacf;
            pacf.label=QStringLiteral("partial autocorrelation");
            pacf.color=in.series.at(in.series.size()>=2?1:0).color;
            for(int k=1;k<=maxLag;++k){
                double num=r[k], den=1.0;
                for(int j=1;j<k;++j){ num-=prev[j]*r[k-j]; den-=prev[j]*r[j]; }
                const double kk=(std::abs(den)>1e-300)?num/den:0.0;
                phi[k]=kk;
                for(int j=1;j<k;++j) phi[j]=prev[j]-kk*prev[k-j];
                prev=phi;
                pacf.x.append(double(k));
                pacf.y.append(kk);
            }
            out.series.append(pacf);
            // The white-noise band. Outside it the lag is worth a term.
            const double band=1.96/std::sqrt(double(n));

            // THE ORDER THE PLOT IMPLIES, which is the one thing anyone reads a
            // PACF for and the one thing it did not say.
            //
            // The figure drew the partial autocorrelations and the 95% band and
            // left the reader to find the largest spike outside it by eye - the
            // same omission as the periodogram that drew a hump and never named
            // its frequency.
            //
            // "SUGGESTS", deliberately, and not "is". The largest significant
            // lag is the textbook reading of where the PACF cuts off, but at
            // this many lags a few will sit outside a 95% band by chance, so
            // the number is a starting point for a person rather than a
            // verdict. Saying it flatly would be claiming more than the
            // arithmetic supports.
            //
            // Marked as well as named, for the reason recorded on the
            // Lomb-Scargle peak: a figure with one named series draws no
            // legend, so a label on `pacf` alone would have been invisible.
            // WHERE THE PACF CUTS OFF, not simply its largest significant lag.
            //
            // "The largest lag outside the band" is the obvious rule and it is
            // wrong in practice, which the gallery showed immediately: on a
            // series that is visibly AR(1) - one spike of 0.43 at lag 1 and
            // nothing else - it reported AR(42), because at sixty lags and a
            // 95% band about three lags sit outside by chance and the last of
            // those wins.
            //
            // The textbook reading is that the PACF CUTS OFF after the order:
            // significant early, quiet thereafter. So walk forward, remember
            // the last significant lag, and stop once the plot has been quiet
            // long enough to call it settled. A lone spike far out on an
            // otherwise flat PACF is noise, and treating it as the order says
            // something about the data that is not true.
            //
            // The quiet run scales with the number of lags, because so does the
            // number of chance crossings: at sixty lags that is five.
            const int quietEnough=qMax(3,maxLag/12);
            int order=0,quiet=0;
            for(int k=1;k<=maxLag;++k){
                if(std::abs(phi[k])>band){ order=k; quiet=0; continue; }
                if(order>0&&++quiet>=quietEnough) break;
            }
            PlotSeries suggested;
            // positive, not warning: the band lines above are already drawn in
            // warning, and a mark in the same colour would read as part of them.
            suggested.color=in.style.positive;
            suggested.drawLine=false; suggested.drawMarkers=true;
            suggested.markerSize=9.0; suggested.markerSizeExplicit=true;
            if(order>0){
                suggested.label=QStringLiteral("suggests AR(%1)").arg(order);
                suggested.x={double(order)};
                suggested.y={phi[order]};
            }else{
                // A real and useful answer, not a failure: nothing outside the
                // band is what white noise looks like.
                suggested.label=QStringLiteral("no lag outside the band — white noise");
                suggested.x={1.0};
                suggested.y={phi[1]};
            }
            out.series.append(suggested);
            out.series.append(horizontalRule(band,1.0,double(maxLag),in.style.warning,
                                             QStringLiteral("95% band"),true));
            out.series.append(horizontalRule(-band,1.0,double(maxLag),in.style.warning,
                                             QString(),false));
        }
        out.xAxis=PlotAxis{QStringLiteral("lag"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("partial autocorrelation"),false,-1.05,1.05};
        return out;
    }

    // ------------------------- Seasonal Decomposition / Subseries Plot
    // A series pulled apart into trend, repeating cycle and what is left. The
    // remainder is the interesting part: a pattern still visible in it is a
    // pattern the period does not explain.
    if(in.engine==QLatin1String("Seasonal Decomposition")
       ||in.engine==QLatin1String("Seasonal Subseries Plot")){
        const bool subseries=(in.engine==QLatin1String("Seasonal Subseries Plot"));
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& xs=in.series.at(0).y;
            const QVector<double>& ys=in.series.at(1).y;
            const int n=qMin(xs.size(),ys.size());
            // The period, from a third column if one is mapped, otherwise the
            // lag of the strongest autocorrelation - which is the honest guess
            // and is reported rather than assumed silently.
            int period=0;
            if(in.series.size()>=3&&!in.series.at(2).y.isEmpty()
               &&finite(in.series.at(2).y.first()))
                period=int(std::lround(in.series.at(2).y.first()));
            if(period<2&&n>=16){
                double mean=0.0; int m=0;
                for(int i=0;i<n;++i) if(finite(ys[i])){ mean+=ys[i]; ++m; }
                if(m>0) mean/=double(m);
                // EVERY LAG AT ONCE, BY TRANSFORM. One pass per lag over a
                // third of the record is quadratic, and a series of 24,000
                // readings spent two seconds deciding what its period was.
                //
                // The autocovariance is a correlation of the series with
                // itself, so the whole set of lags is one inverse transform of
                // the power spectrum. Zero-padding past twice the length keeps
                // the circular correlation from wrapping, so what comes back is
                // the linear one - the same sum the loop computed.
                //
                // The overlap count is the SAME correlation of the validity
                // mask with itself, which is how the missing readings stay
                // accounted for rather than assumed absent: an integer, so it
                // comes back exactly.
                const int size=nextPowerOfTwo(qMax(4,n*2));
                QVector<double> dr(size,0.0),di(size,0.0),kr(size,0.0),ki(size,0.0);
                for(int i=0;i<n;++i){
                    if(!finite(ys[i])) continue;
                    dr[i]=ys[i]-mean; kr[i]=1.0;
                }
                fftInPlace(dr,di); fftInPlace(kr,ki);
                for(int k=0;k<size;++k){
                    dr[k]=dr[k]*dr[k]+di[k]*di[k]; di[k]=0.0;
                    kr[k]=kr[k]*kr[k]+ki[k]*ki[k]; ki[k]=0.0;
                }
                // A power spectrum is real and even, so its inverse transform
                // is its forward transform over the length - no separate
                // inverse is needed.
                fftInPlace(dr,di); fftInPlace(kr,ki);
                double best=0.0;
                for(int lag=2;lag<=n/3;++lag){
                    const double used=std::round(kr[lag]/double(size));
                    if(used<4.0) continue;
                    const double c=(dr[lag]/double(size))/used;
                    // The earliest lag keeps a tie, deliberately. A periodic
                    // series correlates exactly as well at twice its period
                    // and three times it, so the lags TIE and the winner would
                    // otherwise be decided by the last bits of the transform -
                    // reporting a season of twenty-four months for a year.
                    if(c>best+1e-12*std::abs(best)){ best=c; period=lag; }
                }
            }
            period=qBound(2,period,qMax(2,n/2));

            // The trend, by a centred moving average one period wide - the
            // classical decomposition, and the reason the ends are short.
            QVector<double> trend(n,std::numeric_limits<double>::quiet_NaN());
            const int half=period/2;
            // A MOVING AVERAGE MOVES. Re-adding every reading in the window at
            // every position costs the record length times the period, and the
            // period the search above chooses grows with the record - so this
            // was quadratic too, and for a long series the more expensive of
            // the two. Running totals of the sum AND of how many readings in
            // the window are present keep the gaps handled exactly as the
            // re-summing version handled them.
            {
                double sum=0.0; int used=0;
                const auto shift=[&](int j,int sign){
                    if(j<0||j>=n||!finite(ys[j])) return;
                    sum+=sign*ys[j]; used+=sign;
                };
                for(int j=0;j<=2*half&&j<n;++j) shift(j,1);
                for(int i=half;i+half<n;++i){
                    if(i>half){ shift(i-half-1,-1); shift(i+half,1); }
                    if(used>0) trend[i]=sum/double(used);
                }
            }
            // The cycle: the mean of what the trend does not explain, by
            // position within the period.
            QVector<double> cycleSum(period,0.0); QVector<int> cycleCount(period,0);
            for(int i=0;i<n;++i){
                if(!finite(ys[i])||!finite(trend[i])) continue;
                const int slot=i%period;
                cycleSum[slot]+=ys[i]-trend[i];
                cycleCount[slot]+=1;
            }
            QVector<double> cycle(period,0.0);
            // NOT called `slots`: Qt defines that as a macro, and a local
            // named it does not compile. `emit` below was the same trap.
            double cycleMean=0.0; int filledSlots=0;
            for(int s=0;s<period;++s)
                if(cycleCount[s]>0){ cycle[s]=cycleSum[s]/cycleCount[s];
                                     cycleMean+=cycle[s]; ++filledSlots; }
            if(filledSlots>0){
                cycleMean/=double(filledSlots);
                for(double& c:cycle) c-=cycleMean;
            }

            if(subseries){
                // One line per position in the cycle, drawn across the periods
                // it occurs in - so a January that is drifting upward year on
                // year is a rising line rather than a wiggle inside a sawtooth.
                for(int s=0;s<period&&s<24;++s){
                    PlotSeries lane;
                    lane.label=QStringLiteral("position %1").arg(s+1);
                    lane.color=categoryColour(in,s,std::fmod(double(s)/qMax(1,period),1.0),0.6,0.92);
                    lane.lineWidth=qMax(1.0,in.style.lineWidth);
                    for(int i=s;i<n;i+=period){
                        if(!finite(xs[i])||!finite(ys[i])) continue;
                        lane.x.append(xs[i]); lane.y.append(ys[i]);
                    }
                    if(lane.x.size()>=2) out.series.append(lane);
                }
                out.xAxis=PlotAxis{QStringLiteral("time"),false,unsetValue(),unsetValue()};
                out.yAxis=PlotAxis{QStringLiteral("value"),false,unsetValue(),unsetValue()};
                return out;
            }

            const auto addPanel=[&](const QString& name,const QVector<double>& v,const QColor& colour){
                PlotSeries s;
                s.label=name;
                s.color=colour;
                s.lineWidth=qMax(1.2,in.style.lineWidth);
                for(int i=0;i<n;++i){
                    if(!finite(xs[i])||!finite(v[i])) continue;
                    s.x.append(xs[i]); s.y.append(v[i]);
                }
                if(s.x.size()>=2) out.series.append(s);
            };
            QVector<double> seasonal(n,0.0), remainder(n,std::numeric_limits<double>::quiet_NaN());
            for(int i=0;i<n;++i){
                seasonal[i]=cycle[i%period];
                if(finite(ys[i])&&finite(trend[i])) remainder[i]=ys[i]-trend[i]-seasonal[i];
            }
            QVector<double> observed(n,std::numeric_limits<double>::quiet_NaN());
            for(int i=0;i<n;++i) observed[i]=ys[i];
            addPanel(QStringLiteral("observed"),observed,in.style.gridColor);
            addPanel(QStringLiteral("trend, period %1").arg(period),trend,in.series.at(1).color);
            addPanel(QStringLiteral("seasonal"),seasonal,in.style.warning);
            addPanel(QStringLiteral("remainder"),remainder,in.style.danger);
        }
        out.xAxis=PlotAxis{QStringLiteral("time"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("value"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Recurrence Plot
    // A black square at (i, j) wherever the record revisits, within a
    // tolerance, the state it was in earlier. Diagonal lines mean the system
    // repeats a trajectory; a checkerboard means it is periodic; a scatter of
    // isolated dots means it is not returning at all. It is the standard first
    // look at whether a nonlinear system is deterministic, and no amount of
    // staring at the time series shows it.
    if(in.engine==QLatin1String("Recurrence Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("2D Heatmap"));
        const QVector<double>& raw=in.series.size()>=2?in.series.at(1).y
                                                      :(in.series.isEmpty()?QVector<double>()
                                                                           :in.series.at(0).y);
        QVector<double> v;
        for(double q:raw) if(finite(q)) v.append(q);
        // Capped: the plot is n by n, so ten thousand samples is a hundred
        // million cells. Thinned by stride, which keeps the shape of the
        // trajectory rather than its first corner.
        const int cap=220;
        if(v.size()>cap){
            const int stride=(v.size()+cap-1)/cap;
            QVector<double> thin;
            for(int i=0;i<v.size();i+=stride) thin.append(v[i]);
            v=thin;
        }
        const int n=v.size();
        if(n>=8){
            // The tolerance: a tenth of the spread, which is the usual choice
            // and is reported on the axis so it is not a hidden constant.
            const Bounds b=boundsOf(v);
            const double eps=(b.valid?(b.hi-b.lo):1.0)*0.1;
            PlotSeries ix,iy,value;
            ix.label=QStringLiteral("i"); iy.label=QStringLiteral("j");
            value.label=QStringLiteral("recurrence (eps = %1)").arg(eps,0,'g',3);
            for(int i=0;i<n;++i)
                for(int j=0;j<n;++j){
                    ix.x.append(0); iy.x.append(0); value.x.append(0);
                    ix.y.append(double(i));
                    iy.y.append(double(j));
                    value.y.append(std::abs(v[i]-v[j])<=eps?1.0:0.0);
                }
            out.series={ix,iy,value};
            out.style.fieldResolution=n;
        }
        out.xAxis=PlotAxis{QStringLiteral("sample i"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("sample j"),false,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------------------- Wavelet Scalogram
    // Where the frequency content sits IN TIME. A power spectrum says a record
    // contains a 3 Hz component; a scalogram says it contained it for the first
    // ten seconds and not afterwards, which for a transient - a startup, a
    // fault, a dosing event - is the entire question.
    //
    // A Morlet wavelet, correlated with the record at a range of scales. Done
    // directly rather than through the FFT because the transform here is over a
    // few dozen scales on a capped record, and the direct form is forty lines
    // that are obviously right.
    if(in.engine==QLatin1String("Wavelet Scalogram")){
        PlotSpec out=derivedAs(in,QStringLiteral("2D Heatmap"));
        const QVector<double>& raw=in.series.size()>=2?in.series.at(1).y
                                                      :(in.series.isEmpty()?QVector<double>()
                                                                           :in.series.at(0).y);
        QVector<double> v;
        for(double q:raw) if(finite(q)) v.append(q);
        const int cap=1024;
        if(v.size()>cap){
            const int stride=(v.size()+cap-1)/cap;
            QVector<double> thin;
            for(int i=0;i<v.size();i+=stride) thin.append(v[i]);
            v=thin;
        }
        const int n=v.size();
        if(n>=32){
            double mean=0.0;
            for(double q:v) mean+=q;
            mean/=double(n);
            constexpr int kScales=48;
            const int cols=qMin(n,256);
            const int stride=qMax(1,n/cols);
            PlotSeries ix,iy,value;
            ix.label=QStringLiteral("time"); iy.label=QStringLiteral("scale");
            value.label=QStringLiteral("wavelet power");
            constexpr double kOmega=6.0;   // Morlet, the conventional choice
            for(int s=0;s<kScales;++s){
                // Scales spaced logarithmically from a few samples to a quarter
                // of the record - below that there is nothing to resolve, above
                // it there are not enough cycles to be a measurement.
                const double scale=2.0*std::pow(double(n)/8.0,double(s)/(kScales-1));
                const int support=qMin(n,int(scale*4.0)+1);
                for(int c=0;c<cols;++c){
                    const int centre=qMin(n-1,c*stride);
                    double re=0.0,im=0.0;
                    for(int k=-support;k<=support;++k){
                        const int i=centre+k;
                        if(i<0||i>=n) continue;
                        const double t=double(k)/scale;
                        const double envelope=std::exp(-0.5*t*t);
                        const double d=v[i]-mean;
                        re+=d*envelope*std::cos(kOmega*t);
                        im+=d*envelope*std::sin(kOmega*t);
                    }
                    const double norm=1.0/std::sqrt(scale);
                    ix.x.append(0); iy.x.append(0); value.x.append(0);
                    ix.y.append(double(centre));
                    iy.y.append(scale);
                    value.y.append((re*re+im*im)*norm*norm);
                }
            }
            out.series={ix,iy,value};
            out.style.fieldResolution=qMin(cols,kScales*2);
        }
        out.xAxis=PlotAxis{QStringLiteral("time (sample)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("scale"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------- Coherence Spectrum
    // How much of one signal's variation at each frequency is linearly
    // explained by another's. Unlike a cross-correlation it separates the
    // frequencies, so a pair that tracks each other at the drive frequency and
    // not at the noise floor reads as one number near 1 and the rest near 0.
    //
    // Welch's method: overlapping segments, averaged. Averaging is not optional
    // here - the coherence of two signals estimated from a SINGLE segment is
    // exactly 1 at every frequency, whatever the signals are, which is a
    // beautiful picture of nothing.
    if(in.engine==QLatin1String("Coherence Spectrum")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            QVector<double> a,b;
            const QVector<double>& xa=in.series.at(0).y;
            const QVector<double>& xb=in.series.at(1).y;
            const int n=qMin(xa.size(),xb.size());
            for(int i=0;i<n;++i)
                if(finite(xa[i])&&finite(xb[i])){ a.append(xa[i]); b.append(xb[i]); }
            const int m=a.size();
            int seg=1;
            while(seg*2<=m/4) seg*=2;
            if(m>=64&&seg>=16){
                const int step=seg/2, bins=seg/2;
                QVector<double> paa(bins,0.0), pbb(bins,0.0), pre(bins,0.0), pim(bins,0.0);
                int windows=0;
                for(int start=0;start+seg<=m;start+=step){
                    QVector<double> ar(seg),ai(seg,0.0),br(seg),bi(seg,0.0);
                    for(int i=0;i<seg;++i){
                        // Hann, so the segment edges do not leak across the
                        // whole spectrum.
                        const double w=0.5-0.5*std::cos(2.0*M_PI*double(i)/double(seg-1));
                        ar[i]=a[start+i]*w;
                        br[i]=b[start+i]*w;
                    }
                    fftInPlace(ar,ai);
                    fftInPlace(br,bi);
                    for(int k=0;k<bins;++k){
                        paa[k]+=ar[k]*ar[k]+ai[k]*ai[k];
                        pbb[k]+=br[k]*br[k]+bi[k]*bi[k];
                        pre[k]+=ar[k]*br[k]+ai[k]*bi[k];
                        pim[k]+=ai[k]*br[k]-ar[k]*bi[k];
                    }
                    ++windows;
                }
                if(windows>0){
                    PlotSeries coh;
                    // How the estimate was made goes under the figure. A
                    // coherence of one is what a single window ALWAYS returns,
                    // so how many windows were averaged is not decoration - it
                    // is the difference between a result and an artefact. It
                    // was written into the only named series, whose legend row
                    // is suppressed as a caption.
                    coh.label=QStringLiteral("coherence");
                    out.figureNote=QStringLiteral("Averaged over %1 window%2 of %3 "
                                                  "samples; a single window returns "
                                                  "a coherence of one everywhere.")
                                       .arg(windows)
                                       .arg(windows==1?QString():QStringLiteral("s"))
                                       .arg(seg);
                    coh.color=in.series.at(1).color;
                    coh.lineWidth=qMax(1.2,in.style.lineWidth);
                    for(int k=1;k<bins;++k){
                        const double den=paa[k]*pbb[k];
                        const double num=pre[k]*pre[k]+pim[k]*pim[k];
                        coh.x.append(double(k)/double(seg));
                        coh.y.append((den>1e-300)?qBound(0.0,num/den,1.0):0.0);
                    }
                    out.series.append(coh);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("frequency (cycles per sample)"),false,
                           unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("magnitude-squared coherence"),false,0.0,1.05};
        return out;
    }

    // ------------------------------------------------------------ Bode Plot
    // Gain and phase against frequency for a control loop, which is the pair of
    // numbers stability is read from: how much more gain the loop can take
    // before it oscillates, and how much more delay. Distinct from EIS: Bode,
    // which plots impedance magnitude and phase for an electrochemical cell -
    // the same axes, a different quantity, and worth being two entries.
    if(in.engine==QLatin1String("Bode Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& freq=in.series.at(0).y;
            const QVector<double>& gain=in.series.at(1).y;
            const int n=qMin(freq.size(),gain.size());
            PlotSeries mag;
            mag.label=QStringLiteral("gain (dB)");
            mag.color=in.series.at(1).color;
            mag.lineWidth=qMax(1.3,in.style.lineWidth);
            double crossover=0.0;
            double previous=std::numeric_limits<double>::quiet_NaN();
            double previousF=0.0;
            for(int i=0;i<n;++i){
                if(!finite(freq[i])||!finite(gain[i])||!(freq[i]>0.0)) continue;
                // A gain given in absolute terms is converted; one already in
                // decibels is left alone. Negative values cannot be a magnitude,
                // so their presence is what says which it is.
                mag.x.append(freq[i]);
                mag.y.append(gain[i]);
                if(finite(previous)&&((previous>0.0)!=(gain[i]>0.0))&&crossover==0.0){
                    const double t=(0.0-previous)/(gain[i]-previous);
                    crossover=previousF+t*(freq[i]-previousF);
                }
                previous=gain[i]; previousF=freq[i];
            }
            if(!mag.x.isEmpty()){
                if(crossover>0.0)
                    mag.label=QStringLiteral("gain (dB), crosses 0 dB at %1").arg(crossover,0,'g',4);
                out.series.append(mag);
                const Bounds b=boundsOf(mag.x);
                if(b.valid)
                    out.series.append(horizontalRule(0.0,b.lo,b.hi,in.style.warning,
                                                     QStringLiteral("0 dB"),true));
            }
            // Phase, when a third column carries it, on the same axis and said
            // so - two units on one ordinate is a compromise this plot has
            // always made and the label is what keeps it honest.
            if(in.series.size()>=3){
                const QVector<double>& phase=in.series.at(2).y;
                PlotSeries ph;
                ph.label=QStringLiteral("phase (degrees)");
                ph.color=in.style.warning;
                ph.lineWidth=qMax(1.1,in.style.lineWidth);
                const int k=qMin(freq.size(),phase.size());
                for(int i=0;i<k;++i){
                    if(!finite(freq[i])||!finite(phase[i])||!(freq[i]>0.0)) continue;
                    ph.x.append(freq[i]); ph.y.append(phase[i]);
                }
                if(!ph.x.isEmpty()) out.series.append(ph);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("frequency"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("gain (dB) / phase (deg)"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------- Operating Characteristic Curve
    // The probability a sampling plan ACCEPTS a lot, against how defective the
    // lot actually is. It is the plot that shows an inspection scheme is not
    // the guarantee people take it for: a plan that accepts 95% of good lots
    // also accepts a fair share of bad ones, and the curve says how many.
    if(in.engine==QLatin1String("Operating Characteristic Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        // Sample size and acceptance number, from the first two mapped values.
        int sample=50, accept=2;
        if(in.series.size()>=1&&!in.series.at(0).y.isEmpty()&&finite(in.series.at(0).y.first()))
            sample=qBound(1,int(std::lround(in.series.at(0).y.first())),100000);
        if(in.series.size()>=2&&!in.series.at(1).y.isEmpty()&&finite(in.series.at(1).y.first()))
            accept=qBound(0,int(std::lround(in.series.at(1).y.first())),sample);
        PlotSeries oc;
        oc.label=QStringLiteral("n = %1, accept on %2 or fewer").arg(sample).arg(accept);
        oc.color=in.series.isEmpty()?in.style.foreground:in.series.at(0).color;
        oc.lineWidth=qMax(1.3,in.style.lineWidth);
        for(int step=0;step<=200;++step){
            const double p=double(step)/200.0*0.25;   // 0 to 25% defective
            // Binomial tail, by the recurrence rather than by factorials -
            // 100000 choose 2 overflows a double long before it is needed.
            double term=std::pow(1.0-p,double(sample));
            double sum=term;
            for(int k=1;k<=accept;++k){
                if(!(1.0-p>1e-300)) { sum=(k>=accept)?1.0:0.0; break; }
                term*=double(sample-k+1)/double(k)*(p/(1.0-p));
                sum+=term;
            }
            oc.x.append(p*100.0);
            oc.y.append(qBound(0.0,sum,1.0));
        }
        out.series.append(oc);
        out.series.append(horizontalRule(0.95,0.0,25.0,in.style.gridColor,
                                         QStringLiteral("95% accepted"),true));
        out.series.append(horizontalRule(0.10,0.0,25.0,in.style.gridColor,
                                         QStringLiteral("10% accepted"),true));
        out.xAxis=PlotAxis{QStringLiteral("lot defective (%)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("probability of acceptance"),false,0.0,1.05};
        return out;
    }

    // ------------------------------------------------------- Cottrell Plot
    // Current against one over the square root of time, for a potential step
    // into a diffusion-limited reaction. Cottrell's equation makes that a
    // straight line through the origin, so the plot is a test as much as a
    // measurement: curvature means the current is not purely diffusion
    // controlled, and the slope gives the diffusion coefficient when it is.
    if(in.engine==QLatin1String("Cottrell Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& time=in.series.at(0).y;
            const QVector<double>& current=in.series.at(1).y;
            const int n=qMin(time.size(),current.size());
            PlotSeries pts;
            pts.label=QStringLiteral("measured");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(time[i])||!finite(current[i])||!(time[i]>0.0)) continue;
                const double invRoot=1.0/std::sqrt(time[i]);
                pts.x.append(invRoot); pts.y.append(current[i]);
                fx.append(invRoot); fy.append(current[i]);
            }
            if(fx.size()>=2){
                out.series.append(pts);
                // Through the origin, because Cottrell's equation has no
                // constant term: at infinite time the diffusion current is
                // zero. A free intercept would fit better and mean less.
                double sxy=0.0,sxx=0.0;
                for(int i=0;i<fx.size();++i){ sxy+=fx[i]*fy[i]; sxx+=fx[i]*fx[i]; }
                const Bounds b=boundsOf(fx);
                if(sxx>1e-300&&b.valid){
                    const double slope=sxy/sxx;
                    // How straight it actually is, because that is the finding.
                    double ssRes=0.0,ssTot=0.0,mean=0.0;
                    for(double q:fy) mean+=q;
                    mean/=double(fy.size());
                    for(int i=0;i<fx.size();++i){
                        const double r=fy[i]-slope*fx[i];
                        ssRes+=r*r;
                        ssTot+=(fy[i]-mean)*(fy[i]-mean);
                    }
                    const double r2=(ssTot>1e-300)?1.0-ssRes/ssTot:0.0;
                    PlotSeries fit;
                    fit.label=QStringLiteral("Cottrell slope %1, R2 %2")
                                  .arg(slope,0,'g',4).arg(r2,0,'f',4);
                    fit.color=in.style.warning;
                    fit.lineWidth=qMax(1.3,in.style.lineWidth);
                    fit.x={0.0,b.hi};
                    fit.y={0.0,slope*b.hi};
                    out.series.append(fit);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("1 / sqrt(time)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("current"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------- Coulombic Efficiency Trend
    // Charge recovered as a fraction of charge supplied, cycle by cycle, with
    // the cumulative mean over it. In a microbial electrolysis cell it is the
    // number that says whether the reactor is converting substrate to current
    // or losing it to something else, and a slow decline across cycles is the
    // signature of that loss establishing itself.
    if(in.engine==QLatin1String("Coulombic Efficiency Trend")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& cycle=in.series.at(0).y;
            const QVector<double>& efficiency=in.series.at(1).y;
            const int n=qMin(cycle.size(),efficiency.size());
            PlotSeries pts;
            pts.label=QStringLiteral("per cycle");
            pts.color=in.series.at(1).color;
            pts.drawMarkers=true; pts.markerSize=4.0;
            pts.lineWidth=qMax(1.2,in.style.lineWidth);
            PlotSeries running;
            running.label=QStringLiteral("cumulative mean");
            running.color=in.style.warning;
            running.lineWidth=qMax(1.3,in.style.lineWidth);
            double sum=0.0; int count=0;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(cycle[i])||!finite(efficiency[i])) continue;
                pts.x.append(cycle[i]); pts.y.append(efficiency[i]);
                sum+=efficiency[i]; ++count;
                running.x.append(cycle[i]); running.y.append(sum/double(count));
                fx.append(cycle[i]); fy.append(efficiency[i]);
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                out.series.append(running);
                const LineFit f=fitLine(fx,fy);
                const Bounds b=boundsOf(fx);
                if(f.ok&&b.valid){
                    PlotSeries trend;
                    trend.label=QStringLiteral("trend %1 per cycle").arg(f.slope,0,'g',3);
                    trend.color=in.style.danger;
                    trend.lineWidth=qMax(1.0,in.style.lineWidth*0.9);
                    trend.x={b.lo,b.hi};
                    trend.y={f.intercept+f.slope*b.lo,f.intercept+f.slope*b.hi};
                    out.series.append(trend);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("cycle"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("coulombic efficiency"),false,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------------------------------- Biplot
    // The observations AND the variables on one pair of axes. The points are
    // the rows projected onto the first two principal components; the arrows
    // are the columns, showing which variable pulls in which direction. Two
    // arrows close together are two columns carrying the same information,
    // which is the finding a correlation matrix states and a biplot shows.
    if(in.engine==QLatin1String("Biplot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        const int p=in.series.size();
        int rows=0;
        for(const PlotSeries& s:in.series) rows=qMax(rows,int(s.y.size()));
        if(p>=2&&rows>=3){
            // Standardised, because a principal component of unstandardised
            // columns is whichever column has the largest units. That is not a
            // finding about the data, it is a finding about the units.
            QVector<QVector<double>> z(p);
            QVector<double> centre(p,0.0), spread(p,1.0);
            for(int c=0;c<p;++c){
                const QVector<double>& col=in.series.at(c).y;
                double mean=0.0; int used=0;
                for(int i=0;i<rows&&i<col.size();++i)
                    if(finite(col[i])){ mean+=col[i]; ++used; }
                if(used>0) mean/=double(used);
                double var=0.0;
                for(int i=0;i<rows&&i<col.size();++i)
                    if(finite(col[i])) var+=(col[i]-mean)*(col[i]-mean);
                var=(used>1)?var/double(used-1):1.0;
                centre[c]=mean;
                spread[c]=(var>1e-300)?std::sqrt(var):1.0;
                z[c].resize(rows);
                for(int i=0;i<rows;++i)
                    z[c][i]=(i<col.size()&&finite(col[i]))?(col[i]-centre[c])/spread[c]:0.0;
            }
            // Correlation matrix, then its two leading eigenvectors by power
            // iteration with deflation. Two vectors of a p-by-p symmetric
            // matrix is not worth a linear algebra dependency.
            QVector<double> cov(p*p,0.0);
            for(int a=0;a<p;++a)
                for(int b=0;b<p;++b){
                    double s=0.0;
                    for(int i=0;i<rows;++i) s+=z[a][i]*z[b][i];
                    cov[a*p+b]=s/double(qMax(1,rows-1));
                }
            const auto power=[&](QVector<double>& m,QVector<double>& vec,double* value){
                vec.fill(0.0,p);
                vec[0]=1.0;
                for(int it=0;it<200;++it){
                    QVector<double> next(p,0.0);
                    for(int a=0;a<p;++a)
                        for(int b=0;b<p;++b) next[a]+=m[a*p+b]*vec[b];
                    double norm=0.0;
                    for(double q:next) norm+=q*q;
                    norm=std::sqrt(norm);
                    if(!(norm>1e-300)) return false;
                    for(double& q:next) q/=norm;
                    vec=next;
                    *value=norm;
                }
                return true;
            };
            QVector<double> pc1,pc2;
            double lambda1=0.0,lambda2=0.0;
            if(power(cov,pc1,&lambda1)){
                // Deflate and repeat for the second component.
                for(int a=0;a<p;++a)
                    for(int b=0;b<p;++b) cov[a*p+b]-=lambda1*pc1[a]*pc1[b];
                power(cov,pc2,&lambda2);
            }
            if(pc1.size()==p&&pc2.size()==p){
                PlotSeries scores;
                scores.label=QStringLiteral("observations");
                scores.color=in.series.at(0).color;
                scores.drawLine=false; scores.drawMarkers=true; scores.markerSize=4.0;
                double reach=0.0;
                for(int i=0;i<rows;++i){
                    double s1=0.0,s2=0.0;
                    for(int c=0;c<p;++c){ s1+=z[c][i]*pc1[c]; s2+=z[c][i]*pc2[c]; }
                    scores.x.append(s1); scores.y.append(s2);
                    reach=qMax(reach,std::hypot(s1,s2));
                }
                out.series.append(scores);
                // The loadings, scaled to the cloud so both are readable on one
                // pair of axes - which is what makes it a BIplot and is stated
                // on the axis label rather than left as a silent rescaling.
                const double scale=(reach>0.0)?reach*0.9:1.0;
                for(int c=0;c<p;++c){
                    PlotSeries arrow;
                    arrow.label=in.series.at(c).label.isEmpty()
                                ?QStringLiteral("column %1").arg(c+1)
                                :in.series.at(c).label;
                    arrow.color=categoryColour(in,c,std::fmod(double(c)*0.13,1.0),0.65,0.95);
                    arrow.lineWidth=qMax(1.3,in.style.lineWidth);
                    arrow.x={0.0,pc1[c]*scale};
                    arrow.y={0.0,pc2[c]*scale};
                    out.series.append(arrow);
                }
                const double total=lambda1+lambda2;
                out.xAxis=PlotAxis{QStringLiteral("PC1 (%1% of the first two)")
                                       .arg(total>0?100.0*lambda1/total:0.0,0,'f',0),
                                   false,unsetValue(),unsetValue()};
                out.yAxis=PlotAxis{QStringLiteral("PC2 (loadings scaled to the scores)"),
                                   false,unsetValue(),unsetValue()};
                return out;
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("PC1"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("PC2"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Star Glyph Plot
    // One small radar per row, laid out on a grid. Where parallel coordinates
    // draws every observation as a line across shared axes and relies on the
    // eye to follow one of them, this gives each observation a SHAPE - and a
    // shape is what people are good at matching, so outliers and groups fall
    // out of a page of glyphs without any statistics at all.
    if(in.engine==QLatin1String("Star Glyph Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        out.legendVisible=false;
        const int p=in.series.size();
        int rows=0;
        for(const PlotSeries& s:in.series) rows=qMax(rows,int(s.y.size()));
        if(p>=3&&rows>=1){
            // Each column scaled to 0..1 over its own range, because the spokes
            // share a radius and raw values on a shared radius compare nothing.
            QVector<Bounds> range(p);
            for(int c=0;c<p;++c) range[c]=boundsOf(in.series.at(c).y);
            const int shown=qMin(rows,64);
            const int cols=qMax(1,int(std::ceil(std::sqrt(double(shown)))));
            for(int i=0;i<shown;++i){
                const double cx=double(i%cols)*2.4;
                const double cy=-double(i/cols)*2.4;
                PlotSeries glyph;
                glyph.label=QString();
                glyph.color=categoryColour(in,i,std::fmod(double(i)*0.11,1.0),0.55,0.93);
                glyph.lineWidth=qMax(1.0,in.style.lineWidth);
                for(int c=0;c<=p;++c){
                    const int k=c%p;
                    const QVector<double>& col=in.series.at(k).y;
                    const double v=(i<col.size()&&finite(col[i]))?col[i]:0.0;
                    // The same reserved inner radius as the radar chart, now
                    // through the one function rather than written out here.
                    const double r=range[k].valid?radialScale(range[k],v,0.25):0.25;
                    const double a=2.0*M_PI*double(k)/double(p)-M_PI/2.0;
                    glyph.x.append(cx+r*std::cos(a));
                    glyph.y.append(cy+r*std::sin(a));
                }
                out.series.append(glyph);
            }
        }
        out.xAxis=PlotAxis{QString(),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QString(),false,unsetValue(),unsetValue()};
        out.style.gridVisible=false;
        out.style.scaleLabelsVisible=false;
        // AND NO FRAME EITHER. Both of these lay their marks out on a grid of
        // POSITIONS - one glyph per row, one square per unit of a hundred -
        // and the numbers on that grid are the positions themselves. Turning
        // off the gridlines and the scale labels left the box and its ticks,
        // which is an axis reading 0, 2, 4 under a row of glyphs. See
        // PlotSpec::framed.
        out.framed=false;
        return out;
    }

    // ------------------------------------------------------ Sunflower Plot
    // A scatter that says how many points are at each spot. Overplotting hides
    // density and no amount of transparency fixes it above a few thousand
    // points; a hexbin fixes it and throws the individual points away. This
    // keeps both: one dot for a single observation, and a dot with that many
    // short petals wherever several land together.
    if(in.engine==QLatin1String("Sunflower Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        out.legendVisible=false;
        if(in.series.size()>=2){
            const QVector<double>& xs=in.series.at(0).y;
            const QVector<double>& ys=in.series.at(1).y;
            const int n=qMin(xs.size(),ys.size());
            const Bounds bx=boundsOf(xs), by=boundsOf(ys);
            if(bx.valid&&by.valid&&n>0){
                const int side=qBound(8,int(std::sqrt(double(n))*0.9),48);
                QHash<int,int> bin;
                for(int i=0;i<n;++i){
                    if(!finite(xs[i])||!finite(ys[i])) continue;
                    const int cx=qBound(0,int((xs[i]-bx.lo)/qMax(1e-300,bx.hi-bx.lo)*side),side-1);
                    const int cy=qBound(0,int((ys[i]-by.lo)/qMax(1e-300,by.hi-by.lo)*side),side-1);
                    bin[cy*side+cx]+=1;
                }
                const double stepX=(bx.hi-bx.lo)/side, stepY=(by.hi-by.lo)/side;
                const double petal=qMin(stepX,stepY)*0.45;
                PlotSeries singles;
                singles.label=QString();
                singles.color=in.series.at(1).color;
                singles.drawLine=false; singles.drawMarkers=true; singles.markerSize=3.0;
                // Sorted, for the reason given in drawHexbin: a QHash's order
                // depends on a per-process seed, so the flowers were emitted in
                // a different order on every run and the figure was not
                // reproducible.
                QVector<int> filled;
                filled.reserve(bin.size());
                for(auto it=bin.constBegin();it!=bin.constEnd();++it) filled.append(it.key());
                std::sort(filled.begin(),filled.end());
                for(const int cellKey:std::as_const(filled)){
                    const int cx=cellKey%side, cy=cellKey/side;
                    const double x=bx.lo+(cx+0.5)*stepX;
                    const double y=by.lo+(cy+0.5)*stepY;
                    const int here=bin.value(cellKey);
                    if(here<=1){ singles.x.append(x); singles.y.append(y); continue; }
                    // One petal per observation, up to a dozen - beyond that
                    // nobody counts them and the number is written instead by
                    // the density of the flowers themselves.
                    const int petals=qMin(here,12);
                    for(int k=0;k<petals;++k){
                        const double a=2.0*M_PI*double(k)/double(petals);
                        PlotSeries spoke;
                        spoke.label=QString();
                        spoke.color=in.series.at(1).color;
                        spoke.lineWidth=0.9;
                        spoke.x={x,x+petal*std::cos(a)*(stepX/qMax(1e-300,qMin(stepX,stepY)))};
                        spoke.y={y,y+petal*std::sin(a)*(stepY/qMax(1e-300,qMin(stepX,stepY)))};
                        out.series.append(spoke);
                    }
                }
                if(!singles.x.isEmpty()) out.series.append(singles);
            }
        }
        return out;
    }

    // ====================================================================
    // Batch 7: geochronology, hydrology, thermal analysis, spectroscopy,
    // rheology, acoustics, pharmacokinetics and control.
    //
    // Same rule as the six batches before it: every one is a transformation of
    // the data followed by geometry that already exists and is already tested.
    // What each adds is the CONSTRUCTION - the concordia curve from the decay
    // constants, the extrapolated onset tangent, the terminal slope that gives
    // a half-life. That construction is the reason the figure exists, and it is
    // labelled with the number it produces so the reader is not asked to
    // measure it off the page with a ruler.

    // --------------------------------------------------- Concordia Diagram
    // Wetherill concordia: 207Pb/235U against 206Pb/238U. Both ratios grow with
    // age along one curve, so a sample that has stayed closed sits ON it and
    // its position IS its age. A sample that has lost lead falls below onto a
    // chord, and where that chord meets the curve again is the original age.
    //
    // The curve is computed from the two decay constants rather than fitted,
    // because it is not a property of the data: it is what the data is being
    // compared against.
    if(in.engine==QLatin1String("Concordia Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        // Steiger and Jager (1977), the values every U-Pb age is quoted on.
        const double lambda235=9.8485e-10;   // per year
        const double lambda238=1.55125e-10;  // per year
        auto conc207=[&](double t){ return std::expm1(lambda235*t); };
        auto conc206=[&](double t){ return std::expm1(lambda238*t); };
        if(in.series.size()>=2){
            const QVector<double>& r207=in.series.at(0).y;
            const QVector<double>& r206=in.series.at(1).y;
            const int n=qMin(r207.size(),r206.size());
            PlotSeries pts;
            pts.label=QStringLiteral("analyses");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.4;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(r207[i])||!finite(r206[i])) continue;
                pts.x.append(r207[i]); pts.y.append(r206[i]);
                fx.append(r207[i]); fy.append(r206[i]);
            }
            if(!pts.x.isEmpty()){
                // The curve is drawn over the age range the data spans, found
                // by inverting the 206/238 ratio - the slower decay, so the
                // better conditioned of the two.
                const Bounds by=boundsOf(fy);
                double tLo=0.0,tHi=4.6e9;
                if(by.valid){
                    if(by.lo>-0.999999) tLo=std::log1p(qMax(0.0,by.lo))/lambda238;
                    if(by.hi>-0.999999) tHi=std::log1p(qMax(1e-12,by.hi))/lambda238;
                }
                if(!(tHi>tLo)){ tLo=0.0; tHi=4.6e9; }
                const double pad=0.15*(tHi-tLo)+1.0e6;
                tLo=qMax(0.0,tLo-pad); tHi=qMin(4.6e9,tHi+pad);
                PlotSeries curve;
                curve.label=QStringLiteral("concordia %1-%2 Ma")
                                .arg(tLo/1.0e6,0,'f',0).arg(tHi/1.0e6,0,'f',0);
                curve.color=in.style.warning;
                curve.lineWidth=qMax(1.4,in.style.lineWidth);
                for(int k=0;k<=240;++k){
                    const double t=tLo+(tHi-tLo)*double(k)/240.0;
                    curve.x.append(conc207(t));
                    curve.y.append(conc206(t));
                }
                out.series.append(curve);
                // Age ticks on the curve, at whichever round interval gives
                // between four and a dozen of them over the span drawn.
                double stepMa=1.0;
                const double spanMa=(tHi-tLo)/1.0e6;
                const double raw[]={1,2,5,10,20,25,50,100,200,250,500,1000,2000};
                for(double candidate:raw){ stepMa=candidate; if(spanMa/candidate<=12.0) break; }
                PlotSeries ticks;
                ticks.label=QStringLiteral("every %1 Ma").arg(stepMa,0,'g',3);
                ticks.color=in.style.warning;
                ticks.drawLine=false; ticks.drawMarkers=true; ticks.markerSize=5.0;
                for(double ma=std::ceil(tLo/1.0e6/stepMa)*stepMa;ma*1.0e6<=tHi;ma+=stepMa){
                    ticks.x.append(conc207(ma*1.0e6));
                    ticks.y.append(conc206(ma*1.0e6));
                }
                if(!ticks.x.isEmpty()) out.series.append(ticks);
                out.series.append(pts);
                // The discordia chord, and its UPPER intercept - the age the
                // system closed at, which is the number the diagram is for.
                const LineFit chord=fitLine(fx,fy);
                const Bounds bx=boundsOf(fx);
                if(chord.ok&&bx.valid){
                    // Where the chord meets concordia: f(t) = 0 changes sign
                    // once above the lower intercept, so bisection is safe and
                    // needs no derivative of a function of two exponentials.
                    auto miss=[&](double t){
                        return conc206(t)-(chord.intercept+chord.slope*conc207(t));
                    };
                    // A chord crosses concordia TWICE - at the lower
                    // intercept, where lead was lost, and at the upper one,
                    // where the system closed. Bracketing the whole interval
                    // and bisecting once was the first attempt and it found
                    // nothing: with two roots inside, the signs at the two ends
                    // agree and there is no bracket to find. So the interval is
                    // SCANNED for sign changes and each is refined, and the
                    // LARGEST root is the upper intercept - the older age,
                    // which is what the diagram is read for.
                    double root=std::numeric_limits<double>::quiet_NaN();
                    {
                        const int steps=4600;
                        double prevT=1.0e6,prevMiss=miss(prevT);
                        for(int k=1;k<=steps;++k){
                            const double t=1.0e6+(4.6e9-1.0e6)*double(k)/double(steps);
                            const double m=miss(t);
                            if(finite(prevMiss)&&finite(m)&&prevMiss*m<=0.0&&prevMiss!=m){
                                double lo=prevT,hi=t;
                                for(int it=0;it<80;++it){
                                    const double mid=0.5*(lo+hi);
                                    if(miss(lo)*miss(mid)<=0.0) hi=mid; else lo=mid;
                                }
                                root=0.5*(lo+hi);
                            }
                            prevT=t; prevMiss=m;
                        }
                    }
                    PlotSeries disc;
                    disc.label=finite(root)
                        ? QStringLiteral("discordia, upper intercept %1 Ma").arg(root/1.0e6,0,'f',1)
                        : QStringLiteral("discordia (no upper intercept below 4.6 Ga)");
                    disc.color=in.style.danger;
                    disc.lineWidth=qMax(1.1,in.style.lineWidth*0.9);
                    disc.x={bx.lo,bx.hi};
                    disc.y={chord.intercept+chord.slope*bx.lo,
                            chord.intercept+chord.slope*bx.hi};
                    out.series.append(disc);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("207Pb / 235U"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("206Pb / 238U"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Isochron Plot
    // Daughter against parent, both divided by a stable isotope of the
    // daughter element. Samples that formed together from one reservoir fall
    // on a line whose SLOPE is the age and whose INTERCEPT is the initial
    // ratio they started from - so one fit gives both, and the scatter about
    // it says whether they really were one system.
    if(in.engine==QLatin1String("Isochron Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        // 87Rb, the commonest isochron system. Named in the label rather than
        // assumed silently, because a Sm-Nd isochron drawn on it would be
        // wrong by a factor of twenty.
        const double lambdaRb=1.397e-11; // per year
        if(in.series.size()>=2){
            const QVector<double>& parent=in.series.at(0).y;
            const QVector<double>& daughter=in.series.at(1).y;
            const int n=qMin(parent.size(),daughter.size());
            PlotSeries pts;
            pts.label=QStringLiteral("samples");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.4;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(parent[i])||!finite(daughter[i])) continue;
                pts.x.append(parent[i]); pts.y.append(daughter[i]);
                fx.append(parent[i]); fy.append(daughter[i]);
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
                    // A negative slope is a real fit and not an age: log1p of
                    // it is undefined below -1 and meaningless above it.
                    const double age=(f.slope>0.0)?std::log1p(f.slope)/lambdaRb
                                                  :std::numeric_limits<double>::quiet_NaN();
                    PlotSeries line;
                    line.label=finite(age)
                        ? QStringLiteral("isochron %1 Ma (87Rb), initial %2, R2 %3")
                              .arg(age/1.0e6,0,'f',1).arg(f.intercept,0,'f',5).arg(r2,0,'f',4)
                        : QStringLiteral("negative slope - not an age; initial %1")
                              .arg(f.intercept,0,'f',5);
                    line.color=in.style.warning;
                    line.lineWidth=qMax(1.3,in.style.lineWidth);
                    line.x={0.0,bx.hi};
                    line.y={f.intercept,f.intercept+f.slope*bx.hi};
                    out.series.append(line);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("parent / stable"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("daughter / stable"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Harker Diagram
    // Every other oxide against silica, one panel's worth of trends on one
    // pair of axes. A fractionating magma depletes some oxides as silica rises
    // and enriches others, so the SIGN of each trend is the finding and the
    // slopes are worth reading side by side.
    if(in.engine==QLatin1String("Harker Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& silica=in.series.at(0).y;
            for(int c=1;c<in.series.size();++c){
                const QVector<double>& oxide=in.series.at(c).y;
                const int n=qMin(silica.size(),oxide.size());
                PlotSeries pts;
                pts.label=in.series.at(c).label;
                pts.color=in.series.at(c).color;
                pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=3.8;
                QVector<double> fx,fy;
                for(int i=0;i<n;++i){
                    if(!finite(silica[i])||!finite(oxide[i])) continue;
                    pts.x.append(silica[i]); pts.y.append(oxide[i]);
                    fx.append(silica[i]); fy.append(oxide[i]);
                }
                if(pts.x.isEmpty()) continue;
                out.series.append(pts);
                const LineFit f=fitLine(fx,fy);
                const Bounds bx=boundsOf(fx);
                if(f.ok&&bx.valid){
                    PlotSeries trend;
                    trend.label=QStringLiteral("%1 %2 per wt% SiO2")
                                    .arg(in.series.at(c).label)
                                    .arg(f.slope,0,'g',3);
                    trend.color=in.series.at(c).color;
                    trend.lineWidth=qMax(1.0,in.style.lineWidth*0.85);
                    trend.dashPattern={5,4};
                    trend.x={bx.lo,bx.hi};
                    trend.y={f.intercept+f.slope*bx.lo,f.intercept+f.slope*bx.hi};
                    out.series.append(trend);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("SiO2 (wt%)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("oxide (wt%)"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------- Normalised Spider Diagram
    // Each sample divided element by element by a reference composition, on a
    // log axis, with the elements in whatever order the columns arrive in. The
    // normalisation is what makes the picture readable: raw abundances span
    // six orders of magnitude between the light and heavy ends and no shape
    // survives that, while the RATIO to a reference is a curve near one whose
    // kinks are anomalies.
    //
    // The reference is the FIRST mapped column, not a built-in chondrite
    // table. A built-in one would be a silent assumption about which
    // normalisation the reader wanted, and there are half a dozen in use.
    if(in.engine==QLatin1String("Normalised Spider Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& reference=in.series.at(0).y;
            for(int c=1;c<in.series.size();++c){
                const QVector<double>& sample=in.series.at(c).y;
                const int n=qMin(reference.size(),sample.size());
                PlotSeries s;
                s.label=in.series.at(c).label;
                s.color=in.series.at(c).color;
                s.lineWidth=qMax(1.2,in.style.lineWidth);
                s.drawMarkers=true; s.markerSize=3.4;
                for(int i=0;i<n;++i){
                    if(!finite(reference[i])||!finite(sample[i])) continue;
                    if(!(reference[i]>0.0)||!(sample[i]>0.0)) continue;
                    s.x.append(double(i+1));
                    s.y.append(sample[i]/reference[i]);
                }
                if(!s.x.isEmpty()) out.series.append(s);
            }
            // Unity, where a sample matches the reference exactly. Without it
            // the reader has no way to tell enrichment from depletion at a
            // glance, which is the whole question.
            int elements=0;
            for(const PlotSeries& s:out.series) elements=qMax(elements,int(s.x.size()));
            if(elements>0){
                PlotSeries unity;
                unity.label=QStringLiteral("reference (%1)").arg(in.series.at(0).label);
                unity.color=in.style.warning;
                unity.lineWidth=qMax(1.0,in.style.lineWidth*0.8);
                unity.dashPattern={5,4};
                unity.x={1.0,double(elements)};
                unity.y={1.0,1.0};
                out.series.append(unity);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("element (column order)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("sample / reference"),true,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------------------------- Keeling Plot
    // The isotopic composition of a mixture against ONE OVER its
    // concentration. Two-component mixing is a straight line in those
    // coordinates and its intercept - the limit of infinite concentration - is
    // the composition of whatever is being added to the background. That
    // intercept is the measurement; the points are only how it was reached.
    if(in.engine==QLatin1String("Keeling Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& conc=in.series.at(0).y;
            const QVector<double>& delta=in.series.at(1).y;
            const int n=qMin(conc.size(),delta.size());
            PlotSeries pts;
            pts.label=QStringLiteral("samples");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(conc[i])||!finite(delta[i])||!(conc[i]>0.0)) continue;
                const double inv=1.0/conc[i];
                pts.x.append(inv); pts.y.append(delta[i]);
                fx.append(inv); fy.append(delta[i]);
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
                    // Drawn back to x = 0, because that is where the answer is
                    // and a fit stopped at the data leaves it to be guessed.
                    line.label=QStringLiteral("source %1, R2 %2")
                                   .arg(f.intercept,0,'f',3).arg(r2,0,'f',4);
                    line.color=in.style.warning;
                    line.lineWidth=qMax(1.3,in.style.lineWidth);
                    line.x={0.0,bx.hi};
                    line.y={f.intercept,f.intercept+f.slope*bx.hi};
                    out.series.append(line);
                    PlotSeries mark;
                    mark.label=QStringLiteral("intercept");
                    mark.color=in.style.danger;
                    mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.5;
                    mark.x={0.0}; mark.y={f.intercept};
                    out.series.append(mark);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("1 / concentration"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("delta"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------------ IDF Curve
    // Rainfall intensity against storm duration, one curve per return period.
    // Short storms are intense and long ones are not, and the whole family
    // falls close to a straight line on log-log axes - so the exponent of that
    // line is the local rainfall's signature and is worth reading off each
    // curve rather than eyeballed across them.
    if(in.engine==QLatin1String("IDF Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=3){
            const QVector<double>& duration=in.series.at(0).y;
            const QVector<double>& intensity=in.series.at(1).y;
            const QVector<double>& period=in.series.at(2).y;
            const int n=qMin(duration.size(),qMin(intensity.size(),period.size()));
            // Grouped by return period, and the groups are drawn in ascending
            // order so the legend reads the way the curves stack.
            QMap<double,QVector<QPair<double,double>>> byPeriod;
            for(int i=0;i<n;++i){
                if(!finite(duration[i])||!finite(intensity[i])||!finite(period[i])) continue;
                if(!(duration[i]>0.0)||!(intensity[i]>0.0)) continue;
                byPeriod[period[i]].append(qMakePair(duration[i],intensity[i]));
            }
            int index=0;
            for(auto it=byPeriod.constBegin();it!=byPeriod.constEnd();++it,++index){
                QVector<QPair<double,double>> rows=it.value();
                std::sort(rows.begin(),rows.end(),
                          [](const QPair<double,double>& a,const QPair<double,double>& b){
                              return a.first<b.first; });
                QVector<double> lx,ly;
                PlotSeries s;
                s.color=categoryColour(in,index,std::fmod(0.58+double(index)*0.13,1.0),0.62,0.95);
                s.lineWidth=qMax(1.3,in.style.lineWidth);
                s.drawMarkers=true; s.markerSize=3.4;
                for(const QPair<double,double>& r:rows){
                    s.x.append(r.first); s.y.append(r.second);
                    lx.append(std::log10(r.first)); ly.append(std::log10(r.second));
                }
                const LineFit f=fitLine(lx,ly);
                s.label=f.ok
                    ? QStringLiteral("%1-year, exponent %2").arg(it.key(),0,'g',4).arg(f.slope,0,'f',3)
                    : QStringLiteral("%1-year").arg(it.key(),0,'g',4);
                out.series.append(s);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("duration"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("intensity"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Van 't Hoff Plot
    // ln K against one over temperature. The slope is minus the enthalpy over
    // R and the intercept is the entropy over R, so one straight line gives
    // both halves of the free energy - and curvature in it means the enthalpy
    // is itself temperature dependent, which is a finding rather than noise.
    if(in.engine==QLatin1String("Van 't Hoff Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        const double gasConstant=8.314462618; // J / (mol K)
        if(in.series.size()>=2){
            const QVector<double>& temperature=in.series.at(0).y;
            const QVector<double>& equilibrium=in.series.at(1).y;
            const int n=qMin(temperature.size(),equilibrium.size());
            PlotSeries pts;
            pts.label=QStringLiteral("measured");
            pts.color=in.series.at(1).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;
            QVector<double> fx,fy;
            for(int i=0;i<n;++i){
                if(!finite(temperature[i])||!finite(equilibrium[i])) continue;
                // Kelvin and a positive constant, both required by the
                // transform rather than by taste: 1/T at absolute zero and
                // ln K of a negative number are not quantities.
                if(!(temperature[i]>0.0)||!(equilibrium[i]>0.0)) continue;
                pts.x.append(1000.0/temperature[i]);
                pts.y.append(std::log(equilibrium[i]));
                fx.append(1000.0/temperature[i]);
                fy.append(std::log(equilibrium[i]));
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                const LineFit f=fitLine(fx,fy);
                const Bounds bx=boundsOf(fx);
                if(f.ok&&bx.valid){
                    // The x axis is 1000/T, so the slope carries that factor
                    // and the enthalpy comes out in kJ/mol without a second
                    // conversion - which is the unit it is quoted in anyway.
                    const double enthalpy=-f.slope*gasConstant;          // kJ/mol
                    const double entropy=f.intercept*gasConstant;        // J/(mol K)
                    PlotSeries line;
                    line.label=QStringLiteral("dH %1 kJ/mol, dS %2 J/(mol K)")
                                   .arg(enthalpy,0,'f',2).arg(entropy,0,'f',1);
                    line.color=in.style.warning;
                    line.lineWidth=qMax(1.3,in.style.lineWidth);
                    line.x={bx.lo,bx.hi};
                    line.y={f.intercept+f.slope*bx.lo,f.intercept+f.slope*bx.hi};
                    out.series.append(line);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("1000 / T (1/K)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("ln K"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------------ Tauc Plot
    // (alpha h nu) squared against photon energy, for a direct allowed
    // transition. The absorption edge becomes a straight segment and where
    // that segment crosses zero is the band gap.
    //
    // The segment is FOUND rather than assumed: the window of points with the
    // steepest fitted slope is the edge, and extrapolating a window chosen by
    // eye is how two people get two band gaps from one spectrum.
    if(in.engine==QLatin1String("Tauc Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& energy=in.series.at(0).y;
            const QVector<double>& absorption=in.series.at(1).y;
            const int n=qMin(energy.size(),absorption.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(energy[i])||!finite(absorption[i])) continue;
                if(!(energy[i]>0.0)||absorption[i]<0.0) continue;
                const double tauc=(absorption[i]*energy[i])*(absorption[i]*energy[i]);
                rows.append(qMakePair(energy[i],tauc));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=6){
                PlotSeries curve;
                curve.label=QStringLiteral("(a h v)^2");
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.2,in.style.lineWidth);
                QVector<double> xs,ys;
                for(const QPair<double,double>& r:rows){
                    curve.x.append(r.first); curve.y.append(r.second);
                    xs.append(r.first); ys.append(r.second);
                }
                out.series.append(curve);
                const int window=qBound(4,rows.size()/6,rows.size());
                const WindowFit picked=slidingBestFit(xs,ys,window,true);
                const LineFit best=picked.fit;
                const int bestAt=picked.at;
                if(bestAt>=0&&best.slope>1e-300){
                    const double gap=-best.intercept/best.slope;
                    // The tangent is drawn from the gap up through the window
                    // it was fitted on, so the reader sees WHICH points made
                    // the number rather than a line floating over the edge.
                    const double top=ys[qMin(rows.size()-1,bestAt+window-1)];
                    PlotSeries tangent;
                    tangent.label=QStringLiteral("Eg %1 eV (direct allowed)").arg(gap,0,'f',3);
                    tangent.color=in.style.warning;
                    tangent.lineWidth=qMax(1.3,in.style.lineWidth);
                    tangent.x={gap,xs[qMin(rows.size()-1,bestAt+window-1)]};
                    tangent.y={0.0,top};
                    out.series.append(tangent);
                    PlotSeries mark;
                    mark.label=QStringLiteral("band gap");
                    mark.color=in.style.danger;
                    mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.0;
                    mark.x={gap}; mark.y={0.0};
                    out.series.append(mark);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("photon energy (eV)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("(a h v)^2"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Stern-Volmer Plot
    // Unquenched intensity over quenched intensity against quencher
    // concentration. Purely dynamic quenching gives a straight line whose
    // slope is the Stern-Volmer constant; a line that curves UPWARD means a
    // static component as well, and that curvature is the reason the ratio is
    // plotted rather than the intensity.
    if(in.engine==QLatin1String("Stern-Volmer Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& quencher=in.series.at(0).y;
            const QVector<double>& intensity=in.series.at(1).y;
            const int n=qMin(quencher.size(),intensity.size());
            // I0 is the intensity at the LOWEST quencher concentration
            // present, which is the only defensible choice when no zero-
            // quencher row was supplied - and it is named in the label so a
            // reader with a true blank knows the constant is relative to it.
            double atLowest=std::numeric_limits<double>::quiet_NaN();
            double lowest=std::numeric_limits<double>::infinity();
            for(int i=0;i<n;++i){
                if(!finite(quencher[i])||!finite(intensity[i])||!(intensity[i]>0.0)) continue;
                if(quencher[i]<lowest){ lowest=quencher[i]; atLowest=intensity[i]; }
            }
            if(finite(atLowest)&&atLowest>0.0){
                PlotSeries pts;
                pts.label=QStringLiteral("I0 at [Q] = %1").arg(lowest,0,'g',4);
                pts.color=in.series.at(1).color;
                pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.2;
                QVector<double> fx,fy;
                for(int i=0;i<n;++i){
                    if(!finite(quencher[i])||!finite(intensity[i])||!(intensity[i]>0.0)) continue;
                    pts.x.append(quencher[i]);
                    pts.y.append(atLowest/intensity[i]);
                    fx.append(quencher[i]); fy.append(atLowest/intensity[i]);
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
                        line.label=QStringLiteral("Ksv %1, R2 %2")
                                       .arg(f.slope,0,'g',4).arg(r2,0,'f',4);
                        line.color=in.style.warning;
                        line.lineWidth=qMax(1.3,in.style.lineWidth);
                        line.x={bx.lo,bx.hi};
                        line.y={f.intercept+f.slope*bx.lo,f.intercept+f.slope*bx.hi};
                        out.series.append(line);
                    }
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("quencher concentration"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("I0 / I"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- TGA / DTG Curve
    // Mass against temperature, with its derivative over the top. The mass
    // curve says how much was lost and the derivative says WHERE - two
    // overlapping decompositions look like one sloping step on the mass curve
    // and like two clear peaks on the derivative.
    //
    // The two have different units, so the derivative is rescaled to the mass
    // axis and the label says so. Drawing it unscaled would put it flat on the
    // baseline; drawing it scaled without saying so would invite the peak
    // height to be read as a mass.
    if(in.engine==QLatin1String("TGA / DTG Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& temperature=in.series.at(0).y;
            const QVector<double>& mass=in.series.at(1).y;
            const int n=qMin(temperature.size(),mass.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(temperature[i])||!finite(mass[i])) continue;
                rows.append(qMakePair(temperature[i],mass[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=5){
                const double initial=rows.first().second;
                PlotSeries retained;
                retained.label=(std::abs(initial)>1e-300)
                    ? QStringLiteral("mass (% of initial)")
                    : QStringLiteral("mass");
                retained.color=in.series.at(1).color;
                retained.lineWidth=qMax(1.3,in.style.lineWidth);
                QVector<double> percent;
                for(const QPair<double,double>& r:rows){
                    const double p=(std::abs(initial)>1e-300)?100.0*r.second/initial:r.second;
                    retained.x.append(r.first);
                    retained.y.append(p);
                    percent.append(p);
                }
                out.series.append(retained);
                // Centred differences inside, one-sided at the ends. A forward
                // difference throughout biases every peak half a step warm.
                QVector<double> dtg(rows.size(),0.0);
                for(int i=0;i<rows.size();++i){
                    const int lo=qMax(0,i-1), hi=qMin(rows.size()-1,i+1);
                    const double dT=rows[hi].first-rows[lo].first;
                    dtg[i]=(std::abs(dT)>1e-300)?-(percent[hi]-percent[lo])/dT:0.0;
                }
                const Bounds bm=boundsOf(percent), bd=boundsOf(dtg);
                if(bm.valid&&bd.valid&&bd.hi>0.0){
                    const double factor=(bm.hi-bm.lo)/qMax(1e-300,bd.hi);
                    int peakAt=0;
                    for(int i=1;i<dtg.size();++i) if(dtg[i]>dtg[peakAt]) peakAt=i;
                    PlotSeries derivative;
                    derivative.label=QStringLiteral("DTG x %1, peak at %2")
                                         .arg(factor,0,'g',3).arg(rows[peakAt].first,0,'f',1);
                    derivative.color=in.style.warning;
                    derivative.lineWidth=qMax(1.1,in.style.lineWidth*0.9);
                    for(int i=0;i<dtg.size();++i){
                        derivative.x.append(rows[i].first);
                        derivative.y.append(bm.lo+dtg[i]*factor);
                    }
                    out.series.append(derivative);
                    // Extrapolated onset: the steepest tangent taken back to
                    // the starting mass. That is the ISO construction, and it
                    // is reproducible in a way that "where it starts to bend"
                    // is not.
                    const int lo=qMax(0,peakAt-1), hi=qMin(rows.size()-1,peakAt+1);
                    const double slope=(rows[hi].first>rows[lo].first)
                        ?(percent[hi]-percent[lo])/(rows[hi].first-rows[lo].first):0.0;
                    if(slope<-1e-12){
                        const double onset=rows[peakAt].first
                                          +(percent.first()-percent[peakAt])/slope;
                        PlotSeries tangent;
                        tangent.label=QStringLiteral("onset %1").arg(onset,0,'f',1);
                        tangent.color=in.style.danger;
                        tangent.lineWidth=qMax(1.0,in.style.lineWidth*0.85);
                        tangent.dashPattern={5,4};
                        tangent.x={onset,rows[qMin(rows.size()-1,peakAt+1)].first};
                        tangent.y={percent.first(),
                                   percent[peakAt]+slope*(rows[qMin(rows.size()-1,peakAt+1)].first
                                                          -rows[peakAt].first)};
                        out.series.append(tangent);
                    }
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("temperature"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("mass (% of initial)"),false,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------------------------- DSC Thermogram
    // Heat flow against temperature, with the baseline the peak is integrated
    // against drawn in. The peak AREA is the transition enthalpy and it cannot
    // be read off the curve without knowing where the baseline was put, so
    // putting it on the page is not decoration - it is the working.
    if(in.engine==QLatin1String("DSC Thermogram")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& temperature=in.series.at(0).y;
            const QVector<double>& heatFlow=in.series.at(1).y;
            const int n=qMin(temperature.size(),heatFlow.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(temperature[i])||!finite(heatFlow[i])) continue;
                rows.append(qMakePair(temperature[i],heatFlow[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=8){
                PlotSeries curve;
                curve.label=QStringLiteral("heat flow");
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.3,in.style.lineWidth);
                for(const QPair<double,double>& r:rows){
                    curve.x.append(r.first); curve.y.append(r.second);
                }
                out.series.append(curve);
                // A straight baseline between the flat ends, taken as the mean
                // of the first and last tenth so one noisy point at an end
                // cannot tilt the whole integration.
                const int edge=qMax(1,rows.size()/10);
                double leftT=0,leftQ=0,rightT=0,rightQ=0;
                for(int i=0;i<edge;++i){ leftT+=rows[i].first; leftQ+=rows[i].second; }
                for(int i=rows.size()-edge;i<rows.size();++i){
                    rightT+=rows[i].first; rightQ+=rows[i].second;
                }
                leftT/=edge; leftQ/=edge; rightT/=edge; rightQ/=edge;
                const double span=rightT-leftT;
                const double slope=(std::abs(span)>1e-300)?(rightQ-leftQ)/span:0.0;
                auto baselineAt=[&](double t){ return leftQ+slope*(t-leftT); };
                PlotSeries baseline;
                baseline.color=in.style.warning;
                baseline.lineWidth=qMax(1.0,in.style.lineWidth*0.85);
                baseline.dashPattern={5,4};
                for(const QPair<double,double>& r:rows){
                    baseline.x.append(r.first);
                    baseline.y.append(baselineAt(r.first));
                }
                // Trapezoidal integration of the excess over the baseline, and
                // the largest excursion from it in either direction. Which way
                // is exothermic depends on the instrument's convention, so the
                // label reports the SIGN rather than naming the direction.
                double area=0.0; int peakAt=0; double peakExcess=0.0;
                for(int i=0;i<rows.size();++i){
                    const double excess=rows[i].second-baselineAt(rows[i].first);
                    if(std::abs(excess)>std::abs(peakExcess)){ peakExcess=excess; peakAt=i; }
                    if(i==0) continue;
                    const double before=rows[i-1].second-baselineAt(rows[i-1].first);
                    area+=0.5*(excess+before)*(rows[i].first-rows[i-1].first);
                }
                baseline.label=QStringLiteral("baseline; peak %1, area %2 (flow x temperature)")
                                   .arg(rows[peakAt].first,0,'f',1).arg(area,0,'g',4);
                out.series.append(baseline);
                PlotSeries mark;
                mark.label=QStringLiteral("peak");
                mark.color=in.style.danger;
                mark.drawLine=false; mark.drawMarkers=true; mark.markerSize=6.0;
                mark.x={rows[peakAt].first}; mark.y={rows[peakAt].second};
                out.series.append(mark);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("temperature"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("heat flow"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------------ Creep Curve
    // Strain against time under a held load. The number the curve exists to
    // give is the MINIMUM creep rate in the secondary stage, because that is
    // what a life prediction is built on - the primary stage is transient and
    // the tertiary one is already failing.
    if(in.engine==QLatin1String("Creep Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& time=in.series.at(0).y;
            const QVector<double>& strain=in.series.at(1).y;
            const int n=qMin(time.size(),strain.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(time[i])||!finite(strain[i])) continue;
                rows.append(qMakePair(time[i],strain[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=6){
                PlotSeries curve;
                curve.label=QStringLiteral("strain");
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.3,in.style.lineWidth);
                QVector<double> xs,ys;
                for(const QPair<double,double>& r:rows){
                    curve.x.append(r.first); curve.y.append(r.second);
                    xs.append(r.first); ys.append(r.second);
                }
                out.series.append(curve);
                // The flattest fitted window, which is the secondary stage
                // wherever it happens to be. Taking "the middle third" instead
                // assumes the three stages are equal in duration, and they
                // never are.
                const int window=qBound(3,rows.size()/5,rows.size());
                const WindowFit picked=slidingBestFit(xs,ys,window,false);
                const LineFit best=picked.fit;
                const int bestAt=picked.at;
                if(bestAt>=0&&best.ok){
                    PlotSeries secondary;
                    secondary.label=QStringLiteral("minimum creep rate %1 per unit time")
                                        .arg(best.slope,0,'g',4);
                    secondary.color=in.style.warning;
                    secondary.lineWidth=qMax(1.3,in.style.lineWidth);
                    const double t0=xs[bestAt], t1=xs[qMin(rows.size()-1,bestAt+window-1)];
                    secondary.x={t0,t1};
                    secondary.y={best.intercept+best.slope*t0,best.intercept+best.slope*t1};
                    out.series.append(secondary);
                }
                PlotSeries rupture;
                rupture.label=QStringLiteral("last point, t %1, strain %2")
                                  .arg(rows.last().first,0,'g',4).arg(rows.last().second,0,'g',4);
                rupture.color=in.style.danger;
                rupture.drawLine=false; rupture.drawMarkers=true; rupture.markerSize=6.0;
                rupture.x={rows.last().first}; rupture.y={rows.last().second};
                out.series.append(rupture);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("time"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("strain"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------- Master Curve (TTS)
    // Isotherms shifted horizontally in log time or log frequency until they
    // overlap, which extends the measured range by decades without measuring
    // for decades. The shift factors are the OUTPUT as much as the curve is:
    // if they do not vary smoothly with temperature, superposition does not
    // hold for this material and the master curve is an artefact.
    if(in.engine==QLatin1String("Master Curve (TTS)")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=3){
            const QVector<double>& frequency=in.series.at(0).y;
            const QVector<double>& modulus=in.series.at(1).y;
            const QVector<double>& temperature=in.series.at(2).y;
            const int n=qMin(frequency.size(),qMin(modulus.size(),temperature.size()));
            // THE TEMPERATURE COLUMN IS A SET OF ISOTHERMS, not a measurement.
            //
            // Every distinct temperature becomes its own isotherm, and the
            // assembly below shifts each one against everything placed so far
            // with a coarse-then-fine search. On a continuous column - a
            // temperature logged alongside the sweep rather than a list of the
            // temperatures the sweep was run AT - that is 24,000 isotherms of
            // one point each, and the assembly is quadratic in their number:
            // 19.9 seconds for a figure that cannot mean anything, because an
            // isotherm of one point has no overlap to be shifted against.
            //
            // Forty is already far more than any real time-temperature
            // superposition, and the count stops as soon as it is exceeded.
            // Same shape of guard as the mosaic plot's, and for the same
            // reason: the work has to be refused before it is done.
            constexpr int kMaxIsotherms=40;
            {
                QSet<double> levels;
                for(int i=0;i<n&&i<temperature.size();++i){
                    if(!finite(temperature[i])) continue;
                    levels.insert(temperature[i]);
                    if(levels.size()>kMaxIsotherms) return out;
                }
            }
            QMap<double,QVector<QPair<double,double>>> isotherms; // T -> (log f, log G)
            for(int i=0;i<n;++i){
                if(!finite(frequency[i])||!finite(modulus[i])||!finite(temperature[i])) continue;
                if(!(frequency[i]>0.0)||!(modulus[i]>0.0)) continue;
                isotherms[temperature[i]].append(
                    qMakePair(std::log10(frequency[i]),std::log10(modulus[i])));
            }
            if(isotherms.size()>=1){
                QList<double> temperatures=isotherms.keys();
                std::sort(temperatures.begin(),temperatures.end());
                for(double t:temperatures){
                    QVector<QPair<double,double>>& rows=isotherms[t];
                    std::sort(rows.begin(),rows.end(),
                              [](const QPair<double,double>& a,const QPair<double,double>& b){
                                  return a.first<b.first; });
                }
                // The reference is the middle isotherm, so the shifts run both
                // ways and no single one carries the whole range.
                const int refAt=temperatures.size()/2;
                const double reference=temperatures[refAt];
                // Assembled outward from the reference, each isotherm shifted
                // against everything placed so far. Shifting each one against
                // the reference alone fails as soon as two isotherms do not
                // overlap it, which is the usual case at the ends.
                QVector<QPair<double,double>> placed=isotherms[reference];
                QMap<double,double> shift;
                QSet<double> undetermined;
                shift[reference]=0.0;
                QVector<int> order;
                for(int step=1;step<=temperatures.size();++step){
                    if(refAt-step>=0) order.append(refAt-step);
                    if(refAt+step<temperatures.size()) order.append(refAt+step);
                }
                for(int at:order){
                    const double t=temperatures[at];
                    const QVector<QPair<double,double>>& rows=isotherms[t];
                    if(rows.isEmpty()) continue;
                    // A coarse-then-fine search over the shift, scoring by how
                    // well the isotherm's modulus values are reproduced by the
                    // assembled curve where the two overlap in MODULUS. A
                    // score over the x overlap alone would be minimised by
                    // sliding the isotherm off the end entirely.
                    auto score=[&](double a){
                        double sum=0.0; int used=0;
                        for(const QPair<double,double>& r:rows){
                            const double x=r.first+a;
                            // Linear interpolation into the assembled curve.
                            if(placed.size()<2) break;
                            if(x<placed.first().first||x>placed.last().first) continue;
                            int hi=1;
                            while(hi<placed.size()&&placed[hi].first<x) ++hi;
                            hi=qBound(1,hi,placed.size()-1);
                            const double x0=placed[hi-1].first,x1=placed[hi].first;
                            const double y0=placed[hi-1].second,y1=placed[hi].second;
                            const double f=(x1>x0)?(x-x0)/(x1-x0):0.0;
                            const double d=r.second-(y0+f*(y1-y0));
                            sum+=d*d; ++used;
                        }
                        // A shift is only determined where the isotherms
                        // genuinely overlap. Accepting two points was the first
                        // attempt, and it slid every isotherm to the edge of
                        // the assembled curve: two points always overlap
                        // somewhere, so the score was minimised by moving as
                        // far as the rule allowed rather than by superposing.
                        const int needed=qMax(3,int(rows.size())/4);
                        return (used>=needed)?sum/double(used)
                                             :std::numeric_limits<double>::infinity();
                    };
                    double bestShift=0.0,bestScore=std::numeric_limits<double>::infinity();
                    for(int k=-120;k<=120;++k){
                        const double a=double(k)*0.1;
                        const double s=score(a);
                        if(s<bestScore){ bestScore=s; bestShift=a; }
                    }
                    for(int k=-10;k<=10;++k){
                        const double a=bestShift+double(k)*0.01;
                        const double s=score(a);
                        if(s<bestScore){ bestScore=s; bestShift=a; }
                    }
                    // No shift superposed it: say so rather than drawing an
                    // arbitrary one as though it had been determined.
                    const bool determined=finite(bestScore);
                    if(!determined) bestShift=0.0;
                    shift[t]=bestShift;
                    if(!determined) undetermined.insert(t);
                    for(const QPair<double,double>& r:rows)
                        placed.append(qMakePair(r.first+bestShift,r.second));
                    std::sort(placed.begin(),placed.end(),
                              [](const QPair<double,double>& a,const QPair<double,double>& b){
                                  return a.first<b.first; });
                }
                int index=0;
                for(double t:temperatures){
                    const QVector<QPair<double,double>>& rows=isotherms[t];
                    PlotSeries s;
                    s.label=undetermined.contains(t)
                        ? QStringLiteral("T %1, no overlap - not shifted").arg(t,0,'g',4)
                        : QStringLiteral("T %1, log aT %2")
                              .arg(t,0,'g',4).arg(shift.value(t,0.0),0,'f',2);
                    s.color=categoryColour(in,index,std::fmod(0.02+double(index)*0.13,1.0),0.6,0.95);
                    s.lineWidth=qMax(1.1,in.style.lineWidth*0.9);
                    s.drawMarkers=true; s.markerSize=3.2;
                    for(const QPair<double,double>& r:rows){
                        s.x.append(std::pow(10.0,r.first+shift.value(t,0.0)));
                        s.y.append(std::pow(10.0,r.second));
                    }
                    if(!s.x.isEmpty()) out.series.append(s);
                    ++index;
                }
                out.title=in.title.isEmpty()
                    ? QStringLiteral("Master curve, reference T %1").arg(reference,0,'g',4)
                    : in.title;
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("reduced frequency"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("modulus"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Rheology Flow Curve
    // Shear stress against shear rate, with Herschel-Bulkley fitted. The three
    // parameters answer three separate questions - does it have a yield stress,
    // how thick is it, and does it thin or thicken with shearing - and a
    // power law fitted without the yield term answers the third one wrongly
    // whenever the first is yes.
    if(in.engine==QLatin1String("Rheology Flow Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& rate=in.series.at(0).y;
            const QVector<double>& stress=in.series.at(1).y;
            const int n=qMin(rate.size(),stress.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(rate[i])||!finite(stress[i])) continue;
                if(!(rate[i]>0.0)||!(stress[i]>0.0)) continue;
                rows.append(qMakePair(rate[i],stress[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=4){
                PlotSeries pts;
                pts.label=QStringLiteral("measured");
                pts.color=in.series.at(1).color;
                pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
                double minStress=std::numeric_limits<double>::infinity();
                for(const QPair<double,double>& r:rows){
                    pts.x.append(r.first); pts.y.append(r.second);
                    minStress=qMin(minStress,r.second);
                }
                out.series.append(pts);
                // Herschel-Bulkley is linear in log space ONCE the yield
                // stress is removed, so the yield stress is the only thing
                // searched over and everything else is a least-squares fit.
                double bestYield=0.0,bestK=0.0,bestN=1.0,bestR2=-1e300;
                bool haveFit=false;
                // Two passes. One pass at a fixed step either stops short of
                // the answer or spends a linear fit on every thousandth of the
                // range; the first attempt capped at 90% of the smallest
                // measured stress and reported 4.95 for a material whose yield
                // stress was 5.00 - the cap, not the fit.
                double coarseBest=0.0;
                for(int pass=0;pass<2;++pass)
                for(int k=0;k<=(pass==0?98:80);++k){
                    const double yield=(pass==0)
                        ? minStress*(double(k)/100.0)
                        : qBound(0.0,coarseBest+minStress*(double(k)-40.0)/4000.0,
                                 minStress*0.999);
                    QVector<double> lx,ly;
                    for(const QPair<double,double>& r:rows){
                        const double excess=r.second-yield;
                        if(!(excess>0.0)) continue;
                        lx.append(std::log(r.first));
                        ly.append(std::log(excess));
                    }
                    if(lx.size()<3) continue;
                    const LineFit f=fitLine(lx,ly);
                    if(!f.ok) continue;
                    double ssRes=0.0,ssTot=0.0,mean=0.0;
                    for(double v:ly) mean+=v;
                    mean/=double(ly.size());
                    for(int i=0;i<lx.size();++i){
                        const double res=ly[i]-(f.intercept+f.slope*lx[i]);
                        ssRes+=res*res; ssTot+=(ly[i]-mean)*(ly[i]-mean);
                    }
                    const double r2=(ssTot>1e-300)?1.0-ssRes/ssTot:-1e300;
                    if(r2>bestR2){
                        bestR2=r2; bestYield=yield; bestN=f.slope;
                        bestK=std::exp(f.intercept); haveFit=true;
                    }
                    if(pass==0&&r2>=bestR2) coarseBest=bestYield;
                }
                if(haveFit){
                    PlotSeries model;
                    model.label=QStringLiteral("HB: t0 %1, K %2, n %3 (%4), R2 %5")
                                    .arg(bestYield,0,'g',4).arg(bestK,0,'g',4)
                                    .arg(bestN,0,'f',3)
                                    .arg(bestN<1.0?QStringLiteral("shear thinning")
                                                  :(bestN>1.0?QStringLiteral("shear thickening")
                                                             :QStringLiteral("Bingham")))
                                    .arg(bestR2,0,'f',4);
                    model.color=in.style.warning;
                    model.lineWidth=qMax(1.3,in.style.lineWidth);
                    const double lo=rows.first().first,hi=rows.last().first;
                    for(int i=0;i<=120;++i){
                        const double g=lo*std::pow(hi/qMax(1e-300,lo),double(i)/120.0);
                        model.x.append(g);
                        model.y.append(bestYield+bestK*std::pow(g,bestN));
                    }
                    out.series.append(model);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("shear rate"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("shear stress"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Octave Band Spectrum
    // Narrowband levels summed into standard third-octave bands, with the
    // A-weighted levels over them. Summing is done in ENERGY and not in
    // decibels, because decibels do not add - averaging them underestimates
    // every band that contains a tone.
    if(in.engine==QLatin1String("Octave Band Spectrum")){
        PlotSpec out=derivedAs(in,QStringLiteral("Stem"));
        if(in.series.size()>=2){
            const QVector<double>& frequency=in.series.at(0).y;
            const QVector<double>& level=in.series.at(1).y;
            const int n=qMin(frequency.size(),level.size());
            // Third-octave centres, 25 Hz to 20 kHz: the ISO preferred series
            // generated from 1000 Hz by the cube root of two rather than typed
            // out, so no centre can be mistyped.
            QVector<double> centres;
            for(int k=-16;k<=13;++k) centres.append(1000.0*std::pow(2.0,double(k)/3.0));
            QVector<double> energy(centres.size(),0.0);
            QVector<int> count(centres.size(),0);
            const double edge=std::pow(2.0,1.0/6.0); // half a third-octave
            for(int i=0;i<n;++i){
                if(!finite(frequency[i])||!finite(level[i])||!(frequency[i]>0.0)) continue;
                for(int b=0;b<centres.size();++b){
                    if(frequency[i]<centres[b]/edge||frequency[i]>=centres[b]*edge) continue;
                    energy[b]+=std::pow(10.0,level[i]/10.0);
                    count[b]+=1;
                    break;
                }
            }
            // A-weighting, IEC 61672. Written out rather than tabulated so it
            // is exact at every centre instead of interpolated between nine.
            auto aWeight=[](double f){
                const double f2=f*f;
                const double num=12194.0*12194.0*f2*f2;
                const double den=(f2+20.6*20.6)
                                *std::sqrt((f2+107.7*107.7)*(f2+737.9*737.9))
                                *(f2+12194.0*12194.0);
                return (den>1e-300)?20.0*std::log10(num/den)+2.00:-1e300;
            };
            PlotSeries bands;
            bands.label=QStringLiteral("band level (dB)");
            bands.color=in.series.at(1).color;
            PlotSeries weighted;
            weighted.label=QStringLiteral("A-weighted (dBA)");
            weighted.color=in.style.warning;
            weighted.lineWidth=qMax(1.3,in.style.lineWidth);
            weighted.drawMarkers=true; weighted.markerSize=3.4;
            double totalA=0.0;
            for(int b=0;b<centres.size();++b){
                if(count[b]==0) continue;
                const double db=10.0*std::log10(qMax(1e-300,energy[b]));
                const double dba=db+aWeight(centres[b]);
                bands.x.append(centres[b]); bands.y.append(db);
                weighted.x.append(centres[b]); weighted.y.append(dba);
                totalA+=std::pow(10.0,dba/10.0);
            }
            if(!bands.x.isEmpty()){
                weighted.label=QStringLiteral("A-weighted, overall %1 dBA")
                                   .arg(10.0*std::log10(qMax(1e-300,totalA)),0,'f',1);
                out.series.append(bands);
                out.series.append(weighted);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("band centre (Hz)"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("level (dB)"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------ Pharmacokinetic Profile
    // Concentration against time after a dose, with the four numbers a
    // profile is reported as: the peak and when it happened, the area under
    // the curve, and the terminal half-life. All four are computed from the
    // same points the curve is drawn from, so the figure and the table cannot
    // disagree.
    if(in.engine==QLatin1String("Pharmacokinetic Profile")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& time=in.series.at(0).y;
            const QVector<double>& conc=in.series.at(1).y;
            const int n=qMin(time.size(),conc.size());
            QVector<QPair<double,double>> rows;
            for(int i=0;i<n;++i){
                if(!finite(time[i])||!finite(conc[i])) continue;
                rows.append(qMakePair(time[i],conc[i]));
            }
            std::sort(rows.begin(),rows.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            if(rows.size()>=3){
                int peakAt=0;
                double area=0.0;
                PlotSeries curve;
                curve.color=in.series.at(1).color;
                curve.lineWidth=qMax(1.3,in.style.lineWidth);
                curve.drawMarkers=true; curve.markerSize=3.6;
                for(int i=0;i<rows.size();++i){
                    curve.x.append(rows[i].first); curve.y.append(rows[i].second);
                    if(rows[i].second>rows[peakAt].second) peakAt=i;
                    if(i>0)
                        area+=0.5*(rows[i].second+rows[i-1].second)
                                 *(rows[i].first-rows[i-1].first);
                }
                // The terminal phase is log-linear, so the half-life comes
                // from a fit to ln C over the points AFTER the peak - fitting
                // the whole profile would mix absorption into elimination and
                // report a half-life that is neither.
                QVector<double> lt,lc;
                for(int i=peakAt+1;i<rows.size();++i){
                    if(!(rows[i].second>0.0)) continue;
                    lt.append(rows[i].first); lc.append(std::log(rows[i].second));
                }
                double halfLife=std::numeric_limits<double>::quiet_NaN();
                double extrapolated=area;
                if(lt.size()>=3){
                    const LineFit f=fitLine(lt,lc);
                    if(f.ok&&f.slope<0.0){
                        halfLife=std::log(2.0)/(-f.slope);
                        extrapolated=area+rows.last().second/(-f.slope);
                        PlotSeries terminal;
                        terminal.label=QStringLiteral("terminal phase, t1/2 %1")
                                           .arg(halfLife,0,'g',4);
                        terminal.color=in.style.warning;
                        terminal.lineWidth=qMax(1.1,in.style.lineWidth*0.9);
                        terminal.dashPattern={5,4};
                        terminal.x={lt.first(),lt.last()};
                        terminal.y={std::exp(f.intercept+f.slope*lt.first()),
                                    std::exp(f.intercept+f.slope*lt.last())};
                        out.series.append(terminal);
                    }
                }
                curve.label=finite(halfLife)
                    ? QStringLiteral("Cmax %1 at %2, AUC(last) %3, AUC(inf) %4")
                          .arg(rows[peakAt].second,0,'g',4).arg(rows[peakAt].first,0,'g',4)
                          .arg(area,0,'g',4).arg(extrapolated,0,'g',4)
                    : QStringLiteral("Cmax %1 at %2, AUC(last) %3")
                          .arg(rows[peakAt].second,0,'g',4).arg(rows[peakAt].first,0,'g',4)
                          .arg(area,0,'g',4);
                out.series.prepend(curve);
                PlotSeries peak;
                peak.label=QStringLiteral("Cmax");
                peak.color=in.style.danger;
                peak.drawLine=false; peak.drawMarkers=true; peak.markerSize=6.0;
                peak.x={rows[peakAt].first}; peak.y={rows[peakAt].second};
                out.series.append(peak);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("time since dose"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("concentration"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------------ Yield Curve
    // Yield against maturity, one curve per observation date. What is being
    // looked for is the SHAPE - a curve that slopes down instead of up is an
    // inversion, and that is a statement about expectations rather than about
    // any single yield - so the slope from the shortest to the longest
    // maturity is computed and said outright.
    if(in.engine==QLatin1String("Yield Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=3){
            const QVector<double>& maturity=in.series.at(0).y;
            const QVector<double>& yield=in.series.at(1).y;
            const QVector<double>& date=in.series.at(2).y;
            const int m=qMin(maturity.size(),qMin(yield.size(),date.size()));
            QMap<double,QVector<QPair<double,double>>> byDate;
            for(int i=0;i<m;++i){
                if(!finite(maturity[i])||!finite(yield[i])||!finite(date[i])) continue;
                byDate[date[i]].append(qMakePair(maturity[i],yield[i]));
            }
            int index=0;
            for(auto it=byDate.constBegin();it!=byDate.constEnd();++it,++index){
                QVector<QPair<double,double>> rows=it.value();
                std::sort(rows.begin(),rows.end(),
                          [](const QPair<double,double>& a,const QPair<double,double>& b){
                              return a.first<b.first; });
                if(rows.isEmpty()) continue;
                PlotSeries s;
                s.color=categoryColour(in,index,std::fmod(0.33+double(index)*0.11,1.0),0.6,0.95);
                s.lineWidth=qMax(1.3,in.style.lineWidth);
                s.drawMarkers=true; s.markerSize=3.4;
                for(const QPair<double,double>& r:rows){ s.x.append(r.first); s.y.append(r.second); }
                const double spread=rows.last().second-rows.first().second;
                s.label=QStringLiteral("%1, spread %2%3")
                            .arg(QString::number(it.key(),'g',12)).arg(spread,0,'f',3)
                            .arg(spread<0.0?QStringLiteral(" (inverted)"):QString());
                out.series.append(s);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("maturity"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("yield"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Nyquist Stability
    // The open-loop frequency response in the complex plane, against the
    // critical point at minus one. This is NOT the impedance Nyquist plot the
    // electrochemistry family draws: the geometry is the same and the question
    // is completely different, so the critical point, the unit circle and the
    // two stability margins are the reason this engine exists separately.
    if(in.engine==QLatin1String("Nyquist Stability Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& re=in.series.at(0).y;
            const QVector<double>& im=in.series.at(1).y;
            const int n=qMin(re.size(),im.size());
            PlotSeries locus;
            locus.label=QStringLiteral("open loop");
            locus.color=in.series.at(1).color;
            locus.lineWidth=qMax(1.3,in.style.lineWidth);
            PlotSeries mirror;
            mirror.label=QStringLiteral("negative frequencies");
            mirror.color=in.series.at(1).color;
            mirror.lineWidth=qMax(1.0,in.style.lineWidth*0.8);
            mirror.dashPattern={5,4};
            double gainMargin=std::numeric_limits<double>::quiet_NaN();
            double phaseMargin=std::numeric_limits<double>::quiet_NaN();
            for(int i=0;i<n;++i){
                if(!finite(re[i])||!finite(im[i])) continue;
                locus.x.append(re[i]); locus.y.append(im[i]);
                mirror.x.append(re[i]); mirror.y.append(-im[i]);
                if(i==0) continue;
                if(!finite(re[i-1])||!finite(im[i-1])) continue;
                // Gain margin where the locus crosses the negative real axis,
                // interpolated rather than taken at the nearer sample: the
                // crossing almost never lands on one.
                if((im[i-1]<0.0)!=(im[i]<0.0)&&im[i]!=im[i-1]){
                    const double f=(0.0-im[i-1])/(im[i]-im[i-1]);
                    const double crossing=re[i-1]+f*(re[i]-re[i-1]);
                    if(crossing<0.0&&!finite(gainMargin))
                        gainMargin=20.0*std::log10(1.0/qMax(1e-300,-crossing));
                }
                // Phase margin where the magnitude passes one.
                const double before=std::hypot(re[i-1],im[i-1]);
                const double now=std::hypot(re[i],im[i]);
                if((before<1.0)!=(now<1.0)&&now!=before&&!finite(phaseMargin)){
                    const double f=(1.0-before)/(now-before);
                    const double x=re[i-1]+f*(re[i]-re[i-1]);
                    const double y=im[i-1]+f*(im[i]-im[i-1]);
                    phaseMargin=180.0+std::atan2(y,x)*180.0/M_PI;
                }
            }
            if(!locus.x.isEmpty()){
                locus.label=QStringLiteral("open loop%1%2")
                    .arg(finite(gainMargin)?QStringLiteral(", GM %1 dB").arg(gainMargin,0,'f',2)
                                           :QString())
                    .arg(finite(phaseMargin)?QStringLiteral(", PM %1 deg").arg(phaseMargin,0,'f',1)
                                            :QString());
                out.series.append(locus);
                out.series.append(mirror);
                PlotSeries circle;
                circle.label=QStringLiteral("unit circle");
                circle.color=in.style.gridColor;
                circle.lineWidth=qMax(0.8,in.style.lineWidth*0.7);
                circle.dashPattern={2,3};
                for(int k=0;k<=180;++k){
                    const double a=2.0*M_PI*double(k)/180.0;
                    circle.x.append(std::cos(a)); circle.y.append(std::sin(a));
                }
                out.series.append(circle);
                PlotSeries critical;
                critical.label=QStringLiteral("critical point -1");
                critical.color=in.style.danger;
                critical.drawLine=false; critical.drawMarkers=true; critical.markerSize=7.0;
                critical.x={-1.0}; critical.y={0.0};
                out.series.append(critical);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("real"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("imaginary"),false,unsetValue(),unsetValue()};
        // The UNIT CIRCLE is drawn on this one and the distance from the
        // critical point at -1 is the stability margin, so both have to be
        // measured the same way on both axes. Fitted to its own range on each,
        // the unit circle came out as a flat ellipse. See
        // PlotSpec::equalAspect.
        out.equalAspect=true;
        return out;
    }

    // ====================================================================
    // Batch 8: geotechnics, flight performance, process integration,
    // metrology, epidemiology and remote sensing.

    // ------------------------------------------------- Proctor Compaction
    // Dry density against moisture content. The curve has a maximum because
    // water lubricates the particles up to a point and then occupies the space
    // they would otherwise fill, and the two numbers a compaction test exists
    // to produce are that maximum and the moisture it happens at.
    return std::nullopt;
}

} // namespace graphvis
