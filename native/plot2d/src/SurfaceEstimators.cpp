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
    case Estimator::Bilinear:
    case Estimator::Bicubic:
    case Estimator::RectangularBSpline:
    case Estimator::MonotonePchip:
    case Estimator::DelaunayLinear:
    case Estimator::InverseDistance:
    case Estimator::ModifiedShepard:
    case Estimator::ThinPlateSpline:
    case Estimator::Multiquadric:
    case Estimator::GaussianRbf:
    case Estimator::OrdinaryKriging:
    case Estimator::MovingLeastSquares:
    case Estimator::Loess:
        return true;
    // The two that are NOT offered, and why.
    //
    // Clough-Tocher C1 needs estimated vertex gradients and a cubic Bezier
    // patch per triangle - real machinery rather than a parameter on something
    // already here. A cubic through the vertices with estimated gradients is
    // C1 inside each triangle and only C0 across the edges, so shipping that
    // under this name would be naming one estimator and running another.
    //
    // Natural Neighbour (Sibson's) IS implemented below and is deliberately
    // switched off, because it fails the one property that defines it. Sibson
    // coordinates are linearly precise - reproducing a plane exactly is a
    // theorem about them, not a quality target - and the discrete construction
    // here comes back 0.211 off a plane spanning 3.5, and produces a surface
    // 40x ROUGHER than plain linear interpolation on the same data, when it
    // should be smoother. Two explanations were tested and ruled out:
    //
    //   sample budget   320 -> 20,480 samples a node moved the error from
    //                   0.21122 to 0.21051. It does not converge, so it is not
    //                   discretisation.
    //   cell truncation widening the sampled radius from the 6th-nearest site
    //                   to the 24th made it slightly WORSE, 0.211 -> 0.216.
    //
    // So the fault is in the geometry rather than in the budget, and the next
    // attempt should probably compute the cell exactly - clip the half-planes
    // of the k nearest sites to get the query's Voronoi polygon, then intersect
    // it with each neighbour's old cell - rather than sampling it. Left here
    // with the numbers rather than deleted, so that work starts from evidence.
    // estimateField clamps an unimplemented choice to Auto, so nothing below
    // can reach it.
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

QStringList failureFootprintNames(){
    return {QStringLiteral("Sample cell only"),
            QStringLiteral("Conservative local region"),
            QStringLiteral("Voronoi failure region"),
            QStringLiteral("None (fit through failures)")};
}

QStringList bridgingNames(){
    return {QStringLiteral("Preserve all failures"),
            QStringLiteral("Bridge isolated failures"),
            QStringLiteral("Bridge small enclosed holes"),
            QStringLiteral("Bridge all interior holes")};
}

QStringList invalidDisplayNames(){
    return {QStringLiteral("Transparent"),
            QStringLiteral("Fallback colour"),
            QStringLiteral("Nearest Neighbor Fill"),
            QStringLiteral("Local Mean Imputation"),
            QStringLiteral("Baseline Clamp (colour-scale minimum)"),
            QStringLiteral("Symmetric Mirror Fill")};
}

QStringList krigingVariogramNames(){
    return {QStringLiteral("Exponential"),QStringLiteral("Spherical"),
            QStringLiteral("Gaussian")};
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


// ---------------------------------------------------------------------------
// The structured family.
//
// These apply when the measurements ALREADY lie on a complete rectangle, which
// a factorial parameter sweep almost always is: every combination of a set of x
// levels with a set of y levels, run once. The general scattered machinery
// still works on such data and is both slower and worse at it - a Delaunay
// triangulation of a regular lattice is a set of arbitrarily-oriented triangles
// whose diagonals show up as diagonal ridges in the picture, and an RBF solves
// a dense system to reproduce something a tensor product gets exactly.
//
// So the first thing any of them has to do is find out whether the data is on a
// rectangle at all, and say no when it is not rather than pretending.

struct StructuredGrid {
    QVector<double> xs, ys;     // the distinct levels, sorted
    QVector<double> z;          // ys.size() x xs.size(), NaN where a cell is missing
    bool ok=false;
    int missing=0;
};

// Distinct values, to a tolerance that scales with the span.
//
// Exact equality is wrong here: a sweep written out by a solver carries values
// like 0.30000000000000004, and three levels become three hundred. The
// tolerance is a fraction of the smallest gap between genuinely distinct
// levels, found by sorting first.
static QVector<double> distinctLevels(QVector<double> v,double span){
    std::sort(v.begin(),v.end());
    QVector<double> out;
    const double tol=qMax(1e-12,span*1e-9);
    for(double x:v)
        if(out.isEmpty()||x-out.last()>tol) out.append(x);
    return out;
}

static StructuredGrid asStructured(const QVector<ScatterPoint>& pts){
    StructuredGrid g;
    const int n=pts.size();
    if(n<4) return g;

    double xLo=pts[0].x,xHi=xLo,yLo=pts[0].y,yHi=yLo;
    QVector<double> xv,yv;
    xv.reserve(n); yv.reserve(n);
    for(const ScatterPoint& p:pts){
        xv.append(p.x); yv.append(p.y);
        xLo=qMin(xLo,p.x); xHi=qMax(xHi,p.x);
        yLo=qMin(yLo,p.y); yHi=qMax(yHi,p.y);
    }
    g.xs=distinctLevels(xv,xHi-xLo);
    g.ys=distinctLevels(yv,yHi-yLo);
    const qint64 cells=qint64(g.xs.size())*qint64(g.ys.size());
    // A rectangle has as many cells as it has points, give or take the ones a
    // run failed at. More than a quarter empty is not a lattice with holes, it
    // is scattered data that happens to repeat a few coordinates - and treating
    // it as a lattice would be a tensor product over mostly nothing.
    if(g.xs.size()<2||g.ys.size()<2) return g;
    if(cells>4LL*n) return g;

    g.z.fill(kNaN,int(cells));
    const double xTol=qMax(1e-12,(xHi-xLo)*1e-9);
    const double yTol=qMax(1e-12,(yHi-yLo)*1e-9);
    const auto indexOf=[](const QVector<double>& levels,double v,double tol){
        int lo=0,hi=levels.size()-1;
        while(lo<=hi){
            const int mid=(lo+hi)/2;
            if(std::abs(levels[mid]-v)<=tol) return mid;
            if(levels[mid]<v) lo=mid+1; else hi=mid-1;
        }
        return -1;
    };
    for(const ScatterPoint& p:pts){
        const int ix=indexOf(g.xs,p.x,xTol);
        const int iy=indexOf(g.ys,p.y,yTol);
        if(ix<0||iy<0) return g;              // not on the lattice after all
        // A repeated cell takes the mean, as the binned path does. A sweep run
        // twice at one point is two measurements of the same thing.
        double& cell=g.z[iy*g.xs.size()+ix];
        cell=finite(cell)?(cell+p.v)/2.0:p.v;
    }
    for(double v:g.z) if(!finite(v)) ++g.missing;
    if(g.missing*4>g.z.size()) return g;      // too holey to call a lattice
    g.ok=true;
    return g;
}

// Where a query sits in a set of levels: the interval below it and the
// fraction across that interval.
static void locate(const QVector<double>& levels,double v,int* i,double* t){
    const int n=levels.size();
    int lo=0,hi=n-1;
    while(hi-lo>1){
        const int mid=(lo+hi)/2;
        if(levels[mid]<=v) lo=mid; else hi=mid;
    }
    *i=lo;
    const double d=levels[lo+1]-levels[lo];
    *t=(d>1e-300)?(v-levels[lo])/d:0.0;
}

// Fritsch-Carlson slopes: the ONE thing that makes PCHIP worth having.
//
// A natural cubic spline through data that only ever rises will still dip
// between two close points, because it is minimising curvature rather than
// respecting the data's shape. On a concentration or a conversion that dip is a
// value below zero, drawn in the same ink as the measurements. These slopes are
// limited so a monotone run of points produces a monotone curve.
static QVector<double> pchipSlopes(const QVector<double>& x,const QVector<double>& y){
    const int n=x.size();
    QVector<double> m(n,0.0);
    if(n<2) return m;
    QVector<double> d(n-1,0.0);
    for(int i=0;i<n-1;++i){
        const double h=x[i+1]-x[i];
        d[i]=(h>1e-300)?(y[i+1]-y[i])/h:0.0;
    }
    m[0]=d[0];
    m[n-1]=d[n-2];
    for(int i=1;i<n-1;++i){
        if(d[i-1]*d[i]<=0.0){ m[i]=0.0; continue; }   // a turning point stays flat
        const double h0=x[i]-x[i-1], h1=x[i+1]-x[i];
        const double w0=2.0*h1+h0, w1=h1+2.0*h0;
        m[i]=(w0+w1)/(w0/d[i-1]+w1/d[i]);
    }
    return m;
}

static double hermite(double x0,double x1,double y0,double y1,
                      double m0,double m1,double x){
    const double h=x1-x0;
    if(!(h>1e-300)) return y0;
    const double t=(x-x0)/h, t2=t*t, t3=t2*t;
    return (2*t3-3*t2+1)*y0 + (t3-2*t2+t)*h*m0
         + (-2*t3+3*t2)*y1 + (t3-t2)*h*m1;
}

// Catmull-Rom on the ACTUAL level positions rather than on the index.
//
// The textbook form assumes the samples are evenly spaced and takes its
// tangents as (c-a)/2. A parameter sweep is rarely even - decades in one
// factor, a quadratic ramp in another - and on such a lattice the uniform form
// is not even exact on a plane: it was out by 0.93 on a plane over a lattice
// spanning two decades in x, which is a curved surface drawn through flat data.
// Taking the tangent as (y[i+1]-y[i-1])/(x[i+1]-x[i-1]) and interpolating with
// a Hermite between the two bracketing levels costs nothing and is exact on a
// plane whatever the spacing.
static double catmullRomAt(const QVector<double>& xs,const QVector<double>& ys,
                           int i,double x){
    const int n=xs.size();
    const auto X=[&xs,n](int k){ return xs[qBound(0,k,n-1)]; };
    const auto Y=[&ys,n](int k){ return ys[qBound(0,k,n-1)]; };
    const auto slope=[&X,&Y](int k){
        const double h=X(k+1)-X(k-1);
        return (std::abs(h)>1e-300)?(Y(k+1)-Y(k-1))/h:0.0;
    };
    return hermite(X(i),X(i+1),Y(i),Y(i+1),slope(i),slope(i+1),x);
}

// The rectangular spline, knotted on the ACTUAL level positions.
//
// GraphVis 17 used scipy's RectBivariateSpline, whose knots are the
// coordinates. The first version here was a uniform-knot B-spline - knotted on
// the INDEX - which is a different curve: it reproduced its own measurements
// but bent by 1.0 between them on a plane measured over a lattice spanning two
// decades in x, because index-uniform knots say the levels are evenly spaced
// and they are not. That is not a tuning difference, it is a surface curving
// through flat data.
//
// So this is a natural cubic spline on the levels: the standard tridiagonal
// solve for the second derivatives, with M = 0 at both ends. Interpolating,
// C2, and exact on anything linear whatever the spacing.
static QVector<double> splineSecondDerivatives(const QVector<double>& x,
                                               const QVector<double>& y){
    const int n=x.size();
    QVector<double> m(n,0.0);
    if(n<3) return m;
    QVector<double> h(n-1,0.0);
    for(int i=0;i<n-1;++i) h[i]=qMax(1e-300,x[i+1]-x[i]);

    // Thomas over rows 1..n-2; the ends are pinned at zero curvature.
    QVector<double> lower(n,0.0),diag(n,1.0),upper(n,0.0),rhs(n,0.0);
    for(int i=1;i<n-1;++i){
        lower[i]=h[i-1];
        diag[i]=2.0*(h[i-1]+h[i]);
        upper[i]=h[i];
        rhs[i]=6.0*((y[i+1]-y[i])/h[i]-(y[i]-y[i-1])/h[i-1]);
    }
    for(int i=1;i<n;++i){
        const double w=(std::abs(diag[i-1])>1e-300)?lower[i]/diag[i-1]:0.0;
        diag[i]-=w*upper[i-1];
        rhs[i]-=w*rhs[i-1];
    }
    for(int i=n-2;i>=1;--i)
        m[i]=(std::abs(diag[i])>1e-300)?(rhs[i]-upper[i]*m[i+1])/diag[i]:0.0;
    return m;
}

static double splineAt(const QVector<double>& x,const QVector<double>& y,
                       const QVector<double>& m,int i,double at){
    const double h=qMax(1e-300,x[i+1]-x[i]);
    const double a=x[i+1]-at, b=at-x[i];
    return (a*a*a*m[i]+b*b*b*m[i+1])/(6.0*h)
         + (y[i]/h - h*m[i]/6.0)*a
         + (y[i+1]/h - h*m[i+1]/6.0)*b;
}

// One structured sample. Every method here is a tensor product: interpolate
// along each row of the lattice, then interpolate the results down the column.
static double structuredAt(const StructuredGrid& g,Estimator kind,double x,double y){
    const int nx=g.xs.size(), ny=g.ys.size();
    if(x<g.xs.first()||x>g.xs.last()||y<g.ys.first()||y>g.ys.last()) return kNaN;

    int ix=0,iy=0; double tx=0.0,ty=0.0;
    locate(g.xs,x,&ix,&tx);
    locate(g.ys,y,&iy,&ty);
    const auto cell=[&g,nx,ny](int cx,int cy){
        return g.z[qBound(0,cy,ny-1)*nx+qBound(0,cx,nx-1)];
    };

    if(kind==Estimator::NearestNeighbour){
        return cell(tx<0.5?ix:ix+1, ty<0.5?iy:iy+1);
    }
    if(kind==Estimator::Bilinear){
        const double a=cell(ix,iy),   b=cell(ix+1,iy);
        const double c=cell(ix,iy+1), d=cell(ix+1,iy+1);
        if(!finite(a)||!finite(b)||!finite(c)||!finite(d)) return kNaN;
        return (a*(1-tx)+b*tx)*(1-ty)+(c*(1-tx)+d*tx)*ty;
    }
    if(kind==Estimator::Bicubic){
        QVector<double> colVals(ny,0.0);
        for(int r=0;r<ny;++r){
            QVector<double> row(nx);
            for(int c=0;c<nx;++c){
                row[c]=g.z[r*nx+c];
                if(!finite(row[c])) return kNaN;
            }
            colVals[r]=catmullRomAt(g.xs,row,ix,x);
        }
        return catmullRomAt(g.ys,colVals,iy,y);
    }
    if(kind==Estimator::MonotonePchip){
        // Along x on the two bracketing rows, then along y between them, with
        // Fritsch-Carlson slopes each time.
        QVector<double> colVals, colY;
        for(int r=0;r<ny;++r){
            QVector<double> row(nx);
            for(int c=0;c<nx;++c){
                row[c]=g.z[r*nx+c];
                if(!finite(row[c])) return kNaN;
            }
            const QVector<double> m=pchipSlopes(g.xs,row);
            colVals.append(hermite(g.xs[ix],g.xs[ix+1],row[ix],row[ix+1],
                                   m[ix],m[ix+1],x));
            colY.append(g.ys[r]);
        }
        const QVector<double> mv=pchipSlopes(colY,colVals);
        return hermite(colY[iy],colY[iy+1],colVals[iy],colVals[iy+1],
                       mv[iy],mv[iy+1],y);
    }
    // Rectangular B-spline.
    QVector<double> colVals(ny,0.0);
    for(int r=0;r<ny;++r){
        QVector<double> row(nx);
        for(int c=0;c<nx;++c){
            row[c]=g.z[r*nx+c];
            if(!finite(row[c])) return kNaN;
        }
        colVals[r]=splineAt(g.xs,row,splineSecondDerivatives(g.xs,row),ix,x);
    }
    return splineAt(g.ys,colVals,splineSecondDerivatives(g.ys,colVals),iy,y);
}


// ---------------------------------------------------------------------------
// Statistical and local regression.
//
// These three are the only estimators here that do NOT pass through their own
// measurements, and that is the point of them rather than a shortcoming. A
// sweep whose response carries noise - a solver tolerance, a measurement error,
// a stochastic model - has an interpolant drawn through every wobble, and the
// resulting surface is a picture of the noise as much as of the field. These
// fit a model near each query and report what the model says.

// A local weighted least-squares fit at one point.
//
// `quadratic` picks the basis: linear (1, x, y) is LOESS's usual degree-1 fit,
// quadratic (1, x, y, x^2, xy, y^2) is what moving least squares means by
// moving - it reproduces curvature that a plane cannot, at the cost of needing
// more points in the neighbourhood before the system is determined.
double localFit(const Workspace& w,double x,double y,QVector<int>& scratch,
                int count,bool quadratic,bool tricube){
    w.index.near(x,y,qMax(6,count),scratch);
    if(scratch.isEmpty()) return kNaN;

    const double wx=qMax(1e-12,w.s.xWeight), wy=qMax(1e-12,w.s.yWeight);
    QVector<QPair<double,int>> byDist;
    byDist.reserve(scratch.size());
    for(int i:scratch){
        const double ddx=(w.pts[i].x-x)/wx, ddy=(w.pts[i].y-y)/wy;
        byDist.append({std::sqrt(ddx*ddx+ddy*ddy),i});
    }
    std::sort(byDist.begin(),byDist.end());
    const int terms=quadratic?6:3;
    const int k=qMin(byDist.size(),qMax(terms+2,count));
    // The bandwidth is the distance to the FURTHEST point used, so the weight
    // falls to zero exactly at the edge of the neighbourhood and a point
    // entering or leaving it does not step the estimate. A fixed bandwidth
    // would leave a seam wherever the sample density changes.
    const double h=qMax(1e-300,byDist[k-1].first);

    QVector<double> A(terms*terms,0.0), b(terms,0.0);
    for(int i=0;i<k;++i){
        const int idx=byDist[i].second;
        const double u=(w.pts[idx].x-x)/wx, v=(w.pts[idx].y-y)/wy;
        const double d=byDist[i].first/h;
        double weight;
        if(tricube){
            // Cleveland's tricube, which is what LOESS means.
            const double t=qMax(0.0,1.0-d*d*d);
            weight=t*t*t;
        }else{
            // A Gaussian, truncated at the bandwidth. The usual MLS choice.
            weight=std::exp(-4.0*d*d);
        }
        if(!(weight>0.0)) continue;
        double basis[6]={1.0,u,v,u*u,u*v,v*v};
        for(int r=0;r<terms;++r){
            for(int c=0;c<terms;++c) A[r*terms+c]+=weight*basis[r]*basis[c];
            b[r]+=weight*basis[r]*w.pts[idx].v;
        }
    }
    // Solved AT the query point - the basis above is centred on it - so the
    // answer is simply the constant term, and no evaluation step can get the
    // centring wrong.
    QVector<double> Acopy=A, rhs=b;
    if(!solveInPlace(Acopy,rhs,terms)){
        // Not enough independent points for this basis. A degenerate
        // neighbourhood is a real situation - a row of collinear runs - and
        // dropping to the linear fit is better than reporting nothing.
        if(!quadratic) return kNaN;
        return localFit(w,x,y,scratch,count,false,tricube);
    }
    return rhs[0];
}

// Natural neighbour, Sibson's version.
//
// The weights are AREAS, not distances. Insert the query point into the Voronoi
// diagram of the samples; its new cell is carved out of the cells that were
// there before, and each old site's weight is the fraction of the new cell that
// was taken from it. That is why the result is smooth, exactly reproduces every
// measurement, adapts on its own to how the samples are spread, and - a theorem
// rather than an accident - reproduces any plane exactly.
//
// Computed DISCRETELY: the new cell is sampled on a polar grid and each sample
// is charged to whichever site owned it before. The exact construction needs a
// correct Voronoi insertion and the second-order cell areas, and a subtly wrong
// one produces a plausible smooth surface that is not this estimator - the
// discrete form is provably the same thing in the limit and its error is
// visible in a test rather than hidden in the geometry. The sample count below
// is what makes a plane come back to 4 decimal places.
double naturalNeighbourAt(const Workspace& w,double x,double y,
                          QVector<int>& scratch){
    const double wx=qMax(1e-12,w.s.xWeight), wy=qMax(1e-12,w.s.yWeight);
    w.index.near(x,y,24,scratch);
    if(scratch.isEmpty()) return kNaN;

    QVector<QPair<double,int>> byDist;
    byDist.reserve(scratch.size());
    for(int i:scratch){
        const double dx=(w.pts[i].x-x)/wx, dy=(w.pts[i].y-y)/wy;
        byDist.append({std::sqrt(dx*dx+dy*dy),i});
    }
    std::sort(byDist.begin(),byDist.end());
    // A query sitting on a measurement takes it. Without this the polar grid
    // degenerates - every ring is the same point - and the weights are noise.
    if(byDist[0].first<1e-12) return w.pts[byDist[0].second].v;

    const int k=qMin(byDist.size(),24);
    // The new cell cannot reach further than the sixth-nearest site; sampling
    // past that only wastes work, because those samples are not in it.
    const double reach=byDist[qMin(k-1,23)].first*1.05;
    if(!(reach>0.0)) return kNaN;

    constexpr int kAngles=64, kRings=20;
    QVector<double> stolen(k,0.0);
    double total=0.0;
    for(int ai=0;ai<kAngles;++ai){
        const double a=2.0*3.14159265358979323846*(double(ai)+0.5)/kAngles;
        const double ca=std::cos(a), sa=std::sin(a);
        for(int ri=0;ri<kRings;++ri){
            // Radii spaced so each sample stands for the same AREA - equal
            // steps in radius would weight the middle of the cell far too
            // heavily, and the weights would be wrong in a way that still
            // looked smooth.
            const double r=reach*std::sqrt((double(ri)+0.5)/kRings);
            const double px=x+r*ca*wx, py=y+r*sa*wy;
            // Is this sample in the query's own cell, and if so whose was it?
            double best=std::numeric_limits<double>::infinity();
            int owner=-1;
            for(int i=0;i<k;++i){
                const ScatterPoint& p=w.pts[byDist[i].second];
                const double dx=(p.x-px)/wx, dy=(p.y-py)/wy;
                const double d=dx*dx+dy*dy;
                if(d<best){ best=d; owner=i; }
            }
            if(owner<0) continue;
            if(r*r>=best) continue;          // nearer to a site than to q
            stolen[owner]+=1.0;
            total+=1.0;
        }
    }
    if(!(total>0.0)) return kNaN;
    double sum=0.0;
    for(int i=0;i<k;++i)
        if(stolen[i]>0.0) sum+=(stolen[i]/total)*w.pts[byDist[i].second].v;
    return sum;
}

// Ordinary kriging.
//
// Alone among these it estimates the SPATIAL STRUCTURE first - how quickly the
// response decorrelates with distance - and then weights the neighbours by what
// that structure implies rather than by distance alone. On an anisotropic sweep
// that is a real difference: two runs a given distance apart along the axis the
// response varies slowly in tell you more about each other than two the same
// distance apart across it.
struct Variogram {
    double nugget=0.0, sill=1.0, range=1.0;
    int model=0;              // 0 exponential, 1 spherical, 2 gaussian
    bool ok=false;
};

double variogramAt(const Variogram& g,double h){
    if(h<=0.0) return 0.0;
    const double a=qMax(1e-300,g.range);
    switch(g.model){
    case 1: return (h>=a)?(g.nugget+g.sill)
                         :(g.nugget+g.sill*(1.5*h/a-0.5*std::pow(h/a,3.0)));
    case 2: return g.nugget+g.sill*(1.0-std::exp(-(h*h)/(a*a)));
    default: return g.nugget+g.sill*(1.0-std::exp(-h/a));
    }
}

Variogram fitVariogram(const QVector<ScatterPoint>& pts,int model,
                       double xWeight,double yWeight){
    Variogram g;
    g.model=model;
    const int n=pts.size();
    if(n<8) return g;

    // The experimental variogram: half the mean squared difference, binned by
    // separation. Sampled rather than exhaustive - all pairs is O(n^2) and a
    // few thousand pairs pins these three numbers perfectly well.
    double maxLag=0.0;
    {
        double xLo=pts[0].x,xHi=xLo,yLo=pts[0].y,yHi=yLo;
        for(const ScatterPoint& p:pts){
            xLo=qMin(xLo,p.x); xHi=qMax(xHi,p.x);
            yLo=qMin(yLo,p.y); yHi=qMax(yHi,p.y);
        }
        maxLag=0.5*std::hypot((xHi-xLo)/qMax(1e-12,xWeight),
                              (yHi-yLo)/qMax(1e-12,yWeight));
    }
    if(!(maxLag>0.0)) return g;

    constexpr int kBins=16;
    QVector<double> sum(kBins,0.0);
    QVector<int> count(kBins,0);
    quint32 seed=88675123u;
    const int pairs=qMin(20000,n*8);
    for(int t=0;t<pairs;++t){
        seed^=seed<<13; seed^=seed>>17; seed^=seed<<5;
        const int i=int(seed%quint32(n));
        seed^=seed<<13; seed^=seed>>17; seed^=seed<<5;
        const int j=int(seed%quint32(n));
        if(i==j) continue;
        const double h=std::hypot((pts[i].x-pts[j].x)/qMax(1e-12,xWeight),
                                  (pts[i].y-pts[j].y)/qMax(1e-12,yWeight));
        if(h<=0.0||h>maxLag) continue;
        const int bin=qBound(0,int(h/maxLag*kBins),kBins-1);
        const double d=pts[i].v-pts[j].v;
        sum[bin]+=0.5*d*d;
        count[bin]+=1;
    }
    QVector<double> lag,gamma;
    for(int b=0;b<kBins;++b){
        if(count[b]<4) continue;
        lag.append(maxLag*(double(b)+0.5)/kBins);
        gamma.append(sum[b]/double(count[b]));
    }
    if(lag.size()<4) return g;

    // Three parameters over a grid rather than by a nonlinear solver: the
    // surface is smooth, the grid is 12 x 12 x 8, and a search that cannot
    // diverge is worth more here than one that is two decimal places better.
    double best=std::numeric_limits<double>::infinity();
    double totalSill=0.0;
    for(double v:gamma) totalSill=qMax(totalSill,v);
    for(int ri=1;ri<=12;++ri){
        const double range=maxLag*double(ri)/12.0;
        for(int si=1;si<=12;++si){
            const double sill=totalSill*double(si)/12.0*1.4;
            for(int ni=0;ni<8;++ni){
                const double nugget=totalSill*double(ni)/8.0*0.5;
                Variogram trial; trial.model=model;
                trial.range=range; trial.sill=sill; trial.nugget=nugget;
                double sse=0.0;
                for(int k=0;k<lag.size();++k){
                    const double r=gamma[k]-variogramAt(trial,lag[k]);
                    sse+=r*r;
                }
                if(sse<best){ best=sse; g=trial; }
            }
        }
    }
    g.ok=std::isfinite(best);
    return g;
}

double krigingAt(const Workspace& w,const Variogram& g,double x,double y,
                 QVector<int>& scratch){
    if(!g.ok) return kNaN;
    // A neighbourhood, not the whole sample: the kriging system is
    // (k+1) x (k+1) and solved PER NODE, so k is what decides whether a
    // 160 x 160 grid takes a moment or an afternoon.
    const int want=qBound(6,w.s.neighbours,64);
    w.index.near(x,y,want,scratch);
    if(scratch.isEmpty()) return kNaN;

    const double wx=qMax(1e-12,w.s.xWeight), wy=qMax(1e-12,w.s.yWeight);
    QVector<QPair<double,int>> byDist;
    for(int i:scratch){
        const double ddx=(w.pts[i].x-x)/wx, ddy=(w.pts[i].y-y)/wy;
        byDist.append({std::sqrt(ddx*ddx+ddy*ddy),i});
    }
    std::sort(byDist.begin(),byDist.end());
    if(byDist[0].first<1e-12) return w.pts[byDist[0].second].v;
    const int k=qMin(byDist.size(),want);

    // [ G  1 ] [lambda]   [g0]
    // [ 1' 0 ] [  mu  ] = [ 1]   - the 1s are what makes it ORDINARY kriging:
    // the weights are forced to sum to one, so the estimate is unbiased without
    // anyone having to know the mean.
    const int size=k+1;
    QVector<double> A(size*size,0.0), b(size,0.0);
    for(int i=0;i<k;++i){
        const ScatterPoint& pi=w.pts[byDist[i].second];
        for(int j=0;j<k;++j){
            const ScatterPoint& pj=w.pts[byDist[j].second];
            const double h=std::hypot((pi.x-pj.x)/wx,(pi.y-pj.y)/wy);
            A[i*size+j]=variogramAt(g,h);
        }
        A[i*size+k]=1.0;
        A[k*size+i]=1.0;
        b[i]=variogramAt(g,byDist[i].first);
    }
    A[k*size+k]=0.0;
    b[k]=1.0;
    if(!solveInPlace(A,b,size)) return kNaN;

    double sum=0.0;
    for(int i=0;i<k;++i) sum+=b[i]*w.pts[byDist[i].second].v;
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
    // Runs that produced no finite answer. Their COORDINATES are real - the
    // sweep went there and something came back non-finite - so they are kept
    // and used to mask, rather than dropped as though the point had never been
    // visited. That distinction is the whole of the footprint control below.
    QVector<ScatterPoint> failures;
    pts.reserve(raw.size());
    double vLo=std::numeric_limits<double>::infinity(), vHi=-vLo;
    for(const ScatterPoint& p:raw){
        if(!finite(p.x)||!finite(p.y)) continue;
        if(!finite(p.v)){ failures.append(p); continue; }
        ScatterPoint q=p;
        if(settings.responseSpace==ResponseSpace::Log10){
            // A non-positive value has no logarithm. It is a failure of the
            // chosen response space rather than of the run, and masking it is
            // more honest than substituting a floor - which would put a
            // measurement on the map at a value nobody recorded.
            if(!(q.v>0.0)){ failures.append(p); continue; }
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

    // Is the sweep on a lattice? Asked once, because Auto wants to know and the
    // structured methods cannot run without it.
    const bool wantsStructured=(chosen==Estimator::Bilinear
                              ||chosen==Estimator::Bicubic
                              ||chosen==Estimator::RectangularBSpline
                              ||chosen==Estimator::MonotonePchip);
    StructuredGrid lattice;
    if(wantsStructured||chosen==Estimator::Auto) lattice=asStructured(pts);
    // A structured method asked for on scattered data is a request that cannot
    // be met. It falls back rather than drawing a tensor product over a lattice
    // that is not there - and the fallback is the scattered method closest in
    // character, not a generic default.
    if(wantsStructured&&!lattice.ok){
        chosen=(chosen==Estimator::Bilinear)?Estimator::DelaunayLinear
                                            :Estimator::ThinPlateSpline;
    }

    if(chosen==Estimator::Auto){
        // The choice a person would make from the same facts, said out loud
        // rather than left to a comment. A complete rectangle is the one case
        // with an exactly right answer, so it takes it; otherwise too few
        // points for a global solve is a triangulation, a small clean sample
        // earns a thin-plate spline, and a large one gets the local method
        // that stays O(n).
        if(lattice.ok&&lattice.missing==0) chosen=Estimator::Bicubic;
        else if(pts.size()<12) chosen=Estimator::NearestNeighbour;
        else if(pts.size()<=400) chosen=Estimator::ThinPlateSpline;
        else chosen=Estimator::DelaunayLinear;
    }
    const bool structured=(chosen==Estimator::Bilinear
                         ||chosen==Estimator::Bicubic
                         ||chosen==Estimator::RectangularBSpline
                         ||chosen==Estimator::MonotonePchip)&&lattice.ok;

    // The triangulation, only for the estimators that read it. It is the
    // expensive part and three of seventeen methods need it.
    QVector<Triangle> tris;
    const bool needsTriangles=(chosen==Estimator::DelaunayLinear
                             ||chosen==Estimator::CloughTocher
                             ||chosen==Estimator::NaturalNeighbour);
    if(needsTriangles) tris=delaunay(pts);

    // The variogram, for kriging only. Fitted once over the whole sample; the
    // per-node solve below reads it.
    Variogram variogram;
    if(chosen==Estimator::OrdinaryKriging)
        variogram=fitVariogram(pts,qBound(0,settings.kriging,2),
                               settings.xWeight,settings.yWeight);

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
    field.failed.fill(false,nx*ny);
    field.bridged.fill(false,nx*ny);

    // Where the failures reach. Computed before anything is estimated, so a
    // masked node costs nothing to produce.
    if(!failures.isEmpty()&&settings.footprint!=FailureFootprint::None){
        // The scale of a footprint is the sweep's own spacing near THAT
        // failure, not one number for the whole sweep.
        //
        // A global median was tried first and was wrong in a way that only
        // shows on an uneven sweep: with the runs crowded at one end of the
        // range, the median spacing is the crowded end's, and the conservative
        // footprint collapses onto the single-cell one everywhere. Measured on
        // a lattice cubed towards zero it masked 2 nodes and 1 node - exactly
        // what "sample cell only" masked - so the control existed and did
        // nothing. Each failure now asks its own neighbours how far away they
        // are.
        const double cellX=(xHi-xLo)/qMax(1,nx-1), cellY=(yHi-yLo)/qMax(1,ny-1);
        const double cellReach=0.5*std::hypot(cellX,cellY);
        const PointIndex spacingIndex(pts,xLo,xHi,yLo,yHi);
        QVector<double> reachOf(failures.size(),cellReach);
        if(settings.footprint==FailureFootprint::ConservativeRegion){
            QVector<int> found;
            for(int i=0;i<failures.size();++i){
                spacingIndex.near(failures[i].x,failures[i].y,8,found);
                QVector<double> d;
                for(int j:found)
                    d.append(std::hypot(pts[j].x-failures[i].x,pts[j].y-failures[i].y));
                std::sort(d.begin(),d.end());
                // The mean of the four nearest runs: far enough to cover the
                // gap the failure leaves, near enough not to swallow its
                // neighbours' own results.
                const int take=qMin(4,d.size());
                double sum=0.0;
                for(int k=0;k<take;++k) sum+=d[k];
                const double local=(take>0)?sum/double(take):0.0;
                reachOf[i]=qMax(cellReach,0.75*local);
            }
        }
        const PointIndex goodIndex(pts,xLo,xHi,yLo,yHi);
        QVector<int> near;
        for(int gy=0;gy<ny;++gy){
            const double y=yLo+(yHi-yLo)*double(gy)/double(ny-1);
            for(int gx=0;gx<nx;++gx){
                const double x=xLo+(xHi-xLo)*double(gx)/double(nx-1);
                double toFailure=std::numeric_limits<double>::infinity();
                bool withinReach=false;
                for(int i=0;i<failures.size();++i){
                    const double d=std::hypot(failures[i].x-x,failures[i].y-y);
                    toFailure=qMin(toFailure,d);
                    if(d<=reachOf[i]) withinReach=true;
                }
                if(settings.footprint==FailureFootprint::VoronoiRegion){
                    // Masked when the nearest RUN of any kind is a failed one:
                    // the region the failure owns. Conservative, and the reason
                    // a dense sweep full of isolated dropouts comes out looking
                    // like a slice of Swiss cheese.
                    goodIndex.near(x,y,1,near);
                    double toGood=std::numeric_limits<double>::infinity();
                    for(int i:near) toGood=qMin(toGood,std::hypot(pts[i].x-x,pts[i].y-y));
                    if(toFailure<toGood) field.failed[gy*nx+gx]=true;
                }else if(withinReach){
                    field.failed[gy*nx+gx]=true;
                }
            }
        }
    }

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
            // that masking costs nothing. The same for a node a failed run
            // masks - the bridging pass below decides whether any of those
            // come back.
            if(!inside&&settings.extrapolation==Extrapolation::MaskOutsideHull) continue;
            if(field.failed[k]) continue;

            double v=kNaN;
            if(structured){
                v=structuredAt(lattice,chosen,x,y);
                // A hole in the lattice defeats the tensor product for every
                // node whose stencil touches it. Nearest is the honest patch:
                // it repeats a measured value rather than inventing a smooth
                // one across a gap the sweep never covered.
                if(!finite(v)) v=nearestValue(work,x,y,scratch);
                if(settings.valuePolicy==ValuePolicy::ClampToObserved)
                    v=qBound(vLo,v,vHi);
                if(finite(v)) field.value[k]=v;
                continue;
            }
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
            case Estimator::Loess:
                // Degree one with tricube weights, over a FRACTION of the
                // sample rather than a fixed count - which is what the f
                // parameter means in LOESS and why it adapts to sample size.
                v=localFit(work,x,y,scratch,
                           qMax(6,int(pts.size()*qBound(0.02,settings.loessFraction,1.0))),
                           false,true); break;
            case Estimator::MovingLeastSquares:
                v=localFit(work,x,y,scratch,settings.neighbours,true,false); break;
            case Estimator::OrdinaryKriging:
                v=krigingAt(work,variogram,x,y,scratch); break;
            case Estimator::NaturalNeighbour:
                v=naturalNeighbourAt(work,x,y,scratch); break;
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

    // ---------------------------------------------------------------- bridging
    //
    // Which masked regions may be estimated across after all. Connected
    // components of masked cells, four-connected; a component touching the edge
    // of the field is NEVER bridged whatever the mode, because its far side was
    // never measured and joining across it would be invention rather than
    // interpolation.
    if(settings.bridging!=Bridging::PreserveAll){
        QVector<int> label(nx*ny,-1);
        QVector<int> stack;
        int nextLabel=0;
        QVector<int> sizes;
        QVector<bool> touchesEdge;
        for(int start=0;start<nx*ny;++start){
            if(label[start]>=0) continue;
            if(finite(field.value[start])) continue;   // not masked
            const int id=nextLabel++;
            sizes.append(0);
            touchesEdge.append(false);
            stack.clear();
            stack.append(start);
            label[start]=id;
            while(!stack.isEmpty()){
                const int k=stack.takeLast();
                sizes[id]+=1;
                const int cx=k%nx, cy=k/nx;
                if(cx==0||cy==0||cx==nx-1||cy==ny-1) touchesEdge[id]=true;
                const int steps[4]={k-1,k+1,k-nx,k+nx};
                const bool ok[4]={cx>0,cx<nx-1,cy>0,cy<ny-1};
                for(int d=0;d<4;++d){
                    if(!ok[d]) continue;
                    const int nk=steps[d];
                    if(label[nk]>=0||finite(field.value[nk])) continue;
                    label[nk]=id;
                    stack.append(nk);
                }
            }
        }

        QVector<bool> bridge(nextLabel,false);
        for(int id=0;id<nextLabel;++id){
            if(touchesEdge[id]) continue;
            switch(settings.bridging){
            case Bridging::IsolatedOnly:  bridge[id]=(sizes[id]<=1); break;
            case Bridging::SmallEnclosed: bridge[id]=(sizes[id]<=qMax(1,settings.bridgeMaxCells)); break;
            case Bridging::AllInterior:   bridge[id]=true; break;
            default: break;
            }
        }

        // Filled from the ring of finite neighbours, iterated until the whole
        // component is covered - which is a local mean rather than a re-run of
        // the estimator, and deliberately: the estimator was told not to look
        // here, and asking it now would give the answer the mask exists to
        // withhold. What the surrounding cells say is the most that can honestly
        // be claimed about a hole.
        bool changed=true;
        int guard=0;
        while(changed&&guard++<256){
            changed=false;
            QVector<double> next=field.value;
            for(int k=0;k<nx*ny;++k){
                if(finite(field.value[k])||label[k]<0||!bridge[label[k]]) continue;
                const int cx=k%nx, cy=k/nx;
                double sum=0.0; int count=0;
                for(int dy=-1;dy<=1;++dy)
                    for(int dx=-1;dx<=1;++dx){
                        const int qx=cx+dx, qy=cy+dy;
                        if(qx<0||qy<0||qx>=nx||qy>=ny) continue;
                        const double v=field.value[qy*nx+qx];
                        if(!finite(v)) continue;
                        sum+=v; ++count;
                    }
                if(count==0) continue;
                next[k]=sum/double(count);
                field.bridged[k]=true;
                changed=true;
            }
            field.value=next;
        }
    }

    // --------------------------------------------------- what a mask shows
    //
    // Everything still masked at this point is a region nothing is known about.
    // Transparent is the default and the honest one; the rest are for the cases
    // where a hole in the middle of a figure is worse than a stated guess, and
    // each says in the interface which it is.
    if(settings.invalidDisplay!=InvalidDisplay::Transparent
       &&settings.invalidDisplay!=InvalidDisplay::FallbackColour){
        QVector<int> scratch2;
        QVector<double> filled=field.value;
        for(int gy=0;gy<ny;++gy){
            const double y=yLo+(yHi-yLo)*double(gy)/double(ny-1);
            for(int gx=0;gx<nx;++gx){
                const int k=gy*nx+gx;
                if(finite(field.value[k])) continue;
                const double x=xLo+(xHi-xLo)*double(gx)/double(nx-1);
                switch(settings.invalidDisplay){
                case InvalidDisplay::NearestFill:
                    filled[k]=nearestValue(work,x,y,scratch2); break;
                case InvalidDisplay::BaselineClamp:
                    // The bottom of the colour scale, so a hole reads as "as
                    // low as this map goes" rather than as a measurement.
                    filled[k]=vLo; break;
                case InvalidDisplay::MirrorFill: {
                    // Reflected across the edge of the masked region.
                    //
                    // Walk out along each axis to the first cell that has a
                    // value; that cell is the boundary. Continue the same
                    // distance again and take THAT value, which is the mirror
                    // image of the masked node through the boundary. Falling
                    // back to the boundary value itself when the mirror lands
                    // outside is what makes the fill flat rather than absent at
                    // the edge of the field.
                    //
                    // The point of the mode is that a contour running into a
                    // hole continues with the slope it arrived with instead of
                    // stopping dead - which a nearest fill cannot do, because
                    // it repeats one value.
                    const int step[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
                    double sum=0.0; int count=0;
                    for(int d=0;d<4;++d){
                        int dist=0;
                        double edge=kNaN;
                        for(int t=1;t<qMax(nx,ny);++t){
                            const int qx=gx+step[d][0]*t, qy=gy+step[d][1]*t;
                            if(qx<0||qy<0||qx>=nx||qy>=ny) break;
                            const double v=field.value[qy*nx+qx];
                            if(!finite(v)) continue;
                            edge=v; dist=t; break;
                        }
                        if(!finite(edge)) continue;
                        const int mx=gx+step[d][0]*dist*2, my=gy+step[d][1]*dist*2;
                        double mirrored=kNaN;
                        if(mx>=0&&my>=0&&mx<nx&&my<ny) mirrored=field.value[my*nx+mx];
                        // 2*edge - mirrored reflects the VALUE as well as the
                        // position, so a rising field goes on rising into the
                        // hole rather than turning back on itself.
                        sum+=finite(mirrored)?(2.0*edge-mirrored):edge;
                        ++count;
                    }
                    if(count>0) filled[k]=sum/double(count);
                    break;
                }
                default: break;    // local mean, below
                }
            }
        }
        field.value=filled;

        if(settings.invalidDisplay==InvalidDisplay::LocalMean){
            // Iterated neighbour mean over everything still masked - the same
            // pass the bridging uses, applied without the topology test.
            bool changed=true;
            int guard=0;
            while(changed&&guard++<256){
                changed=false;
                QVector<double> next=field.value;
                for(int k=0;k<nx*ny;++k){
                    if(finite(field.value[k])) continue;
                    const int cx=k%nx, cy=k/nx;
                    double sum=0.0; int count=0;
                    for(int dy=-1;dy<=1;++dy)
                        for(int dx=-1;dx<=1;++dx){
                            const int qx=cx+dx, qy=cy+dy;
                            if(qx<0||qy<0||qx>=nx||qy>=ny) continue;
                            const double v=field.value[qy*nx+qx];
                            if(!finite(v)) continue;
                            sum+=v; ++count;
                        }
                    if(count==0) continue;
                    next[k]=sum/double(count);
                    changed=true;
                }
                field.value=next;
            }
        }
    }

    for(int k=0;k<nx*ny;++k){
        if(field.failed[k]) ++field.failedCount;
        if(field.bridged[k]) ++field.bridgedCount;
    }

    // Back out of the response space, so the caller and the colour bar are in
    // the units the data came in.
    if(settings.responseSpace==ResponseSpace::Log10)
        for(double& v:field.value) if(finite(v)) v=std::pow(10.0,v);

    field.valid=true;
    return field;
}

} // namespace graphvis
