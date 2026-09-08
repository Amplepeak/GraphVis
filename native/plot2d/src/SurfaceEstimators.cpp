#include "SurfaceEstimators.h"

#include <QHash>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <limits>

namespace graphvis {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

inline bool finite(double v){ return v==v && v>-1e308 && v<1e308; }

// A grid of cells holding point indices, so a local estimator asks its
// neighbourhood rather than the whole sample.
//
// Without this every local method is O(nodes x points): a 160x160 grid over a
// ten-thousand-row sweep is 256 million distance evaluations per redraw, and
// the redraw happens on every drag. With it the same query touches the ring of
// cells that can hold the nearest k, which for an even sample is a few dozen
// points.
class PointIndex {
public:
    PointIndex(const QVector<ScatterPoint>& pts,double xLo,double xHi,
               double yLo,double yHi)
        : points_(pts),xLo_(xLo),yLo_(yLo)
    {
        const int n=pts.size();
        // About four points a cell, which is the usual compromise: fewer and
        // the ring search walks a lot of empty cells, more and each cell visit
        // costs too much.
        side_=qBound(1,int(std::sqrt(double(qMax(1,n))/4.0)),256);
        dx_=(xHi>xLo)?(xHi-xLo)/side_:1.0;
        dy_=(yHi>yLo)?(yHi-yLo)/side_:1.0;
        cells_.resize(side_*side_);
        for(int i=0;i<n;++i){
            const int cx=cellOf(pts[i].x,xLo_,dx_);
            const int cy=cellOf(pts[i].y,yLo_,dy_);
            cells_[cy*side_+cx].append(i);
        }
    }

    // The indices within `rings` cells of (x,y), growing the ring until at
    // least `want` have been found or the whole grid has been walked.
    void near(double x,double y,int want,QVector<int>& out) const {
        out.clear();
        const int cx=cellOf(x,xLo_,dx_), cy=cellOf(y,yLo_,dy_);
        for(int r=0;r<side_;++r){
            const int x0=qMax(0,cx-r), x1=qMin(side_-1,cx+r);
            const int y0=qMax(0,cy-r), y1=qMin(side_-1,cy+r);
            for(int gy=y0;gy<=y1;++gy)
                for(int gx=x0;gx<=x1;++gx){
                    // Only the new ring, not the whole square again.
                    if(r>0&&gx>cx-r&&gx<cx+r&&gy>cy-r&&gy<cy+r) continue;
                    out.append(cells_[gy*side_+gx]);
                }
            if(out.size()>=want&&r>0) return;
            if(x0==0&&y0==0&&x1==side_-1&&y1==side_-1) return;
        }
    }

private:
    int cellOf(double v,double lo,double step) const {
        return qBound(0,int((v-lo)/step),side_-1);
    }
    const QVector<ScatterPoint>& points_;
    double xLo_,yLo_,dx_,dy_;
    int side_=1;
    QVector<QVector<int>> cells_;
};

// Gauss elimination with partial pivoting, for the RBF and kriging solves.
// Small dense systems only - the callers cap the sample they solve over.
bool solveInPlace(QVector<double>& a,QVector<double>& b,int n){
    for(int col=0;col<n;++col){
        int pivot=col;
        double best=std::abs(a[col*n+col]);
        for(int r=col+1;r<n;++r){
            const double v=std::abs(a[r*n+col]);
            if(v>best){ best=v; pivot=r; }
        }
        if(best<1e-300) return false;      // singular; the caller falls back
        if(pivot!=col){
            for(int k=0;k<n;++k) std::swap(a[col*n+k],a[pivot*n+k]);
            std::swap(b[col],b[pivot]);
        }
        const double d=a[col*n+col];
        for(int r=col+1;r<n;++r){
            const double f=a[r*n+col]/d;
            if(f==0.0) continue;
            for(int k=col;k<n;++k) a[r*n+k]-=f*a[col*n+k];
            b[r]-=f*b[col];
        }
    }
    for(int r=n-1;r>=0;--r){
        double s=b[r];
        for(int k=r+1;k<n;++k) s-=a[r*n+k]*b[k];
        b[r]=s/a[r*n+r];
    }
    return true;
}

double cross(const ScatterPoint& o,const ScatterPoint& a,const ScatterPoint& b){
    return (a.x-o.x)*(b.y-o.y)-(a.y-o.y)*(b.x-o.x);
}

} // namespace

// ---------------------------------------------------------------------------
// Names. GraphVis 17's exactly, because a person's habits and a saved figure
// both travel through these strings.

QStringList estimatorNames(){
    return {QStringLiteral("Auto (data-aware)"),
            QStringLiteral("Nearest Neighbor"),
            QStringLiteral("Bilinear Interpolation"),
            QStringLiteral("Bicubic Interpolation"),
            QStringLiteral("Rectangular B-Spline"),
            QStringLiteral("Monotone PCHIP (Structured)"),
            QStringLiteral("Delaunay Triangulation (Linear)"),
            QStringLiteral("Clough-Tocher C1 Smooth"),
            QStringLiteral("Natural Neighbor (Sibson's)"),
            QStringLiteral("Inverse Distance Weighting (IDW)"),
            QStringLiteral("Modified Shepard's Method"),
            QStringLiteral("Thin Plate Spline (TPS)"),
            QStringLiteral("Multiquadric RBF"),
            QStringLiteral("Gaussian RBF"),
            QStringLiteral("Ordinary Kriging"),
            QStringLiteral("Moving Least Squares (MLS)"),
            QStringLiteral("LOESS / LOWESS")};
}

bool estimatorImplemented(Estimator e){
    switch(e){
    case Estimator::Auto:
    case Estimator::NearestNeighbour:
    case Estimator::DelaunayLinear:
    case Estimator::InverseDistance:
    case Estimator::ModifiedShepard:
    case Estimator::ThinPlateSpline:
    case Estimator::Multiquadric:
    case Estimator::GaussianRbf:
        return true;
    // Not yet ported. Each needs real machinery of its own rather than a
    // parameter on something already here: Clough-Tocher needs estimated
    // vertex gradients and a cubic Bezier patch per triangle, natural
    // neighbour needs Voronoi areas recomputed per query, kriging needs a
    // fitted variogram and a solve per neighbourhood, and the four structured
    // methods only apply when the samples already lie on a complete rectangle,
    // which needs detecting first.
    default:
        return false;
    }
}

QStringList estimatorGroupNames(){
    return {QStringLiteral("Recommended"),
            QStringLiteral("Structured matrix methods"),
            QStringLiteral("Structured matrix methods"),
            QStringLiteral("Structured matrix methods"),
            QStringLiteral("Structured matrix methods"),
            QStringLiteral("Structured matrix methods"),
            QStringLiteral("Unstructured scattered estimators"),
            QStringLiteral("Unstructured scattered estimators"),
            QStringLiteral("Unstructured scattered estimators"),
            QStringLiteral("Unstructured scattered estimators"),
            QStringLiteral("Unstructured scattered estimators"),
            QStringLiteral("Radial basis functions"),
            QStringLiteral("Radial basis functions"),
            QStringLiteral("Radial basis functions"),
            QStringLiteral("Statistical and local regression"),
            QStringLiteral("Statistical and local regression"),
            QStringLiteral("Statistical and local regression")};
}

QStringList extrapolationNames(){
    return {QStringLiteral("Mask outside convex hull"),
            QStringLiteral("Nearest fill outside hull"),
            QStringLiteral("IDW full-domain extension"),
            QStringLiteral("Linear + nearest full rectangle"),
            QStringLiteral("Edge-clamped full rectangle")};
}

QStringList valuePolicyNames(){
    return {QStringLiteral("Allow estimator overshoot"),
            QStringLiteral("Clamp to observed response range")};
}

QStringList responseSpaceNames(){
    return {QStringLiteral("Linear values"),QStringLiteral("Log10 values")};
}

// ---------------------------------------------------------------------------
// Geometry.

QVector<int> convexHull(const QVector<ScatterPoint>& points){
    const int n=points.size();
    QVector<int> order;
    order.reserve(n);
    for(int i=0;i<n;++i)
        if(finite(points[i].x)&&finite(points[i].y)) order.append(i);
    if(order.size()<3) return order;
    std::sort(order.begin(),order.end(),[&points](int a,int b){
        if(points[a].x!=points[b].x) return points[a].x<points[b].x;
        return points[a].y<points[b].y;
    });

    QVector<int> hull(2*order.size());
    int k=0;
    for(int i=0;i<order.size();++i){
        while(k>=2&&cross(points[hull[k-2]],points[hull[k-1]],points[order[i]])<=0) --k;
        hull[k++]=order[i];
    }
    const int lower=k+1;
    for(int i=order.size()-2;i>=0;--i){
        while(k>=lower&&cross(points[hull[k-2]],points[hull[k-1]],points[order[i]])<=0) --k;
        hull[k++]=order[i];
    }
    hull.resize(qMax(0,k-1));
    return hull;
}

bool insideHull(const QVector<ScatterPoint>& points,const QVector<int>& hull,
                double x,double y){
    const int n=hull.size();
    if(n<3) return false;
    // Anticlockwise hull: inside means never to the right of an edge. The
    // tolerance is relative to the edge length, so a point ON the boundary
    // counts as inside - a measurement at the edge of its own sweep must not
    // be masked as an extrapolation of itself.
    for(int i=0;i<n;++i){
        const ScatterPoint& a=points[hull[i]];
        const ScatterPoint& b=points[hull[(i+1)%n]];
        const double side=(b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);
        const double scale=std::hypot(b.x-a.x,b.y-a.y);
        if(side< -1e-9*qMax(1.0,scale)) return false;
    }
    return true;
}

QVector<Triangle> delaunay(const QVector<ScatterPoint>& points){
    // Bowyer-Watson over a super-triangle. Fine for the sample sizes a figure
    // is drawn from - this is O(n^1.5) in practice and the caller caps n.
    QVector<Triangle> out;
    const int n=points.size();
    if(n<3) return out;

    double xLo=points[0].x,xHi=xLo,yLo=points[0].y,yHi=yLo;
    for(const ScatterPoint& p:points){
        if(!finite(p.x)||!finite(p.y)) continue;
        xLo=qMin(xLo,p.x); xHi=qMax(xHi,p.x);
        yLo=qMin(yLo,p.y); yHi=qMax(yHi,p.y);
    }
    const double dx=qMax(1e-12,xHi-xLo), dy=qMax(1e-12,yHi-yLo);
    const double mx=(xLo+xHi)/2.0, my=(yLo+yHi)/2.0;
    const double big=20.0*qMax(dx,dy);

    // The super-triangle's vertices live past the end of `points`, and are
    // dropped at the end along with every triangle touching one.
    QVector<ScatterPoint> work=points;
    work.append({mx-big,my-big,0.0});
    work.append({mx+big,my-big,0.0});
    work.append({mx,my+big,0.0});
    const int s0=n,s1=n+1,s2=n+2;

    QVector<Triangle> tris;
    tris.append({s0,s1,s2});

    const auto inCircle=[&work](const Triangle& t,const ScatterPoint& p){
        const double ax=work[t.a].x-p.x, ay=work[t.a].y-p.y;
        const double bx=work[t.b].x-p.x, by=work[t.b].y-p.y;
        const double cx=work[t.c].x-p.x, cy=work[t.c].y-p.y;
        const double det=(ax*ax+ay*ay)*(bx*cy-cx*by)
                        -(bx*bx+by*by)*(ax*cy-cx*ay)
                        +(cx*cx+cy*cy)*(ax*by-bx*ay);
        // The super-triangle is built anticlockwise and Bowyer-Watson keeps
        // every triangle so; a positive determinant is then "inside".
        return det>0.0;
    };

    QVector<Triangle> bad;
    QVector<QPair<int,int>> edges;
    for(int i=0;i<n;++i){
        const ScatterPoint& p=work[i];
        if(!finite(p.x)||!finite(p.y)) continue;
        bad.clear(); edges.clear();
        for(int t=tris.size()-1;t>=0;--t){
            if(!inCircle(tris[t],p)) continue;
            bad.append(tris[t]);
            tris.remove(t);
        }
        for(const Triangle& t:bad){
            const int e[3][2]={{t.a,t.b},{t.b,t.c},{t.c,t.a}};
            for(int k=0;k<3;++k){
                const int u=qMin(e[k][0],e[k][1]), v=qMax(e[k][0],e[k][1]);
                int at=-1;
                for(int q=0;q<edges.size();++q)
                    if(edges[q].first==u&&edges[q].second==v){ at=q; break; }
                // An edge shared by two removed triangles is interior to the
                // hole and must not become a new triangle's side.
                if(at>=0) edges.remove(at);
                else edges.append({u,v});
            }
        }
        for(const QPair<int,int>& e:edges){
            // Kept anticlockwise, so inCircle's sign test stays valid for the
            // triangles this pass creates.
            Triangle t{e.first,e.second,i};
            if(cross(work[t.a],work[t.b],work[t.c])<0.0) std::swap(t.a,t.b);
            tris.append(t);
        }
    }

    for(const Triangle& t:tris){
        if(t.a>=n||t.b>=n||t.c>=n) continue;   // touches the super-triangle
        out.append(t);
    }
    return out;
}

// ---------------------------------------------------------------------------
// The estimators.

namespace {

struct Workspace {
    const QVector<ScatterPoint>& pts;
    const PointIndex& index;
    const QVector<Triangle>& tris;
    const EstimatorSettings& s;
    double vLo=0, vHi=1;
};

// Barycentric interpolation over the Delaunay triangle containing (x,y).
//
// Exact at every measurement and linear between them, which is the one
// property that makes it the honest default for a scattered sweep: it invents
// no maximum that was not measured, anywhere.
double delaunayLinear(const Workspace& w,double x,double y,bool* hit){
    *hit=false;
    for(const Triangle& t:w.tris){
        const ScatterPoint& a=w.pts[t.a];
        const ScatterPoint& b=w.pts[t.b];
        const ScatterPoint& c=w.pts[t.c];
        const double d=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);
        if(std::abs(d)<1e-300) continue;
        const double l1=((b.y-c.y)*(x-c.x)+(c.x-b.x)*(y-c.y))/d;
        const double l2=((c.y-a.y)*(x-c.x)+(a.x-c.x)*(y-c.y))/d;
        const double l3=1.0-l1-l2;
        const double eps=-1e-9;
        if(l1<eps||l2<eps||l3<eps) continue;
        *hit=true;
        return l1*a.v+l2*b.v+l3*c.v;
    }
    return kNaN;
}

// Shepard's inverse-distance weighting over the k nearest points.
//
// GLOBAL inverse distance is the textbook form and is wrong for a plot: every
// sample influences every node, so a dense cluster at one end pulls the whole
// field towards its mean and the far corner takes the average of the dataset.
// The k-nearest form is what GraphVis 17 used and what is used here.
double inverseDistance(const Workspace& w,double x,double y,QVector<int>& scratch,
                       bool modified){
    w.index.near(x,y,qMax(4,w.s.neighbours),scratch);
    if(scratch.isEmpty()) return kNaN;

    // Sorted by distance, so `neighbours` means the nearest ones rather than
    // whichever the cell walk happened to reach first.
    const double wx=qMax(1e-12,w.s.xWeight), wy=qMax(1e-12,w.s.yWeight);
    QVector<QPair<double,int>> byDist;
    byDist.reserve(scratch.size());
    for(int i:scratch){
        const double ddx=(w.pts[i].x-x)/wx, ddy=(w.pts[i].y-y)/wy;
        byDist.append({ddx*ddx+ddy*ddy,i});
    }
    std::sort(byDist.begin(),byDist.end());
    const int k=qMin(byDist.size(),qMax(4,w.s.neighbours));

    // A node sitting exactly on a measurement takes it, rather than dividing
    // by zero and producing an infinity that colours the whole map.
    if(byDist[0].first<1e-24) return w.pts[byDist[0].second].v;

    const double power=qBound(0.5,w.s.idwPower,8.0);
    double num=0.0,den=0.0;
    // Modified Shepard tapers the weight to zero at the search radius, so a
    // point entering or leaving the neighbourhood does not step the estimate.
    // Plain IDW does not, and the seam shows as a contour that follows the
    // search radius rather than the data.
    const double rSq=byDist[k-1].first;
    const double r=std::sqrt(qMax(rSq,1e-300));
    for(int i=0;i<k;++i){
        const double d=std::sqrt(qMax(byDist[i].first,1e-300));
        double weight;
        if(modified){
            const double taper=qMax(0.0,(r-d))/(r*d);
            weight=taper*taper;
        }else{
            weight=1.0/std::pow(d,power);
        }
        if(!(weight>0.0)) continue;
        num+=weight*w.pts[byDist[i].second].v;
        den+=weight;
    }
    return (den>0.0)?num/den:kNaN;
}

double nearestValue(const Workspace& w,double x,double y,QVector<int>& scratch){
    w.index.near(x,y,1,scratch);
    double best=std::numeric_limits<double>::infinity();
    double v=kNaN;
    const double wx=qMax(1e-12,w.s.xWeight), wy=qMax(1e-12,w.s.yWeight);
    for(int i:scratch){
        const double ddx=(w.pts[i].x-x)/wx, ddy=(w.pts[i].y-y)/wy;
        const double d=ddx*ddx+ddy*ddy;
        if(d<best){ best=d; v=w.pts[i].v; }
    }
    return v;
}

// A radial basis function fit: one dense solve over the whole sample, then
// every node is a weighted sum of kernels.
//
// Capped, and the cap matters. The solve is O(n^3) and the evaluation O(n) per
// node, so ten thousand points is a trillion operations for the solve alone.
// Above the cap the sample is thinned by taking every m-th point in a shuffled
// order rather than the first n, which would take one corner of the sweep.
struct RbfFit {
    QVector<ScatterPoint> centres;
    QVector<double> weights;   // one per centre, plus 3 for the linear tail
    double shape=1.0;
    Estimator kind=Estimator::ThinPlateSpline;
    bool ok=false;
};

double rbfKernel(Estimator kind,double r,double shape){
    switch(kind){
    case Estimator::Multiquadric: return std::sqrt(r*r+shape*shape);
    case Estimator::GaussianRbf:  return std::exp(-(r*r)/(2.0*shape*shape));
    default:                      return (r>1e-300)?r*r*std::log(r):0.0;  // TPS
    }
}

RbfFit fitRbf(const QVector<ScatterPoint>& pts,Estimator kind,double smoothing,
              double xWeight,double yWeight){
    RbfFit fit;
    fit.kind=kind;
    constexpr int kMaxCentres=600;
    const int n=pts.size();
    if(n<3) return fit;

    if(n<=kMaxCentres) fit.centres=pts;
    else {
        // Every m-th point of a deterministically shuffled order: spread over
        // the whole sweep, and the same subset every redraw so the figure does
        // not shimmer.
        QVector<int> order(n);
        for(int i=0;i<n;++i) order[i]=i;
        quint32 seed=2463534242u;
        for(int i=n-1;i>0;--i){
            seed^=seed<<13; seed^=seed>>17; seed^=seed<<5;
            std::swap(order[i],order[int(seed%quint32(i+1))]);
        }
        for(int i=0;i<kMaxCentres;++i) fit.centres.append(pts[order[i]]);
    }

    const int m=fit.centres.size();
    // The shape parameter, from the average spacing. A multiquadric or a
    // Gaussian with a shape unrelated to the data is either a plane or a set
    // of spikes; this is the standard heuristic and it is scale-free.
    double span=0.0;
    for(const ScatterPoint& p:fit.centres)
        for(int k=0;k<4;++k){
            const ScatterPoint& q=fit.centres[(k*37+1)%m];
            span+=std::hypot((p.x-q.x)/qMax(1e-12,xWeight),
                             (p.y-q.y)/qMax(1e-12,yWeight));
        }
    fit.shape=qMax(1e-9,span/(4.0*m));

    // [ A  P ] [w]   [v]
    // [ P' 0 ] [c] = [0]     with the linear tail that makes TPS well posed.
    const int size=m+3;
    QVector<double> A(size*size,0.0), b(size,0.0);
    for(int i=0;i<m;++i){
        for(int j=0;j<m;++j){
            const double r=std::hypot((fit.centres[i].x-fit.centres[j].x)/qMax(1e-12,xWeight),
                                      (fit.centres[i].y-fit.centres[j].y)/qMax(1e-12,yWeight));
            A[i*size+j]=rbfKernel(kind,r,fit.shape);
        }
        A[i*size+i]+=smoothing;          // 0 interpolates exactly
        A[i*size+m]=1.0;
        A[i*size+m+1]=fit.centres[i].x;
        A[i*size+m+2]=fit.centres[i].y;
        A[m*size+i]=1.0;
        A[(m+1)*size+i]=fit.centres[i].x;
        A[(m+2)*size+i]=fit.centres[i].y;
        b[i]=fit.centres[i].v;
    }
    if(!solveInPlace(A,b,size)) return fit;
    fit.weights=b;
    fit.ok=true;
    return fit;
}

double evaluateRbf(const RbfFit& fit,double x,double y,double xWeight,double yWeight){
    if(!fit.ok) return kNaN;
    const int m=fit.centres.size();
    double sum=fit.weights[m]+fit.weights[m+1]*x+fit.weights[m+2]*y;
    for(int i=0;i<m;++i){
        const double r=std::hypot((x-fit.centres[i].x)/qMax(1e-12,xWeight),
                                  (y-fit.centres[i].y)/qMax(1e-12,yWeight));
        sum+=fit.weights[i]*rbfKernel(fit.kind,r,fit.shape);
    }
    return sum;
}

} // namespace

EstimatedField estimateField(const QVector<ScatterPoint>& raw,
                             int nx,int ny,
                             double xLo,double xHi,double yLo,double yHi,
                             const EstimatorSettings& settings){
    EstimatedField field;
    field.nx=nx; field.ny=ny;
    field.xLo=xLo; field.xHi=xHi; field.yLo=yLo; field.yHi=yHi;
    if(nx<2||ny<2) return field;

    // Only the usable measurements, and in the response space asked for.
    QVector<ScatterPoint> pts;
    pts.reserve(raw.size());
    double vLo=std::numeric_limits<double>::infinity(), vHi=-vLo;
    for(const ScatterPoint& p:raw){
        if(!finite(p.x)||!finite(p.y)||!finite(p.v)) continue;
        ScatterPoint q=p;
        if(settings.responseSpace==ResponseSpace::Log10){
            // A non-positive value has no logarithm, and dropping it is the
            // only honest choice: substituting a floor would put a measurement
            // on the map at a value nobody recorded.
            if(!(q.v>0.0)) continue;
            q.v=std::log10(q.v);
        }
        pts.append(q);
        vLo=qMin(vLo,q.v); vHi=qMax(vHi,q.v);
    }
    if(pts.size()<3||!finite(vLo)||!finite(vHi)) return field;

    const QVector<int> hull=convexHull(pts);
    Estimator chosen=settings.estimator;
    // An estimator this build does not compute becomes Auto rather than
    // silently falling through to nearest-neighbour under another name.
    if(!estimatorImplemented(chosen)) chosen=Estimator::Auto;
    if(chosen==Estimator::Auto){
        // The choice a person would make from the same three facts, said out
        // loud rather than left to a comment: too few points for a global
        // solve is a triangulation, a small clean sample earns a thin-plate
        // spline, and a large one gets the local method that stays O(n).
        if(pts.size()<12) chosen=Estimator::NearestNeighbour;
        else if(pts.size()<=400) chosen=Estimator::ThinPlateSpline;
        else chosen=Estimator::DelaunayLinear;
    }

    // The triangulation, only for the estimators that read it. It is the
    // expensive part and three of seventeen methods need it.
    QVector<Triangle> tris;
    const bool needsTriangles=(chosen==Estimator::DelaunayLinear
                             ||chosen==Estimator::CloughTocher
                             ||chosen==Estimator::NaturalNeighbour);
    if(needsTriangles) tris=delaunay(pts);

    RbfFit rbf;
    const bool isRbf=(chosen==Estimator::ThinPlateSpline
                    ||chosen==Estimator::Multiquadric
                    ||chosen==Estimator::GaussianRbf);
    if(isRbf) rbf=fitRbf(pts,chosen,settings.smoothing,settings.xWeight,settings.yWeight);

    const PointIndex index(pts,xLo,xHi,yLo,yHi);
    const Workspace work{pts,index,tris,settings,vLo,vHi};

    field.value.fill(kNaN,nx*ny);
    field.outsideHull.fill(false,nx*ny);
    field.estimated.fill(true,nx*ny);

    QVector<int> scratch;
    for(int gy=0;gy<ny;++gy){
        const double y=yLo+(yHi-yLo)*double(gy)/double(ny-1);
        for(int gx=0;gx<nx;++gx){
            const double x=xLo+(xHi-xLo)*double(gx)/double(nx-1);
            const int k=gy*nx+gx;
            const bool inside=insideHull(pts,hull,x,y);
            field.outsideHull[k]=!inside;

            // Outside the measured region, what happens is a policy rather
            // than an estimate, and it is applied before the estimator runs so
            // that masking costs nothing.
            if(!inside&&settings.extrapolation==Extrapolation::MaskOutsideHull) continue;

            double v=kNaN;
            switch(chosen){
            case Estimator::DelaunayLinear: {
                bool hit=false;
                v=delaunayLinear(work,x,y,&hit);
                if(!hit) v=kNaN;
                break;
            }
            case Estimator::InverseDistance:
                v=inverseDistance(work,x,y,scratch,false); break;
            case Estimator::ModifiedShepard:
                v=inverseDistance(work,x,y,scratch,true); break;
            case Estimator::ThinPlateSpline:
            case Estimator::Multiquadric:
            case Estimator::GaussianRbf:
                v=evaluateRbf(rbf,x,y,settings.xWeight,settings.yWeight); break;
            default:
                v=nearestValue(work,x,y,scratch); break;
            }

            // The policies that DO reach beyond the hull. Nearest is the
            // conservative one - it repeats a measured value rather than
            // inventing a trend - and the rest are the full-domain modes for
            // when the rectangle is the experiment.
            if(!finite(v)){
                switch(settings.extrapolation){
                case Extrapolation::NearestOutsideHull:
                case Extrapolation::EdgeClamped:
                case Extrapolation::LinearThenNearest:
                    v=nearestValue(work,x,y,scratch); break;
                case Extrapolation::IdwFullDomain:
                    v=inverseDistance(work,x,y,scratch,false); break;
                default: break;
                }
            }
            if(!finite(v)) continue;
            if(settings.valuePolicy==ValuePolicy::ClampToObserved)
                v=qBound(vLo,v,vHi);
            field.value[k]=v;
        }
    }

    // Back out of the response space, so the caller and the colour bar are in
    // the units the data came in.
    if(settings.responseSpace==ResponseSpace::Log10)
        for(double& v:field.value) if(finite(v)) v=std::pow(10.0,v);

    field.valid=true;
    return field;
}

} // namespace graphvis
