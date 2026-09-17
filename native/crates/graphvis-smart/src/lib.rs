//! Intelligent Auto-Optimization for GraphVis.
//!
//! The engine is renderer-neutral and deliberately deterministic. It uses
//! robust statistics, sampled quantiles, density-aware spatial stratification,
//! and conservative signal/anomaly scoring to generate a SmartRenderPlan.
//! It never mutates source data: the plan controls presentation only.

use rayon::prelude::*;
use serde::{Deserialize, Serialize};

const QUANTILE_SAMPLE_MAX: usize = 200_000;

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum SmartProfile { Balanced, Clarity, Performance }

impl SmartProfile {
    pub fn from_code(code: u32) -> Option<Self> {
        match code { 1 => Some(Self::Balanced), 2 => Some(Self::Clarity), 3 => Some(Self::Performance), _ => None }
    }
    fn target_points(self, n: usize) -> usize {
        match self {
            Self::Clarity => n.min(450_000),
            Self::Balanced => n.min(280_000),
            Self::Performance => n.min(140_000),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RobustStats {
    pub count: usize,
    pub min: f64,
    pub max: f64,
    pub q01: f64,
    pub q05: f64,
    pub q25: f64,
    pub median: f64,
    pub q75: f64,
    pub q95: f64,
    pub q99: f64,
    pub mad: f64,
    pub skewness: f64,
    pub outlier_fraction: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SmartRenderPlan {
    pub profile: SmartProfile,
    pub confidence: f64,
    pub finite_points: usize,
    pub display_min: f64,
    pub display_max: f64,
    pub color_transfer: String,
    pub colormap: String,
    pub interpolation: String,
    pub target_points: usize,
    pub decimation_ratio: f64,
    pub voxel_bins: usize,
    pub point_size: f32,
    pub base_opacity: f32,
    pub minimum_alpha: f32,
    pub signal_floor: f64,
    pub anomaly_z: f64,
    pub histogram_equalization: bool,
    pub density_emphasis: bool,
    pub preserve_extrema: bool,
    pub reasons: Vec<String>,
    pub stats: RobustStats,
}

#[derive(Debug, Clone, Copy)]
pub struct SmartPoint {
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub mapped_response: f32,
    pub size: f32,
    pub alpha: f32,
}

fn finite_sample(values: &[f64]) -> Vec<f64> {
    let finite_count = values.iter().filter(|v| v.is_finite()).count();
    if finite_count == 0 { return vec![]; }
    let stride = (finite_count / QUANTILE_SAMPLE_MAX).max(1);
    let mut out = Vec::with_capacity(finite_count.min(QUANTILE_SAMPLE_MAX + 1));
    let mut seen = 0usize;
    for &v in values {
        if !v.is_finite() { continue; }
        if seen.is_multiple_of(stride) { out.push(v); }
        seen += 1;
    }
    out.sort_by(|a,b| a.total_cmp(b));
    out
}

fn q(sorted: &[f64], p: f64) -> f64 {
    if sorted.is_empty() { return f64::NAN; }
    let pos = p.clamp(0.0,1.0) * (sorted.len()-1) as f64;
    let lo = pos.floor() as usize; let hi = pos.ceil() as usize;
    if lo == hi { sorted[lo] } else { sorted[lo] + (sorted[hi]-sorted[lo])*(pos-lo as f64) }
}

pub fn robust_stats(values: &[f64]) -> Option<RobustStats> {
    let sorted = finite_sample(values); if sorted.is_empty() { return None; }
    let min=*sorted.first()?; let max=*sorted.last()?;
	let q01=q(&sorted,0.01); let q05=q(&sorted,0.05); let q25=q(&sorted,0.25); let median=q(&sorted,0.5);
	let q75=q(&sorted,0.75); let q95=q(&sorted,0.95); let q99=q(&sorted,0.99);
    let mut deviations=sorted.iter().map(|v|(v-median).abs()).collect::<Vec<_>>(); deviations.sort_by(|a,b|a.total_cmp(b));
	let mad=q(&deviations,0.5).max(f64::EPSILON);
    let scale=1.4826*mad;
    let mut n=0usize; let mut m2=0.0; let mut m3=0.0; let mut mean=0.0; let mut outliers=0usize;
    for &v in values { if !v.is_finite(){continue;} n+=1; let delta=v-mean; let dn=delta/n as f64; let term=delta*dn*(n.saturating_sub(1)) as f64; m3 += term*dn*(n.saturating_sub(2)) as f64 - 3.0*dn*m2; m2 += term; mean += dn; if ((v-median)/scale).abs()>4.5 { outliers+=1; } }
    let skewness=if n>2 && m2>f64::EPSILON {(n as f64).sqrt()*m3/m2.powf(1.5)}else{0.0};
    Some(RobustStats{count:n,min,max,q01,q05,q25,median,q75,q95,q99,mad,skewness,outlier_fraction:outliers as f64/n.max(1) as f64})
}

fn choose_color_transfer(s:&RobustStats)->(String,bool,String,Vec<String>){
    let mut reasons=vec![];
    if s.q05 < 0.0 && s.q95 > 0.0 {
        reasons.push("Response spans negative and positive values; symmetric contrast preserves sign around zero.".into());
        return ("symlog".into(),false,"CoolWarm".into(),reasons)
    }
    let positive = s.q01 > 0.0;
    let decade_ratio = if positive { s.q99 / s.q01.max(f64::MIN_POSITIVE) } else { 1.0 };
    if positive && decade_ratio > 250.0 {
        reasons.push(format!("Positive response spans {:.1}× across the robust range; logarithmic contrast prevents the high tail washing out lower structure.",decade_ratio));
        return ("log10".into(),false,"Viridis".into(),reasons)
    }
    if s.skewness.abs() > 1.6 || s.outlier_fraction > 0.025 {
        reasons.push(format!("Distribution is strongly skewed/outlier-heavy (skew {:.2}, robust outliers {:.1}%); histogram-equalized contrast improves core detail.",s.skewness,100.0*s.outlier_fraction));
        return ("histogram_equalized".into(),true,"Viridis".into(),reasons)
    }
    reasons.push("Distribution is sufficiently balanced for perceptually uniform linear contrast.".into());
    ("linear".into(),false,"Viridis".into(),reasons)
}

fn choose_interpolation(s:&RobustStats, n:usize)->(String,String){
    let robust_span=(s.q95-s.q05).abs(); let core=(s.q75-s.q25).abs();
    let roughness=if robust_span>0.0 {core/robust_span}else{0.0};
    if n < 2_500 { ("nearest".into(),"Sparse/low-resolution data: nearest-neighbor interpolation avoids inventing smooth structure.".into()) }
    else if n >= 16_000 && roughness < 0.58 && s.outlier_fraction < 0.02 { ("bicubic".into(),"Dense, comparatively smooth data: bicubic interpolation improves continuous heatmap readability.".into()) }
    else { ("bilinear".into(),"Moderate density/variation: bilinear interpolation balances continuity and fidelity.".into()) }
}

pub fn analyze(response:&[f64], point_count:usize, profile:SmartProfile)->Option<SmartRenderPlan>{
    let stats=robust_stats(response)?;
    let (color_transfer,histeq,colormap,mut reasons)=choose_color_transfer(&stats);
    let (interpolation,interp_reason)=choose_interpolation(&stats,point_count); reasons.push(interp_reason);
    let tail_pressure=(stats.outlier_fraction*10.0).clamp(0.0,1.0);
    let clip_pad: f64=match profile {SmartProfile::Clarity=>0.005,SmartProfile::Balanced=>0.01,SmartProfile::Performance=>0.02};	
    let display_min=if tail_pressure>0.35 {q(&finite_sample(response),clip_pad.max(0.01))}else{stats.q01};
	let display_max=if tail_pressure>0.35 {q(&finite_sample(response),1.0-clip_pad.max(0.01))}else{stats.q99};
    if display_min>stats.min || display_max<stats.max { reasons.push(format!("Robust display bounds [{:.5}, {:.5}] isolate extreme tails without deleting source observations.",display_min,display_max)); }
    let target=profile.target_points(point_count.max(stats.count));
    let ratio=if stats.count>0 {target.min(stats.count) as f64/stats.count as f64}else{1.0};
    let voxel_bins=((target.max(1) as f64).cbrt()*1.15).round().clamp(12.0,96.0) as usize;
    if ratio<0.999 { reasons.push(format!("LOD target {} preserves spatial coverage/extrema while reducing the visible point load to {:.1}%.",target,100.0*ratio)); }
    let density_emphasis=stats.count>80_000;
    if density_emphasis { reasons.push("Density-aware alpha/size weighting is enabled to reveal clusters without turning overlap into opaque blobs.".into()); }
    let point_size=match profile {SmartProfile::Clarity=>5.5,SmartProfile::Balanced=>4.5,SmartProfile::Performance=>3.5};
    let base_opacity=match profile {SmartProfile::Clarity=>0.82,SmartProfile::Balanced=>0.72,SmartProfile::Performance=>0.62};
    let minimum_alpha=match profile {SmartProfile::Clarity=>0.08,SmartProfile::Balanced=>0.055,SmartProfile::Performance=>0.035};
    let confidence=(0.62 + (stats.count as f64+1.0).log10()/20.0 - (stats.outlier_fraction*0.5)).clamp(0.55,0.98);
    Some(SmartRenderPlan{
        profile,confidence,finite_points:stats.count,display_min,display_max,color_transfer,colormap,interpolation,
        target_points:target,decimation_ratio:ratio,voxel_bins,point_size,base_opacity,minimum_alpha,
        signal_floor:stats.q05,anomaly_z:3.5,histogram_equalization:histeq,density_emphasis,preserve_extrema:true,reasons,stats
    })
}

fn transfer(v:f64, plan:&SmartRenderPlan, sorted_sample:&[f64])->f32{
    let lo=plan.display_min; let hi=plan.display_max; if !v.is_finite(){return 0.0;}
    let t=match plan.color_transfer.as_str(){
        "log10" => { let vlo=v.max(f64::MIN_POSITIVE).log10(); let a=lo.max(f64::MIN_POSITIVE).log10(); let b=hi.max(f64::MIN_POSITIVE).log10(); (vlo-a)/(b-a).max(f64::EPSILON) },
        "symlog" => { let lin=(plan.stats.mad*1.4826).max((hi-lo).abs()*1e-4).max(f64::EPSILON); let f=|x:f64| x.signum()*(1.0+(x.abs()/lin)).ln(); let a=f(lo);let b=f(hi);(f(v)-a)/(b-a).max(f64::EPSILON) },
        "histogram_equalized" => { if sorted_sample.is_empty(){0.5}else{let idx=sorted_sample.partition_point(|x|*x<=v); idx as f64/sorted_sample.len() as f64} },
        _ => (v-lo)/(hi-lo).max(f64::EPSILON),
    }; t.clamp(0.0,1.0) as f32
}

#[derive(Clone, Copy, Default)] struct CellAcc{count:u32,best_idx:usize,best_score:f64}

/// Builds a clarity-preserving point set. Dense background regions are spatially
/// stratified, while extrema and robust anomalies are always preserved.
pub fn optimize_scatter(x:&[f64],y:&[f64],z:&[f64],response:&[f64],plan:&SmartRenderPlan)->Vec<SmartPoint>{
    let n=x.len().min(y.len()).min(z.len()).min(response.len()); if n==0{return vec![];}
    let finite=(0..n).filter(|&i|[x[i],y[i],z[i],response[i]].iter().all(|v|v.is_finite())).collect::<Vec<_>>();
    if finite.is_empty(){return vec![];}
    let extent=|vals:&[f64],idx:&[usize]|{let mut lo=f64::INFINITY;let mut hi=f64::NEG_INFINITY;for&i in idx{lo=lo.min(vals[i]);hi=hi.max(vals[i]);}(lo,hi)};
    let (xl,xh)=extent(x,&finite);let(yl,yh)=extent(y,&finite);let(zl,zh)=extent(z,&finite);
    let bins=plan.voxel_bins.max(2); let total=bins.saturating_mul(bins).saturating_mul(bins);
    let cell_of=|i:usize|{let norm=|v:f64,lo:f64,hi:f64|(((v-lo)/(hi-lo).max(f64::EPSILON))*bins as f64).floor().clamp(0.0,(bins-1)as f64)as usize;let ix=norm(x[i],xl,xh);let iy=norm(y[i],yl,yh);let iz=norm(z[i],zl,zh);ix+bins*(iy+bins*iz)};
    let scale=(1.4826*plan.stats.mad).max(f64::EPSILON);
    let signal=|v:f64|((v-plan.display_min)/(plan.display_max-plan.display_min).max(f64::EPSILON)).clamp(0.0,1.0);
    let score=|v:f64|{let s=signal(v);let a=((v-plan.stats.median)/scale).abs();0.70*s+0.30*(a/plan.anomaly_z).clamp(0.0,1.0)};
    let mut cells=vec![CellAcc::default();total];
    for &i in &finite{let k=cell_of(i);let sc=score(response[i]);let c=&mut cells[k];c.count=c.count.saturating_add(1);if c.count==1||sc>c.best_score{c.best_score=sc;c.best_idx=i;}}
    let max_count=cells.iter().map(|c|c.count).max().unwrap_or(1).max(1) as f64;
    let mut selected=Vec::with_capacity(plan.target_points.min(finite.len())+8);
    if finite.len()<=plan.target_points {selected.extend(finite.iter().copied());}
    else { selected.extend(cells.iter().filter(|c|c.count>0).map(|c|c.best_idx)); }
    // Preserve spatial and response extrema even when they fall in a crowded cell.
    for vals in [x, y, z, response] {
        if let Some(&imin) = finite.iter().min_by(|&&a, &&b| vals[a].total_cmp(&vals[b])) {
            selected.push(imin);
        }
        if let Some(&imax) = finite.iter().max_by(|&&a, &&b| vals[a].total_cmp(&vals[b])) {
            selected.push(imax);
        }
    }
    selected.sort_unstable();selected.dedup();
    // If voxel representatives are still too many, keep highest significance deterministically.
    if selected.len()>plan.target_points {selected.sort_by(|&a,&b|score(response[b]).total_cmp(&score(response[a])));selected.truncate(plan.target_points);selected.sort_unstable();}
    let sorted=finite_sample(response);
    selected.into_par_iter().map(|i|{
        let k=cell_of(i);let density=(1.0+(cells[k].count as f64)).ln()/(1.0+max_count).ln();
        let rz=((response[i]-plan.stats.median)/scale).abs();let anomaly=(rz/plan.anomaly_z).clamp(0.0,1.0);let sig=signal(response[i]);
        let alpha=(plan.minimum_alpha as f64 + plan.base_opacity as f64*(0.30+0.45*sig+0.25*density)).clamp(plan.minimum_alpha as f64,1.0) as f32;
        let size=(0.75+1.30*sig+0.65*anomaly+0.25*density) as f32;
        SmartPoint{x:x[i],y:y[i],z:z[i],mapped_response:transfer(response[i],plan,&sorted),size,alpha}
    }).collect()
}

#[cfg(test)]
mod tests{
    use super::*;
    #[test] fn robust_bounds_ignore_single_extreme(){let mut v=(0..1000).map(|i|i as f64/100.0).collect::<Vec<_>>();v.push(1e9);let p=analyze(&v,v.len(),SmartProfile::Balanced).unwrap();assert!(p.display_max<1e8);assert!(p.preserve_extrema);}
    #[test] fn log_for_multi_decade_positive(){let v=(0..1000).map(|i|10f64.powf(-3.0+6.0*i as f64/999.0)).collect::<Vec<_>>();let p=analyze(&v,v.len(),SmartProfile::Balanced).unwrap();assert_eq!(p.color_transfer,"log10");}
    #[test] fn symlog_for_signed(){let v=(-500..=500).map(|i|i as f64).collect::<Vec<_>>();let p=analyze(&v,v.len(),SmartProfile::Balanced).unwrap();assert_eq!(p.color_transfer,"symlog");}
}
