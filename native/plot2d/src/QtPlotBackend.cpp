#include "QtPlotBackend.h"
#include "ColourMaps.h"
#include "ColourVision.h"
#include "SurfaceEstimators.h"
#include "Expression.h"
#include "QtPlotBackendShared.h"

#include <cmath>
#include <limits>

#include <QFontMetricsF>
#include <QHash>
#include <QMap>
#include <QMutex>
#include <QSet>
#include <QPainter>
#include <QPainterPath>
// QVector3D: the isosurface's vertices, which are three-dimensional until the
// moment they are projected.
#include <QVector3D>
#include <QtMath>
#include <QElapsedTimer>
#include <algorithm>

namespace graphvis {



QStringList QtPlotBackend::supportedEngines() const {
    // The first engine tier. Every other catalogue entry stays visible in the
    // Graph Library but is reported unsupported, so nothing silently draws the
    // wrong picture. Entries are added here as their engines are ported.
    //
    // Built once. This used to construct a fresh 135-element QStringList on
    // every call, and supports() calls it for each engine the graph library
    // asks about.
    static const QStringList kEngines{
        QStringLiteral("Line Chart"),
        QStringLiteral("Stairs"),
        QStringLiteral("Area"),
        QStringLiteral("Error Bar"),
        QStringLiteral("4D / 5D Scatter"),
        QStringLiteral("Bar"),
        QStringLiteral("Horizontal Bar"),
        QStringLiteral("Stem"),
        QStringLiteral("Stacked Lines"),
        QStringLiteral("Histogram"),
        QStringLiteral("Box Plot"),
        QStringLiteral("Pie"),
        QStringLiteral("Donut"),
        // Statistical tier. Each is a rewrite in prepareSpec onto geometry
        // above rather than a draw function of its own - see the comment there.
        QStringLiteral("ECDF"),
        QStringLiteral("Q-Q Plot"),
        QStringLiteral("KDE Density"),
        QStringLiteral("Bland-Altman"),
        QStringLiteral("Control Chart"),
        QStringLiteral("ROC Curve"),
        QStringLiteral("Volcano Plot"),
        QStringLiteral("Pareto Front"),
        QStringLiteral("Performance Ceiling"),
        QStringLiteral("Global Sensitivity"),
        QStringLiteral("Swarm"),
        // Domain tier. These are the engines the bioelectrochemical work
        // actually needs, and they compute what they plot rather than trusting
        // a column to have been kept in step - power from I and V, removal from
        // the starting concentration, the Gompertz parameters from a real fit.
        QStringLiteral("Gompertz H₂ Kinetics"),
        QStringLiteral("sCOD Degradation Profile"),
        QStringLiteral("VFA Concentration Profile"),
        QStringLiteral("Polarisation & Power Curve"),
        QStringLiteral("EIS: Nyquist"),
        QStringLiteral("EIS: Bode"),
        QStringLiteral("1D Marginal Responses"),
        // Field tier. These have geometry of their own - a grid of cells, level
        // crossings through that grid, a mirrored density, a radial projection.
        QStringLiteral("2D Heatmap"),
        QStringLiteral("2D Contour"),
        QStringLiteral("2D Histogram"),
        QStringLiteral("Hexbin Density"),
        QStringLiteral("Correlation Matrix"),
        QStringLiteral("Violin Plot"),
        QStringLiteral("Raincloud"),
        QStringLiteral("Forest Plot"),
        QStringLiteral("Polar Line"),
        QStringLiteral("Polar Scatter"),
        // 3-D tier, orthographic. The VTK viewport remains the interactive,
        // lit, rotatable option; these are the ones that export as vectors.
        QStringLiteral("3D Line"),
        QStringLiteral("3D Scatter"),
        QStringLiteral("3D Topography / Surface"),
        QStringLiteral("3D Mesh"),
        // Advanced tier, signal transforms and marker variants. Each is one
        // operation on the mapped series, drawn with geometry already here.
        QStringLiteral("Derivative"),
        QStringLiteral("Integral"),
        QStringLiteral("Cumulative Sum"),
        QStringLiteral("Rolling Mean"),
        QStringLiteral("Rolling Median"),
        QStringLiteral("Moving Std"),
        QStringLiteral("Autocorrelation"),
        QStringLiteral("Lag Plot"),
        QStringLiteral("Cumulative Histogram"),
        QStringLiteral("Dot Plot"),
        QStringLiteral("Strip Plot"),
        QStringLiteral("Beeswarm"),
        QStringLiteral("Connected Scatter"),
        QStringLiteral("Lollipop"),
        // The mainstream shapes that were missing. See the note on each in
        // prepareEngineGroup6 and beside its painter.
        QStringLiteral("Bump Chart"),
        QStringLiteral("Dumbbell Plot"),
        QStringLiteral("Gauge"),
        QStringLiteral("Bullet Chart"),
        QStringLiteral("Marimekko Chart"),
        QStringLiteral("Step Mid"),
        QStringLiteral("Fill Between"),
        QStringLiteral("Event Plot"),
        QStringLiteral("Residual Plot"),
        QStringLiteral("Calibration Plot"),
        QStringLiteral("Probability Plot"),
        QStringLiteral("Manhattan Plot"),
        // 3-D and polar variants: the same projection with a different mark.
        QStringLiteral("3D Bar"),
        QStringLiteral("3D Horizontal Bar"),
        QStringLiteral("3D Stem"),
        QStringLiteral("3D Bubble"),
        QStringLiteral("3D Swarm"),
        QStringLiteral("3D Contour"),
        QStringLiteral("Surface + Contours"),
        QStringLiteral("Comet 3D"),
        QStringLiteral("Ribbon"),
        QStringLiteral("Polar Histogram"),
        QStringLiteral("Wind Rose"),
        QStringLiteral("Radar Chart"),
        QStringLiteral("Compass"),
        QStringLiteral("Polar Bubble"),
        QStringLiteral("Covariance Matrix"),
        QStringLiteral("Spy Matrix"),
        // Vector fields. Four mapped columns: x, y and the two components.
        QStringLiteral("Quiver Field"),
        QStringLiteral("Feather"),
        QStringLiteral("Stream Field"),
        QStringLiteral("Stream Particles"),
        QStringLiteral("Phase Portrait"),
        QStringLiteral("Flow Texture (LIC)"),
        QStringLiteral("Divergence Map"),
        QStringLiteral("Vorticity Map"),
        // Geographic. Longitude and latitude are an equirectangular projection
        // and nothing more - there is no basemap, and pretending otherwise
        // would be worse than plotting honest coordinates.
        QStringLiteral("Geo Scatter"),
        QStringLiteral("Geo Line"),
        QStringLiteral("Geo Bubble"),
        QStringLiteral("Geo Density"),
        // Regions filled by a value. The outlines come from the person's own
        // file - see importer.py's boundaries reader - because there is no
        // bundled map here and inventing one would break the promise above.
        QStringLiteral("Choropleth"),
        // The projection is a catalogue variant, so these four gained
        // Mercator, Web Mercator, Lambert conformal conic, azimuthal
        // equidistant and UTM without becoming twenty entries.
        QStringLiteral("Great Circle Route"),
        QStringLiteral("Ground Track"),
        QStringLiteral("Terrain Profile"),
        QStringLiteral("Hypsometric Curve"),
        QStringLiteral("Slope Map"),
        QStringLiteral("Aspect Map"),
        QStringLiteral("Hillshade"),
        // Flight and aviation. Each computes what its chart is read for - the
        // fitted drag coefficients, the lift-curve slope below the stall, the
        // resolved wind components, whether the loading is inside the envelope.
        QStringLiteral("Payload-Range Diagram"),
        QStringLiteral("V-n Flight Envelope"),
        QStringLiteral("Altitude-Mach Envelope"),
        QStringLiteral("Drag Polar"),
        QStringLiteral("Lift Curve"),
        QStringLiteral("Flight Profile"),
        QStringLiteral("Runway Crosswind"),
        QStringLiteral("Weight and Balance Envelope"),
        // Maintenance and reliability. Each reports its fitted parameter in the
        // legend rather than leaving a slope to be measured off the picture.
        QStringLiteral("Weibull Probability Plot"),
        QStringLiteral("Reliability Growth"),
        QStringLiteral("MTBF Trend"),
        QStringLiteral("Calendar Heatmap"),
        QStringLiteral("CUSUM Chart"),
        QStringLiteral("EWMA Chart"),
        QStringLiteral("S-N Fatigue Curve"),
        QStringLiteral("Rainflow Matrix"),
        // Logistics and infrastructure.
        QStringLiteral("Gantt Schedule"),
        QStringLiteral("Availability Timeline"),
        QStringLiteral("Borehole Log"),
        QStringLiteral("OHLC Candlestick"),
        QStringLiteral("Network Graph"),
        QStringLiteral("Chord Diagram"),
        QStringLiteral("Origin-Destination Flow"),
        QStringLiteral("Cumulative Flow"),
        QStringLiteral("Duration Curve"),
        QStringLiteral("Inventory Sawtooth"),
        QStringLiteral("Fundamental Diagram"),
        // Chemistry, materials and the laboratory.
        QStringLiteral("Stress-Strain Curve"),
        QStringLiteral("Arrhenius Plot"),
        QStringLiteral("Titration Curve"),
        QStringLiteral("Calibration Curve"),
        QStringLiteral("Michaelis-Menten"),
        QStringLiteral("Dose-Response Curve"),
        // Medicine, epidemiology and quality.
        QStringLiteral("Kaplan-Meier Survival"),
        QStringLiteral("Funnel Plot"),
        QStringLiteral("X-bar and R Chart"),
        QStringLiteral("Process Capability"),
        // Ocean, hydrology, energy, civil works and the specialised charts.
        QStringLiteral("T-S Diagram"),
        QStringLiteral("CTD Profile"),
        QStringLiteral("Rating Curve"),
        QStringLiteral("Wind Power Curve"),
        QStringLiteral("Drawdown Curve"),
        QStringLiteral("Mass Haul Diagram"),
        QStringLiteral("Shear and Moment"),
        QStringLiteral("Eye Diagram"),
        QStringLiteral("Stereonet"),
        QStringLiteral("Psychrometric Chart"),
        QStringLiteral("Smith Chart"),
        QStringLiteral("Mosaic Plot"),
        QStringLiteral("UpSet Plot"),
        // The gaps a comparison against LabPlot and sci-draw.com turned up.
        QStringLiteral("Dendrogram"),
        QStringLiteral("Precision-Recall Curve"),
        QStringLiteral("Confusion Matrix"),
        QStringLiteral("MA Plot"),
        QStringLiteral("p-Chart"),
        QStringLiteral("np-Chart"),
        QStringLiteral("c-Chart"),
        QStringLiteral("u-Chart"),
        QStringLiteral("Rug Plot"),
        // Data reduction and interpolation. One makes a curve smaller without
        // changing its shape; the other reads it between the measured points.
        QStringLiteral("Data Reduction"),
        QStringLiteral("Interpolation"),
        // Spectral. One radix-2 FFT serves all three; the catalogue variant
        // chooses the window, Hann when nothing asks for another.
        QStringLiteral("Power Spectral Density"),
        QStringLiteral("Spectrogram"),
        QStringLiteral("Cross Correlation"),
        QStringLiteral("Parallel Coordinates"),
        QStringLiteral("Andrews Curves"),
        QStringLiteral("Slope Graph"),
        QStringLiteral("Waterfall"),
        QStringLiteral("Confidence Ellipse"),
        // Composition and hierarchy. These divide a whole rather than plot a
        // coordinate, so they have no axes at all.
        QStringLiteral("Treemap"),
        QStringLiteral("Sunburst"),
        QStringLiteral("Venn Diagram"),
        QStringLiteral("Word Cloud"),
        QStringLiteral("Bubble Cloud"),
        QStringLiteral("Sankey Diagram"),
        QStringLiteral("Ridgeline"),
        QStringLiteral("Population Pyramid"),
        QStringLiteral("Ternary Scatter"),
        QStringLiteral("Piper Diagram"),
        QStringLiteral("Patch"),
        QStringLiteral("Comet"),
        QStringLiteral("Animated Line"),
        QStringLiteral("Scatter + Marginals"),
        QStringLiteral("Plot Matrix"),
        // 3-D fields. VTK stays the right tool for a rotatable, lit,
        // million-cell volume; these are the ones that export as vectors.
        QStringLiteral("3D Quiver"),
        QStringLiteral("Cone Plot"),
        QStringLiteral("Stream Tube"),
        QStringLiteral("Stream Ribbon"),
        QStringLiteral("Tensor Glyph Field"),
        QStringLiteral("Volume Show"),
        QStringLiteral("Volume Slice"),
        QStringLiteral("Isosurface"),
        QStringLiteral("Isonormals"),
        QStringLiteral("Isocaps"),
        QStringLiteral("Contour Slice"),
        // Formula engines. These plot an expression rather than a dataset;
        // see Expression.h for what the parser accepts.
        QStringLiteral("Function Plot"),
        QStringLiteral("Function Contour"),
        QStringLiteral("Function Surface"),
        QStringLiteral("Function Mesh"),
        QStringLiteral("Function 3D Parametric"),
        QStringLiteral("Implicit Function"),
        QStringLiteral("Implicit Surface"),
        QStringLiteral("Lorenz Curve"),
        QStringLiteral("Concentration Curve"),
        QStringLiteral("Rank-Abundance Curve"),
        QStringLiteral("Scree Plot"),
        QStringLiteral("Prediction Error Plot"),
        QStringLiteral("Learning Curve"),
        QStringLiteral("Validation Curve"),
        QStringLiteral("Discrimination Threshold"),
        QStringLiteral("Silhouette Plot"),
        QStringLiteral("Elbow Plot"),
        QStringLiteral("Radial Plot"),
        QStringLiteral("L'Abbé Plot"),
        QStringLiteral("Caterpillar Plot"),
        QStringLiteral("Cumulative Meta-Analysis"),
        QStringLiteral("Nelson-Aalen Cumulative Hazard"),
        QStringLiteral("Cumulative Incidence"),
        QStringLiteral("Decision Curve"),
        QStringLiteral("Bathtub Curve"),
        QStringLiteral("Duane Plot"),
        QStringLiteral("Mean Cumulative Function"),
        QStringLiteral("Moody Diagram"),
        QStringLiteral("Pump Performance Curve"),
        QStringLiteral("Stribeck Curve"),
        QStringLiteral("Haigh Diagram"),
        QStringLiteral("Crack Growth Rate"),
        QStringLiteral("Strain-Life Curve"),
        QStringLiteral("Larson-Miller Curve"),
        QStringLiteral("Campbell Diagram"),
        QStringLiteral("Shaft Orbit"),
        QStringLiteral("Plasticity Chart"),
        QStringLiteral("Particle Size Distribution"),
        QStringLiteral("Nichols Chart"),
        QStringLiteral("Pole-Zero Map"),
        QStringLiteral("P-V Nose Curve"),
        QStringLiteral("Airfoil Cp Distribution"),
        QStringLiteral("Lineweaver-Burk Plot"),
        QStringLiteral("Eadie-Hofstee Plot"),
        QStringLiteral("Hanes-Woolf Plot"),
        QStringLiteral("Scatchard Plot"),
        QStringLiteral("van Deemter Plot"),
        QStringLiteral("BET Plot"),
        QStringLiteral("Job Plot"),
        QStringLiteral("Gutenberg-Richter Plot"),
        QStringLiteral("Double-Mass Curve"),
        QStringLiteral("Hodograph"),
        QStringLiteral("Flow-Volume Loop"),
        QStringLiteral("Pressure-Volume Loop"),
        QStringLiteral("Allan Deviation"),
        QStringLiteral("Paschen Curve"),
        QStringLiteral("Phase-Folded Light Curve"),
        QStringLiteral("O-C Diagram"),
        QStringLiteral("Partial Dependence Plot"),
        QStringLiteral("ICE Plot"),
        QStringLiteral("Influence Plot"),
        QStringLiteral("Added-Variable Plot"),
        QStringLiteral("Interaction Plot"),
        QStringLiteral("Tornado Diagram"),
        QStringLiteral("Efficient Frontier"),
        QStringLiteral("Fan Chart"),
        QStringLiteral("Snail Trail"),
        QStringLiteral("Cost-Effectiveness Plane"),
        QStringLiteral("Acceptability Curve"),
        QStringLiteral("Lasagna Plot"),
        QStringLiteral("Swimmer Plot"),
        // Batch 5: electrochemistry, bioprocess kinetics, energy and
        // structures.
        QStringLiteral("Tafel Plot"),
        QStringLiteral("Cyclic Voltammogram"),
        QStringLiteral("Levich Plot"),
        QStringLiteral("Koutecky-Levich Plot"),
        QStringLiteral("Randles-Sevcik Plot"),
        QStringLiteral("Monod Growth Curve"),
        QStringLiteral("Substrate Inhibition Curve"),
        QStringLiteral("Ragone Plot"),
        QStringLiteral("I-V Curve"),
        QStringLiteral("Degree-Day Signature"),
        QStringLiteral("Abatement Cost Curve"),
        QStringLiteral("Mohr's Circle"),
        QStringLiteral("P-M Interaction Diagram"),
        QStringLiteral("Pushover Capacity Curve"),
        QStringLiteral("Response Spectrum"),
        QStringLiteral("Consolidation Curve"),
        QStringLiteral("Lomb-Scargle Periodogram"),
        QStringLiteral("Waffle Chart"),
        // Batch 6: regression diagnostics, signal structure, multivariate
        // views, and two electrochemistry plots.
        QStringLiteral("Cook's Distance Plot"),
        QStringLiteral("Scale-Location Plot"),
        QStringLiteral("Partial Residual Plot"),
        QStringLiteral("Savitzky-Golay Smoothing"),
        QStringLiteral("LOWESS Trend"),
        QStringLiteral("Partial Autocorrelation"),
        QStringLiteral("Seasonal Decomposition"),
        QStringLiteral("Seasonal Subseries Plot"),
        QStringLiteral("Recurrence Plot"),
        QStringLiteral("Wavelet Scalogram"),
        QStringLiteral("Coherence Spectrum"),
        QStringLiteral("Bode Plot"),
        QStringLiteral("Operating Characteristic Curve"),
        QStringLiteral("Cottrell Plot"),
        QStringLiteral("Coulombic Efficiency Trend"),
        QStringLiteral("Biplot"),
        QStringLiteral("Star Glyph Plot"),
        QStringLiteral("Sunflower Plot"),
        // Batch 7: geochronology, hydrology, thermal analysis,
        // spectroscopy, rheology, acoustics, pharmacokinetics and control.
        QStringLiteral("Concordia Diagram"),
        QStringLiteral("Isochron Plot"),
        QStringLiteral("Harker Diagram"),
        QStringLiteral("Normalised Spider Diagram"),
        QStringLiteral("Keeling Plot"),
        QStringLiteral("IDF Curve"),
        QStringLiteral("Van 't Hoff Plot"),
        QStringLiteral("Tauc Plot"),
        QStringLiteral("Stern-Volmer Plot"),
        QStringLiteral("TGA / DTG Curve"),
        QStringLiteral("DSC Thermogram"),
        QStringLiteral("Creep Curve"),
        QStringLiteral("Master Curve (TTS)"),
        QStringLiteral("Rheology Flow Curve"),
        QStringLiteral("Octave Band Spectrum"),
        QStringLiteral("Pharmacokinetic Profile"),
        QStringLiteral("Yield Curve"),
        QStringLiteral("Nyquist Stability Plot"),
        // Batch 8: geotechnics, flight performance, process integration,
        // metrology, epidemiology and remote sensing.
        QStringLiteral("Proctor Compaction Curve"),
        QStringLiteral("Mohr-Coulomb Envelope"),
        QStringLiteral("Influence Line"),
        QStringLiteral("Balanced Field Length"),
        QStringLiteral("Specific Range"),
        QStringLiteral("Composite Curves (Pinch)"),
        QStringLiteral("Residence Time Distribution"),
        QStringLiteral("Harmonic Spectrum"),
        QStringLiteral("Sound Level Statistics"),
        QStringLiteral("Beam Caustic"),
        QStringLiteral("Youden Plot"),
        QStringLiteral("Levey-Jennings Chart"),
        QStringLiteral("Species Accumulation Curve"),
        QStringLiteral("Epidemic Curve"),
        QStringLiteral("Effective Reproduction Number"),
        QStringLiteral("Cumulative Gain Chart"),
        QStringLiteral("Ratkowsky Square-Root Plot"),
        QStringLiteral("Phenology Curve"),
        // Batch 9: scattering, thermal kinetics, astronomy, radio, and
        // three decision curves from operations and trials.
        QStringLiteral("Guinier Plot"),
        QStringLiteral("Kratky Plot"),
        QStringLiteral("Hill Plot"),
        QStringLiteral("Ellingham Diagram"),
        QStringLiteral("Kissinger Plot"),
        QStringLiteral("Avrami Plot"),
        QStringLiteral("Wilson Plot"),
        QStringLiteral("Van Krevelen Diagram"),
        QStringLiteral("Hertzsprung-Russell Diagram"),
        QStringLiteral("Rotation Curve"),
        QStringLiteral("Hubble Diagram"),
        QStringLiteral("Constellation Diagram"),
        QStringLiteral("Group Delay"),
        QStringLiteral("Wind Profile (Log Law)"),
        QStringLiteral("Experience Curve"),
        QStringLiteral("EOQ Cost Curve"),
        QStringLiteral("Half-Normal Plot"),
        QStringLiteral("Response Waterfall"),
        // Batch 10: open-channel and process hydraulics, power systems,
        // imaging, radiation, exercise physiology, traffic, and two
        // sequential decisions.
        QStringLiteral("Specific Energy Diagram"),
        QStringLiteral("NTU-Effectiveness Curve"),
        QStringLiteral("Pump Operating Point"),
        QStringLiteral("Duck Curve (Net Load)"),
        QStringLiteral("Protection Coordination Curve"),
        QStringLiteral("MTF Curve"),
        QStringLiteral("Radioactive Decay Fit"),
        QStringLiteral("Attenuation Curve"),
        QStringLiteral("Depth-Dose Curve"),
        QStringLiteral("Critical Power Curve"),
        QStringLiteral("Lactate Threshold Curve"),
        QStringLiteral("Half-Power Bandwidth"),
        QStringLiteral("Cumulative Vehicle Count"),
        QStringLiteral("Cohort Retention Curve"),
        QStringLiteral("Bass Diffusion Curve"),
        QStringLiteral("Pareto Chart"),
        QStringLiteral("Statistical Power Curve"),
        QStringLiteral("Sequential Test Boundaries"),
        // Batch 11: separation science, adsorption, molecular biology,
        // magnetics, drives, antennas, soil physics, long-memory
        // statistics and four operational curves.
        QStringLiteral("Chromatogram Peak Metrics"),
        QStringLiteral("Langmuir Isotherm"),
        QStringLiteral("Zeta Potential Curve"),
        QStringLiteral("Distillation Curve"),
        QStringLiteral("qPCR Standard Curve"),
        QStringLiteral("Melting Curve (Tm)"),
        QStringLiteral("Growth Rate (OD)"),
        QStringLiteral("Growth Percentile Chart"),
        QStringLiteral("Hysteresis Loop (B-H)"),
        QStringLiteral("Torque-Speed Curve"),
        QStringLiteral("Radiation Pattern"),
        QStringLiteral("Soil Water Retention Curve"),
        QStringLiteral("Detrended Fluctuation Analysis"),
        QStringLiteral("Poincare Plot"),
        QStringLiteral("Kingman Queue Curve"),
        QStringLiteral("Little's Law Check"),
        QStringLiteral("Burndown Chart"),
        QStringLiteral("Regression Confidence Band"),
        // Batch 12, the last of the rewrites: diffuse reflectance,
        // adsorption, dielectrics, titration, thermal conductivity,
        // measurement-system variation, tolerance, rare events, repeated
        // measures, hydrograph analysis, step response, jitter, well
        // testing and thermal kinetics.
        QStringLiteral("Kubelka-Munk Plot"),
        QStringLiteral("Freundlich Isotherm"),
        QStringLiteral("Cole-Cole Plot"),
        QStringLiteral("Conductometric Titration"),
        QStringLiteral("Hot-Wire Conductivity"),
        QStringLiteral("Multi-Vari Chart"),
        QStringLiteral("Tolerance Interval Plot"),
        QStringLiteral("Rare-Event Interval Chart"),
        QStringLiteral("Spaghetti Plot"),
        QStringLiteral("Unit Hydrograph"),
        QStringLiteral("Recession Curve Analysis"),
        QStringLiteral("Step Response Metrics"),
        QStringLiteral("Jitter Bathtub"),
        QStringLiteral("Pressure Derivative Plot"),
        QStringLiteral("Isoconversional Plot"),
        // Batch 13: the first engine that needed a painter of its own.
        QStringLiteral("Skew-T Log-P"),
        // The Skew-T's three siblings. Same sounding, same thermodynamics,
        // different transform - and the differences are the reason a
        // forecaster keeps more than one of them.
        QStringLiteral("Emagram"),
        QStringLiteral("Stuve Diagram"),
        QStringLiteral("Tephigram"),
        // Batch 15: four more that needed geometry of their own.
        QStringLiteral("Stiff Diagram"),
        QStringLiteral("Arc Diagram"),
        QStringLiteral("Icicle Plot"),
        QStringLiteral("Flame Graph"),
        QStringLiteral("Voronoi Diagram"),
        // Batch 16.
        QStringLiteral("Durov Diagram"),
        QStringLiteral("Tripartite Response Spectrum"),
        // Batch 17.
        QStringLiteral("Cladogram"),
        QStringLiteral("Streamgraph"),
        // Batch 18.
        QStringLiteral("Ternary Contour"),
        // Batch 19.
        QStringLiteral("Alluvial Diagram"),
        QStringLiteral("Hive Plot"),
        // Batch 20.
        QStringLiteral("Pourbaix Diagram"),
        QStringLiteral("Sequence Logo"),
        // Batch 21.
        QStringLiteral("Karyotype Ideogram"),
        QStringLiteral("Circos Plot"),
        QStringLiteral("Alignment Nomogram"),
        // Batch 23.
        QStringLiteral("Dalitz Plot"),
        // Batch 24. The UK system first, because that is the one in use here.
        QStringLiteral("Soil Texture Triangle (UK)"),
        QStringLiteral("Soil Texture Triangle (USDA)"),
        QStringLiteral("QAPF Diagram (Plutonic)"),
        // Batch 26. The same construction for fine-grained rocks.
        QStringLiteral("QAPF Diagram (Volcanic)"),
    };
    return kEngines;
}

// A set built from the same list, so supports() is a hash lookup rather than a
// linear scan of 135 strings.
bool QtPlotBackend::supports(const QString& engine) const {
    static const QSet<QString> kEngineSet=[]{
        QSet<QString> out;
        for(const QString& e:QtPlotBackend().supportedEngines()) out.insert(e);
        return out;
    }();
    return kEngineSet.contains(engine);
}

QFont QtPlotBackend::font(const PlotSpec& spec,double pointSize) const {
    QFont f(spec.style.fontFamily);
    f.setPointSizeF(qMax(1.0,pointSize));
    return f;
}

// The rectangle the figure note occupies, or a null one when there is no note.
//
// Layout and the painter BOTH call this. A note the layout did not reserve room
// for is a note drawn over the x axis label, and a note the painter lays out
// differently from the layout is a gap under every figure that has one. The
// wrap width is the plot area's, which is why this takes it as an argument
// rather than reading f.plotArea - the layout has not built it yet.
static QRectF figureNoteRect(const PlotSpec& spec,const QFont& noteFont,QPaintDevice* device,
                             double left,double bottom,double width){
    if(spec.figureNote.isEmpty()) return QRectF();
    const QFontMetricsF fm(noteFont,device);
    const QRectF wrapped=fm.boundingRect(QRectF(0,0,qMax(40.0,width),0),
                                         Qt::AlignLeft|Qt::AlignTop|Qt::TextWordWrap,
                                         spec.figureNote);
    return QRectF(left,bottom,qMax(40.0,width),qMax(fm.height(),wrapped.height()));
}

QVector<AxisTick> QtPlotBackend::linearTicks(double lo,double hi,int wanted){
    QVector<AxisTick> ticks;
    if(!(finite(lo)&&finite(hi))||hi<=lo) return ticks;
    const double step=niceStep((hi-lo)/qMax(1,wanted));
    const double first=std::ceil(lo/step)*step;
    for(double v=first;v<=hi+step*1e-9;v+=step){
        const double snapped=(std::abs(v)<step*1e-9)?0.0:v;
        ticks.append({snapped,formatTick(snapped,step),false});
        if(ticks.size()>512) break;
    }
    return ticks;
}

QVector<AxisTick> QtPlotBackend::logTicks(double lo,double hi){
    // lo/hi are already log10 values. Majors on decades, minors on 2..9.
    QVector<AxisTick> ticks;
    if(!(finite(lo)&&finite(hi))||hi<=lo) return ticks;
    const int first=int(std::floor(lo)), last=int(std::ceil(hi));
    if(last-first>60) return linearTicks(lo,hi,6);

    // A LABEL EVERY DECADE STOPS WORKING WHEN THERE ARE THIRTY OF THEM.
    //
    // A jitter bathtub runs from about 1e-2 down to 1e-36 - bit error rates
    // really do span that - and every decade got its own label, so the y axis
    // was thirty-six numbers stacked into the height of the plot with no gap
    // between them. Unreadable, and worse than unreadable: it looks like the
    // renderer has failed rather than like an axis covering many decades.
    //
    // So past a dozen decades only every nth is LABELLED. The tick itself is
    // still drawn on every decade - the gridlines are the sense of scale on a
    // log axis and losing them loses that - it just carries no number.
    const int span=last-first;
    const int every=(span>12)?int(std::ceil(double(span)/12.0)):1;
    int majors=0;
    for(int e=first;e<=last;++e){
        const double major=double(e);
        if(major>=lo-1e-9&&major<=hi+1e-9){
            // Measured from zero rather than from `first`, so an axis redrawn
            // over a slightly different range keeps its labels on the same
            // decades instead of shifting them all by one.
            const bool labelled=(every==1)||(((e%every)+every)%every==0);
            if(labelled){
                ticks.append({major,QStringLiteral("10")+superscript(e),false});
                ++majors;
            }else{
                ticks.append({major,QString(),true});
            }
        }
        for(int m=2;m<=9;++m){
            const double minor=e+std::log10(double(m));
            if(minor>=lo&&minor<=hi) ticks.append({minor,QString(),true});
        }
    }

    // An axis with no numbers on it.
    //
    // Majors land only on whole decades, so data spanning LESS than one decade
    // without crossing a power of ten - 2 to 9, 20 to 90, 1.5 to 9 - produced
    // eight unlabelled minor ticks and not a single label. The figure drew, the
    // axis line drew, the axis title drew, and the reader had no way to tell
    // what any position meant. EIS: Bode is one that does this to itself: it
    // sets a log frequency axis, and a sweep inside one decade came out with a
    // bare axis reading "frequency".
    //
    // This matters well beyond one engine: the catalogue carries 245
    // "Logarithmic Scale" entries, 243 "Semi-Log X" and 246 "Semi-Log Y", and
    // a range inside a single decade is an ordinary thing for real data to do.
    //
    // When no decade label lands in range, the minors are labelled with their
    // actual values instead. The positions do not move - the spacing stays
    // logarithmic, because the axis still is - only the labels are added, which
    // is what a reader needs and what other plotting libraries do here.
    if(majors==0&&!ticks.isEmpty()){
        // The decade these minors sit in sets the magnitude, so the label is
        // the real value (2, 3, ... or 20, 30, ...) rather than a mantissa.
        //
        // The precision comes from the SMALLEST gap between adjacent labels,
        // not from the span: formatTick decides decimals from the step it is
        // given, and a span-derived step reads 2 to 9 as "2.0 3.0 ... 9.0"
        // when the values are whole numbers.
        double step=std::numeric_limits<double>::infinity();
        for(int i=1;i<ticks.size();++i)
            step=qMin(step,std::pow(10.0,ticks.at(i).value)
                          -std::pow(10.0,ticks.at(i-1).value));
        if(!(finite(step)&&step>0.0)) step=std::pow(10.0,lo);
        // Nudged, because the gap is computed from two pow() results and lands
        // a hair under the round number it should be: 10^log10(3) - 10^log10(2)
        // is 0.9999999999999998, which formatTick reads as needing a decimal
        // place and labels the axis "2.0 3.0 ... 9.0" instead of "2 3 ... 9".
        step*=1.0+1e-9;
        for(AxisTick& tick:ticks){
            tick.label=formatTick(std::pow(10.0,tick.value),step);
            tick.minor=false;
        }
    }
    return ticks;
}

// The engines whose PREPARED spec is still a set of mapped columns, where the
// two axes describe series 0 and series 1 rather than any series' `.x`.
//
// Every other engine in the catalogue is rewritten by prepareSpec into ordinary
// geometry - a ROC curve becomes a curve, a violin becomes an outline - and its
// painter reads `.x` and `.y`. These twelve are not: their painters read
// `series[0].y` and `series[1].y` as the two positions and `.x` holds whatever
// the x MAPPING happened to be, which for a three-column field is a different
// column entirely.
//
// So computeRange was scaling the axes from one column and prepareSpec was
// LABELLING them from another. A 2-D histogram of a signal against time drew an
// x axis spanning the time column, 0 to 12, with "signal" written under it -
// and the signal only ever ran from 2.2 to 5.2. Nothing looked broken: the
// picture filled the frame, because drawHeatmap stretched its cells across the
// plot area and so agreed with the wrong axis. It surfaced when the cells were
// made to go through toDevice and the data landed somewhere else.
//
// Named explicitly rather than derived from columnPlan().asSeries: that flag is
// true for roughly two hundred engines, almost all of which ARE rewritten, and
// reading their range from series 0 and 1 would break every one of them to fix
// these. The two sets below are the same names engineExplain uses for its
// "needs three mapped columns" and "needs four mapped columns" wording.
// Where a stacked figure is actually drawn.
//
// Two places need this for different reasons - computeRange has to scale an
// axis that contains the stack, drawLegend has to know which corner the stack
// occupies - and a third copy computed by eye is exactly how the axis came to
// be scaled from the individual series while the painter drew the total.
//
// `top` is the final running total at each sample, which is the topmost band's
// outline. `lo` and `hi` are the extremes over every PARTIAL sum, not just the
// final one: with a negative contribution somewhere the tallest band is not
// necessarily the last.
//
// Accumulated exactly as drawStackedLines accumulates it, including its rule
// that a non-finite value contributes zero rather than breaking the band.
// Anything else is a second, subtly different idea of where the bands are.
struct StackedBands {
    QVector<double> top;
    double lo=0.0,hi=0.0;
    bool valid=false;
};
static StackedBands stackedBands(const PlotSpec& spec){
    StackedBands out;
    if(spec.engine!=QLatin1String("Stacked Lines")||spec.series.isEmpty()) return out;
    int n=spec.series.first().x.size();
    for(const PlotSeries& s:spec.series) n=qMin(n,qMin(s.x.size(),s.y.size()));
    if(n<2) return out;
    out.top.fill(0.0,n);
    // The baseline counts: it is drawn.
    for(const PlotSeries& s:spec.series){
        // A series the painter does not stack must not be stacked here either.
        // This function exists to be the SAME accumulation drawStackedLines
        // performs, so the frame contains what gets drawn; a series drawn as a
        // plain line still has to fit inside the frame, but at its own values.
        if(!s.stacked){
            for(int i=0;i<n;++i){
                if(!finite(s.y[i])) continue;
                out.lo=qMin(out.lo,s.y[i]);
                out.hi=qMax(out.hi,s.y[i]);
            }
            continue;
        }
        for(int i=0;i<n;++i){
            out.top[i]+=finite(s.y[i])?s.y[i]:0.0;
            out.lo=qMin(out.lo,out.top[i]);
            out.hi=qMax(out.hi,out.top[i]);
        }
    }
    out.valid=finite(out.lo)&&finite(out.hi);
    return out;
}

static bool columnShapedAxes(const QString& engine){
    // gridFromSeries: series 0 is x, 1 is y, 2 is the value.
    static const QSet<QString> kGridEngines{
        QStringLiteral("2D Heatmap"),QStringLiteral("2D Contour"),
        QStringLiteral("2D Histogram"),QStringLiteral("Hexbin Density")};
    // x, y and the two vector components.
    static const QSet<QString> kVectorEngines{
        QStringLiteral("Quiver Field"),QStringLiteral("Feather"),
        QStringLiteral("Stream Field"),QStringLiteral("Stream Particles"),
        QStringLiteral("Phase Portrait"),QStringLiteral("Flow Texture (LIC)"),
        QStringLiteral("Divergence Map"),QStringLiteral("Vorticity Map")};
    // Longitude and latitude are columns 0 and 1, exactly as x and y are for
    // the fields above. Without this the axes were measured from `.x` - the
    // ROW INDEX the canvas fills in - and from every column's values at once,
    // so two unit squares of ground were drawn into the bottom-left corner of
    // a frame that ran to the largest value in the file. The engine's own
    // measured check found it, by looking for two filled regions and finding
    // none.
    if(engine==QLatin1String("Choropleth")) return true;
    return kGridEngines.contains(engine)||kVectorEngines.contains(engine);
}



// The data range one figure occupies, before any of the chrome is measured.
//
// Split out of computeFrame so the canvas can ask what it is currently showing
// without a painter: an interactive zoom has to start from the range the
// figure already has, and re-deriving that in the canvas would be a second
// copy of these rules - the explicit limits, the flat-series padding, the
// engines that must include zero - that could drift from the drawn one.
//
// Values come back in the space the axis is drawn in, so log10 when the axis
// is logarithmic. That is the space a zoom should be uniform in.

// computeRange, broken into the steps it was already doing.
//
// It was 258 lines and complexity 86 in one body, and its own comments record
// four bugs in it - every one the same shape, a rule right for ONE CLASS of
// engine applied to all of them:
//
//   * the box plot fitted to its lower whisker alone, because x and y were
//     paired and truncated to the shorter, which is right for a curve and
//     wrong for one position with a list of numbers at it;
//   * the Gantt row running off the right-hand edge, the same fault transposed;
//   * the forest plot scaled to the number of studies, because that engine
//     draws sideways and the frame did not know;
//   * the lollipop's leftmost head drawn as a semicircle, because "xLo == 0"
//     was read as "this is measured from zero".
//
// Four instances of one failure in one function is what the length costs here.
// Each rule now sits in a named step with its reasoning beside it, so the next
// special case has an obvious place to go and an obvious scope. That is the
// argument for the split - not tidiness.
//
// THE STEPS RUN IN THE ORDER THEY DID, and each body below is the original
// text MOVED, not rewritten. The locals are bound as references under their
// old names so every line is character-for-character what it was: this
// function decides the frame for all 440 engines, and "looks equivalent" is
// not good enough when the gallery can prove it byte for byte.
namespace {

// The bounds under construction. Every step reads and writes this.
struct AxisBounds {
    double xLo=0.0,xHi=0.0,yLo=0.0,yHi=0.0;
    double y2Lo=0.0,y2Hi=0.0;
    bool sawSecondary=false;
};

// Which axes are logarithmic. A stand-in for Frame, which is private to
// QtPlotBackend and so cannot be named by a free function - and the steps read
// only these three flags from it. The field names match Frame's, so the moved
// bodies still say `f.xLog`.
struct AxisLogs { bool xLog=false,yLog=false,y2Log=false; };

// Which bounds the figure STATED rather than had measured. Carried separately
// because headroom must not move a bound somebody typed.
struct StatedBounds { bool xLo=false,xHi=false,yLo=false,yHi=false; };

// ---- step 1: what the data actually spans -------------------------------
static void measureSeriesBounds(const PlotSpec& spec,const AxisLogs& f,
                                bool columns,bool transposed,AxisBounds& b){
    double &xLo=b.xLo,&xHi=b.xHi,&yLo=b.yLo,&yHi=b.yHi,&y2Lo=b.y2Lo,&y2Hi=b.y2Hi;
    bool &sawSecondary=b.sawSecondary;
    if(columns){
        const QVector<double>& cxs=spec.series.at(0).y;
        const QVector<double>& cys=spec.series.at(1).y;
        const int n=qMin(cxs.size(),cys.size());
        for(int i=0;i<n;++i){
            double x=cxs[i],y=cys[i];
            if(f.xLog){ if(!(x>0)) continue; x=std::log10(x); }
            if(f.yLog){ if(!(y>0)) continue; y=std::log10(y); }
            if(!finite(x)||!finite(y)) continue;
            xLo=qMin(xLo,x); xHi=qMax(xHi,x);
            yLo=qMin(yLo,y); yHi=qMax(yHi,y);
        }
    }else{
        for(const PlotSeries& s:spec.series){
            const int nx=s.x.size(),ny=s.y.size();
            // A SUMMARY AGAINST ONE POSITION, and the half of it that got cut
            // off.
            //
            // Pairing x with y and stopping at the shorter of the two is right
            // for a curve, where both are the same length. It is wrong for the
            // shape several engines use: ONE position, and a list of numbers
            // measured there. A box plot carries x={slot} and
            // y={whisker,q1,median,q3,whisker} - so this took the minimum, one,
            // and fitted the axis to the LOWER WHISKER ALONE. Every box in the
            // catalogue had its median, its upper quartile and its top whisker
            // drawn above the frame and clipped away; the figure showed the
            // bottom of each distribution and nothing else, and looked
            // deliberate.
            //
            // A Gantt row is the same shape transposed - y={row},
            // x={start,end} - so only the start was counted and every bar ran
            // off the right-hand edge.
            //
            // So: when one side is a single position and the other is a list,
            // the position is held and the list is walked. Equal lengths, and
            // the pairwise rule is unchanged.
            // A series on the RIGHT-HAND ordinate is measured into its own
            // range and left out of this one, which is the whole point of
            // having a second axis: see PlotSeries::secondaryAxis.
            double& loY=s.secondaryAxis?y2Lo:yLo;
            double& hiY=s.secondaryAxis?y2Hi:yHi;
            if(s.secondaryAxis) sawSecondary=true;
            const bool summary=(nx==1&&ny>1)||(ny==1&&nx>1);
            const int n=summary?qMax(nx,ny):qMin(nx,ny);
            for(int i=0;i<n;++i){
                // A forest plot is drawn SIDEWAYS, and the frame did not know.
                //
                // drawForest calls toDevice(f, s.y[k], row) - the estimate and
                // its interval go along x, and the study's row number goes up
                // y. The series carries them the other way round, x={row} and
                // y={estimate,low,high}, so measuring x from .x scaled the
                // EFFECT axis to the number of studies: forty studies whose
                // effects lay between 1 and 4 were drawn on an axis running to
                // 40, every interval squeezed into the first tenth of the
                // plot. The y axis only looked right because the rewrite sets
                // it explicitly.
                double x=transposed?s.y[qMin(i,ny-1)]:s.x[qMin(i,nx-1)];
                double y=transposed?s.x[qMin(i,nx-1)]:s.y[qMin(i,ny-1)];
                const bool logY=s.secondaryAxis?f.y2Log:f.yLog;
                if(f.xLog){ if(!(x>0)) continue; x=std::log10(x); }
                if(logY){ if(!(y>0)) continue; y=std::log10(y); }
                if(!finite(x)||!finite(y)) continue;
                xLo=qMin(xLo,x); xHi=qMax(xHi,x);
                loY=qMin(loY,y); hiY=qMax(hiY,y);
            }
        }
    }
}

// ---- step 2: the stacked bands are drawn, so the axis must contain them --
static void includeStackedBands(const PlotSpec& spec,const AxisLogs& f,AxisBounds& b){
    double &yLo=b.yLo,&yHi=b.yHi;
    // A stacked band is drawn at the RUNNING TOTAL, so the axis has to contain
    // the total and not the tallest single series.
    //
    // Five columns each reaching about 7 stack to about 25, and the axis ended
    // at 7.2: the top three bands were drawn off the top of the frame and
    // clipped. The engine's own comment says the point of stacking is that
    // "the total is readable as the top of the band" - and the top of the band
    // was not on the picture.
    //
    // Accumulated exactly as drawStackedLines accumulates it, including its
    // rule that a non-finite value contributes zero rather than breaking the
    // band. Anything else is a second, subtly different idea of where the
    // bands are, which is the arrangement that produced this in the first
    // place. The baseline counts too, because it is drawn.
    {
        const StackedBands stack=stackedBands(spec);
        if(stack.valid){
            if(f.yLog){
                // Only the positive part of a stack exists on a log axis,
                // which is the same rule every other value obeys above.
                if(stack.hi>0.0) yHi=qMax(yHi,std::log10(stack.hi));
                if(stack.lo>0.0) yLo=qMin(yLo,std::log10(stack.lo));
            }else{
                yLo=qMin(yLo,stack.lo); yHi=qMax(yHi,stack.hi);
            }
        }
    }
}

// ---- step 3: a bound the figure was GIVEN outranks one that was measured -
static StatedBounds applyStatedLimits(const PlotSpec& spec,const AxisLogs& f,AxisBounds& b){
    double &xLo=b.xLo,&xHi=b.xHi,&yLo=b.yLo,&yHi=b.yHi;

    // Explicit limits win; GraphVis 17 treats an unset limit as "fit to data".
    //
    // And WIN MEANS WIN: each one that was stated is remembered here, because
    // the headroom added further down would otherwise move it. Typing 100 into
    // the axis maximum produced an axis reading 105, and typing 0 into the
    // minimum produced -5 - the figure quietly ignored the number it had been
    // given and drew a different one. Headroom is for a bound that was
    // MEASURED from the data; a bound that was asked for is already the answer.
    const bool xLoStated=!isUnset(spec.xAxis.min);
    const bool xHiStated=!isUnset(spec.xAxis.max);
    const bool yLoStated=!isUnset(spec.yAxis.min);
    const bool yHiStated=!isUnset(spec.yAxis.max);
    if(xLoStated) xLo=f.xLog?std::log10(qMax(1e-300,spec.xAxis.min)):spec.xAxis.min;
    if(xHiStated) xHi=f.xLog?std::log10(qMax(1e-300,spec.xAxis.max)):spec.xAxis.max;
    if(yLoStated) yLo=f.yLog?std::log10(qMax(1e-300,spec.yAxis.min)):spec.yAxis.min;
    if(yHiStated) yHi=f.yLog?std::log10(qMax(1e-300,spec.yAxis.max)):spec.yAxis.max;
    return StatedBounds{xLoStated,xHiStated,yLoStated,yHiStated};
}

// ---- step 4: a flat series still needs a readable axis ------------------
static void widenFlatAxes(AxisBounds& b){
    double &xLo=b.xLo,&xHi=b.xHi,&yLo=b.yLo,&yHi=b.yHi;

    // A flat series must still produce a readable axis.
    if(qFuzzyCompare(xLo,xHi)){ const double pad=qMax(0.5,std::abs(xLo)*0.05); xLo-=pad; xHi+=pad; }
    if(qFuzzyCompare(yLo,yHi)){ const double pad=qMax(0.5,std::abs(yLo)*0.05); yLo-=pad; yHi+=pad; }
}

// ---- step 5: the engines measured FROM zero must contain it -------------
// Returns whether zero is this engine's baseline on x, because the headroom
// step needs to know: an axis pinned to zero is padded at the far end only.
static bool pinZeroBaseline(const PlotSpec& spec,const AxisLogs& f,AxisBounds& b){
    double &xLo=b.xLo,&xHi=b.xHi,&yLo=b.yLo,&yHi=b.yHi;

    // Anything drawn from a zero baseline is read against zero, so the value
    // axis must include it. Only "Bar" did, and Histogram dispatches to drawBar
    // too: a histogram whose bins all held between 100 and 110 counts got an
    // axis starting near 100, so bars were drawn from there and a 10% spread
    // looked like a 2:1 one. Stem and Lollipop draw stalks from the same
    // baseline. Horizontal Bar draws along x, so it is the x axis that must
    // contain zero.
    const bool zeroOnY = spec.engine==QLatin1String("Bar")
                      || spec.engine==QLatin1String("Histogram")
                      || spec.engine==QLatin1String("Cumulative Histogram")
                      || spec.engine==QLatin1String("Stem")
                      || spec.engine==QLatin1String("Lollipop");
    if(zeroOnY&&!f.yLog){ yLo=qMin(yLo,0.0); yHi=qMax(yHi,0.0); }
    const bool zeroOnX = spec.engine==QLatin1String("Horizontal Bar");
    if(zeroOnX&&!f.xLog){ xLo=qMin(xLo,0.0); xHi=qMax(xHi,0.0); }
    return zeroOnX;
}

// ---- step 6: five per cent, so the topmost mark is not on the frame -----
static void addHeadroom(const AxisLogs& f,bool columns,bool zeroOnX,
                        const StatedBounds& stated,AxisBounds& b){
    double &xLo=b.xLo,&xHi=b.xHi,&yLo=b.yLo,&yHi=b.yHi;
    const bool xLoStated=stated.xLo,xHiStated=stated.xHi;
    const bool yLoStated=stated.yLo,yHiStated=stated.yHi;

    // Five per cent of headroom on the value axis, so the topmost marker is not
    // drawn on the frame. NOT for a field: x has never been padded, so padding
    // y alone would leave a strip of background above and below a heatmap and
    // none at either side, and the cells - which span exactly the bounds just
    // measured - would no longer fill the plot area squarely. A field is read
    // as an image of a region, and a region does not have headroom.
    //
    // A LOG AXIS GETS IT TOO, and was the one kind that never did.
    //
    // The bounds here are already logarithms - measureSeriesBounds takes
    // log10 of every value on a log axis - so the whole of this function works
    // in the space the axis is drawn in, and five per cent of a log span is
    // five per cent of the drawn axis exactly as it is on a linear one. There
    // was no reason for the exclusion and no note explaining it; what it did
    // was leave every log figure's outermost mark drawn hard on the frame line
    // and clipped down the middle. The property check reported three of them -
    // the Power Spectral Density's highest frequency, the Paschen Curve's
    // lowest pressure-distance product, the Tripartite Response Spectrum at
    // both ends - as marks on an edge fitted to the data, which is what they
    // were.
    //
    // The two reasons this step steps aside are unchanged and both are about
    // something other than the scale: a field is a region and has no headroom,
    // and a bound somebody typed is not moved.
    if(!columns){
        // Scaled BEFORE subtracting, not after. (yHi - yLo) * 0.05 overflows to
        // infinity on a column holding both 1e308 and -1e308, and then yLo and
        // yHi become -inf and +inf - so two perfectly finite bounds were turned
        // into infinities by the act of padding them, every coordinate came out
        // NaN, and Qt drew markers from numbers that were not numbers.
        // yHi*0.05 - yLo*0.05 is the same value wherever both forms are
        // representable, and stays representable where the other does not.
        const double pad=yHi*0.05-yLo*0.05;
        if(std::isfinite(pad)){
            if(!yLoStated) yLo-=pad;
            if(!yHiStated) yHi+=pad;
        }
    }

    // AND THE SAME FOR X, which had none.
    //
    // The note above says "x has never been padded" and gives the reason: a
    // field is an image of a region and a region has no headroom. That reason
    // is about FIELDS, and the y pad excludes them for it - `!columns` right
    // there. The x axis was excluded for every engine instead, so the reason
    // was applied far past the case it was about.
    //
    // What that looks like: Global Sensitivity, whose value axis IS x, draws
    // its longest bars running exactly into the right-hand frame line; a
    // Horizontal Bar does the same; the rightmost point of any scatter sits on
    // the frame and is drawn half outside it. The figure reads as clipped.
    //
    // The zero baseline is kept. Horizontal Bar has its xLo clamped to zero
    // just above, and padding both ends would lift the bars off the axis they
    // are measured from - so an axis that was pinned to zero is padded at the
    // far end only, and the pin holds.
    // THE PIN IS THE HORIZONTAL BAR'S, NOT EVERY FIGURE WHOSE X HAPPENS TO
    // REACH ZERO.
    //
    // The pin was written as "xLo == 0", which is true of any column that
    // starts counting at zero - a stem plot's sample index, a scatter's first
    // category, a time axis measured from the start of the run. None of those
    // is measured FROM zero the way a bar's length is; zero is simply where
    // their first point happens to sit. Pinning them cost the leftmost mark its
    // half: the lollipop's new head is drawn straddling the frame line and
    // comes out a semicircle. Same shape of fault as the frame pairing and the
    // bar slot division - a rule that is right for one class applied to all of
    // them by a test that does not mention the class.
    //
    // So the pin now asks whether zero is this engine's BASELINE, which is
    // exactly the question the clamp above already answered.
    //
    // And the log axis here for the same reason as above.
    if(!columns){
        const double pad=xHi*0.05-xLo*0.05;
        if(std::isfinite(pad)){
            // THE PIN IS A LINEAR ZERO. On a log axis the stored bound is the
            // logarithm, so a bound of 0.0 there is the value one, not the
            // origin - and a bar measured from zero cannot be drawn on a log
            // axis at all, because zero is not on it. pinZeroBaseline only
            // clamps when the axis is linear, for exactly that reason; asking
            // its answer about a log axis would pin a decade boundary that
            // happens to be 10^0 and cost that end its padding.
            const bool pinnedLow=(zeroOnX&&!f.xLog&&xLo==0.0);
            const bool pinnedHigh=(zeroOnX&&!f.xLog&&xHi==0.0);
            if(!pinnedLow&&!xLoStated) xLo-=pad;
            if(!pinnedHigh&&!xHiStated) xHi+=pad;
        }
    }
}

// ---- step 6b: a mark one slot wide needs that slot inside the frame -----
//
// A BAR IS NOT A POINT, and the axis was scaled as though it were.
//
// Every bar is drawn CENTRED on its position and one slot wide, where the slot
// is the distance to its nearest neighbour. The axis, though, was fitted to the
// positions themselves and then given five per cent of headroom - so with four
// categories at 0, 1, 2, 3 the axis ran -0.15 to 3.15 while the bars spanned
// -0.5 to 3.5. The first and last bars were drawn half outside the frame and
// clipped down the middle, which is why they sat flush against the frame line
// where every other categorical axis keeps a margin.
//
// Five per cent is a fraction of the SPAN, so how bad this is depends on how
// many categories there are: eleven or more and the headroom already exceeds
// half a slot and the picture was always right, four and it loses a third of
// the outer bars. That is why it read as an inconsistency in the bar chart
// rather than as a fault - it is one, and it is this.
//
// THE SAME QUESTION, ASKED ONCE. The painter measures the slot from the
// closest neighbouring pair of positions; so does this. Both go through
// slotWidthFrom, in the same units, over the same positions, so the frame and
// the painter cannot come to disagree about how wide a bar is - which is the
// disagreement that produced the clipping in the first place.
//
// Taken as a FLOOR on the existing headroom rather than added to it. Where the
// five per cent is already the wider of the two, nothing moves and the figure
// is unchanged; where it is not, the bound opens to exactly half a slot, which
// is the conventional categorical margin and no more. A bar fills 0.78 of its
// slot, so that leaves the usual sliver of background beyond the outer bars.
//
// A STATED BOUND IS NEVER MOVED, the same as in the headroom step: somebody who
// typed a limit gets that limit, clipped bars and all.
//
// Asked by engine name, which is what the dispatch in drawFramedEngine does
// too. There is no exception list to keep in step: the property check in
// PlotSelfTest measures ink against the frame for every engine in the
// catalogue, so an engine that draws slot-wide marks and is missing from here
// comes back as a frame overrun rather than staying quietly wrong.
static void containOuterSlots(const PlotSpec& spec,const AxisLogs& f,
                              const StatedBounds& stated,AxisBounds& b){
    // Which axis the slots are counted along - the one the positions sit on,
    // never the one the lengths are measured on. A Horizontal Bar's rows go up
    // y and its values along x, so it is y that needs the half-slot.
    const bool alongY = spec.engine==QLatin1String("Horizontal Bar")
                     || spec.engine==QLatin1String("Floating Row");
    const bool alongX = spec.engine==QLatin1String("Bar")
                     || spec.engine==QLatin1String("Histogram")
                     || spec.engine==QLatin1String("Floating Bar")
                     || spec.engine==QLatin1String("Candlestick");
    if(!alongX&&!alongY) return;
    // A log axis has no slots to speak of: the positions are decades apart in
    // the drawn space and a "half slot" there is half a decade, which is a
    // margin nobody asked for. Bars on a log position axis are rare enough to
    // leave exactly as they were.
    if(alongY?f.yLog:f.xLog) return;

    QVector<double> centres;
    for(const PlotSeries& s:spec.series){
        const QVector<double>& seats=alongY?s.y:s.x;
        for(double v:seats) if(finite(v)) centres.push_back(v);
    }
    if(centres.size()<2) return;

    double &lo=alongY?b.yLo:b.xLo, &hi=alongY?b.yHi:b.xHi;
    const bool loStated=alongY?stated.yLo:stated.xLo;
    const bool hiStated=alongY?stated.yHi:stated.xHi;

    // The outermost positions BEFORE the headroom step widened the bounds -
    // measured here rather than read off lo and hi, which are no longer where
    // the data is.
    double first=centres.at(0),last=centres.at(0);
    for(double v:centres){ first=qMin(first,v); last=qMax(last,v); }

    // slotWidthFrom clamps to at least 1.0 because it is normally handed
    // device pixels. These are data units, where a slot of 0.001 is perfectly
    // ordinary, so the measurement is done here in the same shape without that
    // clamp. The fallback is unused: there are two positions or more.
    std::sort(centres.begin(),centres.end());
    double slot=std::numeric_limits<double>::infinity();
    // What counts as two distinct positions, in units of the span. The same
    // idea as slotWidthFrom's half-pixel rule - a repeated position is a
    // grouped category, not a narrower bar - expressed relatively because
    // there is no pixel here to be half of.
    const double tiny=qMax(std::abs(first),std::abs(last))*1e-12
                      +(last-first)*1e-9;
    for(int i=1;i<centres.size();++i){
        const double d=centres[i]-centres[i-1];
        if(d>tiny) slot=qMin(slot,d);
    }
    if(!finite(slot)||slot<=0.0) return;

    if(!loStated) lo=qMin(lo,first-slot*0.5);
    if(!hiStated) hi=qMax(hi,last+slot*0.5);
}

// ---- step 7: the right-hand ordinate, settled the same way --------------
// Returns whether there is one to draw.
static bool settleSecondaryAxis(const PlotSpec& spec,const AxisLogs& f,AxisBounds& b){
    double &y2Lo=b.y2Lo,&y2Hi=b.y2Hi;
    const bool sawSecondary=b.sawSecondary;

    // The right-hand ordinate is settled the same way the left one is: its own
    // explicit limits if the figure states them, a pad for a flat series, and
    // five per cent of headroom so the topmost mark is not on the frame.
    if(sawSecondary&&finite(y2Lo)&&finite(y2Hi)){
        const bool y2LoStated=!isUnset(spec.y2Axis.min);
        const bool y2HiStated=!isUnset(spec.y2Axis.max);
        if(y2LoStated) y2Lo=f.y2Log?std::log10(qMax(1e-300,spec.y2Axis.min)):spec.y2Axis.min;
        if(y2HiStated) y2Hi=f.y2Log?std::log10(qMax(1e-300,spec.y2Axis.max)):spec.y2Axis.max;
        if(qFuzzyCompare(y2Lo,y2Hi)){
            const double pad=qMax(0.5,std::abs(y2Lo)*0.05);
            y2Lo-=pad; y2Hi+=pad;
        }
        if(!f.y2Log){
            const double pad=y2Hi*0.05-y2Lo*0.05;
            if(std::isfinite(pad)){
                if(!y2LoStated) y2Lo-=pad;
                if(!y2HiStated) y2Hi+=pad;
            }
        }
        return true;
    }
    return false;
}

} // namespace

QtPlotBackend::Frame QtPlotBackend::computeRange(const PlotSpec& spec) const {
    Frame f;
    f.xLog=spec.xAxis.log10;
    f.yLog=spec.yAxis.log10;
    f.y2Log=spec.y2Axis.log10;
    const AxisLogs logs{f.xLog,f.yLog,f.y2Log};

    AxisBounds b;
    b.xLo=std::numeric_limits<double>::infinity(); b.xHi=-b.xLo;
    b.yLo=b.xLo; b.yHi=b.xHi;
    b.y2Lo=b.xLo; b.y2Hi=b.xHi;


    // See columnShapedAxes above: for these the two positions are columns, not
    // `.x` and `.y`. Everything after this point - the explicit limits, the
    // flat-series padding, the inversion, the finite guard - is the same for
    // both shapes, because it operates on the four bounds and not on how they
    // were arrived at.
    const bool columns=columnShapedAxes(spec.engine)&&spec.series.size()>=2;
    // See the note at the read below: this one painter puts .y along x.
    const bool transposed=spec.engine==QLatin1String("Forest Plot");
    measureSeriesBounds(spec,logs,columns,transposed,b);
    includeStackedBands(spec,logs,b);


    if(!finite(b.xLo)||!finite(b.xHi)){ b.xLo=0; b.xHi=1; }
    if(!finite(b.yLo)||!finite(b.yHi)){ b.yLo=0; b.yHi=1; }

    const StatedBounds stated=applyStatedLimits(spec,logs,b);
    widenFlatAxes(b);
    const bool zeroOnX=pinZeroBaseline(spec,logs,b);
    addHeadroom(logs,columns,zeroOnX,stated,b);
    // After the headroom, because it is a floor on it and not an addition to
    // it: where five per cent of the span is already wider than half a slot,
    // this leaves the bounds exactly where addHeadroom put them.
    containOuterSlots(spec,logs,stated,b);


    // An inverted axis is the same range, mapped the other way round. Doing it
    // by swapping the bounds means every engine, every tick and every export
    // gets it for free: toDevice divides by (hi - lo), which is simply negative
    // now, and nothing else has to know.
    if(spec.xAxis.inverted) std::swap(b.xLo,b.xHi);
    if(spec.yAxis.inverted) std::swap(b.yLo,b.yHi);


    // Last line of defence. Everything above is meant to keep the bounds
    // finite, and every future addition to it is another chance to lose that -
    // padding, a zero baseline, an explicit limit read from a saved figure.
    // A non-finite bound is not a slightly wrong picture, it is NaN in every
    // coordinate and nothing drawn, so it is caught here rather than trusted.
    if(!finite(b.xLo)||!finite(b.xHi)){ b.xLo=0; b.xHi=1; }
    if(!finite(b.yLo)||!finite(b.yHi)){ b.yLo=0; b.yHi=1; }

    if(settleSecondaryAxis(spec,logs,b)){
        f.hasY2=true; f.y2Lo=b.y2Lo; f.y2Hi=b.y2Hi;
    }

    f.xLo=b.xLo; f.xHi=b.xHi; f.yLo=b.yLo; f.yHi=b.yHi;
    return f;
}

// What the canvas asks. prepareSpec first, because for a histogram or a violin
// the drawn range belongs to the rewritten geometry rather than to the mapped
// column - a histogram of values 0 to 1 is drawn against counts.
QtPlotBackend::DataRange QtPlotBackend::rangeFor(const PlotSpec& spec) const {
    const Frame f=computeRange(preparedCached(spec));
    return DataRange{f.xLo,f.xHi,f.yLo,f.yHi,f.xLog,f.yLog};
}

// Acklam's inverse normal CDF, defined with the other statistics further down.
// Declared here because computeFrame needs it to place a probability axis's
// ticks, and moving the definition up would put a statistics routine in the
// middle of the frame geometry.

QtPlotBackend::Frame QtPlotBackend::computeFrame(QPainter* p,const QRectF& target,const PlotSpec& spec,
                                                 QVector<AxisTick>& xTicks,QVector<AxisTick>& yTicks) const {
    Frame f=computeRange(spec);
    const double xLo=f.xLo,xHi=f.xHi,yLo=f.yLo,yHi=f.yHi;

    // Ticks are chosen over the range in its natural order; which end of the
    // picture each one lands on is toDevice's business.
    const double xFrom=qMin(xLo,xHi),xTo=qMax(xLo,xHi);
    const double yFrom=qMin(yLo,yHi),yTo=qMax(yLo,yHi);
    // 7 across and 6 up unless the figure asks for something else. Clamped:
    // below two an axis has no scale to read, and above about twenty-five the
    // labels collide and the grid becomes the picture.
    const int wantX=spec.style.gridDensity>0?qBound(2,spec.style.gridDensity,25):7;
    // The vertical takes its own number when one is set. Falling back to
    // "gridDensity - 1" keeps every figure made before the axes could differ
    // looking exactly as it did, including the 7-and-6 default.
    const int wantY=spec.style.gridDensityY>0
                        ? qBound(2,spec.style.gridDensityY,25)
                        : (spec.style.gridDensity>0
                               ? qBound(2,spec.style.gridDensity-1,25) : 6);
    xTicks=f.xLog?logTicks(xFrom,xTo):linearTicks(xFrom,xTo,wantX);
    yTicks=f.yLog?logTicks(yFrom,yTo):linearTicks(yFrom,yTo,wantY);

    // AN AXIS OF CATEGORIES supplies its own ticks and keeps them.
    //
    // The chooser above picks round numbers, which is right for a measurement
    // and wrong for a category. A confusion matrix ran from -0.5 to 3.5 with
    // its cells at 0, 1, 2 and 3, and got ticks at 0.5, 1.5 and 2.5 - three
    // labels naming classes that do not exist, and none naming the four that
    // do. See PlotAxis::tickValues.
    //
    // Only the ticks inside the drawn range, so a zoom still behaves: the
    // categories outside the window are not labelled on its edge.
    const auto categoryTicks=[](const PlotAxis& axis,double from,double to){
        QVector<AxisTick> out;
        const int n=qMin(axis.tickValues.size(),axis.tickLabels.size());
        for(int i=0;i<n;++i){
            const double v=axis.tickValues.at(i);
            if(!finite(v)||v<from||v>to) continue;
            out.append({v,axis.tickLabels.at(i),false});
        }
        return out;
    };
    if(!spec.xAxis.tickValues.isEmpty()){
        const QVector<AxisTick> given=categoryTicks(spec.xAxis,xFrom,xTo);
        if(!given.isEmpty()) xTicks=given;
    }
    if(!spec.yAxis.tickValues.isEmpty()){
        const QVector<AxisTick> given=categoryTicks(spec.yAxis,yFrom,yTo);
        if(!given.isEmpty()) yTicks=given;
    }

    // A PROBABILITY axis: the values are normal quantiles, the labels are per
    // cent, and the spacing between them is uneven by construction. That
    // uneven spacing is the whole mechanism - it is what makes a normal sample
    // plot as a straight line - so the ticks cannot be chosen by the ordinary
    // "seven evenly spaced round numbers" rule. They are the percentiles the
    // form is conventionally read at, each placed at its own quantile.
    //
    // Set by the Probability Plot rewrite. Without it that engine relabelled
    // itself to Q-Q Plot and the two were one engine with two names; with it
    // the reader can take a percentile off the axis, which is the reason
    // reliability and hydrology use this form rather than the Q-Q.
    if(spec.parameter(QStringLiteral("@probabilityAxisY"),0.0)>0.5&&!f.yLog){
        static const double kPercents[]={0.1,1,2,5,10,25,50,75,90,95,98,99,99.9};
        QVector<AxisTick> probability;
        for(const double percent:kPercents){
            const double atQuantile=normalQuantile(percent/100.0);
            if(!finite(atQuantile)) continue;
            if(atQuantile<yFrom||atQuantile>yTo) continue;
            AxisTick tick;
            tick.value=atQuantile;
            tick.label=(percent<1.0||percent>99.0)
                           ? QString::number(percent,'f',1)
                           : QString::number(percent,'f',0);
            probability.append(tick);
        }
        // Only if enough of them are in range to be a scale. A window that
        // happens to contain two percentiles is better served by the ordinary
        // ticks than by two labels and nothing between them.
        if(probability.size()>=3) yTicks=probability;
    }

    // Widen the left margin so the longest y label always fits.
    const QFontMetricsF fm(font(spec,spec.style.tickSize),p->device());
    double widest=0;
    for(const AxisTick& t:yTicks) if(!t.minor) widest=qMax(widest,fm.horizontalAdvance(t.label));
    const double left=qMax(kMarginLeft,widest+kTickLen+14.0+fm.height());

    // Room on the right for the colour bar, when the engine is one that paints
    // a quantity as colour. Reserved here rather than taken out of the plot
    // area later, because a bar drawn over the frame is a bar drawn over data.
    // The LAST x label is centred on the last tick, so half of it hangs past
    // the right end of the frame. kMarginRight is 18 points, which is enough
    // for "20" and not for "1.80e+06" - and the half that does not fit is
    // simply cut off by the edge of the canvas. Found by asking every engine
    // whether it puts ink on the rim: an acceptability curve, whose x axis is a
    // willingness-to-pay in hundreds of thousands, was the one that did.
    //
    // The whole last label rather than half of it, so a figure exported at a
    // different width does not lose it again.
    double lastXLabel=0.0;
    for(const AxisTick& t:xTicks)
        if(!t.minor) lastXLabel=qMax(lastXLabel,fm.horizontalAdvance(t.label));
    double right=qMax(kMarginRight,lastXLabel*0.5+6.0);
    // Room on the right for the SECOND ORDINATE, when the figure has one.
    // Reserved here for the same reason the colour bar's room is: numbers
    // drawn over the frame are numbers drawn over data.
    if(f.hasY2){
        // widestY2, not widest: the primary axis has its own `widest` above
        // and both are live here.
        double widestY2=0.0;
        for(const AxisTick& t:secondaryTicks(spec,f))
            if(!t.minor) widestY2=qMax(widestY2,fm.horizontalAdvance(t.label));
        right+=kTickLen+4.0+widestY2
              +(spec.y2Axis.label.isEmpty()?0.0:fm.height()*1.5);
    }
    if(usesColourMap(spec.engine)){
        right+=fm.height()*0.9                                        // gap
              +qMax(8.0,fm.height()*0.85)                             // the bar
              +3.0+5.0
              +fm.horizontalAdvance(QStringLiteral("-0.00e+00"))+4.0  // its numbers
              +fm.height()*1.4;                                       // the caption
    }
    // Room for the figure note, when there is one. Taken out of the plot area
    // rather than drawn under it: the canvas is the size it is, and a note
    // painted below kMarginBottom would fall off the bottom of an export.
    const double noteWidth=qMax(10.0,target.width()-left-right);
    const double noteHeight=figureNoteRect(spec,font(spec,spec.style.tickSize*0.95),
                                           p->device(),0,0,noteWidth).height();
    const double bottomMargin=kMarginBottom+(noteHeight>0?noteHeight+6.0:0.0);

    f.plotArea=QRectF(target.left()+left,target.top()+kMarginTop,
                      noteWidth,
                      qMax(10.0,target.height()-kMarginTop-bottomMargin));

    // ONE UNIT ACROSS IS ONE UNIT UP, where the figure says so.
    //
    // See PlotSpec::equalAspect. Mohr's circle is read by how round it is -
    // the radius IS the maximum shear - and both of its axes carry stress in
    // the same unit, so fitting each to its own range drew it as an ellipse
    // with nothing in the picture to say the distortion belonged to the frame
    // rather than to the material. An impedance locus, a shaft orbit, a
    // hodograph and a Poincare plot all have the same property.
    //
    // Done here and not in computeRange because it needs the plot area, which
    // is only known once the labels have been measured. The shorter range is
    // WIDENED about its own centre rather than the longer one cropped: cropping
    // would hide data to preserve a shape, which is the wrong way round.
    //
    // The ticks were chosen over the old range. Re-chosen over the new one, or
    // a widened axis would be labelled only across the part it used to cover.
    if(spec.equalAspect&&!f.xLog&&!f.yLog
       &&f.plotArea.width()>1.0&&f.plotArea.height()>1.0){
        const double xSpan=std::abs(f.xHi-f.xLo),ySpan=std::abs(f.yHi-f.yLo);
        if(xSpan>0.0&&ySpan>0.0){
            const double perPixelX=xSpan/f.plotArea.width();
            const double perPixelY=ySpan/f.plotArea.height();
            const double unit=qMax(perPixelX,perPixelY);
            const auto widen=[&](double& lo,double& hi,double want){
                const double centre=(lo+hi)*0.5;
                const double half=want*0.5;
                const bool flipped=hi<lo;
                lo=flipped?centre+half:centre-half;
                hi=flipped?centre-half:centre+half;
            };
            widen(f.xLo,f.xHi,unit*f.plotArea.width());
            widen(f.yLo,f.yHi,unit*f.plotArea.height());
            const double nxFrom=qMin(f.xLo,f.xHi),nxTo=qMax(f.xLo,f.xHi);
            const double nyFrom=qMin(f.yLo,f.yHi),nyTo=qMax(f.yLo,f.yHi);
            xTicks=linearTicks(nxFrom,nxTo,wantX);
            yTicks=linearTicks(nyFrom,nyTo,wantY);
        }
    }

    lastPlotArea_=f.plotArea;
    lastTarget_=target;
    return f;
}

QVector<AxisTick> QtPlotBackend::secondaryTicks(const PlotSpec& spec,
                                                const QtPlotBackend::Frame& f) const {
    if(!f.hasY2) return {};
    const double from=qMin(f.y2Lo,f.y2Hi),to=qMax(f.y2Lo,f.y2Hi);
    // The same count the left-hand ordinate uses, so the two sets of gridlines
    // do not come out at different densities on the same figure.
    const int wantY=spec.style.gridDensityY>0
                        ? qBound(2,spec.style.gridDensityY,25)
                        : (spec.style.gridDensity>0
                               ? qBound(2,spec.style.gridDensity-1,25) : 6);
    return f.y2Log?logTicks(from,to):linearTicks(from,to,wantY);
}

QPointF QtPlotBackend::toDeviceOn(const Frame& f,double x,double y,bool secondary) const {
    if(!secondary||!f.hasY2) return toDevice(f,x,y);
    // The same mapping as toDevice, against the right-hand ordinate's own
    // range. Written as a temporary frame rather than as a second copy of the
    // arithmetic: the overflow handling and the signed span below are subtle
    // enough that a second copy would drift from this one.
    Frame g=f;
    g.yLo=f.y2Lo; g.yHi=f.y2Hi; g.yLog=f.y2Log;
    return toDevice(g,x,y);
}

QPointF QtPlotBackend::toDevice(const Frame& f,double x,double y) const {
    if(f.xLog) x=(x>0)?std::log10(x):f.xLo;
    if(f.yLog) y=(y>0)?std::log10(y):f.yLo;
    // Where a value sits in [lo, hi], as a fraction. Two things have to be got
    // right, and both were found by looking at what was drawn rather than by
    // reading this code:
    //
    // The span is SIGNED. An inverted axis has hi below lo, and clamping the
    // span up to a positive floor - which qMax(1e-300, span) does - turns
    // every coordinate into an infinity and draws nothing at all. Only a span
    // of exactly zero needs the floor.
    //
    // The span can also OVERFLOW. A column holding both 1e308 and -1e308 has a
    // range wider than a double can represent: hi - lo is +inf, (v - lo) is
    // +inf as well, and inf/inf is NaN - so every coordinate became NaN and Qt
    // drew shapes out of numbers that were not numbers ("QPainterPath::arcTo:
    // Adding arc where a parameter is NaN", once per marker). Halving all
    // three is an exact fix rather than an approximate one: division by two is
    // exact in binary floating point, and the ratio (v-lo)/(hi-lo) is
    // unchanged by scaling v, lo and hi together.
    const auto fraction=[](double v,double lo,double hi){
        double span=hi-lo;
        for(int halvings=0;!std::isfinite(span)&&halvings<8;++halvings){
            v*=0.5; lo*=0.5; hi*=0.5;
            span=hi-lo;
        }
        return (v-lo)/((std::abs(span)>1e-300)?span:1e-300);
    };
    const double tx=fraction(x,f.xLo,f.xHi);
    const double ty=fraction(y,f.yLo,f.yHi);
    return QPointF(f.plotArea.left()+tx*f.plotArea.width(),
                   f.plotArea.bottom()-ty*f.plotArea.height());
}

void QtPlotBackend::drawChrome(QPainter* p,const Frame& f,const PlotSpec& spec,
                               const QVector<AxisTick>& xTicks,const QVector<AxisTick>& yTicks) const {
    // A figure that brings its own coordinate system gets no rectangular one.
    //
    // See PlotSpec::framed: engineHasAxes decides this for an engine drawn by
    // its own painter, and cannot for one that REWRITES onto a painter that
    // does have axes - a ternary scatter becomes a Line Chart so that
    // drawLineChart can draw its triangle, and the frame decision was then
    // being taken on the name "Line Chart".
    //
    // The TITLE is not chrome in that sense: it names the figure whatever
    // coordinates the figure is drawn in, so it is still drawn here. Nothing
    // else is - no box, no ticks, no grid, no axis labels.
    if(!spec.framed){
        if(spec.title.isEmpty()) return;
        p->save();
        p->setPen(spec.style.foreground);
        p->setFont(font(spec,spec.style.titleSize));
        const QFontMetricsF tfm(p->font(),p->device());
        p->drawText(QRectF(f.plotArea.left(),f.plotArea.top()-tfm.height()-8,
                           f.plotArea.width(),tfm.height()+4),
                    Qt::AlignHCenter|Qt::AlignVCenter,spec.title);
        p->restore();
        return;
    }
    p->save();
    const QColor fg=spec.style.foreground;

    if(spec.style.gridVisible){
        QPen grid(spec.style.gridColor); grid.setWidthF(0.6); p->setPen(grid);
        for(const AxisTick& t:xTicks){ if(t.minor) continue;
            const double dx=toDevice(f,f.xLog?std::pow(10.0,t.value):t.value,f.yLo).x();
            p->drawLine(QPointF(dx,f.plotArea.top()),QPointF(dx,f.plotArea.bottom())); }
        for(const AxisTick& t:yTicks){ if(t.minor) continue;
            const double dy=toDevice(f,f.xLo,f.yLog?std::pow(10.0,t.value):t.value).y();
            p->drawLine(QPointF(f.plotArea.left(),dy),QPointF(f.plotArea.right(),dy)); }
    }

    QPen axis(fg); axis.setWidthF(qMax(0.5,spec.style.lineWidth*0.8)); p->setPen(axis);
    p->drawRect(f.plotArea);

    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    p->setFont(tickFont);

    for(const AxisTick& t:xTicks){
        const double dx=toDevice(f,f.xLog?std::pow(10.0,t.value):t.value,f.yLo).x();
        const double len=t.minor?kTickLen*0.5:kTickLen;
        p->drawLine(QPointF(dx,f.plotArea.bottom()),QPointF(dx,f.plotArea.bottom()+len));
        if(t.minor||t.label.isEmpty()) continue;
        p->drawText(QRectF(dx-45,f.plotArea.bottom()+len+2,90,fm.height()),
                    Qt::AlignHCenter|Qt::AlignTop,t.label);
    }
    for(const AxisTick& t:yTicks){
        const double dy=toDevice(f,f.xLo,f.yLog?std::pow(10.0,t.value):t.value).y();
        const double len=t.minor?kTickLen*0.5:kTickLen;
        p->drawLine(QPointF(f.plotArea.left()-len,dy),QPointF(f.plotArea.left(),dy));
        if(t.minor||t.label.isEmpty()) continue;
        p->drawText(QRectF(f.plotArea.left()-len-8-200,dy-fm.height()/2,200,fm.height()),
                    Qt::AlignRight|Qt::AlignVCenter,t.label);
    }

    // THE RIGHT-HAND ORDINATE, when some series is drawn against one.
    //
    // Its ticks go OUTSIDE the frame on the right - no gridlines, because a
    // second set of horizontal lines through the same plot area cannot be told
    // from the first and the reader would not know which axis either belonged
    // to. The numbers, and the axis title, are what carry it.
    if(f.hasY2){
        const QVector<AxisTick> y2=secondaryTicks(spec,f);
        double widest=0.0;
        for(const AxisTick& t:y2){
            const double dy=toDeviceOn(f,f.xLo,f.y2Log?std::pow(10.0,t.value):t.value,true).y();
            const double len=t.minor?kTickLen*0.5:kTickLen;
            p->drawLine(QPointF(f.plotArea.right(),dy),QPointF(f.plotArea.right()+len,dy));
            if(t.minor||t.label.isEmpty()) continue;
            widest=qMax(widest,fm.horizontalAdvance(t.label));
            p->drawText(QRectF(f.plotArea.right()+len+4,dy-fm.height()/2,200,fm.height()),
                        Qt::AlignLeft|Qt::AlignVCenter,t.label);
        }
        if(!spec.y2Axis.label.isEmpty()){
            p->save();
            p->setFont(font(spec,spec.style.axisLabelSize));
            const QFontMetricsF afm(p->font(),p->device());
            p->translate(f.plotArea.right()+kTickLen+8.0+widest+afm.height(),
                         f.plotArea.center().y());
            // THE SAME WAY ROUND AS THE LEFT-HAND LABEL, which uses -90 a few
            // lines below. Rotating the secondary label +90 instead turned it
            // through 180 degrees relative to its neighbour: on the X-bar and R
            // chart "subgroup range" came out upside down beside a "subgroup
            // mean" that read normally, which looks like a font fault rather
            // than a rotation.
            //
            // Bottom-to-top on both sides is what matplotlib's twinx,
            // Origin and Excel all produce, and it is the only choice that
            // lets a reader tilt their head once.
            p->rotate(-90);
            p->drawText(QRectF(-f.plotArea.height()/2,-afm.height(),
                               f.plotArea.height(),afm.height()*1.6),
                        Qt::AlignHCenter|Qt::AlignVCenter,spec.y2Axis.label);
            p->restore();
        }
    }

    p->setFont(font(spec,spec.style.axisLabelSize));
    if(!spec.xAxis.label.isEmpty())
        p->drawText(QRectF(f.plotArea.left(),f.plotArea.bottom()+kTickLen+fm.height()+6,
                           f.plotArea.width(),fm.height()+6),
                    Qt::AlignHCenter|Qt::AlignTop,spec.xAxis.label);
    if(!spec.yAxis.label.isEmpty()){
        p->save();
        p->translate(f.plotArea.left()-kMarginLeft+12,f.plotArea.center().y());
        p->rotate(-90);
        p->drawText(QRectF(-f.plotArea.height()/2,-fm.height(),f.plotArea.height(),fm.height()*1.6),
                    Qt::AlignHCenter|Qt::AlignVCenter,spec.yAxis.label);
        p->restore();
    }
    if(!spec.title.isEmpty()){
        p->setFont(font(spec,spec.style.titleSize));
        const QFontMetricsF tfm(p->font(),p->device());
        p->drawText(QRectF(f.plotArea.left(),f.plotArea.top()-tfm.height()-8,
                           f.plotArea.width(),tfm.height()+4),
                    Qt::AlignHCenter|Qt::AlignVCenter,spec.title);
    }

    // The figure note, under the x axis label, in the foreground colour at
    // reduced strength - it has to be readable without competing with the data,
    // and a note nobody reads is the same as no note at all.
    if(!spec.figureNote.isEmpty()){
        const QFont noteFont=font(spec,spec.style.tickSize*0.95);
        p->setFont(noteFont);
        const QFontMetricsF nfm(noteFont,p->device());
        const QRectF box=figureNoteRect(spec,noteFont,p->device(),
                                        f.plotArea.left(),
                                        f.plotArea.bottom()+kTickLen+nfm.height()
                                            +fm.height()+10,
                                        f.plotArea.width());
        QColor ink=spec.style.foreground;
        ink.setAlphaF(0.72);
        p->setPen(ink);
        p->drawText(box,Qt::AlignLeft|Qt::AlignTop|Qt::TextWordWrap,spec.figureNote);
    }
    p->restore();
}

void QtPlotBackend::drawLegend(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    // Cleared first, so a figure whose legend is suppressed does not leave the
    // last one's rectangle behind for the next reader of it.
    lastLegendRect_=QRectF();
    if(!spec.legendVisible) return;

    // A FIELD HAS A COLOUR BAR, and no legend.
    //
    // Its series are the x, y and value COLUMNS rather than things to name, so
    // a legend over a field lists the column headings against swatches in
    // colours that appear nowhere in the picture. Most rewrites onto a field
    // remember to turn the legend off; the lasagna plot did not, and carried a
    // three-row key reading "time", "subject", "value" across the top-right
    // corner of its own heatmap.
    //
    // Stated once here rather than at each rewrite, because "did the author
    // remember" is not a property a figure should depend on. The 3-D engines
    // are excluded: they use a colour map too, and they draw no colour bar.
    if(usesColourMap(spec.engine)&&!spec.engine.startsWith(QLatin1String("3D ")))
        return;

    // A series with NO LABEL gets no row.
    //
    // This drew a row per series whatever the label said, which made an empty
    // label an empty row rather than no row - and several engines already rely
    // on the opposite. The ground-track rewrite clears the label on each piece
    // after an antimeridian cut and says "one legend entry for the track"; it
    // got one entry and several blank ones. The comet's faded trail is six
    // pieces per series, so with five columns it produced a legend of
    // thirty-five rows, thirty of them blank, taller than the plot area it was
    // drawn over.
    //
    // So an unlabelled series is a piece of a figure rather than a thing in its
    // own right, and the legend is a list of things.
    // And a NAME already in the list gets no second row.
    //
    // A flight profile emits one series per leg per column and names each with
    // the leg, so five columns of the same shape produced "top of climb 5",
    // "top of climb 5", "top of climb 1", "top of climb 1" ... - a legend that
    // repeats itself is a legend whose rows the reader has to check against
    // each other before believing any of them. The first series to claim a
    // name keeps it, so the swatch shown is the one drawn first.
    QVector<const PlotSeries*> listed;
    listed.reserve(int(spec.series.size()));
    QSet<QString> seen;
    for(const PlotSeries& s:spec.series){
        if(s.label.isEmpty()) continue;
        if(seen.contains(s.label)) continue;
        seen.insert(s.label);
        listed.append(&s);
    }
    // Still two, but two NAMED ones. One name and nine anonymous pieces is a
    // caption, and the figure's own labels already say it.
    if(listed.size()<2) return;

    p->save();
    const QFont lf=font(spec,spec.style.legendSize);
    p->setFont(lf);
    const QFontMetricsF fm(lf,p->device());
    // NEVER WIDER THAN A THIRD OF THE PLOT.
    //
    // The width was the longest label and nothing else, so a lift curve whose
    // series are named "signal (a 0.1851 /deg, 10.604 /rad, CLmax 5.238)" drew
    // a legend that covered the right-hand half of the figure. A fitted
    // parameter belongs in a label, and a label belongs inside a box that
    // leaves the graph visible: past the cap the text is elided, which says
    // there is more without spending the picture on it.
    const double labelCap=qMax(60.0,f.plotArea.width()/3.0-34.0);
    // A series on the RIGHT-HAND ordinate says so in its key.
    //
    // Two quantities on one figure are read off two different axes, and
    // nothing else on the page says which series belongs to which. Without
    // this the reader has a power curve and a voltage curve, two ordinates,
    // and no way to pair them up - which is worse than the single crowded axis
    // the second one was added to fix.
    const auto keyFor=[&](const PlotSeries* s){
        return (s->secondaryAxis&&f.hasY2)
                   ? s->label+QStringLiteral("  (right)")
                   : s->label;
    };
    double widest=0;
    for(const PlotSeries* s:listed)
        widest=qMax(widest,qMin(labelCap,fm.horizontalAdvance(keyFor(s))));
    // WRAPPING IS A CHOICE, not a rule - see PlotStyle::legendLabels. Eliding
    // keeps one row per series and cuts the label; wrapping keeps the label and
    // spends rows on it. Which is right depends on whether the reader is
    // comparing twenty series or quoting one number, so the figure is asked
    // rather than told.
    const bool wrapLabels=(spec.style.legendLabels==1);
    // How many lines each row needs once wrapped. Measured with the same font
    // metrics the text is drawn with, so the box reserves exactly what gets
    // used - a row counted short is a row drawn over its neighbour.
    QVector<int> lineCount(listed.size(),1);
    int totalLines=listed.size();
    if(wrapLabels){
        totalLines=0;
        for(int i=0;i<listed.size();++i){
            const QRectF need=fm.boundingRect(QRectF(0,0,widest+4,1e6),
                                              Qt::AlignLeft|Qt::TextWordWrap,
                                              keyFor(listed.at(i)));
            lineCount[i]=qBound(1,int(std::ceil(need.height()/qMax(1.0,fm.height()))),6);
            totalLines+=lineCount[i];
        }
    }
    // THE KEY IS A PICTURE OF WHAT THE SERIES DRAWS.
    //
    // Every row drew an 18-pixel stroke, whatever the series was. On a scatter
    // that is a line the figure does not contain; on the graduated-symbol map
    // it is worse than absent, because the sizes ARE the reading and the key
    // showed five identical strokes against five different size classes.
    //
    // So the row is given room for the largest marker it has to show, and the
    // swatch column widens with it - a 14pt bubble in an 18-pixel slot would be
    // clipped to a sliver and read as the same size as the 10pt one above it.
    double keyMark=0.0;
    for(const PlotSeries* s:listed)
        if(s->drawMarkers) keyMark=qMax(keyMark,s->markerSize);
    const double swatchW=qBound(18.0,keyMark+6.0,34.0);
    const double rowH=qMax(fm.height()+3,qMin(keyMark,26.0)+4.0);
    const double boxW=widest+swatchW+16;
    // Never taller than the area it sits in. A legend that overflows the plot
    // is drawn across the axis labels and off the bottom of the figure, which
    // is what thirty-five rows did.
    const double boxH=qMin(rowH*totalLines+8,f.plotArea.height()-16.0);

    // Which corner. It was always the top right, and in this catalogue the top
    // right is very often where the data is: a stacked band, a cumulative
    // curve and a raincloud's last group all finish underneath it. A legend
    // over the data is worse than a legend anywhere else, because the reader
    // cannot see what is missing - there is nothing to notice.
    //
    // Counted, not guessed. Each candidate is scored by how many of the
    // figure's own points land inside it and the emptiest wins. TIES GO TO THE
    // TOP RIGHT, and the order below is what decides a tie, so a figure with
    // room in every corner is drawn exactly where it always was and nothing
    // already published moves.
    //
    // Points are sampled with a stride rather than all read: a legend box is a
    // large target and a few thousand samples settle which corner is busiest
    // long before a million would. Reading them all would put an O(n) pass
    // into every repaint to place a box that is 90 pixels wide.
    const QRectF candidates[4]={
        QRectF(f.plotArea.right()-boxW-8,f.plotArea.top()+8,boxW,boxH),
        QRectF(f.plotArea.left()+8,      f.plotArea.top()+8,boxW,boxH),
        QRectF(f.plotArea.right()-boxW-8,f.plotArea.bottom()-boxH-8,boxW,boxH),
        QRectF(f.plotArea.left()+8,      f.plotArea.bottom()-boxH-8,boxW,boxH)};
    // Measured in "sample-columns of coverage", so a point and a fill can be
    // added together: one point contributes 1, and a fill contributes the
    // FRACTION of the box's height it covers at that sample.
    //
    // The fraction is the part I got wrong twice. A boolean "does the fill
    // reach into this box" scored the stacked figure's top-left and top-right
    // boxes exactly the same - the band's top is 12.3 on the left and 15.0 on
    // the right, and the box floor is at 11.6, so the fill reaches into both -
    // and the tie handed it back to the top right for a third build running.
    // The eye is not asking whether the fill reaches the box; it is asking how
    // much of the box the fill takes up. On that measure the left corner is
    // 17% covered and the right is 81%, which is what the picture looks like.
    double occupancy[4]={0.0,0.0,0.0,0.0};
    {
        int total=0;
        for(const PlotSeries& s:spec.series) total+=qMin(s.x.size(),s.y.size());
        const int stride=qMax(1,total/4000);

        // A POINT that is drawn: counted if it lands in the box.
        const auto scorePoint=[&](double x,double y){
            if(!finite(x)||!finite(y)||(f.xLog&&x<=0)||(f.yLog&&y<=0)) return;
            const QPointF pt=toDevice(f,x,y);
            for(int k=0;k<4;++k) if(candidates[k].contains(pt)) occupancy[k]+=1.0;
        };
        // A FILL: counted wherever it reaches above the box's lower edge.
        //
        // This distinction is why the legend did not move the first two times.
        // A filled band puts ink everywhere UNDER its envelope, not only along
        // it, and scoring the outline asks the wrong question: on the stacked
        // figure the top band's outline crosses the top-left and the top-right
        // boxes about equally, both scored the same, the tie handed it back to
        // the top right - and the left of the picture had a visibly empty
        // corner the whole time. The eye answers "does the fill come up past
        // here", so that is what is counted.
        const auto scoreFill=[&](double x,double top){
            if(!finite(x)||!finite(top)||(f.xLog&&x<=0)||(f.yLog&&top<=0)) return;
            const QPointF pt=toDevice(f,x,top);
            for(int k=0;k<4;++k){
                const QRectF& box=candidates[k];
                if(pt.x()<box.left()||pt.x()>box.right()) continue;
                if(!(box.height()>0.0)) continue;
                // Device y counts DOWN. The fill occupies everything below its
                // own top edge, so what it takes out of this box is the part of
                // the box below pt.y().
                const double covered=qBound(0.0,
                                            box.bottom()-qMax(pt.y(),box.top()),
                                            box.height());
                occupancy[k]+=covered/box.height();
            }
        };

        // Where the ink actually goes, which is not always `.x` and `.y`.
        //
        // Three shapes in this catalogue, and getting this wrong is invisible:
        // the legend simply stays where it was. Engines with a packing not
        // listed here fall through to the ordinary case, score their unused
        // coordinates, and end up in the top right - which is where they were
        // drawn before any of this existed, so the failure mode is the old
        // behaviour rather than a wrong answer.
        const StackedBands stack=stackedBands(spec);
        const bool violin=spec.engine==QLatin1String("Violin Plot")
                        ||spec.engine==QLatin1String("Raincloud");

        if(stack.valid&&!spec.series.isEmpty()){
            // The bands are at the running total; the raw series values are
            // never drawn anywhere, so they are not scored.
            const PlotSeries& first=spec.series.first();
            for(int i=0;i<stack.top.size()&&i<first.x.size();i+=stride)
                scoreFill(first.x[i],stack.top[i]);
        }else if(violin){
            // Packed as x = {slot, sample positions...}, y = {0, densities...}
            // and drawn at (slot, x[i]) - see drawViolin. Scoring `.y` here
            // would score the kernel density, in units nothing is drawn in.
            for(const PlotSeries& s:spec.series){
                if(s.x.size()<2) continue;
                const double slot=s.x.first();
                for(int i=1;i<s.x.size();i+=stride) scorePoint(slot,s.x[i]);
            }
        }else{
            for(const PlotSeries& s:spec.series){
                const int n=qMin(s.x.size(),s.y.size());
                for(int i=0;i<n;i+=stride) scorePoint(s.x[i],s.y[i]);
            }
            // These stroke their curves AND fill under them, so they get both.
            if(spec.engine==QLatin1String("Area")
               ||spec.engine==QLatin1String("Fill Between")){
                int n=std::numeric_limits<int>::max();
                for(const PlotSeries& s:spec.series)
                    n=qMin(n,qMin(s.x.size(),s.y.size()));
                const PlotSeries& first=spec.series.first();
                for(int i=0;i<n;i+=stride){
                    double top=-std::numeric_limits<double>::infinity();
                    for(const PlotSeries& s:spec.series)
                        if(finite(s.y[i])) top=qMax(top,s.y[i]);
                    scoreFill(first.x[i],top);
                }
            }
        }
    }
    int chosen=0;
    for(int k=1;k<4;++k) if(occupancy[k]<occupancy[chosen]) chosen=k;
    const QRectF box=candidates[chosen];
    // WHERE THE LEGEND ENDED UP, recorded the same way the plot area is and
    // for the same kind of reason: something outside this function needs to
    // know, and re-deriving it would be a second copy of the four-corner
    // occupancy rule above.
    //
    // The property check that asks "does any engine draw its data onto its own
    // plot frame" was counting this box. A legend is pinned INSIDE the plot
    // area, a few pixels in from two edges, which is exactly the band that
    // check scans - so the u-Chart, whose topmost point sits at 0.192 on an
    // axis running to 0.20, was reported as drawing to its top frame on the
    // strength of its legend's outline. Turning the legend off for the check
    // does not work, because prepareSpec sets it back on per engine.
    lastLegendRect_=box;
    QColor panel=spec.style.background; panel.setAlpha(215);
    p->setBrush(panel);
    p->setPen(QPen(spec.style.gridColor,0.6));
    p->drawRoundedRect(box,4,4);
    double y=box.top()+4;
    // How many rows fit, so the last one can SAY what it is hiding.
    //
    // A capped box used to stop drawing when it ran out of room and leave the
    // reader with a legend that looks complete and is not - a figure of twelve
    // series showing seven names, with nothing anywhere to say the other five
    // exist. The pie's legend already ends with "+N more"; this is the same
    // sentence in the same place.
    // COUNTED IN ROWS, WHICH ARE NOT SERIES once labels wrap. A wrapped
    // three-line entry costs three rows, so asking how many SERIES fit by
    // dividing the height by one row height would promise room that is not
    // there and draw the last entry over the axis.
    int fits=0;
    {
        double used=0.0;
        const double room=box.height()-6.0;
        for(int i=0;i<listed.size();++i){
            const double need=rowH*lineCount.at(i);
            if(used+need>room) break;
            used+=need; ++fits;
        }
    }
    const bool truncated=fits<listed.size();
    const int rows=truncated?qMax(0,fits-1):listed.size();
    for(int i=0;i<rows;++i){
        const PlotSeries* s=listed.at(i);
        // Centred on the WHOLE entry, not on its first line: a wrapped
        // three-line label with its key pinned to the top line reads as a key
        // belonging to the row above it.
        const double rowHeight=rowH*lineCount.at(i);
        const double cy=y+rowHeight/2, x0=box.left()+7, x1=x0+swatchW;
        p->save();
        p->setOpacity(qBound(0.15,s->opacity,1.0));
        if(s->drawLine){
            QPen swatch(s->color); swatch.setWidthF(qMax(1.0,s->lineWidth));
            applySeriesDash(swatch,*s); p->setPen(swatch);
            p->drawLine(QPointF(x0,cy),QPointF(x1,cy));
        }
        if(s->drawMarkers){
            p->setPen(Qt::NoPen); p->setBrush(s->color);
            const double r=qBound(1.5,s->markerSize/2.0,qMin(swatchW,rowH)/2.0-1.0);
            p->drawEllipse(QPointF((x0+x1)/2.0,cy),r,r);
        }
        if(!s->drawLine&&!s->drawMarkers){
            // Neither a line nor a point: the series is a SOLID - a bar, a
            // band, a wedge - and its key is the colour it is filled with.
            // A stroke here is a picture of something the figure has none of.
            p->setPen(Qt::NoPen); p->setBrush(s->color);
            p->drawRect(QRectF(x0,cy-qMin(5.0,rowH/2-1),swatchW,qMin(10.0,rowH-2)));
        }
        p->restore();
        p->setPen(spec.style.foreground);
        if(wrapLabels)
            p->drawText(QRectF(x1+5,y,widest+4,rowHeight),
                        Qt::AlignLeft|Qt::AlignVCenter|Qt::TextWordWrap,keyFor(s));
        else
            p->drawText(QRectF(x1+5,y,widest+4,rowHeight),
                        Qt::AlignLeft|Qt::AlignVCenter,
                        fm.elidedText(keyFor(s),Qt::ElideRight,widest+4));
        y+=rowHeight;
    }
    if(truncated&&y+rowH<=box.bottom()-2){
        p->setPen(spec.style.gridColor);
        p->drawText(QRectF(box.left()+12+swatchW,y,widest+4,rowH),Qt::AlignLeft|Qt::AlignVCenter,
                    QStringLiteral("+%1 more").arg(listed.size()-rows));
    }
    p->restore();
}

void QtPlotBackend::drawLineChart(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    for(const PlotSeries& s:spec.series){
        const int n=qMin(s.x.size(),s.y.size());
        if(n==0) continue;
        QPainterPath path; bool started=false;
        for(int i=0;i<n;++i){
            const double x=s.x[i],y=s.y[i];
            // Against the RIGHT-HAND ordinate where the series asks for one:
            // see PlotSeries::secondaryAxis. A series that does not ask is
            // mapped exactly as before.
            const bool logY=(s.secondaryAxis&&f.hasY2)?f.y2Log:f.yLog;
            if(!finite(x)||!finite(y)||(f.xLog&&x<=0)||(logY&&y<=0)){ started=false; continue; }
            const QPointF pt=toDeviceOn(f,x,y,s.secondaryAxis);
            if(!started){ path.moveTo(pt); started=true; } else path.lineTo(pt);
        }
        p->save();
        p->setOpacity(qBound(0.0,s.opacity,1.0));
        // A BLOCK, when the series says its outline encloses a solid. See
        // PlotSeries::fillClosed: the Pareto chart and the abatement cost
        // curve build their bars as closed polylines here rather than through
        // drawBar, and a bar is read from the ink in it.
        if(s.fillClosed&&path.elementCount()>2){
            QColor fill=s.color; fill.setAlphaF(qBound(0.0,s.opacity,1.0)*0.55);
            p->setPen(Qt::NoPen); p->setBrush(fill);
            p->drawPath(path);
        }
        if(s.drawLine){
            QPen pen(s.color); pen.setWidthF(qMax(0.2,s.lineWidth)); applySeriesDash(pen,s);
            pen.setJoinStyle(Qt::RoundJoin); pen.setCapStyle(Qt::RoundCap);
            p->setPen(pen); p->setBrush(Qt::NoBrush); p->drawPath(path);
        }
        if(s.drawMarkers){
            p->setPen(Qt::NoPen); p->setBrush(s.color);
            drawSeriesMarkers(p,f,s);
        }
        p->restore();
    }
}

void QtPlotBackend::drawScatter(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    for(const PlotSeries& s:spec.series){
        p->save();
        p->setOpacity(qBound(0.0,s.opacity,1.0));

        // A SERIES THAT ASKS FOR A LINE GETS ONE, and thirteen engines depend
        // on it.
        //
        // This painter drew markers and nothing else, on the reasoning below:
        // for a scatter the markers are the plot. True of the scatter's own
        // data, and false of the REFERENCE LINES that thirteen rewrites append
        // to it - Bland-Altman's bias and limits of agreement, the Q-Q
        // reference, Volcano and Manhattan's significance thresholds, the two
        // arms of a Funnel plot, MA's zero line, Influence's Cook's-distance
        // contours, and five more.
        //
        // Each of those appends a two-point series with drawLine set. The
        // painter ignored it, so the line vanished and its two endpoints were
        // drawn as two stray markers - which is exactly what the Bland-Altman
        // figure showed: a legend naming bias and +/-1.96 SD, and four dots.
        // Every one of those engines was missing the thing that makes it that
        // engine, and the legend said so while the picture did not.
        if(s.drawLine){
            const int n=qMin(s.x.size(),s.y.size());
            QPainterPath path; bool started=false;
            for(int i=0;i<n;++i){
                const double x=s.x[i],y=s.y[i];
                if(!finite(x)||!finite(y)||(f.xLog&&x<=0)||(f.yLog&&y<=0)){
                    started=false; continue;
                }
                const QPointF pt=toDevice(f,x,y);
                if(!started){ path.moveTo(pt); started=true; } else path.lineTo(pt);
            }
            QPen pen(s.color); pen.setWidthF(qMax(0.2,s.lineWidth)); applySeriesDash(pen,s);
            pen.setJoinStyle(Qt::RoundJoin); pen.setCapStyle(Qt::RoundCap);
            p->setPen(pen); p->setBrush(Qt::NoBrush); p->drawPath(path);
        }

        // Not guarded on s.drawMarkers: for this engine the markers are the
        // plot, whatever the point budget decided about the line style.
        //
        // Guarded on drawLine instead, which is the distinction that was
        // missing. A reference line is a line and not a cloud of two points,
        // so it gets markers only when it actually asks for them.
        if(!s.drawLine||s.drawMarkers){
            p->setPen(Qt::NoPen); p->setBrush(s.color);
            drawSeriesMarkers(p,f,s);
        }
        p->restore();
    }
}

// The joint scatter with the two marginal distributions along the axes.
//
// "Scatter + Marginals" and "Plot Matrix" shared one rewrite and one painter,
// on the reasoning that both are small multiples and this backend draws one
// figure. That is true of a plot matrix - an n x n grid of panels is n^2
// figures and there is nowhere to put them - but it is NOT true of this one. A
// scatter with its marginals is a single panel with two strips on it, which is
// exactly what this backend draws, so the limitation was being applied to a
// figure that did not have it and the two came out identical.
//
// The marginals are the point of the form. The scatter answers "how do these
// two move together"; the strips answer "and what does each one look like on
// its own" - whether an outlier is extreme in x, in y, or only in the pair;
// whether a cloud that looks uniform is actually two modes in one variable.
// None of that is readable from the joint cloud.
//
// Drawn INSIDE the plot area along the top and right edges rather than in
// reserved gutters. A gutter would mean computeFrame reserving space that
// every other engine sharing the frame does not want, and the strips are
// translucent and capped at a sixth of the frame, so the points underneath
// stay visible and the scatter still occupies the axes it is scaled to.
void QtPlotBackend::drawScatterMarginals(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    drawScatter(p,f,spec);
    if(spec.series.isEmpty()) return;

    constexpr int kBins=36;
    QVector<double> alongX(kBins,0.0),alongY(kBins,0.0);
    const double xFrom=qMin(f.xLo,f.xHi),xTo=qMax(f.xLo,f.xHi);
    const double yFrom=qMin(f.yLo,f.yHi),yTo=qMax(f.yLo,f.yHi);
    if(!(xTo>xFrom)||!(yTo>yFrom)) return;

    int counted=0;
    for(const PlotSeries& s:spec.series){
        const int n=qMin(s.x.size(),s.y.size());
        for(int i=0;i<n;++i){
            double x=s.x[i],y=s.y[i];
            if(!finite(x)||!finite(y)) continue;
            // Binned in the space the axis is DRAWN in, so the strip lines up
            // with the ticks on a logarithmic axis instead of piling every
            // decade but the last into one bin.
            if(f.xLog){ if(!(x>0)) continue; x=std::log10(x); }
            if(f.yLog){ if(!(y>0)) continue; y=std::log10(y); }
            alongX[qBound(0,int((x-xFrom)/(xTo-xFrom)*kBins),kBins-1)]+=1.0;
            alongY[qBound(0,int((y-yFrom)/(yTo-yFrom)*kBins),kBins-1)]+=1.0;
            ++counted;
        }
    }
    if(counted<2) return;

    double peakX=0.0,peakY=0.0;
    for(int k=0;k<kBins;++k){ peakX=qMax(peakX,alongX[k]); peakY=qMax(peakY,alongY[k]); }
    if(!(peakX>0.0)||!(peakY>0.0)) return;

    // A sixth of the frame each. Enough to read a shape, little enough that
    // the strip cannot be mistaken for the data it sits over.
    const double bandH=f.plotArea.height()/6.0;
    const double bandW=f.plotArea.width()/6.0;
    const double binW=f.plotArea.width()/double(kBins);
    const double binH=f.plotArea.height()/double(kBins);

    QColor ink=spec.series.first().color;
    ink.setAlphaF(0.30);
    p->save();
    p->setClipRect(f.plotArea);
    p->setPen(Qt::NoPen);
    p->setBrush(ink);
    for(int k=0;k<kBins;++k){
        const double h=alongX[k]/peakX*bandH;
        if(h>0.0)
            p->drawRect(QRectF(f.plotArea.left()+k*binW,f.plotArea.top(),binW,h));
        const double w=alongY[k]/peakY*bandW;
        // k counts UP the y axis, and device y counts down, so the strip is
        // built from the bottom of the plot area rather than the top.
        if(w>0.0)
            p->drawRect(QRectF(f.plotArea.right()-w,
                               f.plotArea.bottom()-(k+1)*binH,w,binH));
    }
    p->restore();
}

void QtPlotBackend::drawArea(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    const double baseline=f.yLog?f.yLo:qBound(f.yLo,0.0,f.yHi);
    for(const PlotSeries& s:spec.series){
        const int n=qMin(s.x.size(),s.y.size());
        if(n<2) continue;
        QPainterPath path; bool started=false; double firstX=0,lastX=0;
        for(int i=0;i<n;++i){
            const double x=s.x[i],y=s.y[i];
            if(!finite(x)||!finite(y)||(f.xLog&&x<=0)||(f.yLog&&y<=0)) continue;
            const QPointF pt=toDevice(f,x,y);
            if(!started){ path.moveTo(pt); firstX=pt.x(); started=true; } else path.lineTo(pt);
            lastX=pt.x();
        }
        if(!started) continue;
        const double baseY=f.plotArea.bottom()-((baseline-f.yLo)/qMax(1e-300,f.yHi-f.yLo))*f.plotArea.height();
        path.lineTo(QPointF(lastX,baseY));
        path.lineTo(QPointF(firstX,baseY));
        path.closeSubpath();
        QColor fill=s.color; fill.setAlphaF(0.35);
        p->save();
        // In Monochrome colour-vision mode the dash pattern is the only thing
        // separating one series from another, so a draw function that drops it
        // renders every series as the same grey outline.
        QPen pen(s.color,qMax(0.2,s.lineWidth)); applySeriesDash(pen,s);
        p->setPen(pen);
        p->setBrush(fill);
        p->drawPath(path);
        // A series that asked for markers gets them. drawArea dropped the
        // request silently, and several engines rewrite themselves INTO an
        // Area and set it - a payload-range envelope is four corner points
        // with straight legs between them, and the corners are the numbers
        // the chart is read for. They were never drawn, and the figure was
        // indistinguishable from a plain Area.
        if(s.drawMarkers){
            p->setBrush(s.color);
            p->setPen(QPen(s.color,qMax(0.2,s.lineWidth)));
            drawSeriesMarkers(p,f,s);
        }
        p->restore();
    }
}

// The region BETWEEN two curves, which is what the name means.
//
// "Fill Between" was an alias: prepareSpec set out.engine="Area" and returned,
// so the catalogue offered two entries and the program had one behaviour. Area
// fills each curve down to the baseline. Filling between two curves is a
// different figure, and it is the one a confidence band, a tolerance envelope
// or a daily min/max range is drawn as - none of which can be expressed by
// filling to zero.
//
// One band per ADJACENT pair, so two columns give the usual single band and
// three give two stacked bands rather than the third being silently ignored.
// A single column has nothing to be between and falls back to the baseline,
// which is the one case where the two engines legitimately agree.
void QtPlotBackend::drawFillBetween(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    if(spec.series.size()<2){ drawArea(p,f,spec); return; }

    for(int k=0;k+1<spec.series.size();++k){
        const PlotSeries& lower=spec.series.at(k);
        const PlotSeries& upper=spec.series.at(k+1);
        const int n=qMin(qMin(lower.x.size(),lower.y.size()),
                         qMin(upper.x.size(),upper.y.size()));
        // Both curves come from the same x column, so lower.x is the position
        // of both edges. Reading upper.x as well would be the same numbers and
        // one more array to fall off the end of.
        auto usable=[&](int j){
            const double x=lower.x[j],lo=lower.y[j],hi=upper.y[j];
            if(!finite(x)||!finite(lo)||!finite(hi)) return false;
            if(f.xLog&&x<=0) return false;
            if(f.yLog&&(lo<=0||hi<=0)) return false;
            return true;
        };
        // Runs of consecutive samples where BOTH curves have a value. A gap in
        // either one is a gap in the band: bridging it would fill a region one
        // of whose own two edges is unknown, which is inventing data rather
        // than drawing it.
        int i=0;
        while(i<n){
            if(!usable(i)){ ++i; continue; }
            int j=i;
            while(j<n&&usable(j)) ++j;
            if(j-i>=2){
                QPainterPath band;
                band.moveTo(toDevice(f,lower.x[i],lower.y[i]));
                for(int q=i+1;q<j;++q) band.lineTo(toDevice(f,lower.x[q],lower.y[q]));
                for(int q=j-1;q>=i;--q) band.lineTo(toDevice(f,lower.x[q],upper.y[q]));
                band.closeSubpath();
                // Tinted from the upper curve and outlined by neither: both
                // curves are drawn over the band below in their own colours,
                // and an outline in a third colour would read as a third
                // series that is not in the data.
                QColor fill=upper.color; fill.setAlphaF(0.30);
                p->save();
                p->setPen(Qt::NoPen);
                p->setBrush(fill);
                p->drawPath(band);
                p->restore();
            }
            i=j;
        }
    }

    // Every edge, so the figure reads as curves with a region between them
    // rather than as a coloured blob with no numbers on it.
    for(const PlotSeries& s:spec.series){
        const int n=qMin(s.x.size(),s.y.size());
        if(n<2) continue;
        QPainterPath line; bool started=false;
        for(int i=0;i<n;++i){
            const double x=s.x[i],y=s.y[i];
            if(!finite(x)||!finite(y)||(f.xLog&&x<=0)||(f.yLog&&y<=0)){ started=false; continue; }
            const QPointF pt=toDevice(f,x,y);
            if(!started){ line.moveTo(pt); started=true; } else line.lineTo(pt);
        }
        // In Monochrome colour-vision mode the dash pattern is the only thing
        // separating one series from another.
        QPen pen(s.color,qMax(0.2,s.lineWidth)); applySeriesDash(pen,s);
        p->save(); p->setPen(pen); p->setBrush(Qt::NoBrush); p->drawPath(line); p->restore();
    }
}

void QtPlotBackend::drawStairs(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    for(const PlotSeries& s:spec.series){
        const int n=qMin(s.x.size(),s.y.size());
        if(n<2) continue;
        QPainterPath path; bool started=false; QPointF prev;
        for(int i=0;i<n;++i){
            const double x=s.x[i],y=s.y[i];
            if(!finite(x)||!finite(y)||(f.xLog&&x<=0)||(f.yLog&&y<=0)){ started=false; continue; }
            const QPointF pt=toDevice(f,x,y);
            if(!started){ path.moveTo(pt); started=true; }
            else { path.lineTo(QPointF(pt.x(),prev.y())); path.lineTo(pt); }
            prev=pt;
        }
        QPen pen(s.color); pen.setWidthF(qMax(0.2,s.lineWidth)); pen.setJoinStyle(Qt::MiterJoin); applySeriesDash(pen,s);
        p->save(); p->setPen(pen); p->setBrush(Qt::NoBrush); p->drawPath(path); p->restore();
    }
}

void QtPlotBackend::drawFloatingTitle(QPainter* p,const QRectF& target,
                                      const PlotSpec& spec) const {
    if(spec.title.isEmpty()) return;
    p->setPen(spec.style.foreground);
    p->setFont(font(spec,spec.style.titleSize));
    p->drawText(QRectF(target.left(),target.top()+6,target.width(),26),
                Qt::AlignHCenter|Qt::AlignTop,spec.title);
}

double QtPlotBackend::slotWidthFrom(QVector<double> centres,double fallback,double limit){
    if(centres.isEmpty()) return qBound(1.0,fallback,qMax(1.0,limit));
    std::sort(centres.begin(),centres.end());
    double minGap=std::numeric_limits<double>::infinity();
    for(int i=1;i<centres.size();++i){
        const double d=centres[i]-centres[i-1];
        // Sub-pixel gaps are ignored: duplicated slots are a grouped category,
        // not a narrower bar.
        if(d>0.5) minGap=qMin(minGap,d);
    }
    // With no second slot to measure against, the fallback is a guess - and a
    // guess the size of the whole axis is not a bar, it is a fill. A Global
    // Sensitivity of one input came out as a solid rectangle from the left
    // edge to the right and top to bottom, which says nothing about the input
    // it is drawn for. A third of the plot still reads as a bar at any size.
    const double slot=finite(minGap)?minGap:qMin(fallback,qMax(1.0,limit)/3.0);
    return qBound(1.0,slot,qMax(1.0,limit));
}

void QtPlotBackend::drawSeriesMarkers(QPainter* p,const Frame& f,const PlotSeries& s,
                                      double minRadius) const {
    const int n=qMin(s.x.size(),s.y.size());
    const double r=qMax(minRadius,s.markerSize/2.0);
    // The markers have to go where the line goes. Left reading f.yLog and
    // toDevice while drawLineChart moved to the second ordinate, a series on
    // the right-hand axis would have had its line in one place and its dots in
    // another - which is the kind of disagreement that looks like data.
    const bool logY=(s.secondaryAxis&&f.hasY2)?f.y2Log:f.yLog;
    for(int i=0;i<n;++i){
        const double x=s.x[i],y=s.y[i];
        if(!finite(x)||!finite(y)||(f.xLog&&x<=0)||(logY&&y<=0)) continue;
        p->drawEllipse(toDeviceOn(f,x,y,s.secondaryAxis),r,r);
    }
}

void QtPlotBackend::drawBar(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    int total=0;
    for(const PlotSeries& s:spec.series) total=qMax(total,qMin(s.x.size(),s.y.size()));
    if(total<=0) return;

    // SEVERAL HISTOGRAMS SHARE THE BIN; several bar series divide it.
    //
    // Bars from different series stand side by side, each a slot-width divided
    // by the number of series. That is right for a grouped bar chart, whose
    // series share their categories and must not overlap. It is wrong for a
    // histogram: five columns bin independently, so dividing each bin five
    // ways left every bar one pixel wide and the figure read as a barcode.
    // Distributions are compared by overlaying them - which is why the rewrite
    // makes these translucent.
    const bool overlaid=spec.engine==QLatin1String("Histogram");
    const int groups=overlaid?1:qMax(1,spec.series.size());

    // A bar belongs at its own x value, not at its position in the array. The
    // axis underneath is scaled to the data, so spacing bars evenly draws them
    // under labels they do not correspond to - and for a histogram, whose bin
    // centres are the entire content of the x column, every bar lands wrong.
    //
    // THE WIDTH IS EACH SERIES' OWN.
    //
    // It was measured from every series' positions POOLED into one list, whose
    // smallest gap is what every bar was then sized by. For a grouped bar
    // chart that is correct and unchanged - those series share their
    // categories, so each one's own spacing is the shared spacing. For five
    // histograms it was ruinous: each column bins independently, one of them
    // finely, and the whole figure was drawn at the finest column's bin width.
    // A column binned into a hundred and twenty made hairlines of the other
    // four. Narrowing every series to the narrowest is not a compromise - it
    // is letting one column decide how the rest are drawn.
    auto slotFor=[&](const PlotSeries& s){
        const int n=qMin(s.x.size(),s.y.size());
        QVector<double> centres;
        centres.reserve(n);
        for(int i=0;i<n;++i){
            const double x=s.x[i];
            if(!finite(x)||(f.xLog&&x<=0)) continue;
            centres.push_back(toDevice(f,x,f.yLo).x());
        }
        if(centres.isEmpty()) return 0.0;
        return slotWidthFrom(centres,f.plotArea.width()/double(total),f.plotArea.width());
    };

    const double baseY=f.plotArea.bottom()-((qBound(f.yLo,0.0,f.yHi)-f.yLo)/qMax(1e-300,f.yHi-f.yLo))*f.plotArea.height();

    for(int g=0;g<spec.series.size();++g){
        const PlotSeries& s=spec.series[g];
        const int n=qMin(s.x.size(),s.y.size());

        // A SERIES THAT ASKS FOR A LINE GETS ONE, the same rule drawScatter
        // now follows and for the same reason: a rewrite appends a REFERENCE
        // RULE to a bar chart and has nowhere to put it.
        //
        // Process Capability is the one that needed it. It bins a column into
        // a histogram and marks the two specification limits on it - and the
        // limits were being drawn as bars, so a vertical rule at the lower
        // limit came out as a block of the same shape as the data it was meant
        // to be read against.
        //
        // Bars never set this themselves: the bins a histogram produces clear
        // drawLine, and a Bar chosen from the library has it cleared in
        // prepareSpecCore - because PlotSeries' default is true and the
        // backend must not depend on its caller having cleared a flag.
        if(s.drawLine&&n>=2){
            QPainterPath rule;
            bool started=false;
            for(int i=0;i<n;++i){
                const double x=s.x[i],y=s.y[i];
                if(!finite(x)||!finite(y)||(f.xLog&&x<=0)||(f.yLog&&y<=0)){ started=false; continue; }
                const QPointF pt=toDevice(f,x,y);
                if(!started){ rule.moveTo(pt); started=true; } else rule.lineTo(pt);
            }
            QPen pen(s.color); pen.setWidthF(qMax(0.6,s.lineWidth)); applySeriesDash(pen,s);
            p->save();
            p->setOpacity(qBound(0.0,s.opacity,1.0));
            p->setPen(pen); p->setBrush(Qt::NoBrush);
            p->drawPath(rule);
            p->restore();
            continue;
        }

        const double slot=slotFor(s);
        if(!(slot>0)) continue;
        const double barW=qMax(1.0,(slot*0.78)/double(groups));
        p->save(); p->setPen(Qt::NoPen); p->setBrush(s.color);
        p->setOpacity(qBound(0.0,s.opacity,1.0));
        for(int i=0;i<n;++i){
            const double x=s.x[i],y=s.y[i];
            if(!finite(x)||!finite(y)||(f.xLog&&x<=0)||(f.yLog&&y<=0)) continue;
            const QPointF pt=toDevice(f,x,y);
            const double left=pt.x()-(groups*barW)/2.0+g*barW;
            p->drawRect(QRectF(left,qMin(pt.y(),baseY),barW,std::abs(baseY-pt.y())));
        }
        p->restore();
    }
}

// ---------------------------------------------------------------------------
// Statistical engines.
//
// Every engine below is expressed as a rewrite in prepareSpec rather than as a
// new draw function, because each one is a transformation of the data followed
// by geometry that already exists. An ECDF is a staircase; a Q-Q plot is a
// scatter with a reference line; a control chart is a line with three rules
// across it. Retargeting spec.engine after the rewrite means render() dispatches
// to drawing code that is already tested, and the only new thing here is
// arithmetic.
//
// The alternative - a bespoke draw function per catalogue entry - is how a
// plotting library ends up with forty subtly different ways to draw a line.

// ---------------------------------------------------------------------------
// Field engines.
//
// A heatmap and a contour both read a value over a 2-D domain, and both get it
// here the same way: series[0] is x, series[1] is y, series[2] is the value,
// and the points are binned onto a regular grid. Requiring a pre-gridded matrix
// would be the easier implementation and the less useful one - measurements
// arrive as scattered triples far more often than as a rectangle.
// ValueGrid and GridAggregate are declared in the class so a built grid can be
// cached across repaints; these two are therefore members rather than local
// helpers. Everything below them is still file-local.
QtPlotBackend::ValueGrid QtPlotBackend::gridFromSeries(const PlotSpec& spec,int requested,
                                                       GridAggregate how){
    ValueGrid g;
    if(spec.series.size()<3) return g;
    const QVector<double>& xs=spec.series.at(0).y;
    const QVector<double>& ys=spec.series.at(1).y;
    const QVector<double>& vs=spec.series.at(2).y;
    const int n=qMin(xs.size(),qMin(ys.size(),vs.size()));
    if(n<4) return g;

    double xLo=std::numeric_limits<double>::infinity(),xHi=-xLo;
    double yLo=xLo,yHi=-xLo;
    for(int i=0;i<n;++i){
        if(!finite(xs[i])||!finite(ys[i])||!finite(vs[i])) continue;
        xLo=qMin(xLo,xs[i]); xHi=qMax(xHi,xs[i]);
        yLo=qMin(yLo,ys[i]); yHi=qMax(yHi,ys[i]);
    }
    if(!finite(xLo)||!finite(yLo)||!(xHi>xLo)||!(yHi>yLo)) return g;

    // Grid resolution follows the sample count: enough cells to show structure,
    // few enough that most of them get a sample. sqrt(n)/2 is the usual
    // compromise and behaves for anything from a hundred points to a million.
    // The person's resolution wins, then the caller's, then the sample count.
    // The ceiling rises with interpolation on: a fine grid of holes is worse
    // than a coarse one, but a fine grid that has been filled is smoother.
    const int wanted=spec.style.fieldResolution>0 ? spec.style.fieldResolution
                                                  : (requested>0?requested:int(std::sqrt(double(n))/2.0));
    const int ceiling=(how==GridAggregate::Mean&&spec.style.fieldInterpolation>0)?360:160;
    // THE FLOOR OF TWELVE IS FOR THE GUESS, not for a number somebody stated.
    //
    // Twelve cells is a sensible minimum when the resolution is inferred from
    // the sample count - fewer than that and a scattered field has no
    // structure left to show. It is wrong when the caller knows the answer: a
    // confusion matrix over four classes is four cells wide by definition, and
    // clamping it up to twelve drew that 4x4 table as a smooth twelve-by-twelve
    // field - the diagonal smeared across it, and intermediate counts shown
    // that exist in no cell of the table.
    //
    // So an explicit resolution - the person's setting, or an engine stating
    // its own - is honoured down to two. Below two there is no grid.
    const bool statedResolution=spec.style.fieldResolution>0||requested>0;
    const int side=qBound(statedResolution?2:12,wanted,ceiling);
    g.nx=side; g.ny=side;
    // A SAMPLE SITS AT THE CENTRE OF ITS CELL, so the grid's edges are half a
    // cell outside the data rather than on it.
    //
    // The bounds were the data's own extremes, divided into `side` equal bins
    // starting at the minimum. That puts the first sample on the LEFT EDGE of
    // the first cell and the last on the right edge of the last, so every cell
    // is drawn half a cell to the right of the value it stands for. On a
    // hundred-and-sixty-cell heatmap that is invisible; on a confusion matrix
    // of four classes it is a quarter of a cell, and the figure shows class 1's
    // cell sitting between the ticks for 1 and 2. The matrix engines set their
    // axes to -0.5 .. n-0.5 - cell edges - and the cells did not line up with
    // them.
    //
    // Half a cell out at each end makes each sample the centre of its own cell
    // and the grid's edges the cell edges, which is what those axes describe
    // and what a binned field means anyway: the value stands for the middle of
    // the bin, not for its left-hand corner.
    const double halfX=(xHi-xLo)/(2.0*double(qMax(1,side-1)));
    const double halfY=(yHi-yLo)/(2.0*double(qMax(1,side-1)));
    g.xLo=xLo-halfX; g.xHi=xHi+halfX;
    g.yLo=yLo-halfY; g.yHi=yHi+halfY;
    g.cells.fill(std::numeric_limits<double>::quiet_NaN(),side*side);
    QVector<int> counts(side*side,0);
    QVector<double> sums(side*side,0.0);
    for(int i=0;i<n;++i){
        if(!finite(xs[i])||!finite(ys[i])||!finite(vs[i])) continue;
        // Binned against the CELL EDGES above, not against the data extremes:
        // a sample and the cell it is counted into have to agree, or the
        // picture is drawn from one grid and filled from another.
        const int cx=qBound(0,int((xs[i]-g.xLo)/(g.xHi-g.xLo)*side),side-1);
        const int cy=qBound(0,int((ys[i]-g.yLo)/(g.yHi-g.yLo)*side),side-1);
        const int k=cy*side+cx;
        sums[k]+=vs[i]; counts[k]+=1;
    }
    double vLo=std::numeric_limits<double>::infinity(),vHi=-vLo;
    for(int k=0;k<side*side;++k){
        if(counts[k]==0){
            // For a density the absence of samples IS zero; for a measured
            // field it is unknown, and colouring it would invent data.
            if(how==GridAggregate::Count){ g.cells[k]=0.0; vLo=qMin(vLo,0.0); vHi=qMax(vHi,0.0); }
            continue;
        }
        g.cells[k]=(how==GridAggregate::Count)?double(counts[k]):sums[k]/double(counts[k]);
        vLo=qMin(vLo,g.cells[k]); vHi=qMax(vHi,g.cells[k]);
    }
    // The scattered path, when an estimator has been chosen.
    //
    // It replaces the binned grid entirely rather than filling its holes: the
    // binning is what threw the measurements away, so an estimate built on top
    // of it inherits that loss. Only for a measured field - for a density the
    // count in a bin IS the quantity, and interpolating between counts would
    // report a fractional number of events.
    if(how==GridAggregate::Mean&&spec.style.fieldEstimator>=0){
        QVector<ScatterPoint> scatter;
        scatter.reserve(n);
        for(int i=0;i<n;++i){
            if(!finite(xs[i])||!finite(ys[i])||!finite(vs[i])) continue;
            scatter.append({xs[i],ys[i],vs[i]});
        }
        EstimatorSettings settings;
        settings.estimator=Estimator(qBound(0,spec.style.fieldEstimator,
                                            int(Estimator::Count)-1));
        settings.extrapolation=Extrapolation(qBound(0,spec.style.fieldExtrapolation,
                                                    int(Extrapolation::Count)-1));
        settings.valuePolicy=ValuePolicy(qBound(0,spec.style.fieldValuePolicy,
                                                int(ValuePolicy::Count)-1));
        settings.responseSpace=ResponseSpace(qBound(0,spec.style.fieldResponseSpace,
                                                    int(ResponseSpace::Count)-1));
        settings.neighbours=qBound(4,spec.style.fieldNeighbours,512);
        settings.idwPower=spec.style.fieldIdwPower;
        settings.smoothing=qMax(0.0,spec.style.fieldSmoothing);
        settings.footprint=FailureFootprint(qBound(0,spec.style.fieldFootprint,
                                                   int(FailureFootprint::Count)-1));
        settings.bridging=Bridging(qBound(0,spec.style.fieldBridging,
                                          int(Bridging::Count)-1));
        settings.bridgeMaxCells=qBound(1,spec.style.fieldBridgeMaxCells,2500);
        settings.invalidDisplay=InvalidDisplay(qBound(0,spec.style.fieldInvalidDisplay,
                                                      int(InvalidDisplay::Count)-1));
        settings.kriging=qBound(0,spec.style.fieldKrigingVariogram,2);
        settings.loessFraction=qBound(0.02,spec.style.fieldLoessFraction,1.0);
        // The distance weights make the two axes comparable. Without them a
        // sweep that spans 0..1 in x and 0..10000 in y has every neighbourhood
        // stretched into a horizontal sliver, and the field is smeared along
        // one axis for no reason but the units.
        settings.xWeight=qMax(1e-12,xHi-xLo);
        settings.yWeight=qMax(1e-12,yHi-yLo);
        const EstimatedField f=estimateField(scatter,side,side,xLo,xHi,yLo,yHi,settings);
        if(f.valid){
            g.cells=f.value;
            vLo=std::numeric_limits<double>::infinity(); vHi=-vLo;
            for(int k=0;k<side*side;++k){
                if(!finite(g.cells[k])) continue;
                vLo=qMin(vLo,g.cells[k]); vHi=qMax(vHi,g.cells[k]);
            }
            if(!finite(vLo)||!finite(vHi)) return g;
            if(qFuzzyCompare(vLo,vHi)) vHi=vLo+1.0;
            g.vLo=vLo; g.vHi=vHi;
            g.valid=true;
            return g;
        }
        // An estimator that could not run - fewer than three usable points,
        // or a singular solve - falls through to the binned grid rather than
        // drawing nothing. A worse picture beats no picture, and the notice
        // under the figure says which was used.
    }

    // Estimate the cells no sample landed in, if asked to. Only for a measured
    // field: for a count an empty cell is a measured zero and was filled above.
    if(how==GridAggregate::Mean&&spec.style.fieldInterpolation>0){
        estimateEmptyCells(g,spec.style.fieldInterpolation);
        // The range has to be recomputed: an estimate can sit outside the range
        // of the measured cells, and colouring against the old range would clip
        // it to the end of the map and read as a plateau that is not there.
        vLo=std::numeric_limits<double>::infinity(); vHi=-vLo;
        for(int k=0;k<side*side;++k){
            if(!finite(g.cells[k])) continue;
            vLo=qMin(vLo,g.cells[k]); vHi=qMax(vHi,g.cells[k]);
        }
    }

    if(!finite(vLo)||!finite(vHi)) return g;
    if(qFuzzyCompare(vLo,vHi)) vHi=vLo+1.0;
    g.vLo=vLo; g.vHi=vHi;
    g.valid=true;
    return g;
}

// Fill the unsampled cells of a gridded field.
//
// A heat map of scattered measurements is mostly holes. The sweep behind the
// figure that prompted this had ten thousand rows over a grid of fifty by
// fifty, and still left most of it black, because the samples are dense at one
// end of the range and thin at the other - so the cells at the thin end catch
// nothing and the map reads as confetti rather than as a field. GraphVis 17
// offered a choice of estimators here and the port shipped none, which is the
// whole difference between the two pictures.
//
// Two methods, and they are different in kind:
//
//   Nearest  an exact nearest-measured-cell assignment, by a two-pass chamfer
//            distance transform. Every filled cell holds a value that was
//            really measured somewhere; the map is blocky and tells no lies
//            about magnitude.
//
//   Linear   a pull-push pyramid. The grid is repeatedly halved, summing value
//            and weight together, until a level is dense; then the levels are
//            walked back down, each filling its holes by bilinear interpolation
//            of the level above it. Measured cells always keep their own value
//            - the estimate only ever reaches cells that had nothing - so the
//            data is never smoothed away, only surrounded. O(n) and complete
//            in one pass, where an inverse-distance search would be quadratic
//            in the number of holes, which is most of the grid.
//
//   Cubic    Linear followed by one binomial smoothing applied ONLY to filled
//            cells, which softens the pyramid's blocky diagonals without
//            touching a single measurement.
void QtPlotBackend::estimateEmptyCells(ValueGrid& g,int mode){
    const int nx=g.nx, ny=g.ny;
    if(nx<2||ny<2||g.cells.size()<nx*ny) return;

    int holes=0;
    for(int k=0;k<nx*ny;++k) if(!finite(g.cells[k])) ++holes;
    if(holes==0||holes==nx*ny) return;   // nothing to do, or nothing to do it from

    if(mode==1){
        // Chamfer distance transform carrying the nearest source's index.
        // 1 and sqrt(2) are the standard 3x3 chamfer weights; the error against
        // true Euclidean distance is under 4%, which cannot move a cell to a
        // different nearest sample often enough to matter in a picture.
        constexpr double kOrtho=1.0, kDiag=1.41421356237;
        const double inf=std::numeric_limits<double>::infinity();
        QVector<double> dist(nx*ny,inf);
        QVector<int> src(nx*ny,-1);
        for(int k=0;k<nx*ny;++k) if(finite(g.cells[k])){ dist[k]=0.0; src[k]=k; }

        const auto relax=[&](int k,int nk,double w){
            if(src[nk]<0) return;
            const double d=dist[nk]+w;
            if(d<dist[k]){ dist[k]=d; src[k]=src[nk]; }
        };
        for(int y=0;y<ny;++y)
            for(int x=0;x<nx;++x){
                const int k=y*nx+x;
                if(y>0){
                    if(x>0) relax(k,k-nx-1,kDiag);
                    relax(k,k-nx,kOrtho);          // always, inside the y>0 guard
                    if(x<nx-1) relax(k,k-nx+1,kDiag);
                }
                if(x>0) relax(k,k-1,kOrtho);
            }
        for(int y=ny-1;y>=0;--y)
            for(int x=nx-1;x>=0;--x){
                const int k=y*nx+x;
                if(y<ny-1){
                    if(x<nx-1) relax(k,k+nx+1,kDiag);
                    relax(k,k+nx,kOrtho);          // always, inside the y<ny-1 guard
                    if(x>0) relax(k,k+nx-1,kDiag);
                }
                if(x<nx-1) relax(k,k+1,kOrtho);
            }
        for(int k=0;k<nx*ny;++k)
            if(!finite(g.cells[k])&&src[k]>=0) g.cells[k]=g.cells[src[k]];
        return;
    }

    // ---- Linear and Cubic: the pull-push pyramid.
    struct Level { int nx,ny; QVector<double> v,w; };
    QVector<Level> pyramid;
    {
        Level base;
        base.nx=nx; base.ny=ny;
        base.v.resize(nx*ny); base.w.resize(nx*ny);
        for(int k=0;k<nx*ny;++k){
            const bool known=finite(g.cells[k]);
            base.v[k]=known?g.cells[k]:0.0;
            base.w[k]=known?1.0:0.0;
        }
        pyramid.append(base);
    }
    // Pull: halve until one cell, so even a grid with a single measured cell
    // reaches a level where every cell has weight.
    while(pyramid.last().nx>1||pyramid.last().ny>1){
        const Level& fine=pyramid.last();
        Level up;
        up.nx=qMax(1,(fine.nx+1)/2);
        up.ny=qMax(1,(fine.ny+1)/2);
        up.v.fill(0.0,up.nx*up.ny);
        up.w.fill(0.0,up.nx*up.ny);
        for(int y=0;y<fine.ny;++y)
            for(int x=0;x<fine.nx;++x){
                const int k=y*fine.nx+x;
                if(fine.w[k]<=0.0) continue;
                const int uk=(y/2)*up.nx+(x/2);
                up.v[uk]+=fine.v[k];
                up.w[uk]+=fine.w[k];
            }
        pyramid.append(up);
    }

    // Push: carry estimates back down, bilinearly.
    QVector<double> coarse;          // the level above, already estimated
    int cnx=0,cny=0;
    for(int L=int(pyramid.size())-1;L>=0;--L){
        const Level& lv=pyramid.at(L);
        QVector<double> est(lv.nx*lv.ny,0.0);
        for(int y=0;y<lv.ny;++y){
            for(int x=0;x<lv.nx;++x){
                const int k=y*lv.nx+x;
                if(lv.w[k]>0.0){ est[k]=lv.v[k]/lv.w[k]; continue; }
                if(coarse.isEmpty()){ est[k]=0.0; continue; }
                // This cell's centre in the coarser level's coordinates. The
                // half-cell offsets are what stop the estimate drifting half a
                // cell up and left at every level, which over eight levels is a
                // visible shear of the whole field.
                const double fx=qBound(0.0,(double(x)+0.5)/2.0-0.5,double(cnx-1));
                const double fy=qBound(0.0,(double(y)+0.5)/2.0-0.5,double(cny-1));
                const int x0=int(fx), y0=int(fy);
                const int x1=qMin(x0+1,cnx-1), y1=qMin(y0+1,cny-1);
                const double tx=fx-x0, ty=fy-y0;
                const double a=coarse[y0*cnx+x0], b=coarse[y0*cnx+x1];
                const double c=coarse[y1*cnx+x0], d=coarse[y1*cnx+x1];
                est[k]=(a*(1-tx)+b*tx)*(1-ty)+(c*(1-tx)+d*tx)*ty;
            }
        }
        coarse=est; cnx=lv.nx; cny=lv.ny;
    }

    // The finest level's estimate, written only where nothing was measured.
    QVector<bool> filled(nx*ny,false);
    for(int k=0;k<nx*ny;++k)
        if(!finite(g.cells[k])){ g.cells[k]=coarse[k]; filled[k]=true; }

    // Relax the estimate toward a smooth surface that passes exactly through
    // the measurements.
    //
    // Gauss-Seidel on Laplace's equation, with every measured cell held fixed
    // as a boundary condition: each estimated cell is repeatedly replaced by
    // the mean of its four neighbours. That IS linear interpolation in the
    // sense a person means it - the harmonic surface through the samples - and
    // it is what makes the field ramp into a measurement instead of arriving
    // beside it and stepping.
    //
    // Without this the pyramid alone leaves every sample as a hard little
    // square stamped on a smooth field, because the finest estimate is built
    // from a 2x2-averaged level and never converges to the single cell inside
    // it. One or two smoothing passes do not close that; solving for the
    // surface does.
    //
    // The pyramid is the initial guess, which is the whole reason this needs
    // tens of sweeps rather than the thousands plain relaxation would take
    // from a cold start. A measured cell is never written, so no measurement
    // moves by so much as a bit.
    {
        const int sweeps=(mode>=3) ? qBound(60,nx*3,600) : qBound(20,nx,200);
        for(int it=0;it<sweeps;++it){
            for(int y=0;y<ny;++y)
                for(int x=0;x<nx;++x){
                    const int k=y*nx+x;
                    if(!filled[k]) continue;          // a measurement: fixed
                    double sum=0.0; int used=0;
                    if(x>0)    { sum+=g.cells[k-1];  ++used; }
                    if(x<nx-1) { sum+=g.cells[k+1];  ++used; }
                    if(y>0)    { sum+=g.cells[k-nx]; ++used; }
                    if(y<ny-1) { sum+=g.cells[k+nx]; ++used; }
                    if(used>0) g.cells[k]=sum/double(used);
                }
        }
    }
}


// Fills unknown cells from their finite neighbours, a few passes outward.
// Marching squares needs all four corners of a cell, so a grid built from
// scattered measurements has to be closed before it can be contoured. This is
// nearest-neighbour smoothing, not interpolation with any claim to accuracy -
// it exists so the contour follows the data that IS there rather than vanishing.
void QtPlotBackend::closeGaps(ValueGrid& g,int passes){
    for(int pass=0;pass<passes;++pass){
        QVector<double> next=g.cells;
        bool changed=false;
        for(int cy=0;cy<g.ny;++cy){
            for(int cx=0;cx<g.nx;++cx){
                const int k=cy*g.nx+cx;
                if(finite(g.cells[k])) continue;
                double sum=0.0; int used=0;
                for(int dy=-1;dy<=1;++dy){
                    for(int dx=-1;dx<=1;++dx){
                        const int nx=cx+dx, ny=cy+dy;
                        if(nx<0||ny<0||nx>=g.nx||ny>=g.ny) continue;
                        const double v=g.cells[ny*g.nx+nx];
                        if(finite(v)){ sum+=v; ++used; }
                    }
                }
                if(used>0){ next[k]=sum/double(used); changed=true; }
            }
        }
        g.cells=next;
        if(!changed) break;
    }
}

const QtPlotBackend::ValueGrid& QtPlotBackend::cachedGrid(const PlotSpec& gridInput,
                                                          GridAggregate how,
                                                          int gapPasses,int slot) const {
    GridCacheEntry& entry=gridCache_[qBound(0,slot,2)];
    const quint64 hash=specFingerprint(gridInput);
    if(entry.valid&&entry.hash==hash
       &&entry.aggregate==int(how)&&entry.gapPasses==gapPasses)
        return entry.grid;

    entry.grid=gridFromSeries(gridInput,0,how);
    if(gapPasses>0&&entry.grid.valid) closeGaps(entry.grid,gapPasses);
    entry.hash=hash;
    entry.aggregate=int(how);
    entry.gapPasses=gapPasses;
    entry.valid=true;
    return entry.grid;
}

// The list is kept beside the dispatch above rather than in QML, so a new field
// engine cannot be added to one and forgotten in the other. Every name here
// reaches drawHeatmap, drawContour, drawVectorField, draw3D or draw3DField in
// render() - and every branch of render() that reaches one of those is here.
bool QtPlotBackend::usesColourMap(const QString& preparedEngine){
    static const QSet<QString> kFieldEngines{
        // Flat fields.
        QStringLiteral("2D Heatmap"),QStringLiteral("2D Histogram"),
        QStringLiteral("Hexbin Density"),QStringLiteral("Correlation Matrix"),
        QStringLiteral("Covariance Matrix"),QStringLiteral("Spy Matrix"),
        QStringLiteral("2D Contour"),
        // Cells shaded by their own area, through the same ramp as a field.
        QStringLiteral("Voronoi Diagram"),
        // Regions shaded by a value, through the same ramp.
        QStringLiteral("Choropleth"),
        // Vector fields.
        QStringLiteral("Quiver Field"),QStringLiteral("Feather"),
        QStringLiteral("Stream Field"),QStringLiteral("Stream Particles"),
        QStringLiteral("Phase Portrait"),QStringLiteral("Flow Texture (LIC)"),
        QStringLiteral("Divergence Map"),QStringLiteral("Vorticity Map"),
        // Surfaces and volumes.
        QStringLiteral("Surface + Contours"),QStringLiteral("Comet 3D"),
        QStringLiteral("Ribbon"),QStringLiteral("Stream Ribbon"),
        QStringLiteral("Tensor Glyph Field"),QStringLiteral("Volume Show"),
        QStringLiteral("Volume Slice"),QStringLiteral("Isosurface"),
        QStringLiteral("Isonormals"),QStringLiteral("Isocaps"),
        QStringLiteral("Contour Slice")};
    // Everything drawn by draw3D, which is selected by prefix rather than by
    // name - "3D Surface", "3D Scatter" and the rest.
    if(preparedEngine.startsWith(QLatin1String("3D "))) return true;
    return kFieldEngines.contains(preparedEngine);
}

// Does this engine put a mapped series on the RIGHT-HAND axis because the
// mapping asked it to?
//
// ASKED BY ASKING, not by keeping a list of engine names. The whole secondary
// axis path has existed for a long time and eight engines use it internally -
// Pareto's cumulative percent, Bode's phase, the X-bar chart's range - but no
// mapping role ever reached it, so "plot this column against its own axis on
// the right", which is the ordinary reason anyone wants two axes, could not be
// asked for. Offering the role needs an answer to "will this engine honour
// it?", and this project's own recorded trap is answering that kind of question
// with a name list: a rule right for one class applied to all, and a guard that
// matches a NAME rather than a STATEMENT.
//
// So the statement is made and the answer measured. Two probe specs go through
// the same prepareSpec the renderer uses, identical but for the one bit being
// asked about:
//
//   * one with the second series asking for the secondary axis;
//   * one with nothing asking.
//
// The engine honours the role when the FLAG MAKES THE DIFFERENCE. Requiring the
// difference rather than merely the presence is what tells a Line Chart from a
// Pareto Chart: Pareto marks its own cumulative series whatever it is handed,
// so it answers "yes" to both probes and "no" to this question - which is the
// truth, because it rebuilds the series and the person's choice would vanish.
//
// Measured once per engine and cached; there are 440 of them and a person
// changes engine by hand.
bool QtPlotBackend::honoursSecondaryAxis(const QString& engine) const {
    static QMutex guard;
    static QHash<QString,bool> answers;
    {
        QMutexLocker lock(&guard);
        const auto found=answers.constFind(engine);
        if(found!=answers.constEnd()) return *found;
    }

    auto probe=[this,&engine](bool ask)->bool{
        PlotSpec s;
        s.engine=engine;
        for(int k=0;k<2;++k){
            PlotSeries series;
            series.label=QStringLiteral("probe %1").arg(k);
            for(int i=0;i<8;++i){
                series.x.append(double(i+1));
                series.y.append(double((i+1)*(k+1)));
            }
            if(ask&&k==1) series.secondaryAxis=true;
            s.series.append(series);
        }
        // A probe must never be the thing that takes the application down. An
        // engine handed two short series it was not designed for is entitled to
        // refuse; what it is not entitled to do is decide whether a control
        // appears by throwing.
        PlotSpec out;
        try { out=prepareSpec(s); }
        catch(...) { return false; }
        for(const PlotSeries& drawn:std::as_const(out.series))
            if(drawn.secondaryAxis) return true;
        return false;
    };

    const bool honours=probe(true)&&!probe(false);
    QMutexLocker lock(&guard);
    answers.insert(engine,honours);
    return honours;
}

// ---------------------------------------------------------------------------
// Annotations.
//
// Drawn last, over everything including the legend, because a note is about
// the figure rather than part of it.
//
// Real text, not a path: this goes through the same painter as the screen, so
// an exported PDF carries a selectable, searchable string in an embedded font
// rather than an outline. The whole point of the vector export is that the
// words in it are words.
//
// Positions are DATA coordinates and offsets are typographic points, so the
// note follows its point through a zoom and keeps its distance from it at any
// figure size - a note placed at 900x650 and exported at 89 mm must not end up
// half a plot away from the thing it is pointing at.
void QtPlotBackend::drawAnnotations(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    // Where each note ended up, so that a click can be matched back to it.
    //
    // RECORDED rather than recomputed. The placement below is not a formula a
    // caller could repeat: it clips on the anchor, nudges the label back inside
    // the frame, and pins the leading edge when the text is wider than the plot.
    // A hit test that reimplemented all of that would be a second copy free to
    // drift from the first, and the symptom of the drift would be notes that
    // cannot be clicked - which reads as the feature being broken rather than
    // as two functions disagreeing.
    //
    // Index-aligned with spec.annotations; a null rectangle means that note was
    // not drawn, and an undrawn note cannot be picked.
    annotationBoxes_.fill(QRectF(),int(spec.annotations.size()));
    if(spec.annotations.isEmpty()) return;
    p->save();
    // Clipped to the plot area. A note whose anchor has been zoomed out of view
    // must not reappear among the axis labels, which is where an unclipped one
    // ends up as soon as the offset carries it past the frame.
    p->setClipRect(f.plotArea.adjusted(-1,-1,1,1));
    const QFont base=font(spec,spec.style.baseFontSize);
    QFontMetricsF metrics(base);
    p->setFont(base);
    // Points to device pixels, the same conversion the rest of the typography
    // uses, so an offset given in points is the same physical distance as a
    // font size given in points.
    const double scale=double(qMax(1,spec.style.dpi))/72.0;

    int noteIndex=-1;
    for(const PlotAnnotation& note:spec.annotations){
        ++noteIndex;
        if(note.text.isEmpty()) continue;
        if(!finite(note.x)||!finite(note.y)) continue;
        if(f.xLog&&note.x<=0) continue;
        if(f.yLog&&note.y<=0) continue;
        const QPointF anchor=toDevice(f,note.x,note.y);
        if(!finite(anchor.x())||!finite(anchor.y())) continue;
        // The ANCHOR decides whether the note exists at all. A note about a
        // point that has been zoomed or panned out of view is not about
        // anything on screen, and the nudge below would otherwise drag it back
        // into the frame and draw it beside data it has nothing to do with.
        // The clip alone is not enough: it removes the label but leaves the
        // leader line, and the nudge would keep the label too.
        if(!f.plotArea.contains(anchor)) continue;
        const QPointF at=anchor+QPointF(note.offsetX*scale,note.offsetY*scale);

        const QColor ink=note.color.isValid()?note.color:spec.style.foreground;
        QRectF text=metrics.boundingRect(note.text).translated(at);
        QRectF plate=text.adjusted(-4,-2,4,2);

        // Pushed back inside the frame if the offset carried it out.
        //
        // The clip above is for a note whose ANCHOR has been zoomed away, which
        // should vanish. This is the different case: the anchor is on the plot
        // and only the label overhangs, and clipping that leaves a leader line
        // pointing at nothing and a note nobody can read. A default offset near
        // an edge does it immediately - the very first test figure had a note
        // at the bottom of the range whose text landed under the x axis.
        // qBound(lo, 0, hi) is not safe here: when the label is WIDER than the
        // plot area - a long note on a narrow figure, or a paragraph pasted
        // into one - lo exceeds hi and qBound asserts. Pinning to the leading
        // edge instead keeps the beginning of the text readable, which is the
        // half worth having when the whole of it cannot fit.
        const auto slide=[](double lo,double hi){
            if(lo>hi) return lo;              // too big to fit: show the start
            return qBound(lo,0.0,hi);
        };
        const QPointF nudge(slide(f.plotArea.left()+2-plate.left(),
                                  f.plotArea.right()-2-plate.right()),
                            slide(f.plotArea.top()+2-plate.top(),
                                  f.plotArea.bottom()-2-plate.bottom()));
        if(!nudge.isNull()){
            text.translate(nudge);
            plate.translate(nudge);
        }
        // The plate, widened a little so a small note is not a pixel-hunt, and
        // united with the anchor so the leader line's own end is clickable too.
        annotationBoxes_[noteIndex]=plate.adjusted(-3,-3,3,3)
                                        .united(QRectF(anchor-QPointF(5,5),
                                                       QSizeF(10,10)));

        // A backing plate, because a note over a dense plot is unreadable
        // otherwise, and because "put it somewhere empty" is not available when
        // the note has to sit next to the point it is about.
        QColor behind=spec.style.background;
        behind.setAlphaF(0.78f);
        p->setPen(Qt::NoPen);
        p->setBrush(behind);
        p->drawRoundedRect(plate,3,3);

        if(note.leader){
            // From the edge of the plate nearest the anchor, not from the text
            // itself, so the line does not run underneath its own label.
            const QPointF from(qBound(plate.left(),anchor.x(),plate.right()),
                               qBound(plate.top(),anchor.y(),plate.bottom()));
            QPen leader(ink);
            leader.setWidthF(qMax(0.5,spec.style.lineWidth*0.6));
            p->setPen(leader);
            p->setBrush(Qt::NoBrush);
            p->drawLine(from,anchor);
            // A dot on the anchor: without it the line ends in mid-air and it
            // is not clear which point of several the note is about.
            p->setPen(Qt::NoPen);
            p->setBrush(ink);
            p->drawEllipse(anchor,qMax(1.2,spec.style.lineWidth),qMax(1.2,spec.style.lineWidth));
        }

        p->setPen(ink);
        p->setBrush(Qt::NoBrush);
        p->drawText(at+nudge,note.text);
    }
    p->restore();
}

// What each engine wants from the mapping, read out of the guard that enforces
// it rather than guessed at from the engine's name.
//
// The first version of this read only the PAINTERS, and that was the mistake:
// most of this catalogue is expressed as a data rewrite in prepareSpec, and a
// rewrite's requirement is an `in.series.size()>=N` at the top of its block,
// not a check in a draw function. Twenty-nine engines guard on two series -
// Bland-Altman's two methods, an ROC score and its labels, a residual's fit and
// observation - and were being handed one, for ever, with no mapping the person
// could choose that would change it. Another twenty-two wanted three, four or
// six and were handed two or three. Every one of them drew a correct empty
// frame and said nothing.
//
// Anything not listed takes x plus one y, which is every ordinary series
// engine, and several y columns draw several lines.
QtPlotBackend::ColumnPlan QtPlotBackend::columnPlan(const QString& engine){
    // ---- Two columns, read as two series: a comparison between two columns.
    // Every one of these guards on in.series.size()>=2 in prepareSpec.
    static const QSet<QString> kPairs{
        QStringLiteral("Bland-Altman"),QStringLiteral("ROC Curve"),
        QStringLiteral("Precision-Recall Curve"),QStringLiteral("Volcano Plot"),
        QStringLiteral("MA Plot"),QStringLiteral("Pareto Front"),
        QStringLiteral("EIS: Nyquist"),
        QStringLiteral("Event Plot"),QStringLiteral("Residual Plot"),
        QStringLiteral("Calibration Plot"),QStringLiteral("Confusion Matrix"),
        QStringLiteral("Kaplan-Meier Survival"),QStringLiteral("Funnel Plot"),
        QStringLiteral("X-bar and R Chart"),QStringLiteral("T-S Diagram"),
        QStringLiteral("CTD Profile"),QStringLiteral("Eye Diagram"),
        QStringLiteral("Stereonet"),QStringLiteral("Psychrometric Chart"),
        QStringLiteral("Smith Chart"),QStringLiteral("Cross Correlation"),
        QStringLiteral("Slope Graph"),QStringLiteral("Confidence Ellipse"),
        QStringLiteral("Population Pyramid"),
        QStringLiteral("Scatter + Marginals"),
        // Batch 1 of the expansion. Each reads two columns and compares them:
        // an outcome against the variable it is ranked by, an observation
        // against a prediction, a score against a label, a time against an
        // event flag.
        QStringLiteral("Concentration Curve"),
        QStringLiteral("Prediction Error Plot"),
        QStringLiteral("Learning Curve"),
        QStringLiteral("Validation Curve"),
        QStringLiteral("Discrimination Threshold"),
        QStringLiteral("Silhouette Plot"),
        QStringLiteral("Radial Plot"),
        QStringLiteral("L'Abbé Plot"),
        QStringLiteral("Cumulative Meta-Analysis"),
        QStringLiteral("Nelson-Aalen Cumulative Hazard"),
        QStringLiteral("Cumulative Incidence"),
        QStringLiteral("Decision Curve"),
        QStringLiteral("Mean Cumulative Function"),
        // Batch 2: engineering. Each is a measured quantity against the one it
        // is plotted over - friction against Reynolds number, alternating
        // stress against mean stress, gain against phase.
        QStringLiteral("Moody Diagram"),
        QStringLiteral("Pump Performance Curve"),
        QStringLiteral("Stribeck Curve"),
        QStringLiteral("Haigh Diagram"),
        QStringLiteral("Crack Growth Rate"),
        QStringLiteral("Strain-Life Curve"),
        QStringLiteral("Shaft Orbit"),
        QStringLiteral("Plasticity Chart"),
        QStringLiteral("Particle Size Distribution"),
        QStringLiteral("Nichols Chart"),
        QStringLiteral("Pole-Zero Map"),
        QStringLiteral("P-V Nose Curve"),
        // Batch 3: chemistry, earth science, physics and medicine. Each reads
        // the measured quantity and the one it is plotted against.
        QStringLiteral("Lineweaver-Burk Plot"),
        QStringLiteral("Eadie-Hofstee Plot"),
        QStringLiteral("Hanes-Woolf Plot"),
        QStringLiteral("Scatchard Plot"),
        QStringLiteral("van Deemter Plot"),
        QStringLiteral("BET Plot"),
        QStringLiteral("Job Plot"),
        QStringLiteral("Gutenberg-Richter Plot"),
        QStringLiteral("Double-Mass Curve"),
        QStringLiteral("Hodograph"),
        QStringLiteral("Flow-Volume Loop"),
        QStringLiteral("Pressure-Volume Loop"),
        QStringLiteral("Allan Deviation"),
        QStringLiteral("Paschen Curve"),
        QStringLiteral("O-C Diagram"),
        // Batch 4: model interpretation, diagnostics and decision analysis.
        QStringLiteral("Partial Dependence Plot"),
        QStringLiteral("Influence Plot"),
        QStringLiteral("Tornado Diagram"),
        QStringLiteral("Efficient Frontier"),
        QStringLiteral("Snail Trail"),
        QStringLiteral("Cost-Effectiveness Plane"),
        QStringLiteral("Acceptability Curve"),
        // Batch 5. Each is a measured quantity against the one it is swept
        // over: overpotential against current, current against potential,
        // growth rate against substrate, energy against outside temperature.
        QStringLiteral("Tafel Plot"),
        QStringLiteral("Cyclic Voltammogram"),
        QStringLiteral("Levich Plot"),
        QStringLiteral("Koutecky-Levich Plot"),
        QStringLiteral("Randles-Sevcik Plot"),
        QStringLiteral("Monod Growth Curve"),
        QStringLiteral("Substrate Inhibition Curve"),
        QStringLiteral("I-V Curve"),
        QStringLiteral("Degree-Day Signature"),
        QStringLiteral("Abatement Cost Curve"),
        QStringLiteral("P-M Interaction Diagram"),
        QStringLiteral("Pushover Capacity Curve"),
        QStringLiteral("Consolidation Curve"),
        QStringLiteral("Lomb-Scargle Periodogram"),
        // Batch 6.
        QStringLiteral("Cook's Distance Plot"),
        QStringLiteral("Scale-Location Plot"),
        QStringLiteral("Partial Residual Plot"),
        QStringLiteral("Savitzky-Golay Smoothing"),
        QStringLiteral("LOWESS Trend"),
        QStringLiteral("Coherence Spectrum"),
        QStringLiteral("Operating Characteristic Curve"),
        QStringLiteral("Cottrell Plot"),
        QStringLiteral("Coulombic Efficiency Trend"),
        // Batch 7. Each reads exactly two columns and computes its
        // construction from them: two isotope ratios, a concentration and
        // a delta, a temperature and a mass, a real and an imaginary part.
        QStringLiteral("Concordia Diagram"),
        QStringLiteral("Isochron Plot"),
        QStringLiteral("Keeling Plot"),
        QStringLiteral("Van 't Hoff Plot"),
        QStringLiteral("Tauc Plot"),
        QStringLiteral("Stern-Volmer Plot"),
        QStringLiteral("TGA / DTG Curve"),
        QStringLiteral("DSC Thermogram"),
        QStringLiteral("Creep Curve"),
        QStringLiteral("Rheology Flow Curve"),
        QStringLiteral("Octave Band Spectrum"),
        QStringLiteral("Pharmacokinetic Profile"),
        QStringLiteral("Nyquist Stability Plot"),
        // Batch 8. Two columns each: a moisture and a density, a normal
        // stress and a shear stress, a sample and the species found in it,
        // a score and an outcome, a day of year and an index.
        QStringLiteral("Proctor Compaction Curve"),
        QStringLiteral("Mohr-Coulomb Envelope"),
        QStringLiteral("Influence Line"),
        QStringLiteral("Specific Range"),
        QStringLiteral("Residence Time Distribution"),
        QStringLiteral("Harmonic Spectrum"),
        QStringLiteral("Sound Level Statistics"),
        QStringLiteral("Beam Caustic"),
        QStringLiteral("Youden Plot"),
        QStringLiteral("Levey-Jennings Chart"),
        QStringLiteral("Species Accumulation Curve"),
        QStringLiteral("Epidemic Curve"),
        QStringLiteral("Effective Reproduction Number"),
        QStringLiteral("Cumulative Gain Chart"),
        QStringLiteral("Ratkowsky Square-Root Plot"),
        QStringLiteral("Phenology Curve"),
        // Batch 9. Two columns each: a q and an intensity, a heating rate
        // and a peak temperature, a distance and a recession speed, an
        // in-phase and a quadrature component.
        QStringLiteral("Guinier Plot"),
        QStringLiteral("Kratky Plot"),
        QStringLiteral("Hill Plot"),
        QStringLiteral("Kissinger Plot"),
        QStringLiteral("Avrami Plot"),
        QStringLiteral("Wilson Plot"),
        QStringLiteral("Van Krevelen Diagram"),
        QStringLiteral("Hertzsprung-Russell Diagram"),
        QStringLiteral("Rotation Curve"),
        QStringLiteral("Hubble Diagram"),
        QStringLiteral("Constellation Diagram"),
        QStringLiteral("Group Delay"),
        QStringLiteral("Wind Profile (Log Law)"),
        QStringLiteral("Experience Curve"),
        QStringLiteral("Half-Normal Plot"),
        QStringLiteral("Response Waterfall"),
        // Batch 10. Two columns each: a depth and a discharge, a thickness
        // and an intensity, a duration and a power, a category and a count.
        QStringLiteral("Specific Energy Diagram"),
        QStringLiteral("MTF Curve"),
        QStringLiteral("Radioactive Decay Fit"),
        QStringLiteral("Attenuation Curve"),
        QStringLiteral("Depth-Dose Curve"),
        QStringLiteral("Critical Power Curve"),
        QStringLiteral("Lactate Threshold Curve"),
        QStringLiteral("Half-Power Bandwidth"),
        QStringLiteral("Bass Diffusion Curve"),
        QStringLiteral("Pareto Chart"),
        QStringLiteral("Statistical Power Curve"),
        QStringLiteral("Sequential Test Boundaries"),
        // Batch 11. Two columns each: a retention time and a signal, a pH
        // and a potential, a field and a flux density, a bearing and a
        // gain, a suction and a water content.
        QStringLiteral("Chromatogram Peak Metrics"),
        QStringLiteral("Langmuir Isotherm"),
        QStringLiteral("Zeta Potential Curve"),
        QStringLiteral("Distillation Curve"),
        QStringLiteral("qPCR Standard Curve"),
        QStringLiteral("Melting Curve (Tm)"),
        QStringLiteral("Growth Rate (OD)"),
        QStringLiteral("Growth Percentile Chart"),
        QStringLiteral("Hysteresis Loop (B-H)"),
        QStringLiteral("Radiation Pattern"),
        QStringLiteral("Soil Water Retention Curve"),
        QStringLiteral("Detrended Fluctuation Analysis"),
        QStringLiteral("Poincare Plot"),
        QStringLiteral("Kingman Queue Curve"),
        QStringLiteral("Burndown Chart"),
        QStringLiteral("Regression Confidence Band"),
        // Batch 12. Two columns each: a wavelength and a reflectance, a
        // real and an imaginary permittivity, a titrant volume and a
        // conductivity, a sampling position and an error rate.
        QStringLiteral("Kubelka-Munk Plot"),
        QStringLiteral("Freundlich Isotherm"),
        QStringLiteral("Cole-Cole Plot"),
        QStringLiteral("Conductometric Titration"),
        QStringLiteral("Tolerance Interval Plot"),
        QStringLiteral("Rare-Event Interval Chart"),
        QStringLiteral("Unit Hydrograph"),
        QStringLiteral("Recession Curve Analysis"),
        QStringLiteral("Step Response Metrics"),
        QStringLiteral("Jitter Bathtub"),
        QStringLiteral("Pressure Derivative Plot"),
        // Two positions and nothing else. The cells are shaded by their own
        // area, so a third column would be a second thing competing to colour
        // them.
        QStringLiteral("Voronoi Diagram"),
        // Period and pseudo-velocity; displacement and acceleration are read
        // off the two diagonal families rather than supplied.
        QStringLiteral("Tripartite Response Spectrum"),
        // A value and its uncertainty. drawErrorBar reads series[0] as the
        // measurement and series[1] as the error and IGNORES everything after
        // that, so without a plan an Error Bar of five mapped columns drew two
        // of them under a legend naming all five - the legend said more than
        // the picture contained. Two columns, and the panel asks for the
        // second one by name.
        QStringLiteral("Error Bar")};
    if(kPairs.contains(engine)) return {2,2,true};

    // ---- Every column mapped, read as one series each. These get WIDER with
    // more columns rather than ignoring the extras: a correlation matrix of six
    // variables is a 6x6 matrix, and parallel coordinates has one axis per
    // column. Capping them at a fixed count would throw away the whole point.
    static const QSet<QString> kAllColumns{
        QStringLiteral("Correlation Matrix"),QStringLiteral("Covariance Matrix"),
        QStringLiteral("Spy Matrix"),QStringLiteral("Parallel Coordinates"),
        QStringLiteral("Andrews Curves")};
    if(kAllColumns.contains(engine)) return {2,0,true};
    // One spoke per column, and fewer than three spokes is not a radar chart.
    if(engine==QLatin1String("Radar Chart")) return {3,0,true};
    // A Campbell diagram has one curve per mode and an airfoil has one per
    // surface, so both take the speed or the chord and then as many more
    // columns as are mapped. Capping them would silently drop a mode.
    if(engine==QLatin1String("Campbell Diagram")
       ||engine==QLatin1String("Airfoil Cp Distribution")) return {2,0,true};
    // A response spectrum has one curve per damping ratio and a waffle has one
    // block per category, so both widen with the columns mapped.
    if(engine==QLatin1String("Response Spectrum")) return {2,0,true};
    // Angle, radius and the column the marks are SIZED by. Three, because with
    // the default plan the mapping filled two and drawPolar had nothing to size
    // anything with - which is how Polar Bubble stayed Polar Scatter.
    if(engine==QLatin1String("Polar Bubble")) return {3,3,false};
    // A band needs two edges, so this is the one ordinary series engine that
    // wants x plus TWO y columns rather than x plus one. With the default plan
    // the automatic mapping filled a single y column, drawFillBetween had
    // nothing to be between, and it fell back to the baseline - which would
    // have left the alias in place in everything but name.
    if(engine==QLatin1String("Fill Between")) return {3,0,false};
    // A streamgraph has one band per column and takes its x from the frame, so
    // it widens the same way a stacked area does. asSeries stays false: these
    // are ordinary y columns sharing one x, not roles.
    if(engine==QLatin1String("Streamgraph")) return {2,0,false};
    // pH and then one column per species boundary; position and then one column
    // per symbol. Both widen with the columns mapped, and capping either at two
    // would draw one boundary out of six or a logo of a two-letter alphabet.
    if(engine==QLatin1String("Pourbaix Diagram")
       ||engine==QLatin1String("Sequence Logo")) return {2,0,false};
    // Silica and then every oxide mapped, a reference and then every
    // sample: both compare the FIRST column against all the rest, so
    // capping them at two would draw one trend out of eight.
    if(engine==QLatin1String("Harker Diagram")
       ||engine==QLatin1String("Normalised Spider Diagram")) return {2,0,true};
    // A temperature and then one free-energy line per metal. Two would
    // draw one metal, which is not an Ellingham diagram.
    if(engine==QLatin1String("Ellingham Diagram")) return {2,0,true};
    // Ca, Mg, Na+K, HCO3, SO4, Cl - in milliequivalents, because the
    // diagram is about charge balance. Three of them is a ternary, which
    // is what this engine used to be and is not a Piper diagram.
    if(engine==QLatin1String("Piper Diagram")) return {6,6,true};
    if(engine==QLatin1String("Waffle Chart")) return {1,0,true};
    // One signal, and these read it from the y column alone.
    if(engine==QLatin1String("Partial Autocorrelation")
       ||engine==QLatin1String("Recurrence Plot")
       ||engine==QLatin1String("Wavelet Scalogram")) return {1,2,true};
    // Time, value, and the period if a column carries one.
    if(engine==QLatin1String("Seasonal Decomposition")) return {2,3,true};
    if(engine==QLatin1String("Seasonal Subseries Plot")) return {2,2,true};
    // Frequency, gain, and phase if it is mapped.
    if(engine==QLatin1String("Bode Plot")) return {2,3,true};
    // Every column is a variable: a biplot's arrows and a glyph's spokes are
    // one per column, so both get wider rather than dropping the extras.
    if(engine==QLatin1String("Biplot")
       ||engine==QLatin1String("Star Glyph Plot")) return {2,0,true};
    // Power and energy density, and a column that names the device family.
    if(engine==QLatin1String("Ragone Plot")) return {2,3,true};
    // A plane stress state: sigma-x, sigma-y and tau-xy.
    if(engine==QLatin1String("Mohr's Circle")) return {3,3,true};
    // Time, flux, and the trial period if there is a column for it.
    if(engine==QLatin1String("Phase-Folded Light Curve")) return {2,3,true};
    // Feature and prediction, plus an id column that splits the cloud into one
    // curve per observation - which is the whole difference between an ICE
    // plot and the partial dependence it averages to.
    if(engine==QLatin1String("ICE Plot")) return {2,3,true};
    // Response, the predictor of interest, and the one being controlled for.
    if(engine==QLatin1String("Added-Variable Plot")) return {3,3,true};
    // Two factors and a response; subject, time and value; subject, start, stop.
    if(engine==QLatin1String("Interaction Plot")
       ||engine==QLatin1String("Lasagna Plot")
       ||engine==QLatin1String("Swimmer Plot")) return {3,3,true};

    static const QSet<QString> kThree{
        // gridFromSeries: x, y, value.
        QStringLiteral("2D Heatmap"),QStringLiteral("2D Contour"),
        QStringLiteral("2D Histogram"),QStringLiteral("Hexbin Density"),
        // Edge lists: from, to, weight.
        QStringLiteral("Network Graph"),QStringLiteral("Chord Diagram"),
        // Set membership.
        QStringLiteral("UpSet Plot"),
        // Three corners.
        QStringLiteral("Ternary Scatter"),
        // Surfaces and 3-D: x, y, z.
        QStringLiteral("Surface + Contours"),QStringLiteral("Comet 3D"),
        QStringLiteral("Ribbon"),
        // Estimate and its two confidence bounds.
        QStringLiteral("Forest Plot"),QStringLiteral("Caterpillar Plot"),
        // Temperature, hours to rupture, and the stress they were held at.
        QStringLiteral("Larson-Miller Curve"),
        // A surface sampled on a grid: x, y and elevation. The whole terrain
        // family reads the same three and then differs in what it computes.
        QStringLiteral("Terrain Profile"),QStringLiteral("Slope Map"),
        QStringLiteral("Aspect Map"),QStringLiteral("Hillshade"),
        // Runway heading, wind direction, wind speed.
        QStringLiteral("Runway Crosswind"),
        // Batch 7. A third column that GROUPS rather than measures: the
        // return period a rainfall curve belongs to, the temperature an
        // isotherm was run at, the date a yield curve was observed on.
        QStringLiteral("IDF Curve"),
        QStringLiteral("Master Curve (TTS)"),
        QStringLiteral("Yield Curve"),
        // Batch 8. A decision speed and the two distances it decides
        // between; a stream's supply and target temperature and the heat
        // capacity flowrate that carries it between them.
        QStringLiteral("Balanced Field Length"),
        QStringLiteral("Composite Curves (Pinch)"),
        // Batch 9. An order quantity and the two costs it trades off.
        QStringLiteral("EOQ Cost Curve"),
        // Batch 10. A third column that is either a SECOND CURVE to cross
        // against the first - a system head, a renewable output, a
        // departure count - or the group a curve belongs to: a capacity
        // ratio, a protective device, an intake cohort.
        QStringLiteral("NTU-Effectiveness Curve"),
        QStringLiteral("Pump Operating Point"),
        QStringLiteral("Duck Curve (Net Load)"),
        QStringLiteral("Protection Coordination Curve"),
        QStringLiteral("Cumulative Vehicle Count"),
        QStringLiteral("Cohort Retention Curve"),
        // Batch 11. A speed with the two torques it decides between, and
        // the three quantities Little's law claims are one equation.
        QStringLiteral("Torque-Speed Curve"),
        QStringLiteral("Little's Law Check"),
        // Batch 12. A third column that is a constant of the experiment
        // (the power per unit length), or the group each reading belongs
        // to: an operator, a subject, a heating rate.
        QStringLiteral("Hot-Wire Conductivity"),
        QStringLiteral("Multi-Vari Chart"),
        QStringLiteral("Spaghetti Plot"),
        QStringLiteral("Isoconversional Plot"),
        // A sounding: pressure, temperature, dewpoint. Pressure first even
        // though it is drawn vertically, because it is the independent
        // variable and the two temperatures share it.
        QStringLiteral("Skew-T Log-P"),
        QStringLiteral("Emagram"),
        QStringLiteral("Stuve Diagram"),
        QStringLiteral("Tephigram"),
        // A hierarchy as node, parent, and the value the node holds itself.
        // Numbers, because that is what a mapped column carries - the same
        // convention as the edge list the network engines read.
        QStringLiteral("Icicle Plot"),
        QStringLiteral("Flame Graph"),
        // The same three, with the value read as a branch length.
        QStringLiteral("Cladogram"),
        // The two scales read from and the one read off.
        QStringLiteral("Alignment Nomogram")};
    if(kThree.contains(engine)) return {3,3,true};

    // The Piper's six, in milliequivalents - drawn as one shape per water by
    // the Stiff, and projected into a square by the Durov.
    if(engine==QLatin1String("Stiff Diagram")
       ||engine==QLatin1String("Durov Diagram")) return {6,6,true};
    // An edge list: source, target, and an optional weight.
    if(engine==QLatin1String("Arc Diagram")
       ||engine==QLatin1String("Alluvial Diagram")
       ||engine==QLatin1String("Hive Plot")) return {2,3,true};

    // Contingency: the two categories, and a count column if there is one.
    if(engine==QLatin1String("Mosaic Plot")) return {2,3,true};
    // Row, start, end - and a fourth column colours the bars if it is mapped.
    if(engine==QLatin1String("Gantt Schedule")
       ||engine==QLatin1String("Availability Timeline")
       ||engine==QLatin1String("Borehole Log")) return {3,4,true};
    // The two plotted invariant masses, then the parent and the three daughter
    // masses as constant columns. Six for what is really a two-column scatter,
    // because four of them are parameters and this program has no other way to
    // carry one.
    // Two mapped columns and four engine settings; six columns are still read
    // as m2(12), m2(23) and the four masses, which is how it shipped.
    if(engine==QLatin1String("Dalitz Plot")) return {2,6,true};
    // Source segment and position, target segment and position, and a weight
    // if one is mapped.
    if(engine==QLatin1String("Circos Plot")) return {4,5,true};
    // Frequency, magnitude, and - optionally - phase.
    //
    // It was in the two-column set, which meant a Bode plot could never carry
    // the phase at all: the mapping panel asked for two columns and stopped,
    // and the y axis was labelled "|Z| / phase" for a figure that only ever
    // had one of them on it. The phase is drawn on the right-hand ordinate
    // now, so there is somewhere for it to go.
    if(engine==QLatin1String("EIS: Bode")) return {2,3,true};
    // Every mapped column against every other. Six is the practical ceiling:
    // 36 panels in one figure is already small, and the panels shrink with the
    // SQUARE of the count.
    if(engine==QLatin1String("Plot Matrix")) return {2,6,true};
    // Modal per cent quartz, alkali feldspar, plagioclase and feldspathoid.
    if(engine==QLatin1String("QAPF Diagram (Plutonic)")
       ||engine==QLatin1String("QAPF Diagram (Volcanic)")) return {4,4,true};
    // Per cent sand, silt and clay, in the order an analysis reports them.
    if(engine==QLatin1String("Soil Texture Triangle (UK)")
       ||engine==QLatin1String("Soil Texture Triangle (USDA)")) return {3,3,true};
    // Chromosome, band start, band end, stain.
    if(engine==QLatin1String("Karyotype Ideogram")) return {4,4,true};
    // Three components and the value measured over them.
    if(engine==QLatin1String("Ternary Contour")) return {4,4,true};
    // Open, high, low, close. The period comes from the x mapping.
    if(engine==QLatin1String("OHLC Candlestick")) return {4,4,true};

    static const QSet<QString> kFour{
        // x, y and the two vector components.
        QStringLiteral("Quiver Field"),QStringLiteral("Feather"),
        QStringLiteral("Stream Field"),QStringLiteral("Stream Particles"),
        QStringLiteral("Phase Portrait"),QStringLiteral("Flow Texture (LIC)"),
        QStringLiteral("Divergence Map"),QStringLiteral("Vorticity Map"),
        // x, y, z and the scalar the volume is made of.
        QStringLiteral("Volume Show"),QStringLiteral("Volume Slice"),
        QStringLiteral("Isosurface"),QStringLiteral("Isonormals"),
        QStringLiteral("Isocaps"),QStringLiteral("Contour Slice")};
    if(kFour.contains(engine)) return {4,4,true};
    // Origin and destination are two coordinate PAIRS, and a fifth column
    // weights the flow.
    if(engine==QLatin1String("Origin-Destination Flow")) return {4,5,true};

    // Longitude, latitude, region and value - and a fifth column, the ring,
    // when the file distinguishes an island from the mainland. Four draw a
    // correct map for regions that are single polygons; the fifth is what
    // stops an archipelago joining itself up across the water.
    if(engine==QLatin1String("Choropleth")) return {4,5,true};

    // 3-D vector fields: x, y, z and the three components.
    //
    // Six rather than the four draw3DField will tolerate, and the difference
    // matters: with four it has a magnitude and no direction, so it draws cones
    // that all point the same way and stream tubes that do not flow. That is a
    // picture of the wrong thing, which is worse than a blank one. Asking for
    // six means the automatic mapping fills six; a hand-mapped four still draws
    // and is told what is missing.
    static const QSet<QString> kVector3D{
        QStringLiteral("3D Quiver"),QStringLiteral("Cone Plot"),
        QStringLiteral("Stream Tube"),QStringLiteral("Stream Ribbon"),
        QStringLiteral("Tensor Glyph Field")};
    if(kVector3D.contains(engine)) return {6,6,true};

    if(engine.startsWith(QLatin1String("3D "))) return {3,3,true};

    // NOT column engines, and each was tried and removed after reading what the
    // engine does with its input:
    //   Spectrogram      takes ONE signal column and PRODUCES three series.
    //                    Demanding three would have made it read the x column
    //                    as the signal.
    //   4D / 5D Scatter  is an ordinary series engine - drawScatter iterates
    //                    spec.series and takes x from the frame. Its extra
    //                    dimensions are marker size and colour, not columns.
    // Both drew correctly before this function existed, and listing them here
    // would have broken them in the name of fixing something else.
    return {2,2,false};
}

int QtPlotBackend::columnsRequired(const QString& engine){
    return columnPlan(engine).minimum;
}

// ----------------------------------------------------------------- parameters
//
// The constants engines declare. One table, read by the renderer for its
// defaults and by the interface to build controls; there is no second list of
// keys anywhere, which is the whole point of the mechanism.
//
// Ranges are what the engine can actually draw, not taste. Dalitz's masses are
// bounded below at zero because a negative mass has no meaning and above at a
// number large enough for any decay anyone will plot in GeV; the relation that
// really matters - parent heavier than the sum of its daughters - cannot be
// expressed as a per-control range and is checked by the engine, which says so
// on the figure rather than refusing to draw one.
QVector<QtPlotBackend::EngineParameter> QtPlotBackend::engineParameters(const QString& engine){
    if(engine==QLatin1String("Dalitz Plot")){
        // Units are the caller's: the axes are squared masses, so masses in
        // GeV/c^2 give axes in GeV^2/c^4. Nothing here assumes GeV - the
        // defaults are round numbers rather than any particular particle, so a
        // fresh Dalitz draws a valid region and names nothing it should not.
        return {
            {QStringLiteral("parentMass"),QStringLiteral("Parent mass"),
             QStringLiteral("mass"),
             QStringLiteral("The decaying particle. Must exceed the three daughters "
                            "together, or there is no allowed region."),
             1.0,0.0,1000.0,6},
            {QStringLiteral("daughter1"),QStringLiteral("Daughter 1"),
             QStringLiteral("mass"),
             QStringLiteral("The particle whose squared mass with daughter 2 is the "
                            "x axis."),
             0.14,0.0,1000.0,6},
            {QStringLiteral("daughter2"),QStringLiteral("Daughter 2"),
             QStringLiteral("mass"),
             QStringLiteral("Shared by both axes: m2(12) on x and m2(23) on y."),
             0.14,0.0,1000.0,6},
            {QStringLiteral("daughter3"),QStringLiteral("Daughter 3"),
             QStringLiteral("mass"),
             QStringLiteral("The particle whose squared mass with daughter 2 is the "
                            "y axis."),
             0.14,0.0,1000.0,6}};
    }
    if(engine==QLatin1String("Choropleth")){
        // HOW THE VALUE BECOMES A COLOUR - the choice a choropleth is actually
        // read on, and the one the data cannot make for you.
        //
        // A skewed column on a linear ramp paints almost every region the same
        // colour, and population, income and incidence are all skewed - so a
        // map of them in one shade is a map of nothing. Quantile classing is
        // the standard cartographic answer, and it is NOT made the default,
        // because the three answers say different things:
        //
        //   linear   - equal steps in the value are equal steps in colour. The
        //              only one where a colour reads back as a magnitude
        //              without consulting the key's class edges.
        //   log10    - for a quantity spanning orders of magnitude.
        //   quantile - equal COUNT per class. Shows RANK rather than
        //              magnitude: two regions in adjacent classes may differ
        //              by almost nothing, and two in the same class by a lot.
        //
        // Nothing in the data says which the reader wants, so this is a choice
        // rather than a default - the same argument as the forest plot's null
        // line immediately below.
        return {
            {QStringLiteral("valueScale"),QStringLiteral("Value scale"),
             QString(),
             QStringLiteral("0 linear. 1 log10, for a quantity spanning orders "
                            "of magnitude - a region whose value is zero or "
                            "negative is left unfilled, because it has no place "
                            "on a logarithmic ramp. 2 quantile, which puts an "
                            "equal number of regions in each class and shows "
                            "rank rather than magnitude."),
             0.0,0.0,2.0,0},
            {QStringLiteral("classes"),QStringLiteral("Classes"),
             QString(),
             QStringLiteral("How many classes quantile classing uses; ignored "
                            "by the other two scales. Five is the usual "
                            "cartographic choice - past about nine a reader "
                            "can no longer tell two classes apart on the map."),
             5.0,2.0,9.0,0}};
    }
    if(engine==QLatin1String("Forest Plot")){
        // OFF BY DEFAULT, and the setting decides both questions at once.
        //
        // The pooled estimate itself is unambiguous - inverse-variance
        // weighting, with each study's variance taken from the half-width of
        // its interval. What is NOT knowable from the data is where the null
        // line belongs: a risk ratio has no effect at 1, a difference in means
        // has no effect at 0, and the engine sees three columns of numbers with
        // nothing to say which it is holding. Drawing the line in the wrong
        // place would put every study on the wrong side of "no effect", which
        // is worse than drawing no line at all.
        //
        // So one control answers both: off, or a measure type that also fixes
        // the null. That is why this is a choice rather than a default.
        return {
            {QStringLiteral("pooled"),QStringLiteral("Pooled estimate"),
             QString(),
             QStringLiteral("0 none. 1 for a ratio measure - risk ratio, odds "
                            "ratio, hazard ratio - with the null at 1. 2 for a "
                            "difference measure - mean difference, risk "
                            "difference - with the null at 0. The weighting is "
                            "inverse-variance either way; the number only "
                            "chooses where no effect sits, which the data "
                            "cannot say."),
             0.0,0.0,2.0,0}};
    }
    // EVERY COLOUR-MAPPED ENGINE GETS THE SAME CHOICE, last, so a named engine
    // above keeps its own list.
    //
    // The reasoning is the choropleth's, written out where it was first needed
    // and true of all of them: a skewed quantity on a linear ramp paints nearly
    // everything one colour, and a quantity spanning orders of magnitude is
    // unreadable without a log ramp. The machinery - ColourScale,
    // scalePosition, the classing-aware colour bar - was built for the
    // choropleth and worked; the other ten colour-bar call sites simply never
    // reached it.
    //
    // Asked of `usesColourMap`, which is the same question `render` asks when
    // it decides whether a figure is a field, so the control appears on
    // exactly the engines the scale applies to.
    if(usesColourMap(engine)&&!engine.startsWith(QLatin1String("3D "))){
        return {
            {QStringLiteral("valueScale"),QStringLiteral("Value scale"),
             QString(),
             QStringLiteral("0 linear - equal steps in the value are equal "
                            "steps in colour, and the only one where a colour "
                            "reads back as a magnitude without consulting the "
                            "key. 1 log10, for a quantity spanning orders of "
                            "magnitude; a value of zero or below has no place "
                            "on a logarithmic ramp and is left undrawn. "
                            "2 quantile, equal COUNT per class, which shows "
                            "rank rather than magnitude."),
             0.0,0.0,2.0,0},
            {QStringLiteral("classes"),QStringLiteral("Classes"),
             QString(),
             QStringLiteral("How many classes quantile classing uses; ignored "
                            "by the other two scales. Past about nine a reader "
                            "can no longer tell two classes apart."),
             5.0,2.0,9.0,0}};
    }
    return {};
}

QMap<QString,double> QtPlotBackend::engineParameterDefaults(const QString& engine){
    QMap<QString,double> out;
    const QVector<EngineParameter> declared=engineParameters(engine);
    for(const EngineParameter& p:declared) out.insert(p.key,p.defaultValue);
    return out;
}

bool QtPlotBackend::isPolarEngine(const QString& engine){
    static const QSet<QString> kPolar{
        QStringLiteral("Polar Line"),QStringLiteral("Radar Chart"),
        QStringLiteral("Compass"),QStringLiteral("Polar Scatter"),
        QStringLiteral("Polar Bubble"),QStringLiteral("Polar Histogram"),
        QStringLiteral("Wind Rose"),
        // Derives to Polar Scatter, so it is drawn as one and the interface
        // has to know that while the person still has "Stereonet" selected.
        QStringLiteral("Stereonet")};
    return kPolar.contains(engine);
}

QString QtPlotBackend::explainEmpty(const PlotSpec& chosen,const PlotSpec& prepared){
    const QString e=chosen.engine;
    // How many columns the person actually mapped, which is what the
    // requirements below are about. The prepared spec is used only to decide
    // whether anything came out.
    const int n=int(chosen.series.size());

    // How many it needed comes from columnsRequired(), the one place that
    // knows; the sets below only choose the WORDING, because "x, y and the
    // value it colours by" and "x, y and z" are the same requirement described
    // to different people. Deriving the number here as well is how the message
    // and the mapping drift apart.
    const ColumnPlan plan=columnPlan(e);
    const int needed=plan.minimum;

    // gridFromSeries: series 0 is x, 1 is y, 2 is the value.
    static const QSet<QString> kGridEngines{
        QStringLiteral("2D Heatmap"),QStringLiteral("2D Contour"),
        QStringLiteral("2D Histogram"),QStringLiteral("Hexbin Density")};
    if(kGridEngines.contains(e)&&n<needed){
        return QStringLiteral(
            "%1 needs three mapped columns - x, y and the value it colours by - "
            "and %2 %3 mapped. The axes come from the data, which is why the "
            "frame is drawn and the plot area is empty.")
            .arg(e).arg(n).arg(n==1?QStringLiteral("is"):QStringLiteral("are"));
    }

    // Vector fields read two more columns for the components.
    static const QSet<QString> kVectorEngines{
        QStringLiteral("Quiver Field"),QStringLiteral("Feather"),
        QStringLiteral("Stream Field"),QStringLiteral("Stream Particles"),
        QStringLiteral("Phase Portrait"),QStringLiteral("Flow Texture (LIC)"),
        QStringLiteral("Divergence Map"),QStringLiteral("Vorticity Map")};
    if(kVectorEngines.contains(e)&&n<needed){
        return QStringLiteral(
            "%1 needs four mapped columns - x, y and the two vector components - "
            "and %2 mapped.").arg(e).arg(n);
    }

    if(e.startsWith(QLatin1String("3D "))
       ||e==QLatin1String("Surface + Contours")
       ||e==QLatin1String("Ribbon")){
        if(n<needed) return QStringLiteral(
            "%1 needs three mapped columns - x, y and z - and %2 mapped.").arg(e).arg(n);
    }

    if(e==QLatin1String("Ternary Scatter")&&n<needed)
        return QStringLiteral("Ternary Scatter needs three mapped columns, one per corner.");

    // Anything else that wants more than it got. Without this an engine added
    // to columnsRequired but not to a wording set above says nothing at all,
    // which is the failure this function exists to prevent.
    //
    // Only for the COLUMN engines, and the reason is that `n` counts SERIES
    // rather than mapped columns, and the two are the same number only for
    // those. An ordinary engine takes x from the frame and one series per y
    // column, so a perfectly mapped Line Chart has n == 1 and needed == 2 - and
    // the first version of this said "Line Chart needs 2 mapped columns and 1
    // is mapped" over a figure drawing two hundred thousand points quite
    // happily. A warning on a working plot is worse than no warning at all.
    //
    // This was `needed>=3` while the plan was only a number, which was the same
    // test by accident and stopped being so the moment two-column comparison
    // engines were described honestly.
    if(plan.asSeries&&n<needed&&n>0)
        return QStringLiteral("%1 needs %2 mapped columns and %3 %4 mapped.")
            .arg(e).arg(needed).arg(n)
            .arg(n==1?QStringLiteral("is"):QStringLiteral("are"));

    if(n==0)
        return QStringLiteral(
            "No column produced any drawable values. Check that the mapped "
            "columns are numeric and are not entirely blank.");

    // CATEGORIES, given a continuous column.
    //
    // A chord diagram of ten thousand distinct node ids is not a chord diagram,
    // and a confusion matrix of ten thousand classes is not a confusion matrix.
    // Each of these counts its categories and returns before drawing when there
    // are too many, which is the right thing to do and looks exactly like a
    // broken program: a title, a blank rectangle, and no explanation.
    //
    // The number tested here is the number the engine's own guard tests, so the
    // sentence appears when the figure is blank and never when it is not.
    {
        int limit=0; bool perAxis=false;
        if(e==QLatin1String("Chord Diagram")) limit=60;
        else if(e==QLatin1String("Mosaic Plot")){ limit=40; perAxis=true; }
        else if(e==QLatin1String("Confusion Matrix")) limit=20;
        if(limit>0&&n>=2){
            const auto levels=[&](int which,int upTo){
                QSet<double> seen;
                for(double d:chosen.series.at(which).y){
                    if(!std::isfinite(d)) continue;
                    seen.insert(d);
                    if(seen.size()>upTo) return upTo+1;
                }
                return int(seen.size());
            };
            int found;
            if(perAxis) found=qMax(levels(0,limit),levels(1,limit));
            else {
                QSet<double> both;
                for(int i=0;i<2;++i)
                    for(double d:chosen.series.at(i).y){
                        if(!std::isfinite(d)) continue;
                        both.insert(d);
                        if(both.size()>limit) break;
                    }
                found=int(both.size());
            }
            if(found>limit)
                return QStringLiteral(
                    "%1 groups rows into categories, and the mapped columns hold "
                    "more than %2 distinct values - so there is nothing to group. "
                    "Map columns that take a small number of values, such as a "
                    "label, a class or a bin, rather than a measured quantity.")
                    .arg(e).arg(limit);
        }
    }

    // TOO MANY SITES, which is not a category problem and needs its own
    // sentence: the columns here are supposed to be continuous, so the advice
    // above - map a label rather than a measured quantity - would be exactly
    // wrong. What is too large is the number of DISTINCT positions, and the
    // count stops one past the limit for the same reason the guard does.
    if(e==QLatin1String("Voronoi Diagram")&&n>=2){
        QSet<QPair<double,double>> here;
        bool over=false;
        for(const PlotSeries& s:chosen.series){
            if(over) break;
            const int rows=qMin(s.x.size(),s.y.size());
            for(int i=0;i<rows;++i){
                if(!std::isfinite(s.x[i])||!std::isfinite(s.y[i])) continue;
                here.insert(qMakePair((s.x[i]==0.0)?0.0:s.x[i],
                                      (s.y[i]==0.0)?0.0:s.y[i]));
                if(here.size()>2000){ over=true; break; }
            }
        }
        if(over)
            return QStringLiteral(
                "A Voronoi diagram is built by clipping the frame against every "
                "other site, so its cost grows with the square of the number of "
                "sites - and more than 2000 of them would give cells too small "
                "to tell apart in any case. Filter the rows, or aggregate them, "
                "before tessellating.");
    }

    // A MAPPED COLUMN THAT CANNOT BE AN AXIS.
    //
    // Reported with two screenshots of the same engine on two datasets: one
    // drew a surface, the other drew a completely blank canvas - no cube, no
    // axes, no title, no notice - while the mapping panel said, in green, "3D
    // Mesh reads 3 columns, and has them".
    //
    // It did. Having a column and being able to draw an axis from it are
    // different questions, and only the first was being asked. That is this
    // project's most-repeated fault - two measurements of one property, one of
    // which the person is shown and the other of which decides.
    //
    // There are two ways it goes wrong and they look identical on screen:
    //
    //   NO FINITE VALUES AT ALL. draw3D takes three spans and returns if any
    //   is invalid, which happens before the cube, before the axes and before
    //   the title - so the canvas is exactly as it was after the background
    //   fill. Measured: 0 pixels of ink on 3D Mesh, 3D Surface and 3D Scatter.
    //
    //   EVERY FINITE VALUE THE SAME. The span is then valid - boundsOf widens a
    //   flat column to lo..lo+1 so the cube can be drawn - but gridFromSeries
    //   refuses, because there is nothing to bin along that side. The cube and
    //   its axes appear and the surface does not. Measured: 5,975 pixels, all
    //   of them chrome.
    //
    // ASKED OF THE SAME FUNCTIONS THE PAINTERS ASK. `boundsOf` is draw3D's own
    // call and the flat test is gridFromSeries' own condition, so this cannot
    // become a second opinion about what is drawable.
    {
        struct AxisRole { int index; const char* role; };
        QVector<AxisRole> axes;
        const bool cube=e.startsWith(QLatin1String("3D "))
                      ||e==QLatin1String("Surface + Contours")
                      ||e==QLatin1String("Ribbon");
        const bool lattice=kGridEngines.contains(e)||kVectorEngines.contains(e);
        if(cube)         axes={{0,"x"},{1,"y"},{2,"z"}};
        else if(lattice) axes={{0,"x"},{1,"y"},{2,"the value it colours by"}};
        for(const AxisRole& a:axes){
            if(a.index>=n) continue;
            const PlotSeries& column=chosen.series.at(a.index);
            // "z, the column mapped to z" is how the first version read when a
            // file's columns happen to be called x, y and z, which is most
            // exported grids. Named once, and only once.
            const QString role=QString::fromLatin1(a.role);
            const QString named=(column.label.isEmpty()
                                 ||column.label.compare(role,Qt::CaseInsensitive)==0)
                ? QStringLiteral("the column mapped to %1").arg(role)
                : QStringLiteral("%1, the column mapped to %2").arg(column.label,role);

            if(!boundsOf(column.y).valid)
                return QStringLiteral(
                    "%1 cannot draw this dataset: %2, has no numeric values in "
                    "it at all - every row there is blank, text, or not a "
                    "number. There is no axis to build from it, which is why "
                    "nothing at all was drawn. Map a measured column, or try "
                    "another engine.")
                    .arg(e,named);

            // Flat. Only the two axes a lattice is binned along care - a flat
            // VALUE column is a legitimate figure, just a featureless one, and
            // a flat z on a cube draws as a floor rather than as nothing.
            const bool binnedAlong=lattice ? (a.index<2) : false;
            if(!binnedAlong) continue;
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            int rows=0;
            for(double d:column.y) if(finite(d)){ ++rows; lo=qMin(lo,d); hi=qMax(hi,d); }
            if(rows>0&&!(hi>lo))
                return QStringLiteral(
                    "%1 bins the rows onto a lattice, and %2, holds the single "
                    "value %3 in all %4 of its numeric rows - so the lattice has "
                    "no width along that side and no cell can be filled. The "
                    "frame is drawn and the plot area is not. Map a column that "
                    "varies, or try another engine.")
                    .arg(e,named).arg(lo).arg(rows);
        }
        // The cube engines bin too, through the same gridFromSeries, but only
        // the ones that draw a surface. A 3-D scatter places each row on its
        // own and is perfectly happy with a flat side.
        if(cube&&(e==QLatin1String("3D Topography / Surface")
                ||e==QLatin1String("3D Mesh")
                ||e==QLatin1String("3D Contour")
                ||e==QLatin1String("Surface + Contours"))&&n>=2){
            for(int which=0;which<2;++which){
                const PlotSeries& column=chosen.series.at(which);
                double lo=std::numeric_limits<double>::infinity(),hi=-lo;
                int rows=0;
                for(double d:column.y) if(finite(d)){ ++rows; lo=qMin(lo,d); hi=qMax(hi,d); }
                if(rows==0||hi>lo) continue;
                const QString role=which==0?QStringLiteral("x"):QStringLiteral("y");
                const QString named=(column.label.isEmpty()
                                     ||column.label.compare(role,Qt::CaseInsensitive)==0)
                    ? QStringLiteral("the column mapped to %1").arg(role)
                    : QStringLiteral("%1, the column mapped to %2").arg(column.label,role);
                return QStringLiteral(
                    "%1 fits a surface to a lattice, and %2, holds the single "
                    "value %3 in all %4 of its numeric rows - a surface needs "
                    "two sides that vary and this one has one. The cube and its "
                    "axes are drawn and the surface is not. Map a column that "
                    "varies, or try a 3D Scatter, which places each row on its "
                    "own and does not need a lattice.")
                    .arg(e,named).arg(lo).arg(rows);
            }
        }
    }

    // Columns were mapped, and the rewrite still produced nothing.
    bool anyPoints=false;
    for(const PlotSeries& s:prepared.series) if(!s.y.isEmpty()){ anyPoints=true; break; }

    // The preparer's own explanation wins over anything guessed here.
    //
    // When prepareSpec empties a spec it usually knows exactly why and writes
    // it into the title - Implicit Surface names the formula that has no zero
    // and the range it looked over. The generic sentence below would answer
    // that case with "its own guards rejected the data - most often too few
    // rows", which is not what happened and sends the reader off to look at
    // their rows. A wrong explanation is worse than the vague one it replaced,
    // because it is actionable and the action is useless.
    if(!anyPoints&&!prepared.title.isEmpty()&&prepared.title!=chosen.title)
        return prepared.title;

    if(!anyPoints)
        return QStringLiteral(
            "%1 produced no points from the mapped columns. Its own guards "
            "rejected the data - most often too few rows, or values it cannot "
            "use such as zero or negative numbers on a log axis.").arg(e);

    // DEGENERATE, which is not the same as empty and looks identical.
    //
    // A plot can draw every one of its points and still show nothing, because
    // they all landed in the same few pixels. That is what a 200,000-row
    // parameter sweep did: its x column was a five-value solver flag with one
    // stray outlier, so the axis stretched across 200,000 while every point sat
    // at one end - and 97% of the y column was within 1% of zero, so they sat
    // on the bottom axis too. The line WAS drawn, exactly on the gridline, and
    // the only honest reading of the picture was that the application was
    // broken.
    //
    // Measured on the data, not on the pixels: the renderer must not have to
    // look at its own output, and the answer has to be the same in a PDF at any
    // size as it is on screen.
    const auto spread=[](bool useX,const PlotSpec& p,QString& why){
        double lo=std::numeric_limits<double>::infinity(),hi=-lo;
        int finite=0;
        QSet<double> levels;
        for(const PlotSeries& s:p.series){
            const QVector<double>& v=useX?s.x:s.y;
            for(double d:v){
                if(!std::isfinite(d)) continue;
                ++finite;
                lo=qMin(lo,d); hi=qMax(hi,d);
                if(levels.size()<8) levels.insert(d);
            }
        }
        if(finite<8||!(hi>lo)) return false;
        // How much of the span the middle 98% of the values actually occupy.
        // One outlier can own the axis while everything else shares a pixel.
        QVector<double> all;
        all.reserve(finite);
        for(const PlotSeries& s:p.series){
            const QVector<double>& v=useX?s.x:s.y;
            for(double d:v) if(std::isfinite(d)) all.append(d);
        }
        std::sort(all.begin(),all.end());
        const double p01=all.at(int(all.size()*0.01));
        const double p99=all.at(qMin(int(all.size())-1,int(all.size()*0.99)));
        const double span=hi-lo;
        const double used=p99-p01;
        const QString axis=useX?QStringLiteral("x"):QStringLiteral("y");
        if(levels.size()<=5&&int(all.size())>200){
            why=QStringLiteral(
                "The %1 column has only %2 distinct values across %3 points, so every "
                "point is stacked onto %2 positions. It is probably a flag or a "
                "category rather than an axis - choose a measured column, or an "
                "engine that expects categories.")
                .arg(axis).arg(levels.size()).arg(all.size());
            return true;
        }
        if(used<span*0.02){
            why=QStringLiteral(
                // A single %, not %%: QString::arg is not printf and does not
                // collapse a doubled one, so "%%" reaches the user verbatim.
                // Safe because neither % here is followed by a digit.
                "99% of the %1 values lie within %2% of the axis range - one or "
                "two outliers are holding the axis open and everything else is drawn "
                "into a couple of pixels. Try a log %1 axis, or zoom, or plot the "
                "distribution instead.")
                .arg(axis).arg(100.0*used/span,0,'f',2);
            return true;
        }
        return false;
    };
    QString why;
    if(spread(true,prepared,why))  return why;
    if(spread(false,prepared,why)) return why;

    return QString();
}

// The key to a colour-mapped figure.
//
// Every field engine in this catalogue - heat maps, contours, vector fields,
// the Voronoi cells - paints a quantity as colour, and until now not one of
// them said what the colours meant. A field plot without a scale is not a
// weaker figure than one with a scale; it is a picture from which the quantity
// cannot be recovered at all, which is the whole reason the quantity was
// plotted. Reported from the built application, and correctly.
//
// Drawn by the backend rather than by the interface, so it is in the PDF and
// the SVG as well as on the screen - an exported figure that has lost its scale
// is exactly as unreadable as one that never had it.
// The same key, for a ramp that is not linear in the value.
//
// What this exists to prevent is stated in graphvis-choropleth-scale-decision.md:
// quantile classing without a key that can express it produces a figure whose
// colours no longer mean what the key says they mean, which is the exact
// failure the colour bar was written to stop. So the key learned first, and
// this is that.
//
// Two shapes:
//
//   * CONTINUOUS with a transform. The ramp is unchanged - it is still the
//     colour map top to bottom - and the TICKS move, because on a log scale
//     the middle of the bar is the geometric mean and not the arithmetic one.
//   * DISCRETE classes. The bar becomes N blocks of equal height, each the
//     single colour its class is painted in, with the class edges written
//     between them. That is what a cartographic key looks like, and it is the
//     only honest drawing of classing: a gradient would say the colour varies
//     within a class when it does not.
//
// Every position comes from scalePosition, the same function the engine
// coloured with. That is deliberate and it is the whole design - see the note
// on it in PlotSpec.h.
PlotStyle QtPlotBackend::scaledStyle(const PlotSpec& spec,const QVector<double>& values){
    PlotStyle out=spec.style;
    if(out.colourScaleKind!=ColourScale::Quantile) return out;
    QVector<double> usable;
    usable.reserve(values.size());
    for(double v:values) if(finite(v)) usable.append(v);
    out.colourBreaks=quantileBreaks(usable,out.colourClasses);
    // quantileBreaks returns nothing when the values cannot be divided - fewer
    // values than classes, or ties that would make two edges equal. Falling
    // back to the continuous ramp is the choropleth's decision, made for the
    // same reason: a key with a zero-width class on it explains nothing, and
    // silently drawing fewer classes than were asked for would be worse.
    if(out.colourBreaks.isEmpty()) out.colourScaleKind=ColourScale::Linear;
    return out;
}

void QtPlotBackend::drawColourBar(QPainter* p,const Frame& f,const PlotSpec& spec,
                                  const ColourScale& scale,const QString& caption) const {
    if(!scale.discrete()){
        // A continuous scale with a linear transform IS the old bar, so it goes
        // to the old code rather than to a second implementation of it.
        if(scale.kind==ColourScale::Linear){
            drawColourBar(p,f,spec,scale.lo,scale.hi,caption);
            return;
        }
    }
    if(!finite(scale.lo)||!finite(scale.hi)) return;

    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    const double barW=qMax(8.0,fm.height()*0.85);
    const QRectF bar(f.plotArea.right()+fm.height()*0.9,f.plotArea.top(),
                     barW,f.plotArea.height());
    if(bar.right()>lastTarget_.right()-2.0&&lastTarget_.isValid()) return;

    p->save();
    p->setClipping(false);
    p->setFont(tickFont);
    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);

    // Where a label goes, given a position 0..1 up the bar.
    const auto labelAt=[&](double t,const QString& text){
        const double y=bar.bottom()-qBound(0.0,t,1.0)*bar.height();
        p->drawLine(QPointF(bar.right(),y),QPointF(bar.right()+3.0,y));
        p->drawText(QRectF(bar.right()+5.0,y-fm.height()*0.5,
                           fm.horizontalAdvance(QStringLiteral("-0.00e+00"))+4.0,
                           fm.height()),
                    Qt::AlignLeft|Qt::AlignVCenter,text);
    };

    if(scale.discrete()){
        const int n=scale.classes();
        for(int i=0;i<n;++i){
            const QRectF block(bar.left(),
                               bar.bottom()-double(i+1)*bar.height()/double(n),
                               bar.width(),bar.height()/double(n)+0.5);
            // The class's own colour, asked for with a value INSIDE the class
            // rather than computed here - so a change to how a class picks its
            // colour moves the fill and the key together.
            const double mid=0.5*(scale.breaks[i]+scale.breaks[i+1]);
            p->fillRect(block,colourMapStyled(cmap,spec.style,scalePosition(mid,scale)));
        }
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(spec.style.foreground,0.9));
        p->drawRect(bar);
        for(int i=0;i<n;++i)
            p->drawLine(QPointF(bar.left(),bar.bottom()-double(i)*bar.height()/double(n)),
                        QPointF(bar.right(),bar.bottom()-double(i)*bar.height()/double(n)));
        // EVERY EDGE LABELLED, not every class. A class has two edges and the
        // reader needs both to know what falls in it; labelling the middle
        // would give a number no region necessarily has.
        const double classStep=(scale.breaks.last()-scale.breaks.first())/double(n);
        for(int i=0;i<=n;++i)
            labelAt(double(i)/double(n),formatTick(scale.breaks[i],classStep));
    }else{
        const int rows=qMax(2,int(bar.height()));
        for(int i=0;i<rows;++i){
            const double t=1.0-double(i)/double(rows-1);
            p->fillRect(QRectF(bar.left(),bar.top()+double(i)*bar.height()/double(rows),
                               bar.width(),bar.height()/double(rows)+0.6),
                        colourMapStyled(cmap,spec.style,t));
        }
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(spec.style.foreground,0.9));
        p->drawRect(bar);
        // A DECADE AT A TIME on a log ramp, which is what a log key is read in.
        // Placed through scalePosition, so a tick sits where a value of that
        // size is actually coloured.
        const double a=std::log10(scale.lo),b=std::log10(scale.hi);
        const int first=int(std::ceil(a)),last=int(std::floor(b));
        int drawn=0;
        for(int e=first;e<=last&&drawn<8;++e,++drawn){
            const double v=std::pow(10.0,double(e));
            labelAt(scalePosition(v,scale),formatTick(v,v));
        }
        // A range inside one decade has no decade tick in it at all, and a bar
        // with no numbers on it is the thing this whole function exists to
        // prevent. The ends always carry theirs.
        if(drawn<2){
            const double ends=(scale.hi-scale.lo)/4.0;
            labelAt(0.0,formatTick(scale.lo,ends));
            labelAt(1.0,formatTick(scale.hi,ends));
        }
    }

    if(!caption.isEmpty()){
        p->save();
        p->translate(bar.right()+5.0
                     +fm.horizontalAdvance(QStringLiteral("-0.00e+00"))+8.0
                     +fm.height(),
                     bar.center().y());
        p->rotate(-90.0);
        p->drawText(QRectF(-bar.height()*0.5,-fm.height()*0.5,
                           bar.height(),fm.height()),
                    Qt::AlignCenter,caption);
        p->restore();
    }
    p->restore();
}

void QtPlotBackend::drawColourBar(QPainter* p,const Frame& f,const PlotSpec& spec,
                                  double lo,double hi,const QString& caption) const {
    if(!finite(lo)||!finite(hi)) return;
    // The key says what the COLOURS mean, so when the person has capped the
    // ramp the key has to read the cap. Every caller passes what the data
    // spanned; the numbers printed beside the bar are the range those colours
    // actually cover, which is the only thing that makes a capped figure
    // readable rather than merely differently coloured.
    {
        const double cl=isUnset(spec.style.colourMin)?lo:spec.style.colourMin;
        const double ch=isUnset(spec.style.colourMax)?hi:spec.style.colourMax;
        if(finite(cl)&&finite(ch)&&ch>cl){ lo=cl; hi=ch; }
    }
    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    const double barW=qMax(8.0,fm.height()*0.85);
    const QRectF bar(f.plotArea.right()+fm.height()*0.9,f.plotArea.top(),
                     barW,f.plotArea.height());
    if(bar.right()>lastTarget_.right()-2.0&&lastTarget_.isValid()) return;

    p->save();
    // The field painters all run inside a clip to the plot area - which is
    // right, since a cell that overhangs the frame would be data drawn outside
    // its own axes. The bar lives OUTSIDE that frame by construction, so the
    // first version of it was drawn correctly and then clipped away entirely:
    // margin reserved, nothing in it. Clipping is turned off for the bar alone
    // and restored with the rest of the painter state.
    p->setClipping(false);
    p->setFont(tickFont);
    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);
    // Painted a row of device pixels at a time rather than with a QGradient:
    // the map is a table of sixty-four stops with its own interpolation, and a
    // linear gradient between two endpoint colours would be a DIFFERENT ramp
    // that happened to start and end in the same place. On viridis the middle
    // would come out grey-brown instead of teal.
    const int rows=qMax(2,int(bar.height()));
    for(int i=0;i<rows;++i){
        const double t=1.0-double(i)/double(rows-1);
        p->fillRect(QRectF(bar.left(),bar.top()+double(i)*bar.height()/double(rows),
                           bar.width(),bar.height()/double(rows)+0.6),
                    colourMapStyled(cmap,spec.style,t));
    }
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(spec.style.foreground,0.9));
    p->drawRect(bar);

    // Ticks on the same generator the axes use, so the numbers on the bar are
    // rounded the same way as the numbers on the frame.
    const QVector<AxisTick> ticks=linearTicks(qMin(lo,hi),qMax(lo,hi),5);
    const double span=hi-lo;
    for(const AxisTick& tick:ticks){
        if(tick.minor) continue;
        if(tick.value<qMin(lo,hi)-1e-12||tick.value>qMax(lo,hi)+1e-12) continue;
        const double frac=(std::abs(span)>1e-300)?(tick.value-lo)/span:0.5;
        const double y=bar.bottom()-frac*bar.height();
        p->drawLine(QPointF(bar.right(),y),QPointF(bar.right()+3.0,y));
        p->drawText(QRectF(bar.right()+5.0,y-fm.height()*0.5,
                           fm.horizontalAdvance(QStringLiteral("-0.00e+00"))+4.0,
                           fm.height()),
                    Qt::AlignLeft|Qt::AlignVCenter,tick.label);
    }
    // The caption runs up the bar, because across the top there is no room for
    // it without either clipping it or stealing width from the plot.
    if(!caption.isEmpty()){
        p->save();
        p->translate(bar.right()+5.0
                     +fm.horizontalAdvance(QStringLiteral("-0.00e+00"))+8.0
                     +fm.height(),
                     bar.center().y());
        p->rotate(-90.0);
        p->drawText(QRectF(-bar.height()*0.5,-fm.height()*0.5,
                           bar.height(),fm.height()),
                    Qt::AlignCenter,caption);
        p->restore();
    }
    p->restore();
}

// Hexagonal binning, for the engine named after it.
//
// "Hexbin Density" shared the 2-D histogram's branch and its painter, so it
// counted points into SQUARE cells and drew them as rectangles. The two
// engines rendered the same picture, and the sweep's same-picture check found
// them in one group.
//
// That is not a cosmetic difference. Hexagonal binning exists because a
// hexagonal lattice has a more uniform nearest-neighbour distance than a
// square one - every neighbour is the same distance away, where a square cell
// has neighbours at 1 and at sqrt(2) - so a hexagonal count is less biased by
// the orientation of the grid. Offering an engine called "Hexbin Density" that
// performs square binning names a method the program does not implement.
//
// Binned in DEVICE space rather than data space, deliberately: a hexagon is
// defined by equal distances, and equal distances in data space are not equal
// on screen unless the two axes happen to share a scale. Binning where the
// picture is drawn is what makes the cells actually regular hexagons.
void QtPlotBackend::drawHexbin(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    if(spec.series.size()<2) return;
    const QVector<double>& xs=spec.series.at(0).y;
    const QVector<double>& ys=spec.series.at(1).y;
    const int n=qMin(xs.size(),ys.size());
    if(n<1) return;

    // Pointy-top hexagons. For circumradius r the centres sit on a lattice of
    // horizontal pitch w = sqrt(3) r and vertical pitch 1.5 r, with alternate
    // rows offset by half a width.
    const double columns=28.0;                       // across the plot area
    const double r=qMax(2.0,f.plotArea.width()/(columns*std::sqrt(3.0)));
    const double w=std::sqrt(3.0)*r;
    const double vpitch=1.5*r;

    // Nearest of the two candidate centres. Rounding row then column lands in
    // the bounding box of a hexagon rather than the hexagon itself near the
    // slanted edges, so both candidate rows are tested and the closer centre
    // wins - which is the hexagon the point is actually inside.
    auto centreOf=[&](int row,int col){
        const double cx=f.plotArea.left()+(col+((row&1)?0.5:0.0))*w;
        const double cy=f.plotArea.top()+row*vpitch;
        return QPointF(cx,cy);
    };

    QHash<qint64,int> counts;
    auto key=[](int row,int col){
        return (qint64(row)<<32)^qint64(quint32(col));
    };
    for(int i=0;i<n;++i){
        if(!finite(xs[i])||!finite(ys[i])) continue;
        const QPointF q=toDevice(f,xs[i],ys[i]);
        if(!f.plotArea.contains(q)) continue;
        const double ry=(q.y()-f.plotArea.top())/vpitch;
        int bestRow=0,bestCol=0;
        double bestDist=std::numeric_limits<double>::infinity();
        for(int dr=0;dr<2;++dr){
            const int row=int(std::floor(ry))+dr;
            const double offset=(row&1)?0.5:0.0;
            const int col=int(std::round((q.x()-f.plotArea.left())/w-offset));
            const QPointF c=centreOf(row,col);
            const double d=(c.x()-q.x())*(c.x()-q.x())+(c.y()-q.y())*(c.y()-q.y());
            if(d<bestDist){ bestDist=d; bestRow=row; bestCol=col; }
        }
        ++counts[key(bestRow,bestCol)];
    }
    if(counts.isEmpty()) return;

    int lo=std::numeric_limits<int>::max(), hi=0;
    for(const int c:counts){ lo=qMin(lo,c); hi=qMax(hi,c); }

    QPolygonF hexagon;
    for(int k=0;k<6;++k){
        const double a=(double(k)*60.0-90.0)*3.14159265358979323846/180.0;
        hexagon << QPointF(r*std::cos(a),r*std::sin(a));
    }

    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);
    p->save();
    p->setClipRect(f.plotArea);
    p->setPen(Qt::NoPen);
    // IN A FIXED ORDER. A QHash iterates in an order that depends on its
    // hash seed, and Qt randomises that seed per process - so the cells were
    // painted in a different order on every run. Neighbouring hexagons share
    // an edge, and with antialiasing the shared pixels belong to whichever was
    // drawn last: the same data rendered twice gave two different PNGs. It
    // showed up as the only figure in a 434-figure gallery whose checksum
    // changed between two runs of the same binary.
    //
    // A figure that is not reproducible cannot be compared with itself, which
    // is most of what the sweep does, and a person re-exporting a figure for a
    // paper should get the file they had before.
    QVector<qint64> cells;
    cells.reserve(counts.size());
    for(auto it=counts.constBegin();it!=counts.constEnd();++it) cells.append(it.key());
    std::sort(cells.begin(),cells.end());
    // The counts are the distribution a quantile scale classes.
    QVector<double> cellCounts;
    cellCounts.reserve(cells.size());
    for(const qint64 cell:std::as_const(cells))
        cellCounts.append(double(counts.value(cell)));
    const PlotStyle binStyle=scaledStyle(spec,cellCounts);
    for(const qint64 cell:std::as_const(cells)){
        const int row=int(cell>>32);
        const int col=int(qint32(quint32(cell&0xffffffff)));
        const double t=rampPosition(binStyle,double(counts.value(cell)),
                                    double(lo),double(hi));
        p->setBrush(colourMapStyled(cmap,spec.style,t));
        // Translated once into a local polygon rather than moving the painter
        // and moving it back: undoing a translate every cell accumulates
        // floating-point error across several hundred of them.
        p->drawPolygon(hexagon.translated(centreOf(row,col)));
    }
    p->restore();
    ColourScale binScale;
    binScale.kind=binStyle.colourScaleKind;
    binScale.lo=double(lo); binScale.hi=double(hi);
    binScale.breaks=binStyle.colourBreaks;
    drawColourBar(p,f,spec,binScale,QStringLiteral("count"));
}

void QtPlotBackend::drawHeatmap(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    // Hexagonal binning is a different method, not a different colour: see
    // drawHexbin above.
    if(spec.engine==QLatin1String("Hexbin Density")){ drawHexbin(p,f,spec); return; }
    // The field colour map, read once. Viridis unless the style asks for
    // another - see PlotStyle::colourMap.
    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);
    const bool density=spec.engine==QLatin1String("2D Histogram");
    const ValueGrid& g=cachedGrid(spec,density?GridAggregate::Count:GridAggregate::Mean,0,0);
    if(!g.valid) return;
    // Cells are placed THROUGH THE AXES, not stretched across the plot area.
    //
    // This used to divide the plot area by the cell count and draw from the
    // left edge, which silently assumes the grid spans exactly the range the
    // axes show. It does not: `gridFromSeries` computes its own bounds from
    // the data (g.xLo..g.xHi), while the frame's range comes from
    // `computeRange`, which reads every series' `.x` and `.y`. For a
    // COLUMN-SHAPED engine the x data lives in series[0].y and `.x` holds
    // something else entirely, so the two ranges differ - and the cells were
    // then stretched to fill an axis that described different numbers.
    //
    // The effect was invisible because it was self-consistent: the picture
    // filled the frame and looked right. It surfaced only when Hexbin Density
    // got its own painter, which maps through toDevice, and the two engines
    // disagreed about where the same data was. The hexagons were in the right
    // place; the rectangles were not.
    //
    // Mapping each cell's own data coordinates through toDevice makes the
    // cells land where the axes say they do, whatever the frame's range turns
    // out to be. If the axis is wider than the data the figure now shows that
    // honestly, with empty space, rather than hiding it by stretching.
    const double cellDataW=(g.xHi-g.xLo)/double(qMax(1,g.nx));
    const double cellDataH=(g.yHi-g.yLo)/double(qMax(1,g.ny));
    p->save();
    p->setClipRect(f.plotArea);
    p->setPen(Qt::NoPen);
    // The cells ARE the distribution, so a quantile scale can be built here and
    // nowhere earlier. Linear and log need none of this and pass straight
    // through.
    const PlotStyle cellStyle=scaledStyle(spec,g.cells);
    for(int cy=0;cy<g.ny;++cy){
        for(int cx=0;cx<g.nx;++cx){
            const double v=g.cells[cy*g.nx+cx];
            if(!finite(v)) continue;     // leave the background showing through
            const double t=rampPosition(cellStyle,v,g.vLo,g.vHi);
            p->setBrush(colourMapStyled(cmap,spec.style,t));
            const QPointF lowerLeft=toDevice(f,g.xLo+cx*cellDataW,
                                               g.yLo+cy*cellDataH);
            const QPointF upperRight=toDevice(f,g.xLo+(cx+1)*cellDataW,
                                                g.yLo+(cy+1)*cellDataH);
            // Half a pixel of overlap, or antialiasing leaves seams between
            // cells that read as a grid pattern in the data.
            p->drawRect(QRectF(lowerLeft.x(),upperRight.y(),
                               upperRight.x()-lowerLeft.x()+0.5,
                               lowerLeft.y()-upperRight.y()+0.5));
        }
    }
    p->restore();
    // THE KEY IS DRAWN FROM THE SAME SCALE THE CELLS WERE, which is the whole
    // reason the scale is a value: a key that disagrees with the figure is
    // indistinguishable, on the page, from one that agrees with it.
    ColourScale cellScale;
    cellScale.kind=cellStyle.colourScaleKind;
    cellScale.lo=g.vLo; cellScale.hi=g.vHi;
    cellScale.breaks=cellStyle.colourBreaks;
    drawColourBar(p,f,spec,cellScale,
                  density?QStringLiteral("count")
                         :(spec.series.size()>=3?spec.series.at(2).label:QString()));
}

void QtPlotBackend::drawContour(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    // The field colour map, read once. Viridis unless the style asks for
    // another - see PlotStyle::colourMap.
    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);
    // Auto resolution, same as the heatmap: a grid fine enough to show
    // structure and coarse enough that its cells actually contain samples.
    const ValueGrid& g=cachedGrid(spec,GridAggregate::Mean,3,0);
    if(!g.valid) return;

    // Marching squares, the two-segment cases only. Saddle cells are drawn as
    // both crossings, which is the standard resolution and cannot mislead at
    // the level a contour plot is read.
    //
    // Placed THROUGH THE AXES, for the reason set out at length in drawHeatmap:
    // the grid's bounds are computed by gridFromSeries from the data, the
    // frame's range by computeRange, and stretching one to fill the other draws
    // the contours against numbers they do not belong to the moment the two
    // differ - which is what an explicit axis limit is FOR. A cell index is
    // turned into the data coordinate it stands for and mapped like any other
    // point, so a limit clips the contours instead of squashing them.
    const double cellW=(g.xHi-g.xLo)/double(qMax(1,g.nx-1));
    const double cellH=(g.yHi-g.yLo)/double(qMax(1,g.ny-1));
    auto at=[&](int cx,int cy){ return g.cells[qBound(0,cy,g.ny-1)*g.nx+qBound(0,cx,g.nx-1)]; };
    auto pointFor=[&](double cx,double cy){
        return toDevice(f,g.xLo+cx*cellW,g.yLo+cy*cellH);
    };

    // An implicit plot is ONE curve, at zero. See the note in prepareSpec where
    // this is set: ten evenly spaced levels of f are ten curves, nine of which
    // answer a question nobody asked, and the tenth is at zero only by luck.
    const bool zeroOnly=spec.parameter(QStringLiteral("@zeroLevelOnly"),0.0)>0.5;

    constexpr int kLevels=10;
    p->save();
    p->setClipRect(f.plotArea);
    for(int level=1;level<=(zeroOnly?1:kLevels);++level){
        // For the single zero curve, the middle of the ramp: the colour is not
        // carrying a value here, so it should not read as the bottom or the
        // top of one.
        const double frac=zeroOnly?0.5:double(level)/double(kLevels+1);
        const double iso=zeroOnly?0.0:g.vLo+(g.vHi-g.vLo)*frac;
        QPen pen(colourMapStyled(cmap,spec.style,frac));
        pen.setWidthF(qMax(0.6,spec.style.lineWidth));
        p->setPen(pen);
        for(int cy=0;cy<g.ny-1;++cy){
            for(int cx=0;cx<g.nx-1;++cx){
                const double v00=at(cx,cy), v10=at(cx+1,cy), v11=at(cx+1,cy+1), v01=at(cx,cy+1);
                if(!finite(v00)||!finite(v10)||!finite(v11)||!finite(v01)) continue;
                // Corner values above the level, as a four-bit case index.
                const int code=(v00>iso?1:0)|(v10>iso?2:0)|(v11>iso?4:0)|(v01>iso?8:0);
                if(code==0||code==15) continue;
                auto lerp=[&](double va,double vb,double a,double b){
                    const double d=vb-va;
                    return std::abs(d)<1e-15? a : a+(b-a)*((iso-va)/d);
                };
                const QPointF bottom=pointFor(lerp(v00,v10,cx,cx+1),cy);
                const QPointF right =pointFor(cx+1,lerp(v10,v11,cy,cy+1));
                const QPointF top   =pointFor(lerp(v01,v11,cx,cx+1),cy+1);
                const QPointF left  =pointFor(cx,lerp(v00,v01,cy,cy+1));
                switch(code){
                case 1: case 14: p->drawLine(left,bottom); break;
                case 2: case 13: p->drawLine(bottom,right); break;
                case 3: case 12: p->drawLine(left,right); break;
                case 4: case 11: p->drawLine(right,top); break;
                case 6: case 9:  p->drawLine(bottom,top); break;
                case 7: case 8:  p->drawLine(left,top); break;
                case 5:  p->drawLine(left,bottom); p->drawLine(right,top); break;
                case 10: p->drawLine(left,top); p->drawLine(bottom,right); break;
                default: break;
                }
            }
        }
    }
    p->restore();
    // No bar for a single curve. A colour scale beside one line invites the
    // reader to look up what its colour means, and the answer is "nothing" -
    // the curve is at zero, which is the one value the bar would not show.
    if(!zeroOnly)
        drawColourBar(p,f,spec,g.vLo,g.vHi,
                      spec.series.size()>=3?spec.series.at(2).label:QString());
}

// A violin is the kernel density of a distribution, mirrored about its slot.
// The box plot inside it is what makes it readable as a summary rather than as
// a shape: the median and the quartiles are the numbers people quote.
void QtPlotBackend::drawViolin(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    const double slotW=qMin(f.plotArea.width()/qMax(1.0,double(spec.series.size()))*0.72,120.0);
    for(const PlotSeries& s:spec.series){
        // prepareSpec packs each violin as: x = slot, y = density profile, with
        // the profile's sample values carried in x beyond the first entry.
        if(s.x.size()<4||s.y.size()!=s.x.size()) continue;
        const double slot=s.x.first();
        double peak=0.0;
        for(int i=1;i<s.y.size();++i) peak=qMax(peak,s.y[i]);
        if(!(peak>0.0)) continue;

        QPainterPath body;
        bool started=false;
        for(int i=1;i<s.x.size();++i){
            const double halfWidth=(s.y[i]/peak)*slotW*0.5;
            const QPointF centre=toDevice(f,slot,s.x[i]);
            const QPointF right(centre.x()+halfWidth,centre.y());
            if(!started){ body.moveTo(right); started=true; } else body.lineTo(right);
        }
        for(int i=s.x.size()-1;i>=1;--i){
            const double halfWidth=(s.y[i]/peak)*slotW*0.5;
            const QPointF centre=toDevice(f,slot,s.x[i]);
            body.lineTo(QPointF(centre.x()-halfWidth,centre.y()));
        }
        body.closeSubpath();

        p->save();
        QColor fill=s.color; fill.setAlphaF(0.45);
        p->setBrush(fill);
        QPen edge(s.color); edge.setWidthF(qMax(0.6,s.lineWidth));
        p->setPen(edge);
        p->drawPath(body);
        p->restore();
    }
}

// A raincloud: the half violin (the cloud), the summary (the box), and the
// observations themselves (the rain).
//
// "Raincloud" used to dispatch to drawViolin, so the catalogue offered two
// entries and the program had one behaviour - the sweep's same-picture check
// put them in a group together, and the two figures were byte-identical below
// the title.
//
// The rain is not decoration. The form was invented because a violin shows a
// kernel density estimate and nothing else: how many observations it was fitted
// to, whether they are evenly spread or two clumps, whether a lobe is six
// points or six hundred - none of that survives the smoothing. Drawing the
// measurements beside the curve is the whole proposition, and a raincloud
// without them is a violin with a different name on it.
//
// Reads the packing prepareSpec writes: x[0] is the slot, then the density
// profile's positions, then a NaN, then the sorted observations. A spec with no
// NaN - one saved before this existed - has no rain and falls back to the
// symmetric violin, which is what it was drawn as when it was saved.
void QtPlotBackend::drawRaincloud(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    const double slotW=qMin(f.plotArea.width()/qMax(1.0,double(spec.series.size()))*0.72,120.0);
    for(const PlotSeries& s:spec.series){
        if(s.x.size()<4||s.y.size()!=s.x.size()) continue;
        const double slot=s.x.first();

        // The separator. profileEnd is one past the last density sample;
        // rainStart is the first observation, or size() when there are none.
        int split=1;
        while(split<s.x.size()&&finite(s.x[split])) ++split;
        const int profileEnd=split;
        const int rainStart=qMin(split+1,s.x.size());
        const bool hasRain=rainStart<s.x.size();

        double peak=0.0;
        for(int i=1;i<profileEnd;++i) peak=qMax(peak,s.y[i]);
        if(!(peak>0.0)||profileEnd<3) continue;

        const double base=toDevice(f,slot,s.x[1]).x();
        const double gap=hasRain?slotW*0.05:0.0;
        // Half the width when the other half of the slot is holding the rain,
        // so the two never overlap however dense the observations are.
        const double cloudW=hasRain?slotW*0.45:slotW*0.5;

        p->save();

        QPainterPath cloud;
        for(int i=1;i<profileEnd;++i){
            const double halfWidth=(s.y[i]/peak)*cloudW;
            const QPointF centre=toDevice(f,slot,s.x[i]);
            const QPointF outer(base+gap+halfWidth,centre.y());
            if(i==1) cloud.moveTo(outer); else cloud.lineTo(outer);
        }
        // Back down the flat side - or the mirrored side when there is no rain,
        // which is exactly the violin.
        for(int i=profileEnd-1;i>=1;--i){
            const double halfWidth=(s.y[i]/peak)*cloudW;
            const QPointF centre=toDevice(f,slot,s.x[i]);
            cloud.lineTo(QPointF(hasRain?base+gap:base-halfWidth,centre.y()));
        }
        cloud.closeSubpath();

        QColor fill=s.color; fill.setAlphaF(0.45);
        p->setBrush(fill);
        QPen edge(s.color); edge.setWidthF(qMax(0.6,s.lineWidth));
        p->setPen(edge);
        p->drawPath(cloud);

        if(hasRain){
            const int n=s.x.size()-rainStart;
            // Sorted by prepareSpec, so the quartiles are indices rather than
            // another sort on every repaint.
            const auto at=[&](int k){ return s.x[rainStart+qBound(0,k,n-1)]; };
            const double q1=at(n/4), median=at(n/2), q3=at((3*n)/4);

            // The box, on the slot line between the cloud and the rain.
            const double boxW=qMax(3.0,slotW*0.10);
            const QPointF top=toDevice(f,slot,q3), bottom=toDevice(f,slot,q1);
            p->setBrush(spec.style.background);
            p->drawRect(QRectF(base-boxW*0.5,qMin(top.y(),bottom.y()),
                               boxW,std::abs(bottom.y()-top.y())));
            const QPointF mid=toDevice(f,slot,median);
            QPen medianPen(s.color); medianPen.setWidthF(qMax(1.0,s.lineWidth*1.4));
            p->setPen(medianPen);
            p->drawLine(QPointF(base-boxW*0.5,mid.y()),QPointF(base+boxW*0.5,mid.y()));

            // The rain. Displaced by a hash of the index rather than by a
            // random number: a figure that redraws differently every time it is
            // repainted cannot be compared with the one printed in the paper,
            // and the sweep's same-picture check would see noise instead of a
            // duplicate. Same reason Swarm computes its offsets rather than
            // drawing them.
            //
            // A FULL mixing hash, not `index * constant % range`. That was the
            // first version and it printed visible diagonal stripes through the
            // rain: the observations are sorted, so height rises steadily with
            // the index, and a displacement that is near-linear in the index
            // rises with it - two correlated coordinates draw lines. The
            // stripes were not in the data, which is the worst kind of artefact
            // a plot can have. This is the lowbias32 finaliser, whose whole
            // purpose is that neighbouring inputs land nowhere near each other.
            const double jitterW=qMax(2.0,slotW*0.40);
            const double r=qMax(0.9,s.markerSize/3.0);
            QColor dot=s.color; dot.setAlphaF(0.55);
            p->setPen(Qt::NoPen);
            p->setBrush(dot);
            for(int q=0;q<n;++q){
                const double value=s.x[rainStart+q];
                if(!finite(value)) continue;
                if(f.yLog&&value<=0.0) continue;
                quint32 mixed=quint32(q);
                mixed^=mixed>>16; mixed*=0x7feb352du;
                mixed^=mixed>>15; mixed*=0x846ca68bu;
                mixed^=mixed>>16;
                const double u=double(mixed%1024u)/1024.0;
                const QPointF pt=toDevice(f,slot,value);
                p->drawEllipse(QPointF(base-gap-boxW*0.5-u*jitterW,pt.y()),r,r);
            }
        }
        p->restore();
    }
}

// A forest plot is one estimate per row with its confidence interval, and a
// rule at the null value. Horizontal because the rows are named studies.
void QtPlotBackend::drawForest(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    p->save();
    for(const PlotSeries& s:spec.series){
        // x = row position, y = {estimate, low, high}
        if(s.x.isEmpty()||s.y.size()<3) continue;
        const double row=s.x.first();
        const QPointF estimate=toDevice(f,s.y[0],row);
        const QPointF low=toDevice(f,s.y[1],row);
        const QPointF high=toDevice(f,s.y[2],row);

        QPen line(s.color);
        line.setWidthF(qMax(0.8,s.lineWidth));
        p->setPen(line);
        p->drawLine(low,high);
        // End caps, so a wide interval cannot be mistaken for a ruled line.
        p->drawLine(QPointF(low.x(),low.y()-3.5),QPointF(low.x(),low.y()+3.5));
        p->drawLine(QPointF(high.x(),high.y()-3.5),QPointF(high.x(),high.y()+3.5));

        p->setPen(Qt::NoPen);
        p->setBrush(s.color);
        const double box=qMax(3.0,s.markerSize);
        p->drawRect(QRectF(estimate.x()-box/2.0,estimate.y()-box/2.0,box,box));
    }
    p->restore();
}

// Polar plots get their own projection rather than an axis frame: the chrome is
// concentric rings and radial spokes, and the data is drawn in the same call
// because there is no rectangular plot area to hand to a normal draw function.
void QtPlotBackend::drawPolar(QPainter* p,const QRectF& target,const PlotSpec& spec,
                              bool markersOnly) const {
    const QPointF centre=target.center();
    const double radius=qMin(target.width(),target.height())*0.5-46.0;
    if(radius<20.0) return;

    // Polar Bubble reads a THIRD column as the size of each mark. Until it did,
    // it was Polar Scatter under another name - drawPolar drew both with marks
    // of the series' single scalar markerSize, and the two gallery files came
    // out byte-for-byte identical, 25,541 bytes each. A bubble chart whose
    // bubbles are all one size is not a bubble chart.
    const bool bubble=spec.engine==QLatin1String("Polar Bubble")
                     &&spec.series.size()>=2;

    // THE CONVENTION, applied in one place.
    //
    // Every angle below is a DATA angle in degrees; this turns it into the one
    // this painter draws in - anticlockwise from three o'clock - so the
    // difference between a polar scatter and a compass bearing lives here and
    // nowhere else. A second copy of this arithmetic in the wedge code or the
    // arrow code is how a figure ends up with its marks and its labels
    // disagreeing.
    const auto toScreenDegrees=[&spec](double dataDegrees){
        return (spec.style.polarConvention==1)
                   ? 90.0-dataDegrees      // a bearing: north up, clockwise
                   : dataDegrees;          // mathematical: east, anticlockwise
    };

    double rMax=0.0;
    for(int si=0;si<spec.series.size();++si){
        // Series 1 of a bubble is a SIZE, not a radius. Measuring it as one
        // would scale every ring to whichever of the two columns is larger.
        if(bubble&&si!=0) continue;
        for(double v:spec.series.at(si).y) if(finite(v)) rMax=qMax(rMax,std::abs(v));
    }
    if(!(rMax>0.0)) rMax=1.0;

    // ROUND NUMBERS ON THE RINGS.
    //
    // The rings were drawn at quarters of the radius and labelled with quarters
    // of the largest value, so a rose whose biggest petal is 1250 read
    // 312 / 625 / 937 / 1.25e+03 - three numbers nobody would have chosen and a
    // fourth in scientific notation, for a count of observations. Every other
    // axis in this backend rounds outward to a step a person would have picked;
    // this one did not, and `niceStep` was already sitting there.
    //
    // THE STEP DECIDES THE RINGS, rather than the values being back-computed
    // from a radius already fixed. Rounding the maximum outward is what the
    // sounding's temperature axis does for the same reason, and it has the same
    // second benefit here: the largest petal no longer runs into the outermost
    // circle.
    const double ringStep=niceStep(rMax/4.0);
    const int rings=qBound(1,int(std::ceil(rMax/ringStep-1e-9)),8);
    rMax=ringStep*double(rings);

    p->save();
    // Rings and spokes.
    QPen grid(spec.style.gridColor);
    grid.setWidthF(0.6);
    p->setPen(grid);
    p->setBrush(Qt::NoBrush);
    for(int ring=1;ring<=rings;++ring){
        const double r=radius*double(ring)/double(rings);
        p->drawEllipse(centre,r,r);
    }
    for(int spoke=0;spoke<12;++spoke){
        const double angle=spoke*(2.0*3.14159265358979323846/12.0);
        p->drawLine(centre,centre+QPointF(std::cos(angle)*radius,-std::sin(angle)*radius));
    }
    p->setPen(spec.style.foreground);
    p->setFont(font(spec,spec.style.tickSize));
    for(int ring=1;ring<=rings;++ring){
        const double r=radius*double(ring)/double(rings);
        // formatTick rather than 'g' with three significant figures, which is
        // what turned 1250 into "1.25e+03". It gives the decimals the step
        // actually needs and only reaches for an exponent past 1e5.
        p->drawText(QRectF(centre.x()+4,centre.y()-r-8,60,14),Qt::AlignLeft|Qt::AlignVCenter,
                    formatTick(ringStep*double(ring),ringStep));
    }

    // THE ANGLE, which nothing said.
    //
    // Twelve spokes were drawn and not one of them was labelled, so every polar
    // engine showed a direction nobody could read: a wind rose whose bearings
    // are its entire content, a compass with no compass on it, a stereonet with
    // no orientation. The rings carried numbers and the angles carried none.
    //
    // Degrees, at the spokes that are actually drawn. The NUMBER on each spoke
    // is the data angle - 0, 30, 60 - and WHERE that number is placed comes
    // from toScreenDegrees, the same function the wedges and the arrow go
    // through. That is the whole point of having one: the label and the mark
    // for a given bearing land on the same spoke whichever convention is set.
    // Placing these by the raw angle instead would leave 90 at the top of a
    // compass figure, describing a rose this painter is not drawing.
    //
    // UNLESS THE ANGLE IS NOT A DIRECTION. A radar chart's spokes are its
    // VARIABLES - efficiency, cost, weight - and it labelled them 0, 30, 60,
    // 90, which is the position of each variable expressed in degrees and
    // tells a reader nothing about which spoke is which. So an engine may name
    // its own spokes through the x axis' tick labels, the same mechanism a
    // confusion matrix and a box plot use, and only the engines that leave
    // them empty get degrees.
    {
        const QFontMetricsF afm(p->font(),p->device());
        const int named=qMin(spec.xAxis.tickValues.size(),spec.xAxis.tickLabels.size());
        const int count=(named>0)?named:12;
        for(int spoke=0;spoke<count;++spoke){
            const double degrees=(named>0)?spec.xAxis.tickValues.at(spoke)
                                          :spoke*30.0;
            const double angle=toScreenDegrees(degrees)*3.14159265358979323846/180.0;
            const QString text=(named>0)?spec.xAxis.tickLabels.at(spoke)
                                        :QString::number(int(degrees))+QStringLiteral("\u00b0");
            if(text.isEmpty()) continue;
            // Just outside the rim, centred on the spoke. The 46 px the radius
            // already gives back to the frame is what this sits in.
            const QPointF at=centre+QPointF(std::cos(angle)*(radius+14.0),
                                            -std::sin(angle)*(radius+14.0));
            const double w=afm.horizontalAdvance(text)+4.0;
            p->drawText(QRectF(at.x()-w/2.0,at.y()-afm.height()/2.0,w,afm.height()),
                        Qt::AlignCenter,text);
        }
    }

    // A binned distribution is drawn as wedges, not as points. Sixteen dots
    // around a circle is technically the data and visually nothing; the wedge
    // is what makes a rose readable, and its area is the quantity.
    const bool wedges=spec.engine==QLatin1String("Polar Histogram")
                    ||spec.engine==QLatin1String("Wind Rose");
    const bool spokes=spec.engine==QLatin1String("Compass");
    if(wedges||spokes){
        const int sectors=spec.series.isEmpty()?1:qMax(1,int(spec.series.first().x.size()));
        const double sweep=360.0/double(qMax(1,sectors));
        for(const PlotSeries& s:spec.series){
            const int n=qMin(s.x.size(),s.y.size());
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                const double r=std::abs(s.y[i])/rMax*radius;
                if(!(r>0.0)) continue;
                if(spokes){
                    const double angle=toScreenDegrees(s.x[i])*3.14159265358979323846/180.0;
                    QPen arrow(s.color);
                    arrow.setWidthF(qMax(1.0,s.lineWidth*1.3));
                    p->setPen(arrow); p->setBrush(Qt::NoBrush);
                    const QPointF tip=centre+QPointF(std::cos(angle)*r,-std::sin(angle)*r);
                    p->drawLine(centre,tip);
                    // A head, so direction is readable without following the
                    // line back to the origin.
                    const double head=qMin(9.0,r*0.28);
                    for(const double turn:{2.5,-2.5}){
                        const double a=angle+turn;
                        p->drawLine(tip,tip+QPointF(std::cos(a)*head,-std::sin(a)*head));
                    }
                }else{
                    QColor fill=s.color; fill.setAlphaF(0.72);
                    p->setBrush(fill);
                    QPen edge(s.color.darker(120)); edge.setWidthF(0.6);
                    p->setPen(edge);
                    QPainterPath wedge;
                    wedge.moveTo(centre);
                    // Qt measures arcs in sixteenths of a degree, anticlockwise.
                    wedge.arcTo(QRectF(centre.x()-r,centre.y()-r,2*r,2*r),
                                toScreenDegrees(s.x[i])-sweep/2.0,sweep);
                    wedge.closeSubpath();
                    p->drawPath(wedge);
                }
            }
        }
        drawFloatingTitle(p,target,spec);
        p->restore();
        return;
    }

    if(bubble){
        constexpr double kPi=3.14159265358979323846;
        const PlotSeries& s=spec.series.at(0);
        const QVector<double>& sizes=spec.series.at(1).y;
        const int n=qMin(qMin(s.x.size(),s.y.size()),sizes.size());
        double lo=std::numeric_limits<double>::infinity(),hi=-lo;
        for(int i=0;i<n;++i)
            if(finite(sizes[i])){ lo=qMin(lo,sizes[i]); hi=qMax(hi,sizes[i]); }

        p->setPen(Qt::NoPen);
        QColor fill=s.color; fill.setAlphaF(0.55);
        p->setBrush(fill);
        // AREA proportional to the value, not diameter. A bubble is read by
        // how much ink it is, so scaling the diameter linearly makes a value
        // four times larger look sixteen times larger - the single most common
        // way a bubble chart misreports its own data.
        constexpr double kAreaMin=9.0, kAreaMax=200.0;
        for(int i=0;i<n;++i){
            if(!finite(s.x[i])||!finite(s.y[i])) continue;
            const double angle=toScreenDegrees(s.x[i])*kPi/180.0;
            const double r=std::abs(s.y[i])/rMax*radius;
            const double t=(finite(sizes[i])&&hi>lo)?(sizes[i]-lo)/(hi-lo):0.5;
            const double mark=std::sqrt((kAreaMin+t*(kAreaMax-kAreaMin))/kPi);
            p->drawEllipse(centre+QPointF(std::cos(angle)*r,-std::sin(angle)*r),
                           mark,mark);
        }
        drawFloatingTitle(p,target,spec);
        p->restore();
        return;
    }

    for(const PlotSeries& s:spec.series){
        const int n=qMin(s.x.size(),s.y.size());
        if(n<1) continue;
        QPainterPath path;
        bool started=false;
        p->setPen(Qt::NoPen);
        p->setBrush(s.color);
        for(int i=0;i<n;++i){
            if(!finite(s.x[i])||!finite(s.y[i])) continue;
            // x is the angle in degrees, y the radius. Degrees because that is
            // what instruments report and what people type.
            const double angle=toScreenDegrees(s.x[i])*3.14159265358979323846/180.0;
            const double r=std::abs(s.y[i])/rMax*radius;
            const QPointF pt=centre+QPointF(std::cos(angle)*r,-std::sin(angle)*r);
            if(markersOnly){
                p->drawEllipse(pt,qMax(1.5,s.markerSize/2.0),qMax(1.5,s.markerSize/2.0));
            }else{
                if(!started){ path.moveTo(pt); started=true; } else path.lineTo(pt);
            }
        }
        if(!markersOnly&&started){
            QPen pen(s.color);
            pen.setWidthF(qMax(0.8,s.lineWidth));
            if(!s.dashPattern.isEmpty()) pen.setDashPattern(s.dashPattern);
            p->setPen(pen);
            p->setBrush(Qt::NoBrush);
            p->drawPath(path);
        }
    }
    drawFloatingTitle(p,target,spec);
    p->restore();
}

// ---------------------------------------------------------------------------
// 3-D engines, drawn by orthographic projection.
//
// These could have been left to the VTK viewport, and for a rotatable, lit,
// million-triangle surface they should be. But a catalogue entry that only
// works when an optional plugin is loaded is a catalogue entry that usually
// does not work, and a projected wireframe is what a paper figure needs anyway:
// it exports as vectors, it prints, and it does not depend on a GPU.
//
// Orthographic rather than perspective, because a perspective 3-D plot makes
// equal intervals look unequal, and the whole point of the axes is that they
// can be read.

// The unit bounding cube, drawn faintly first so a projection reads as a
// projection. Both 3-D entry points - draw3D and the vector/volume family -
// opened with the same twelve edges, the same 0.6 pen and the same corner
// table; the two copies had already drifted apart in whether the loop body
// carried braces.
void drawBoundingCube(QPainter* p,const Projection& proj,const QColor& gridColor){
    QPen box(gridColor);
    box.setWidthF(0.6);
    p->setPen(box);
    const double c[8][3]={{-0.5,-0.5,-0.5},{0.5,-0.5,-0.5},{0.5,0.5,-0.5},{-0.5,0.5,-0.5},
                          {-0.5,-0.5, 0.5},{0.5,-0.5, 0.5},{0.5,0.5, 0.5},{-0.5,0.5, 0.5}};
    const int edges[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for(const auto& e:edges)
        p->drawLine(project(proj,c[e[0]][0],c[e[0]][1],c[e[0]][2]),
                    project(proj,c[e[1]][0],c[e[1]][1],c[e[1]][2]));
}

// How much room the numbers and names outside the cube need, measured rather
// than assumed - a 6 pt tick font on a thumbnail and a 12 pt one on an exported
// A4 figure want very different margins, and a fixed number is wrong for one of
// them. Two lines of text plus a gap on each side.
double cubeMargin(QPainter* p,const QFont& tickFont,const QFont& nameFont,
                  bool scaleLabels){
    const QFontMetricsF tick(tickFont,p->device());
    const QFontMetricsF name(nameFont,p->device());
    const double lines=(scaleLabels?tick.height():0.0)+name.height();
    // Widthwise a number is the wider of the two, and five digits is the most a
    // round tick value normally takes.
    return qMax(18.0,lines+10.0+(scaleLabels
                                     ? tick.horizontalAdvance(QStringLiteral("00000"))*0.5
                                     : 0.0));
}

// The three axes of the cube: a tick and a number at each round value, and the
// column's name beyond them.
//
// The 3-D engines had names on their axes and no numbers at all, so a surface
// could be looked at but not read - you could see that the peak was on the
// right without being able to say what it was on the right OF. Every other
// engine in this catalogue has numbered axes; these were the exception because
// nobody had written this function.
//
// Which edge carries which axis is decided from the projection rather than
// fixed, because the figure can now be turned. The horizontal axes go on
// whichever bottom edge is nearest the viewer - the one that projects lowest -
// so the numbers are never written across the middle of the surface, and the
// vertical axis goes on whichever upright is leftmost.
void drawCubeAxes(QPainter* p,const Projection& proj,const PlotSpec& spec,
                  const Bounds& bx,const Bounds& by,const Bounds& bz,
                  const QString& xLabel,const QString& yLabel,const QString& zLabel,
                  const QFont& tickFont,const QFont& nameFont){
    const QPointF centre=project(proj,0,0,0);

    // One axis: a segment in cube space, the data bounds along it, and the
    // direction to push text away from the cube.
    struct Edge { double from[3]; double to[3]; int varies; };
    const auto midOf=[&](const Edge& e){
        return project(proj,(e.from[0]+e.to[0])/2.0,(e.from[1]+e.to[1])/2.0,
                            (e.from[2]+e.to[2])/2.0);
    };

    // X runs along the two bottom edges at z = -0.5 that vary in x; pick the
    // front one. Same for Y. Z is one of the four uprights; pick the leftmost.
    Edge ex{{-0.5,-0.5,-0.5},{0.5,-0.5,-0.5},0};
    { const Edge other{{-0.5,0.5,-0.5},{0.5,0.5,-0.5},0};
      if(midOf(other).y()>midOf(ex).y()) ex=other; }
    Edge ey{{0.5,-0.5,-0.5},{0.5,0.5,-0.5},1};
    { const Edge other{{-0.5,-0.5,-0.5},{-0.5,0.5,-0.5},1};
      if(midOf(other).y()>midOf(ey).y()) ey=other; }
    Edge ez{{-0.5,-0.5,-0.5},{-0.5,-0.5,0.5},2};
    for(const Edge cand:{Edge{{0.5,-0.5,-0.5},{0.5,-0.5,0.5},2},
                         Edge{{0.5,0.5,-0.5},{0.5,0.5,0.5},2},
                         Edge{{-0.5,0.5,-0.5},{-0.5,0.5,0.5},2}})
        if(midOf(cand).x()<midOf(ez).x()) ez=cand;

    const QFontMetricsF tickMetrics(tickFont,p->device());
    const QFontMetricsF nameMetrics(nameFont,p->device());

    struct Axis { const Edge* edge; const Bounds* bounds; QString name; };
    const Axis axes[3]={{&ex,&bx,xLabel},{&ey,&by,yLabel},{&ez,&bz,zLabel}};

    for(const Axis& axis:axes){
        const Edge& e=*axis.edge;
        const QPointF mid=midOf(e);
        // Away from the middle of the cube, so numbers sit outside the box
        // whichever way it has been turned.
        QPointF away=mid-centre;
        const double len=std::hypot(away.x(),away.y());
        away=(len>1e-6)?away/len:QPointF(0,1);

        double outermost=0.0;
        if(spec.style.scaleLabelsVisible&&axis.bounds->valid){
            p->setFont(tickFont);
            QPen tickPen(spec.style.gridColor);
            tickPen.setWidthF(0.8);
            for(const AxisTick& t:QtPlotBackend::linearTicks(axis.bounds->lo,
                                                             axis.bounds->hi,5)){
                if(t.minor) continue;
                const double f=axis.bounds->norm(t.value)+0.5;   // 0..1 along the edge
                if(!(f>=-0.001&&f<=1.001)) continue;
                double at[3];
                for(int k=0;k<3;++k) at[k]=e.from[k]+(e.to[k]-e.from[k])*f;
                const QPointF on=project(proj,at[0],at[1],at[2]);
                p->setPen(tickPen);
                p->drawLine(on,on+away*4.0);
                // A box centred on the text position rather than a baseline, so
                // a number is pushed clear of the cube by its own size in
                // whatever direction "away" points - the reason this is not
                // just a fixed offset is that the direction changes as the
                // figure turns.
                const double w=qMax(18.0,tickMetrics.horizontalAdvance(t.label)+4.0);
                const double h=tickMetrics.height();
                const QPointF anchor=on+away*(7.0+std::abs(away.x())*w*0.5
                                                 +std::abs(away.y())*h*0.5);
                p->setPen(spec.style.foreground);
                p->drawText(QRectF(anchor-QPointF(w/2.0,h/2.0),QSizeF(w,h)),
                            Qt::AlignCenter,t.label);
                outermost=qMax(outermost,7.0+std::abs(away.x())*w+std::abs(away.y())*h);
            }
        }

        if(axis.name.isEmpty()) continue;
        p->setFont(nameFont);
        p->setPen(spec.style.foreground);
        const double w=qMax(40.0,nameMetrics.horizontalAdvance(axis.name)+6.0);
        const double h=nameMetrics.height();
        const QPointF anchor=mid+away*(outermost+6.0+std::abs(away.x())*w*0.5
                                                    +std::abs(away.y())*h*0.5);
        p->drawText(QRectF(anchor-QPointF(w/2.0,h/2.0),QSizeF(w,h)),
                    Qt::AlignCenter,axis.name);
    }
}

// Marching squares in the cube: one closed level curve per iso value, each
// projected at the HEIGHT of its own level.
//
// The same two-segment marching squares drawContour runs in two dimensions -
// saddle cells drawn as both crossings, which is the standard resolution and
// cannot mislead at the level a contour plot is read. What differs is the third
// coordinate: a segment of the level at v is drawn at z = v, so the curves
// stack through the box instead of lying flat.
//
// `onSurface` draws them in a colour that reads against the filled quads
// underneath; without it they carry the colour map themselves, because there is
// nothing else in the picture to carry it.
// Takes the grid's FIELDS rather than the grid: ValueGrid is a private member
// type, and Projection, Bounds and ColourMapKind are file-local, so there is no
// signature that both a member declaration in the header and this file can
// name. Spelling out what is read is no worse anyway - it says the routine
// wants a rectangular field and its value range, and nothing else.
static void drawCubeContours(QPainter* p,const Projection& proj,
                             const PlotSpec& spec,
                             int nx,int ny,const QVector<double>& cells,
                             double vLo,double vHi,
                             const Bounds& bz,ColourMapKind cmap,
                             bool onSurface){
    if(nx<2||ny<2||cells.size()<nx*ny) return;
    constexpr int kLevels=8;
    const auto at=[&](int cx,int cy){
        return cells[qBound(0,cy,ny-1)*nx+qBound(0,cx,nx-1)];
    };
    for(int level=1;level<=kLevels;++level){
        const double frac=double(level)/double(kLevels+1);
        const double iso=vLo+(vHi-vLo)*frac;
        QPen pen(onSurface?spec.style.foreground
                          :colourMapStyled(cmap,spec.style,frac));
        pen.setWidthF(qMax(0.4,spec.style.lineWidth*(onSurface?0.5:0.9)));
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        // Every segment of this level sits at the same height, so the z is
        // computed once rather than per corner.
        const double z=bz.norm(iso);
        const auto point=[&](double cx,double cy){
            return project(proj,
                           double(cx)/double(nx-1)-0.5,
                           double(cy)/double(ny-1)-0.5,z);
        };
        for(int cy=0;cy<ny-1;++cy){
            for(int cx=0;cx<nx-1;++cx){
                const double v00=at(cx,cy), v10=at(cx+1,cy);
                const double v11=at(cx+1,cy+1), v01=at(cx,cy+1);
                if(!finite(v00)||!finite(v10)||!finite(v11)||!finite(v01)) continue;
                const int code=(v00>iso?1:0)|(v10>iso?2:0)|(v11>iso?4:0)|(v01>iso?8:0);
                if(code==0||code==15) continue;
                const auto lerp=[&](double va,double vb,double a,double b){
                    const double d=vb-va;
                    return std::abs(d)<1e-15? a : a+(b-a)*((iso-va)/d);
                };
                const QPointF bottom=point(lerp(v00,v10,cx,cx+1),cy);
                const QPointF right =point(cx+1,lerp(v10,v11,cy,cy+1));
                const QPointF top   =point(lerp(v01,v11,cx,cx+1),cy+1);
                const QPointF left  =point(cx,lerp(v00,v01,cy,cy+1));
                switch(code){
                case 1: case 14: p->drawLine(left,bottom); break;
                case 2: case 13: p->drawLine(bottom,right); break;
                case 3: case 12: p->drawLine(left,right); break;
                case 4: case 11: p->drawLine(right,top); break;
                case 6: case 9:  p->drawLine(bottom,top); break;
                case 7: case 8:  p->drawLine(left,top); break;
                case 5:  p->drawLine(left,bottom); p->drawLine(right,top); break;
                case 10: p->drawLine(left,top); p->drawLine(bottom,right); break;
                default: break;
                }
            }
        }
    }
}

// ------------------------------------------------- the 3-D cube, in four pictures
//
// draw3D was 428 lines and complexity 75 in one body, and the shape of it was a
// single `if/else if/else if/else` whose last arm was 250 lines. That last arm
// is on the record as the place nine engines went to be drawn as a scatter,
// because it was the `else` - a 3-D bar as a cloud of dots, a 3-D stem with no
// stalks, Comet 3D with no head.
//
// What they genuinely share is the camera, the cube, its axes and the
// depth-sort, and those stay in draw3D where they are written once. What
// differs is the picture, and each picture is now a function: the dispatch
// below is four lines and reads as a list of the four things this engine family
// can be.

// A grid, so a scattered survey can still be drawn as a surface. Quads are
// sorted by the depth of their centre and painted back to front.
void QtPlotBackend::draw3DSurface(QPainter* p,const Projection& proj,
                                  const PlotSpec& spec,const Bounds& bz,
                                  ColourMapKind cmap,bool limited,
                                  bool contoured,bool wireframe) const {
    // A grid, so a scattered survey can still be drawn as a surface. Quads
    // are sorted by the depth of their centre and painted back to front.
    const ValueGrid& g=cachedGrid(spec,GridAggregate::Mean,4,0);
    if(g.valid){
        struct Quad { double depth; QPolygonF shape; double value; int cx; int cy; };
        QVector<Quad> quads;
        quads.reserve((g.nx-1)*(g.ny-1));
        for(int cy=0;cy<g.ny-1;++cy){
            for(int cx=0;cx<g.nx-1;++cx){
                const double v00=g.cells[cy*g.nx+cx],       v10=g.cells[cy*g.nx+cx+1];
                const double v11=g.cells[(cy+1)*g.nx+cx+1], v01=g.cells[(cy+1)*g.nx+cx];
                if(!finite(v00)||!finite(v10)||!finite(v11)||!finite(v01)) continue;
                auto corner=[&](int ix,int iy,double v,double* d){
                    const double nx=double(ix)/double(g.nx-1)-0.5;
                    const double ny=double(iy)/double(g.ny-1)-0.5;
                    return project(proj,nx,ny,bz.norm(v),d);
                };
                // A quad any of whose corners is outside the height the
                // person asked to see is not drawn. Clipping the SURFACE
                // rather than clamping it leaves a hole where the terrain
                // leaves the window, which is the honest picture - clamping
                // would draw a flat plateau at the cap that is not in the
                // data.
                if(limited&&(v00<bz.lo||v00>bz.hi||v10<bz.lo||v10>bz.hi
                           ||v11<bz.lo||v11>bz.hi||v01<bz.lo||v01>bz.hi)) continue;
                double d0,d1,d2,d3;
                QPolygonF shape;
                shape<<corner(cx,cy,v00,&d0)<<corner(cx+1,cy,v10,&d1)
                     <<corner(cx+1,cy+1,v11,&d2)<<corner(cx,cy+1,v01,&d3);
                quads.append({(d0+d1+d2+d3)/4.0,shape,(v00+v10+v11+v01)/4.0,cx,cy});
            }
        }
        std::sort(quads.begin(),quads.end(),
                  [](const Quad& a,const Quad& b){ return a.depth<b.depth; });
        // HOW MANY LINES THE WIREFRAME HAS. See PlotStyle::meshDensity.
        //
        // Every quad outlined is not a mesh at any resolution a real survey
        // produces - the strokes close the gaps and it draws as a solid.
        // Drawing every Nth grid LINE is, and the lines still follow each
        // cell of the surface underneath, so a coarse mesh over fine data
        // is still the shape of the fine data.
        const int meshLines=spec.style.meshDensity>0
                            ? qBound(4,spec.style.meshDensity,120) : 0;
        const int strideX=meshLines>0?qMax(1,(g.nx-1)/meshLines):1;
        const int strideY=meshLines>0?qMax(1,(g.ny-1)/meshLines):1;
        for(const Quad& q:quads){
            const double t=rampPosition(spec.style,q.value,g.vLo,g.vHi);
            if(wireframe){
                p->setBrush(Qt::NoBrush);
                QPen mesh(colourMapStyled(cmap,spec.style,t));
                mesh.setWidthF(qMax(0.4,spec.style.lineWidth*0.7));
                p->setPen(mesh);
                // The quad's corners in the order they were appended:
                // 0 (cx,cy), 1 (cx+1,cy), 2 (cx+1,cy+1), 3 (cx,cy+1).
                // A line of the wireframe is an edge shared by the quads
                // along it, so each quad contributes the two edges that
                // start at it, plus the far edges of the last row and
                // column - otherwise the mesh has no outside.
                if(q.cy%strideY==0)              p->drawLine(q.shape[0],q.shape[1]);
                if(q.cx%strideX==0)              p->drawLine(q.shape[0],q.shape[3]);
                if(q.cx+2==g.nx)                 p->drawLine(q.shape[1],q.shape[2]);
                if(q.cy+2==g.ny)                 p->drawLine(q.shape[3],q.shape[2]);
            }else{
                p->setBrush(colourMapStyled(cmap,spec.style,t));
                // THE SAME COLOUR AS THE FILL, not a darker one.
                //
                // The edge is here to close the antialiasing seam between
                // neighbouring quads, and darker(115) made it do a second
                // job it was never asked to do: draw a visible lattice over
                // the surface. On a fine grid that lattice is what made a
                // surface look like a mesh, which is half of why the two
                // engines were indistinguishable.
                const QColor fill=colourMapStyled(cmap,spec.style,t);
                QPen edge(fill); edge.setWidthF(0.3);
                p->setPen(edge);
                p->drawPolygon(q.shape);
            }
        }
        // The contours, over the surface they belong to.
        if(contoured)
            drawCubeContours(p,proj,spec,g.nx,g.ny,g.cells,
                             g.vLo,g.vHi,bz,cmap,true);
    }
}


// Level curves and nothing else: the surface is not drawn, so the curves are
// read as a stack rather than as markings on a solid.
void QtPlotBackend::draw3DContourStack(QPainter* p,const Projection& proj,
                                       const PlotSpec& spec,const Bounds& bz,
                                       ColourMapKind cmap) const {
    // Level curves and nothing else: the surface is not drawn, so the
    // curves are read as a stack rather than as markings on a solid.
    const ValueGrid& g=cachedGrid(spec,GridAggregate::Mean,4,0);
    if(g.valid)
        drawCubeContours(p,proj,spec,g.nx,g.ny,g.cells,
                         g.vLo,g.vHi,bz,cmap,false);
}


// One polyline through the rows, in the order they are stored.
void QtPlotBackend::draw3DLine(QPainter* p,const Projection& proj,
                               const PlotSpec& spec,const Bounds& bx,
                               const Bounds& by,const Bounds& bz,
                               bool limited) const {
    const QVector<double>& xs=spec.series.at(0).y;
    const QVector<double>& ys=spec.series.at(1).y;
    const QVector<double>& zs=spec.series.at(2).y;
    const int n=qMin(xs.size(),qMin(ys.size(),zs.size()));
    const auto inBox=[&](double x,double y,double z){
        return x>=bx.lo&&x<=bx.hi&&y>=by.lo&&y<=by.hi&&z>=bz.lo&&z<=bz.hi;
    };
    QPainterPath path;
    bool started=false;
    for(int i=0;i<n;++i){
        if(!finite(xs[i])||!finite(ys[i])||!finite(zs[i])) continue;
        // Out of the window LIFTS THE PEN. Skipping the point while keeping
        // the path going would join the two points either side of the gap
        // with a straight line that is not a path the data ever took.
        if(limited&&!inBox(xs[i],ys[i],zs[i])){ started=false; continue; }
        const QPointF pt=project(proj,bx.norm(xs[i]),by.norm(ys[i]),bz.norm(zs[i]));
        if(!started){ path.moveTo(pt); started=true; } else path.lineTo(pt);
    }
    QPen pen(spec.series.at(2).color);
    pen.setWidthF(qMax(0.9,spec.style.lineWidth*1.2));
    p->setPen(pen); p->setBrush(Qt::NoBrush);
    p->drawPath(path);
}


// Every remaining 3-D engine: one mark per row, depth-sorted.
void QtPlotBackend::draw3DMarks(QPainter* p,const Projection& proj,
                                const PlotSpec& spec,const Bounds& bx,
                                const Bounds& by,const Bounds& bz,
                                ColourMapKind cmap,bool limited) const {
    const QVector<double>& xs=spec.series.at(0).y;
    const QVector<double>& ys=spec.series.at(1).y;
    const QVector<double>& zs=spec.series.at(2).y;
    const int n=qMin(xs.size(),qMin(ys.size(),zs.size()));
    const auto inBox=[&](double x,double y,double z){
        return x>=bx.lo&&x<=bx.hi&&y>=by.lo&&y<=by.hi&&z>=bz.lo&&z<=bz.hi;
    };
    // Every remaining 3-D engine: one mark per row, depth-sorted.
    //
    // NINE of them used to land in this branch and be drawn as a scatter,
    // because it was the `else`. A 3-D bar was a cloud of dots, a 3-D stem
    // had no stalks, Comet 3D had no head, and Surface + Contours had
    // neither a surface nor a contour. All ten came out byte-identical and
    // the sweep found them in one group - the largest it found.
    //
    // They share this loop deliberately. The expensive parts - the
    // projection, the painter's-algorithm sort, the cube and its axes - are
    // written once above and every engine needs all of them; what differs
    // is the mark, which is the part below. Giving each its own painter
    // would be ten copies of the sort.
    const QString& mark=spec.engine;
    const bool bars=mark==QLatin1String("3D Bar");
    const bool barsAcross=mark==QLatin1String("3D Horizontal Bar");
    const bool stems=mark==QLatin1String("3D Stem");
    const bool bubbles=mark==QLatin1String("3D Bubble");
    const bool swarm=mark==QLatin1String("3D Swarm");
    const bool comet=mark==QLatin1String("Comet 3D");
    const bool ribbon=mark==QLatin1String("Ribbon");

    struct Dot { double depth; QPointF at; double value; QPointF foot; int index; };
    QVector<Dot> dots;
    dots.reserve(n);
    // A swarm fans rows that share a height apart so they stop overlapping.
    // Deterministic, like the 2-D Swarm: the same data must draw the same
    // picture, or a figure cannot be compared with the one in the paper.
    QVector<double> nudge(n,0.0);
    if(swarm){
        QVector<int> order(n);
        for(int i=0;i<n;++i) order[i]=i;
        std::sort(order.begin(),order.end(),
                  [&](int a,int b){ return zs[a]<zs[b]; });
        const double span=bz.hi-bz.lo;
        int i=0;
        while(i<order.size()){
            int j=i+1;
            while(j<order.size()
                  &&std::abs(zs[order[j]]-zs[order[i]])<=span*0.012) ++j;
            const int count=j-i;
            for(int k=0;k<count;++k)
                nudge[order[i+k]]=(count==1)?0.0
                    :((double(k)-(count-1)/2.0)/qMax(1.0,double(count-1)))*0.06;
            i=j;
        }
    }
    for(int i=0;i<n;++i){
        if(!finite(xs[i])||!finite(ys[i])||!finite(zs[i])) continue;
        if(limited&&!inBox(xs[i],ys[i],zs[i])) continue;
        double d=0;
        const double nx=bx.norm(xs[i])+nudge[i];
        const double ny=by.norm(ys[i]);
        const QPointF pt=project(proj,nx,ny,bz.norm(zs[i]),&d);
        // Where a bar or a stalk stands on the floor of the cube. The floor
        // is z = 0 when the box contains it and the bottom of the box when
        // it does not: a bar drawn from an arbitrary baseline reports a
        // ratio that is not in the data, which is the same reason
        // computeRange forces zero into a 2-D bar's axis.
        const double floorZ=(bz.lo<=0.0&&bz.hi>=0.0)?bz.norm(0.0):0.0;
        const QPointF foot=barsAcross?project(proj,0.0,ny,bz.norm(zs[i]))
                                     :project(proj,nx,ny,floorZ);
        dots.append({d,pt,zs[i],foot,i});
    }
    std::sort(dots.begin(),dots.end(),[](const Dot& a,const Dot& b){ return a.depth<b.depth; });

    if(comet||ribbon){
        // A trail through the rows in order, with the mark that names it:
        // a comet has a head at the last row, a ribbon is a band with
        // width. Both are a PATH - drawn in row order, not depth order,
        // because the order of the rows is the thing they show.
        QPainterPath trail;
        bool started=false;
        QPointF last;
        QVector<QPointF> along;
        for(int i=0;i<n;++i){
            if(!finite(xs[i])||!finite(ys[i])||!finite(zs[i])) continue;
            if(limited&&!inBox(xs[i],ys[i],zs[i])) continue;
            const QPointF pt=project(proj,bx.norm(xs[i]),by.norm(ys[i]),bz.norm(zs[i]));
            along.append(pt);
            if(!started){ trail.moveTo(pt); started=true; } else trail.lineTo(pt);
            last=pt;
        }
        if(started){
            if(ribbon){
                // A band of the width the value asks for, laid along the
                // path's own normal. A ribbon with no width is a line, and
                // a line is what this engine drew for a year.
                for(int i=1;i<along.size();++i){
                    const QPointF a=along[i-1],b=along[i];
                    const QPointF step=b-a;
                    const double len=std::hypot(step.x(),step.y());
                    if(!(len>0.01)) continue;
                    const QPointF normal(-step.y()/len,step.x()/len);
                    const double t=rampPosition(spec.style,zs[qMin(i,n-1)],bz.lo,bz.hi);
                    const double half=qMax(1.2,3.0+5.0*t);
                    QPolygonF quad;
                    quad<<(a+normal*half)<<(b+normal*half)
                        <<(b-normal*half)<<(a-normal*half);
                    QColor fill=colourMapStyled(cmap,spec.style,t);
                    fill.setAlphaF(0.85);
                    p->setBrush(fill);
                    p->setPen(QPen(fill.darker(120),0.3));
                    p->drawPolygon(quad);
                }
            }else{
                // THE TAIL FADES. Otherwise this is a 3-D Line with a dot
                // on the end - which is what it was, and what the
                // perceptual pass found: 0.13 grey levels out of 255
                // between Comet 3D and 3D Line, the two closest figures in
                // the gallery once the geographic engines were separated.
                //
                // The whole content of a comet is RECENCY. A trail at one
                // uniform opacity shows where the path went and says
                // nothing about which end of it is now, so the head has
                // nothing to be meaningful against. The 2-D Comet has faded
                // in six segments since it was written; this is the same
                // ramp, and it is applied to the width as well, because at
                // 89 mm a change of opacity alone is most of a hairline
                // that was already faint.
                constexpr int kSegments=8;
                p->setBrush(Qt::NoBrush);
                for(int seg=0;seg<kSegments;++seg){
                    const int from=int(qint64(seg)*along.size()/kSegments);
                    const int to=qMin(along.size(),
                                      int(qint64(seg+1)*along.size()/kSegments)+1);
                    if(to-from<2) continue;
                    const double age=(double(seg)+1.0)/double(kSegments);
                    // LINEAR, and never faint enough to disappear. A
                    // squared ramp looked right in the arithmetic and threw
                    // the oldest two thirds of the trail away - the figure
                    // came back as a dot in an empty cube, which is a
                    // comet that has lost the path it is travelling along.
                    // The 2-D Comet's ramp is linear from 0.12; this is the
                    // same one, and the width carries part of it so the
                    // fade survives being printed at 89 mm.
                    QPen pen(spec.series.at(2).color);
                    pen.setWidthF(qMax(0.6,spec.style.lineWidth*(0.55+1.05*age)));
                    p->setPen(pen);
                    p->setOpacity(0.18+0.77*age);
                    QPainterPath piece;
                    piece.moveTo(along[from]);
                    for(int i=from+1;i<to;++i) piece.lineTo(along[i]);
                    p->drawPath(piece);
                }
                p->setOpacity(1.0);
                // The head. Without it a comet is a line, which is what it
                // was: the 2-D Comet has had a head since it was written.
                p->setPen(Qt::NoPen);
                p->setBrush(spec.series.at(2).color);
                p->drawEllipse(last,4.5,4.5);
            }
        }
        // A PLAIN RETURN. The title and the painter's restore belong to
        // draw3D, which called this and does both after every branch - so
        // doing them here as well, which is what this line did when the branch
        // was inline, would draw the title twice and unbalance the save.
        return;
    }

    p->setPen(Qt::NoPen);
    const double base=qMax(2.0,spec.series.at(2).markerSize);
    if(bars||barsAcross){
        // A box standing on the floor, drawn as the three faces that can
        // be seen from any viewpoint this projection allows. Not a filled
        // rectangle in screen space: that would not shrink with depth and
        // would not sit on the floor of the cube when the figure turned.
        const double side=0.035;
        for(const Dot& dot:dots){
            const int i=dot.index;
            const double t=rampPosition(spec.style,dot.value,bz.lo,bz.hi);
            const QColor face=colourMapStyled(cmap,spec.style,t);
            const double nx=bx.norm(xs[i]),ny=by.norm(ys[i]);
            const double z0=(bz.lo<=0.0&&bz.hi>=0.0)?bz.norm(0.0):0.0;
            const double z1=bz.norm(zs[i]);
            // A horizontal bar LIES DOWN: it runs along x from the x floor
            // to the row's own x, at the row's y and z, where a vertical
            // one rises along z. Without this the two drew identical
            // standing boxes and the sweep found them in a group together
            // - the same `else` problem in miniature, one branch further
            // in. The `barsAcross` flag existed and set the foot for the
            // stalk; the box was never told about it.
            const double x0=(bx.lo<=0.0&&bx.hi>=0.0)?bx.norm(0.0):0.0;
            const auto at=[&](double dAcross,double dy,double along){
                return barsAcross?project(proj,along,ny+dy,bz.norm(zs[i])+dAcross)
                                 :project(proj,nx+dAcross,ny+dy,along);
            };
            const double from=barsAcross?x0:z0;
            const double to  =barsAcross?nx:z1;
            // The end cap, then the two long faces. Shaded apart so the box
            // reads as a solid rather than as a flat patch of colour.
            QPolygonF top,front,side2;
            top  <<at(-side,-side,to)<<at(side,-side,to)
                 <<at(side,side,to)<<at(-side,side,to);
            front<<at(-side,side,from)<<at(side,side,from)
                 <<at(side,side,to)<<at(-side,side,to);
            side2<<at(side,-side,from)<<at(side,side,from)
                 <<at(side,side,to)<<at(side,-side,to);
            p->setPen(QPen(face.darker(140),0.3));
            p->setBrush(face.darker(125)); p->drawPolygon(front);
            p->setBrush(face.darker(112)); p->drawPolygon(side2);
            p->setBrush(face);             p->drawPolygon(top);
        }
    }else if(stems){
        // A stalk from the floor to the point, and the point on top of it.
        // The stalk is the whole difference between a stem and a scatter:
        // it is what makes the height readable against the floor rather
        // than against whatever else happens to be nearby.
        for(const Dot& dot:dots){
            const double t=rampPosition(spec.style,dot.value,bz.lo,bz.hi);
            const QColor ink=colourMapStyled(cmap,spec.style,t);
            QPen stalk(ink); stalk.setWidthF(qMax(0.4,spec.style.lineWidth*0.7));
            p->setPen(stalk); p->setBrush(Qt::NoBrush);
            p->drawLine(dot.foot,dot.at);
            p->setPen(Qt::NoPen); p->setBrush(ink);
            p->drawEllipse(dot.at,qMax(1.0,base/2.4),qMax(1.0,base/2.4));
        }
    }else if(bubbles){
        // Sized by a FOURTH column when one is mapped, and by z when one
        // is not - so a bubble plot of three columns is still a bubble
        // plot rather than a scatter. Area, not diameter: sizing the
        // diameter makes a value four times larger read as sixteen.
        const QVector<double>& sized=(spec.series.size()>=4)
                                         ? spec.series.at(3).y : zs;
        double lo=std::numeric_limits<double>::infinity(),hi=-lo;
        for(int i=0;i<n&&i<sized.size();++i)
            if(finite(sized[i])){ lo=qMin(lo,sized[i]); hi=qMax(hi,sized[i]); }
        constexpr double kAreaMin=10.0,kAreaMax=260.0;
        constexpr double kPi=3.14159265358979323846;
        for(const Dot& dot:dots){
            const int i=dot.index;
            const double t=rampPosition(spec.style,dot.value,bz.lo,bz.hi);
            p->setBrush(colourMapStyled(cmap,spec.style,t));
            const double u=(i<sized.size()&&finite(sized[i])&&hi>lo)
                               ? (sized[i]-lo)/(hi-lo) : 0.5;
            const double r=std::sqrt((kAreaMin+u*(kAreaMax-kAreaMin))/kPi);
            p->drawEllipse(dot.at,r,r);
        }
    }else{
        // 3D Scatter, and 3D Swarm, which is the same mark with the rows
        // that share a height fanned apart above. The marker shrinks with
        // depth so the far side of the cloud recedes.
        for(const Dot& dot:dots){
            const double t=rampPosition(spec.style,dot.value,bz.lo,bz.hi);
            p->setBrush(colourMapStyled(cmap,spec.style,t));
            const double r=base*(0.62+0.38*(dot.depth+0.9));
            p->drawEllipse(dot.at,qMax(0.8,r/2.0),qMax(0.8,r/2.0));
        }
    }
}

void QtPlotBackend::draw3D(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    // The field colour map, read once. Viridis unless the style asks for
    // another - see PlotStyle::colourMap.
    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);
    if(spec.series.size()<3) return;
    const QVector<double>& xs=spec.series.at(0).y;
    const QVector<double>& ys=spec.series.at(1).y;
    const QVector<double>& zs=spec.series.at(2).y;
    const int n=qMin(xs.size(),qMin(ys.size(),zs.size()));
    if(n<2) return;

    // What the data spans, then what the person asked to SEE of it.
    //
    // The cube used to be fitted to the data and nothing else, so there was no
    // way to say "VHPR above ten is not what I am looking at" - one outlier at
    // 160 set the height of the box and everything below it was a layer on the
    // floor. The limits are applied to the bounds rather than to the points,
    // which is what makes the cube itself, its tick numbers and the projection
    // all agree about what the axis now means.
    const Bounds bx=withAxisLimits(boundsOf(xs),spec.xAxis);
    const Bounds by=withAxisLimits(boundsOf(ys),spec.yAxis);
    const Bounds bz=withAxisLimits(boundsOf(zs),spec.zAxis);
    if(!bx.valid||!by.valid||!bz.valid) return;

    // Whether a row is inside the box the person asked for. A point outside it
    // is DROPPED rather than clamped: a capped axis is a window on the data,
    // and stacking everything above the cap onto the ceiling would invent a
    // ridge that is not in the numbers.
    const auto inBox=[&](double x,double y,double z){
        return x>=bx.lo&&x<=bx.hi&&y>=by.lo&&y<=by.hi&&z>=bz.lo&&z<=bz.hi;
    };
    // Whether any limit is in force at all, so the unlimited case does the same
    // work it always did.
    const bool limited=!isUnset(spec.xAxis.min)||!isUnset(spec.xAxis.max)
                     ||!isUnset(spec.yAxis.min)||!isUnset(spec.yAxis.max)
                     ||!isUnset(spec.zAxis.min)||!isUnset(spec.zAxis.max);

    // Room for the tick numbers and the axis names outside the cube, and no
    // more: the figure is fitted to whatever is left, so a wide canvas gives a
    // wide figure instead of a small one in the middle of a lot of nothing.
    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFont nameFont=font(spec,spec.style.axisLabelSize);
    const double margin=cubeMargin(p,tickFont,nameFont,spec.style.scaleLabelsVisible);
    const Projection proj=makeProjection(target,spec.view3d.azimuth,spec.view3d.elevation,
                                         spec.view3d.zoom,margin,
                                         spec.view3d.panX,spec.view3d.panY);

    p->save();

    drawBoundingCube(p,proj,spec.style.gridColor);
    drawCubeAxes(p,proj,spec,bx,by,bz,
                 spec.series.at(0).label,spec.series.at(1).label,spec.series.at(2).label,
                 tickFont,nameFont);

    const bool surface=spec.engine==QLatin1String("3D Topography / Surface")
                     ||spec.engine==QLatin1String("3D Mesh")
                     ||spec.engine==QLatin1String("Surface + Contours");
    const bool wireframe=spec.engine==QLatin1String("3D Mesh");
    // Level curves, on the surface or floating in the cube on their own.
    //
    // Both of these were falling into the scatter `else` below, so an engine
    // called "Surface + Contours" had neither, and "3D Contour" was a cloud of
    // dots. Marching squares is the same algorithm drawContour already runs in
    // two dimensions; what differs is that each segment is projected at the
    // HEIGHT of the level it belongs to, which is what makes the curves stack.
    const bool contoured=spec.engine==QLatin1String("Surface + Contours");
    const bool contourOnly=spec.engine==QLatin1String("3D Contour");

    if(surface)      draw3DSurface(p,proj,spec,bz,cmap,limited,contoured,wireframe);
    else if(contourOnly) draw3DContourStack(p,proj,spec,bz,cmap);
    else if(spec.engine==QLatin1String("3D Line"))
                     draw3DLine(p,proj,spec,bx,by,bz,limited);
    else             draw3DMarks(p,proj,spec,bx,by,bz,cmap,limited);

    drawFloatingTitle(p,target,spec);
    p->restore();
}

// ---------------------------------------------------------------------------
// Vector fields.
//
// Four mapped columns: x, y and the two components of the vector at each point.
// Quiver draws the vectors themselves; streamlines integrate through the field;
// divergence and vorticity draw a scalar derived from it. All four share the
// same gridding, because a field measured at scattered points has to be regular
// before anything can be differentiated or followed through it.
//
// SPLIT INTO THE FIVE PICTURES IT DRAWS. This was 482 lines and complexity 104
// in one body, and the cost of that was on the record: the feather branch spent
// a release drawing the quiver's gridded, strided arrows because both lived in
// the same scope under a comment claiming they differed. The functions below
// cannot share a branch by accident, and `drawVectorField` is now the reading
// of the engine name and nothing else.

// A feather plot is not a field, and must not be gridded.
//
// Feather fell through to the quiver branch, so it drew the same gridded,
// strided arrows and the two came out in one same-picture group. The comment on
// that branch said "Quiver, and Feather, which is the same arrows anchored on a
// line" - which described an intention. Both were anchored on the GRID.
//
// The difference is not the mark, it is what is drawn. A quiver bins the
// samples onto a lattice and shows the mean vector in each cell. A feather
// shows EVERY observation's vector at the place it was taken, in the order it
// was taken, which is how a current-meter record or a wind time series is read -
// the reader is looking for a veer, a lull, a reversal, and the binning is
// precisely the step that averages those away. Striding past nine samples in
// ten does the rest.
//
// Called before the grid is built, not after: a feather has no use for one, and
// building it costs more than the drawing does.
void QtPlotBackend::drawFeatherPlot(QPainter* p,const Frame& f,
                                    const PlotSpec& spec) const {
    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);
    const QVector<double>& xs=spec.series.at(0).y;
    const QVector<double>& ys=spec.series.at(1).y;
    const QVector<double>& us=spec.series.at(2).y;
    const QVector<double>& vs=spec.series.at(3).y;
    const int n=qMin(qMin(xs.size(),ys.size()),qMin(us.size(),vs.size()));

    double featherMax=0.0;
    for(int i=0;i<n;++i)
        if(finite(us[i])&&finite(vs[i]))
            featherMax=qMax(featherMax,std::hypot(us[i],vs[i]));
    if(!(featherMax>0.0)) return;
    // Sized so the longest vector is a fixed fraction of the frame, whatever
    // the units are, and every arrow shares that one scale - comparing lengths
    // is the only thing a feather plot is read for.
    const double longest=qMin(f.plotArea.width(),f.plotArea.height())*0.18;
    p->save();
    p->setClipRect(f.plotArea);
    for(int i=0;i<n;++i){
        if(!finite(xs[i])||!finite(ys[i])||!finite(us[i])||!finite(vs[i])) continue;
        const double mag=std::hypot(us[i],vs[i]);
        if(!(mag>0.0)) continue;
        const QPointF from=toDevice(f,xs[i],ys[i]);
        const QPointF dir(us[i]/mag,-vs[i]/mag);
        QPen pen(colourMapStyled(cmap,spec.style,
                 rampPosition(spec.style,mag,0.0,featherMax)));
        pen.setWidthF(qMax(0.4,spec.style.lineWidth*0.8));
        p->setPen(pen);
        // Bare sticks, no heads. A head at this density is a blob, and the
        // quantity being read off a feather is the length and the angle.
        p->drawLine(from,from+dir*(longest*mag/featherMax));
    }
    p->restore();
    drawColourBar(p,f,spec,0.0,featherMax,QStringLiteral("speed"));
}

// Divergence is du/dx + dv/dy, vorticity is dv/dx - du/dy. Central differences
// on the grid, then drawn as the scalar field it is.
void QtPlotBackend::drawDerivedField(QPainter* p,const Frame& f,const PlotSpec& spec,
                                     const ValueGrid& gu,const ValueGrid& gv,
                                     bool vorticity) const {
    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);
    const double cellX=(gu.xHi-gu.xLo)/qMax(1,gu.nx-1);
    const double cellY=(gu.yHi-gu.yLo)/qMax(1,gu.ny-1);
    // Clamped, which is what makes a derivative one-sided at the boundary
    // rather than wrong there - see the note below.
    const auto sampleU=[&](int cx,int cy){ return gu.cells[qBound(0,cy,gu.ny-1)*gu.nx+qBound(0,cx,gu.nx-1)]; };
    const auto sampleV=[&](int cx,int cy){ return gv.cells[qBound(0,cy,gv.ny-1)*gv.nx+qBound(0,cx,gv.nx-1)]; };

    ValueGrid out=gu;
    double lo=std::numeric_limits<double>::infinity(),hi=-lo;
    // ONE-SIDED AT THE EDGE, central inside.
    //
    // The difference was written (f[i+1] - f[i-1]) / 2h with the sampler
    // clamping the index - so on the first and last row or column both samples
    // came from ONE cell apart and were still divided by two cells' worth of
    // distance. Every boundary cell got HALF the slope it should have, which on
    // a smooth field is a value nothing else on the map has: a divergence map
    // of a field whose divergence is nearly constant came out uniform with a
    // bright stripe along the top and bottom edges, and the colour scale was
    // then set by the artefact rather than the field.
    //
    // Dividing by the distance ACTUALLY sampled is the one-sided derivative at
    // the edge and the central difference everywhere else, which is what the
    // comment above always claimed.
    auto slopeX=[&](auto&& sample,int cx,int cy){
        const int a=qMax(0,cx-1),b=qMin(gu.nx-1,cx+1);
        if(b==a) return 0.0;
        return (sample(b,cy)-sample(a,cy))/(double(b-a)*qMax(1e-12,cellX));
    };
    auto slopeY=[&](auto&& sample,int cx,int cy){
        const int a=qMax(0,cy-1),b=qMin(gu.ny-1,cy+1);
        if(b==a) return 0.0;
        return (sample(cx,b)-sample(cx,a))/(double(b-a)*qMax(1e-12,cellY));
    };
    for(int cy=0;cy<gu.ny;++cy){
        for(int cx=0;cx<gu.nx;++cx){
            const double dudx=slopeX(sampleU,cx,cy);
            const double dudy=slopeY(sampleU,cx,cy);
            const double dvdx=slopeX(sampleV,cx,cy);
            const double dvdy=slopeY(sampleV,cx,cy);
            const double value=vorticity?(dvdx-dudy):(dudx+dvdy);
            out.cells[cy*gu.nx+cx]=value;
            if(finite(value)){ lo=qMin(lo,value); hi=qMax(hi,value); }
        }
    }
    if(!finite(lo)||!finite(hi)) return;
    if(qFuzzyCompare(lo,hi)) hi=lo+1.0;
    out.vLo=lo; out.vHi=hi;
    // Through the axes, not stretched across the plot area - see the note in
    // drawHeatmap. The arrows and streamlines already go through toDevice, so
    // this branch stretching was also the two halves of one painter disagreeing
    // about where the same grid was.
    const double cw=(out.xHi-out.xLo)/double(qMax(1,out.nx));
    const double ch=(out.yHi-out.yLo)/double(qMax(1,out.ny));
    p->save();
    p->setClipRect(f.plotArea);
    p->setPen(Qt::NoPen);
    for(int cy=0;cy<out.ny;++cy){
        for(int cx=0;cx<out.nx;++cx){
            const double value=out.cells[cy*out.nx+cx];
            if(!finite(value)) continue;
            p->setBrush(colourMapStyled(cmap,spec.style,
                        rampPosition(spec.style,value,out.vLo,out.vHi)));
            const QPointF lowerLeft=toDevice(f,out.xLo+cx*cw,out.yLo+cy*ch);
            const QPointF upperRight=toDevice(f,out.xLo+(cx+1)*cw,
                                               out.yLo+(cy+1)*ch);
            // Half a pixel of overlap, or antialiasing leaves seams.
            p->drawRect(QRectF(lowerLeft.x(),upperRight.y(),
                               upperRight.x()-lowerLeft.x()+0.5,
                               lowerLeft.y()-upperRight.y()+0.5));
        }
    }
    p->restore();
    drawColourBar(p,f,spec,out.vLo,out.vHi,
                  vorticity?QStringLiteral("vorticity")
                           :QStringLiteral("divergence"));
}

// Streamlines: seed a lattice and integrate with RK2, which is stable enough on
// a gridded field and does not curl inward the way Euler does on a rotational
// flow.
//
// Four engines share this integration - that part IS shared, and should be -
// but what each puts on the page is different:
//
//   Stream Field      the streamlines themselves
//   Stream Particles  markers ALONG those lines, spaced by arc length, so the
//                     field reads as a population being carried rather than as
//                     a set of curves
//   Flow Texture      the lines drawn dense and short from a noise seed, which
//                     is the readable form of line-integral convolution:
//                     texture rather than curves
//   Phase Portrait    the lines, plus the fixed points - drawn separately, see
//                     drawPhasePortraitPoints
void QtPlotBackend::drawStreamlines(QPainter* p,const Frame& f,const PlotSpec& spec,
                                    const ValueGrid& gu,const ValueGrid& gv,
                                    double magMax) const {
    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);
    const QString& engine=spec.engine;
    const bool particles=engine==QLatin1String("Stream Particles");
    const bool texture=engine==QLatin1String("Flow Texture (LIC)");
    const double cellX=(gu.xHi-gu.xLo)/qMax(1,gu.nx-1);
    const double cellY=(gu.yHi-gu.yLo)/qMax(1,gu.ny-1);
    // Clamped, which is what makes a derivative one-sided at the boundary
    // rather than wrong there - see the note below.
    const auto sampleU=[&](int cx,int cy){ return gu.cells[qBound(0,cy,gu.ny-1)*gu.nx+qBound(0,cx,gu.nx-1)]; };
    const auto sampleV=[&](int cx,int cy){ return gv.cells[qBound(0,cy,gv.ny-1)*gv.nx+qBound(0,cx,gv.nx-1)]; };

    // A texture is MANY SHORT lines; the others are few long ones. That is the
    // whole difference in the integration, and it is what turns a set of curves
    // into something the eye reads as a surface being combed.
    const int seeds=texture?qBound(14,gu.nx,54):qBound(6,gu.nx/2,18);
    const int iterations=texture?14:160;
    // AND A LENGTH MEASURED IN THE DOMAIN, not in steps.
    //
    // 160 steps of six tenths of a cell is about fourteen units of arc, and the
    // demonstration field is three and a half units across: every streamline
    // wrapped three times round the same closed orbit and the middle of the
    // figure went solid. A field drawn that way is an ink blot with a colour
    // bar - and it is why the direction heads below, which were added first and
    // on their own, could not be seen even though they were being drawn.
    //
    // Just under half the diagonal is long enough to show where a line is going
    // and short enough that it does not come back. The step count stays as the
    // upper bound, so a slow corner of the field cannot spend the whole budget.
    const double maxArc=0.45*std::hypot(gu.xHi-gu.xLo,gu.yHi-gu.yLo);
    QVector<QPointF> carriedParticles;
    for(int sy=0;sy<seeds;++sy){
        for(int sx=0;sx<seeds;++sx){
            double px=gu.xLo+(gu.xHi-gu.xLo)*(double(sx)+0.5)/double(seeds);
            double py=gu.yLo+(gu.yHi-gu.yLo)*(double(sy)+0.5)/double(seeds);
            QPainterPath path;
            bool started=false;
            double carried=0.0;
            // WHICH WAY THE FLOW GOES.
            //
            // A streamline with no direction on it is half a reading: the same
            // set of curves describes a flow and its exact reverse, and nothing
            // on the page chose between them. It mattered most on the phase
            // portrait, where a stable spiral and an unstable one draw
            // identical trajectories and differ only in which way they are
            // travelled - so the classification in the labels was asserting
            // something the picture did not show.
            //
            // One head per line, at its midpoint, pointing along the local
            // direction. One rather than many because a row of arrowheads along
            // every curve is a dashed line, and the spacing then reads as
            // speed, which it is not.
            QPointF headAt,headFrom;
            bool haveHead=false;
            QPointF previous;
            double travelled=0.0;
            const double step=qMin(cellX,cellY)*0.6;
            for(int iter=0;iter<iterations;++iter){
                const int cx=int((px-gu.xLo)/qMax(1e-12,gu.xHi-gu.xLo)*(gu.nx-1)+0.5);
                const int cy=int((py-gu.yLo)/qMax(1e-12,gu.yHi-gu.yLo)*(gu.ny-1)+0.5);
                if(cx<0||cy<0||cx>=gu.nx||cy>=gu.ny) break;
                const double u=sampleU(cx,cy), v=sampleV(cx,cy);
                if(!finite(u)||!finite(v)) break;
                const double mag=std::hypot(u,v);
                if(!(mag>1e-12)) break;
                carried=qMax(carried,mag);
                // Midpoint step.
                const double hx=px+u/mag*step*0.5, hy=py+v/mag*step*0.5;
                const int mx=int((hx-gu.xLo)/qMax(1e-12,gu.xHi-gu.xLo)*(gu.nx-1)+0.5);
                const int my=int((hy-gu.yLo)/qMax(1e-12,gu.yHi-gu.yLo)*(gu.ny-1)+0.5);
                const double u2=sampleU(mx,my), v2=sampleV(mx,my);
                const double m2=std::hypot(u2,v2);
                if(!finite(m2)||!(m2>1e-12)) break;
                px+=u2/m2*step; py+=v2/m2*step;
                travelled+=step;
                const QPointF pt=toDevice(f,px,py);
                if(!started){ path.moveTo(pt); started=true; } else path.lineTo(pt);
                // Half way along what this line will actually be, which is the
                // arc budget rather than the step budget - on a line that stops
                // early the step midpoint is past its end.
                if(!texture&&!haveHead&&started&&travelled>=maxArc*0.5){
                    headAt=pt; headFrom=previous; haveHead=true;
                }
                previous=pt;
                if(!texture&&travelled>=maxArc) break;
                // Every eighth step of the walk, which is a constant spacing in
                // ARC LENGTH because the step is a fixed distance. Spacing them
                // by iteration count in a field of varying speed would bunch
                // them where the flow is slow, and a reader would take that for
                // a crowd.
                if(particles&&iter%8==0) carriedParticles.append(pt);
            }
            if(!started) continue;
            QPen pen(colourMapStyled(cmap,spec.style,
                     rampPosition(spec.style,carried,0.0,magMax)));
            pen.setWidthF(qMax(0.5,spec.style.lineWidth*(texture?0.7:0.9)));
            if(particles){
                // The line is the path the particles are on, faint enough that
                // the particles are the figure.
                QColor faint=pen.color(); faint.setAlphaF(0.28);
                QPen guide(faint); guide.setWidthF(qMax(0.4,spec.style.lineWidth*0.6));
                p->setPen(guide);
            }else{
                p->setPen(pen);
            }
            p->setBrush(Qt::NoBrush);
            p->drawPath(path);
            if(haveHead){
                const QPointF along=headAt-headFrom;
                const double len=std::hypot(along.x(),along.y());
                if(len>0.05){
                    const double angle=std::atan2(along.y(),along.x());
                    const double size=qMax(5.0,spec.style.lineWidth*4.5);
                    QPolygonF head;
                    head<<headAt
                        <<headAt-QPointF(std::cos(angle-0.42)*size,
                                         std::sin(angle-0.42)*size)
                        <<headAt-QPointF(std::cos(angle+0.42)*size,
                                         std::sin(angle+0.42)*size);
                    p->setPen(Qt::NoPen);
                    p->setBrush(pen.color());
                    p->drawPolygon(head);
                    p->setBrush(Qt::NoBrush);
                }
            }
            if(particles){
                p->setPen(Qt::NoPen);
                p->setBrush(pen.color());
                for(const QPointF& at:std::as_const(carriedParticles))
                    p->drawEllipse(at,2.0,2.0);
                carriedParticles.clear();
            }
        }
    }
}

// The fixed points, and what kind each one is.
//
// A phase portrait is read for these: where the system settles, where it runs
// away, and where it circles. The trajectories show you how it gets there, but
// a portrait without the critical points classified is a picture of some curves.
//
// Found by sign change on the grid rather than by a solver: a cell whose corners
// disagree about the sign of BOTH components contains a zero of the field. That
// is a coarse locator - it finds the cell, not the point - which is the right
// resolution here, because the glyph is several pixels across and the grid is
// what the field is known on anyway.
void QtPlotBackend::drawPhasePortraitPoints(QPainter* p,const Frame& f,
                                            const PlotSpec& spec,
                                            const ValueGrid& gu,
                                            const ValueGrid& gv) const {
    const double cellX=(gu.xHi-gu.xLo)/qMax(1,gu.nx-1);
    const double cellY=(gu.yHi-gu.yLo)/qMax(1,gu.ny-1);
    const auto at=[&](int cx,int cy,bool wantU){
        return wantU?gu.cells[qBound(0,cy,gu.ny-1)*gu.nx+qBound(0,cx,gu.nx-1)]
                    :gv.cells[qBound(0,cy,gv.ny-1)*gv.nx+qBound(0,cx,gv.nx-1)];
    };
    // Collected first, drawn after.
    //
    // A zero sitting on or near a grid line satisfies "both components change
    // sign" in every cell that touches it, so one fixed point is found two,
    // three or four times. Drawn straight from the loop, the Duffing field's
    // three points came out as EIGHT markers with eight labels piled on each
    // other - and a reader counting critical points off the figure would have
    // got eight, which is the one thing a phase portrait must not get wrong.
    struct Critical { QPointF at; QString kind; };
    QVector<Critical> criticals;
    // Scaled to the CELL, because that is what produces the duplicates: a zero
    // on a grid line is inside every cell that touches it, so the copies are
    // one or two cells apart and nothing else is. A fraction of the plot area
    // was the first guess and it was too small - on a 13-cell grid the copies
    // sat 40 px apart and the radius was 17, so all eight survived. 1.6 cells
    // covers the copies with room to spare and is still far short of the 140 px
    // between the Duffing field's genuinely distinct points.
    // NOT a cell of the drawing - a distance for comparing two detections,
    // which is why it is not named like one. The field-painter guard looks for
    // `const double c... = plotArea.width()/double(...)` because that shape is
    // how a grid gets stretched to fill the axes, and this measures the same
    // spacing for an entirely different purpose.
    const double gridPitchX=f.plotArea.width()/double(qMax(1,gu.nx-1));
    const double gridPitchY=f.plotArea.height()/double(qMax(1,gu.ny-1));
    const double nearby=qMax(6.0,1.6*qMax(gridPitchX,gridPitchY));
    for(int cy=0;cy+1<gu.ny;++cy){
        for(int cx=0;cx+1<gu.nx;++cx){
            const double u00=at(cx,cy,true),u10=at(cx+1,cy,true);
            const double u01=at(cx,cy+1,true),u11=at(cx+1,cy+1,true);
            const double v00=at(cx,cy,false),v10=at(cx+1,cy,false);
            const double v01=at(cx,cy+1,false),v11=at(cx+1,cy+1,false);
            if(!finite(u00)||!finite(u10)||!finite(u01)||!finite(u11)) continue;
            if(!finite(v00)||!finite(v10)||!finite(v01)||!finite(v11)) continue;
            const bool uCrosses=(qMin(qMin(u00,u10),qMin(u01,u11))<=0.0)
                              &&(qMax(qMax(u00,u10),qMax(u01,u11))>=0.0);
            const bool vCrosses=(qMin(qMin(v00,v10),qMin(v01,v11))<=0.0)
                              &&(qMax(qMax(v00,v10),qMax(v01,v11))>=0.0);
            if(!uCrosses||!vCrosses) continue;

            // The Jacobian at the cell, by central differences on the corners.
            // Its trace and determinant classify the point - the standard
            // trace/determinant plane:
            //   det < 0                  saddle
            //   det > 0, tr^2 < 4 det    spiral (or a centre at tr = 0)
            //   det > 0 otherwise        node
            const double dudx=((u10-u00)+(u11-u01))*0.5/qMax(1e-12,cellX);
            const double dudy=((u01-u00)+(u11-u10))*0.5/qMax(1e-12,cellY);
            const double dvdx=((v10-v00)+(v11-v01))*0.5/qMax(1e-12,cellX);
            const double dvdy=((v01-v00)+(v11-v10))*0.5/qMax(1e-12,cellY);
            const double trace=dudx+dvdy;
            const double det=dudx*dvdy-dudy*dvdx;
            if(!finite(trace)||!finite(det)) continue;

            const double x=gu.xLo+(gu.xHi-gu.xLo)*(double(cx)+0.5)/double(qMax(1,gu.nx-1));
            const double y=gu.yLo+(gu.yHi-gu.yLo)*(double(cy)+0.5)/double(qMax(1,gu.ny-1));
            const QPointF centre=toDevice(f,x,y);
            if(!f.plotArea.contains(centre)) continue;

            // One per cluster. Same rule drawVoronoi uses for coincident sites,
            // and for the same reason: a duplicate is not extra information, it
            // is a second answer to a question that has one.
            bool duplicate=false;
            for(const Critical& seen:std::as_const(criticals)){
                const double dx=seen.at.x()-centre.x();
                const double dy=seen.at.y()-centre.y();
                if(dx*dx+dy*dy<nearby*nearby){ duplicate=true; break; }
            }
            if(duplicate) continue;

            QString kind;
            if(det<0.0)                       kind=QStringLiteral("saddle");
            else if(std::abs(trace)<1e-9)     kind=QStringLiteral("centre");
            else if(trace*trace<4.0*det)      kind=(trace<0.0)?QStringLiteral("stable spiral")
                                                             :QStringLiteral("unstable spiral");
            else                              kind=(trace<0.0)?QStringLiteral("stable node")
                                                             :QStringLiteral("unstable node");
            criticals.append({centre,kind});
        }
    }

    // Filled where the point ATTRACTS, hollow where it repels, and a cross for a
    // saddle - so the three behaviours are distinguishable without reading the
    // label, which is what matters when there are several.
    p->save();
    p->setFont(font(spec,spec.style.tickSize));
    for(const Critical& point:std::as_const(criticals)){
        QPen mark(spec.style.foreground);
        mark.setWidthF(qMax(0.8,spec.style.lineWidth));
        p->setPen(mark);
        if(point.kind==QLatin1String("saddle")){
            p->drawLine(point.at+QPointF(-4,-4),point.at+QPointF(4,4));
            p->drawLine(point.at+QPointF(-4,4),point.at+QPointF(4,-4));
        }else{
            const bool attracts=point.kind.startsWith(QLatin1String("stable"));
            p->setBrush(attracts?QBrush(spec.style.foreground):Qt::NoBrush);
            p->drawEllipse(point.at,3.6,3.6);
        }
        p->setBrush(Qt::NoBrush);
        p->drawText(QRectF(point.at.x()+6,point.at.y()-8,120,14),
                    Qt::AlignLeft|Qt::AlignVCenter,point.kind);
    }
    p->restore();
}

// Quiver: the gridded, strided arrows. Feather is drawn by drawFeatherPlot and
// never reaches here - it draws every sample and is not a field, whatever the
// shared columns suggest.
void QtPlotBackend::drawQuiverArrows(QPainter* p,const Frame& f,const PlotSpec& spec,
                                     const ValueGrid& gu,const ValueGrid& gv,
                                     double magMax) const {
    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);
    // Clamped, which is what makes a derivative one-sided at the boundary
    // rather than wrong there - see the note below.
    const auto sampleU=[&](int cx,int cy){ return gu.cells[qBound(0,cy,gu.ny-1)*gu.nx+qBound(0,cx,gu.nx-1)]; };
    const auto sampleV=[&](int cx,int cy){ return gv.cells[qBound(0,cy,gv.ny-1)*gv.nx+qBound(0,cx,gv.nx-1)]; };

    const int stride=qMax(1,gu.nx/22);
    const double arrowMax=qMin(f.plotArea.width()/double(gu.nx),
                               f.plotArea.height()/double(gu.ny))*stride*1.6;
    for(int cy=0;cy<gu.ny;cy+=stride){
        for(int cx=0;cx<gu.nx;cx+=stride){
            const double u=sampleU(cx,cy), v=sampleV(cx,cy);
            if(!finite(u)||!finite(v)) continue;
            const double mag=std::hypot(u,v);
            if(!(mag>0.0)) continue;
            const double gx=gu.xLo+(gu.xHi-gu.xLo)*double(cx)/double(qMax(1,gu.nx-1));
            const double gy=gu.yLo+(gu.yHi-gu.yLo)*double(cy)/double(qMax(1,gu.ny-1));
            const QPointF from=toDevice(f,gx,gy);
            const double length=arrowMax*(mag/magMax);
            const QPointF dir(u/mag,-v/mag);
            const QPointF to=from+dir*length;
            QPen pen(colourMapStyled(cmap,spec.style,
                     rampPosition(spec.style,mag,0.0,magMax)));
            pen.setWidthF(qMax(0.5,spec.style.lineWidth));
            p->setPen(pen);
            p->drawLine(from,to);
            // Head as two short strokes; a filled polygon at this size is a
            // blob rather than an arrow.
            const double head=qMin(6.0,length*0.34);
            const double angle=std::atan2(dir.y(),dir.x());
            for(const double turn:{2.6,-2.6}){
                const double a=angle+turn;
                p->drawLine(to,to+QPointF(std::cos(a)*head,std::sin(a)*head));
            }
        }
    }
}

// Which picture this engine is, and the gridding they share. Nothing else: the
// five pictures are the five functions above.
void QtPlotBackend::drawVectorField(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    if(spec.series.size()<4) return;
    const QVector<double>& xs=spec.series.at(0).y;
    const QVector<double>& ys=spec.series.at(1).y;
    const QVector<double>& us=spec.series.at(2).y;
    const QVector<double>& vs=spec.series.at(3).y;
    const int n=qMin(qMin(xs.size(),ys.size()),qMin(us.size(),vs.size()));
    if(n<4) return;

    // Before the grid, not after.
    if(spec.engine==QLatin1String("Feather")){ drawFeatherPlot(p,f,spec); return; }

    // Grid both components onto the same lattice.
    PlotSpec uSpec=spec; uSpec.series={spec.series.at(0),spec.series.at(1),spec.series.at(2)};
    PlotSpec vSpec=spec; vSpec.series={spec.series.at(0),spec.series.at(1),spec.series.at(3)};
    const ValueGrid& gu=cachedGrid(uSpec,GridAggregate::Mean,4,1);
    const ValueGrid& gv=cachedGrid(vSpec,GridAggregate::Mean,4,2);
    if(!gu.valid||!gv.valid) return;

    const QString& engine=spec.engine;
    if(engine==QLatin1String("Divergence Map")||engine==QLatin1String("Vorticity Map")){
        drawDerivedField(p,f,spec,gu,gv,engine==QLatin1String("Vorticity Map"));
        return;
    }

    double magMax=0.0;
    for(int k=0;k<gu.cells.size()&&k<gv.cells.size();++k){
        const double u=gu.cells[k], v=gv.cells[k];
        if(finite(u)&&finite(v)) magMax=qMax(magMax,std::hypot(u,v));
    }
    if(!(magMax>0.0)) return;

    const bool streaming=engine.startsWith(QLatin1String("Stream"))
                       ||engine==QLatin1String("Flow Texture (LIC)")
                       ||engine==QLatin1String("Phase Portrait");

    p->save();
    // An arrow at the edge of the data points outward, and a streamline
    // integrated past the last cell keeps going: without this they are drawn
    // over the axis labels. It also makes an explicit axis limit clip the field
    // rather than leaving part of it outside its own frame.
    p->setClipRect(f.plotArea);
    if(streaming){
        drawStreamlines(p,f,spec,gu,gv,magMax);
        if(engine==QLatin1String("Phase Portrait"))
            drawPhasePortraitPoints(p,f,spec,gu,gv);
    }else{
        drawQuiverArrows(p,f,spec,gu,gv,magMax);
    }
    p->restore();
    // The arrows and the streamlines are both coloured by SPEED, so that is
    // what the bar is a scale of - not the two vector components, which are
    // what was mapped.
    drawColourBar(p,f,spec,0.0,magMax,QStringLiteral("speed"));
}

// Radix-2 FFT, iterative, in place. Written here rather than pulled in because
// it is forty lines and the alternative is a dependency for two engines.
// Sequences are zero-padded to the next power of two, which is standard and
// costs only frequency resolution, not correctness.

// ---------------------------------------------------------------------------
// Composition and hierarchy.
//
// Treemap, sunburst, Venn, word cloud and Sankey all lay out a whole rather
// than plot a coordinate, so none of them uses the axis frame. They share this
// one function because they share the same input shape - a list of labelled
// magnitudes - and differ only in how the area is divided.
// ======================================================================
// Networks
//
// A grid, a pipeline, a route map and a supply chain are all the same object:
// nodes and the links between them. Nothing in the catalogue could draw one -
// a Sankey is a flow through stages, not a graph - so a network arrived as a
// table of pairs and stayed a table of pairs.
//
// Both of these read an edge list: the first three mapped columns are source,
// target and weight, with the weight optional. Node identifiers are numbers,
// because that is what a mapped column can carry.
// ======================================================================

void QtPlotBackend::drawNetwork(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    const EdgeList e=edgesFrom(spec);
    drawFloatingTitle(p,target,spec);
    if(!e.valid) return;
    const int n=e.ids.size();

    // The layout is cached, and computed in a unit square rather than in device
    // coordinates so that a resize reuses it.
    //
    // Measured before caching: 400 nodes took 542 ms per repaint, and a repaint
    // happens on every hover, resize and theme change. The layout depends only
    // on the edges, so recomputing it when nothing about the graph changed was
    // half a second of arithmetic to produce the picture already on screen.
    const quint64 hash=specFingerprint(spec);
    if(!networkCache_.valid||networkCache_.hash!=hash||networkCache_.pos.size()!=n){
        QVector<QPointF> pos(n),disp(n);
        for(int i=0;i<n;++i){
            const double angle=2.0*M_PI*double(i)/double(n);
            pos[i]=QPointF(0.5+0.42*std::cos(angle),0.5+0.42*std::sin(angle));
        }
        // Fruchterman-Reingold, seeded from that circle and run for a fixed
        // number of iterations. Deliberately deterministic: a figure that lays
        // itself out differently every time cannot be exported, compared or
        // published, and a random seed is exactly how that happens.
        //
        // The repulsion step is every pair against every other, so the layout
        // costs iterations x n^2. Past a few hundred nodes that is seconds for a
        // picture nobody can read anyway, so a large graph keeps the circle it
        // was seeded with - the edges still show the structure, and the figure
        // still appears rather than silently not drawing.
        const int iterations=(n<=400)?150:0;
        const double k=std::sqrt(1.0/double(n));
        double temperature=0.10;
        for(int step=0;step<iterations;++step){
            for(int i=0;i<n;++i) disp[i]=QPointF(0,0);
            for(int i=0;i<n;++i){
                for(int j=i+1;j<n;++j){
                    QPointF d=pos[i]-pos[j];
                    double len=std::hypot(d.x(),d.y());
                    if(len<1e-9){
                        d=QPointF(1e-6*double(i+1),1e-6*double(j+1));
                        len=std::hypot(d.x(),d.y());
                    }
                    const QPointF push=d/len*(k*k/len);
                    disp[i]+=push; disp[j]-=push;
                }
            }
            for(int m=0;m<e.from.size();++m){
                const int i=e.from[m],j=e.to[m];
                if(i==j) continue;
                const QPointF d=pos[i]-pos[j];
                const double len=qMax(1e-9,std::hypot(d.x(),d.y()));
                const QPointF pull=d/len*(len*len/k);
                disp[i]-=pull; disp[j]+=pull;
            }
            for(int i=0;i<n;++i){
                const double len=std::hypot(disp[i].x(),disp[i].y());
                if(len>1e-12) pos[i]+=disp[i]/len*qMin(len,temperature);
                pos[i].setX(qBound(0.0,pos[i].x(),1.0));
                pos[i].setY(qBound(0.0,pos[i].y(),1.0));
            }
            temperature*=0.955;
        }
        // Fit what the layout actually used to the whole unit square, so a
        // graph that settled into a corner still fills the figure.
        double xLo=1.0,xHi=0.0,yLo=1.0,yHi=0.0;
        for(const QPointF& q:pos){
            xLo=qMin(xLo,q.x()); xHi=qMax(xHi,q.x());
            yLo=qMin(yLo,q.y()); yHi=qMax(yHi,q.y());
        }
        const double spanX=qMax(1e-6,xHi-xLo),spanY=qMax(1e-6,yHi-yLo);
        for(QPointF& q:pos)
            q=QPointF((q.x()-xLo)/spanX,(q.y()-yLo)/spanY);
        networkCache_.pos=pos;
        networkCache_.hash=hash;
        networkCache_.valid=true;
    }

    const QRectF area=target.adjusted(target.width()*0.12,target.height()*0.12,
                                      -target.width()*0.12,-target.height()*0.12);
    QVector<QPointF> at(n);
    for(int i=0;i<n;++i)
        at[i]=QPointF(area.left()+networkCache_.pos[i].x()*area.width(),
                      area.top()+networkCache_.pos[i].y()*area.height());

    double heaviest=0.0,strongest=0.0;
    for(double w:e.weight) heaviest=qMax(heaviest,w);
    for(double w:e.strength) strongest=qMax(strongest,w);
    if(!(heaviest>0)) heaviest=1.0;
    if(!(strongest>0)) strongest=1.0;

    p->save();
    for(int m=0;m<e.from.size();++m){
        QColor line=spec.style.gridColor;
        line.setAlphaF(0.85);
        QPen pen(line);
        pen.setWidthF(qBound(0.4,3.0*e.weight[m]/heaviest,4.0));
        p->setPen(pen);
        p->drawLine(at[e.from[m]],at[e.to[m]]);
    }
    const QColor node=spec.series.at(0).color;
    p->setPen(QPen(spec.style.foreground,0.8));
    const QFont label=font(spec,spec.style.tickSize);
    p->setFont(label);
    const QFontMetricsF fm(label,p->device());
    for(int i=0;i<n;++i){
        const double radius=qBound(2.5,3.0+6.0*e.strength[i]/strongest,11.0);
        p->setBrush(node);
        p->drawEllipse(at[i],radius,radius);
        // Labels only when they can be read. Forty nodes of overlapping text is
        // less informative than none.
        if(n<=40){
            const QString text=QString::number(e.ids[i],'g',6);
            p->drawText(QRectF(at[i].x()-40,at[i].y()-radius-fm.height()-1,80,fm.height()),
                        Qt::AlignHCenter|Qt::AlignBottom,text);
        }
    }
    p->restore();
}

// A chord diagram: the same edge list read as a matrix of flows between places.
// Each node gets an arc in proportion to everything entering and leaving it,
// and each flow a ribbon between two arcs. It is the one picture that shows an
// origin-destination matrix as a whole rather than as a grid of numbers.
void QtPlotBackend::drawChord(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    const EdgeList e=edgesFrom(spec);
    drawFloatingTitle(p,target,spec);
    if(!e.valid||e.ids.size()>60) return;
    const int n=e.ids.size();

    double grand=0.0;
    for(double w:e.strength) grand+=w;
    if(!(grand>0)||!finite(grand)) return;

    const QPointF centre=target.center();
    const double radius=0.36*qMin(target.width(),target.height());
    const double inner=radius*0.90;
    // A gap between arcs, so two adjacent nodes are two arcs and not one.
    const double gap=qMin(0.03,0.6/double(n));
    const double usable=2.0*M_PI*(1.0-gap*double(n));

    QVector<double> start(n),extent(n);
    double angle=-M_PI/2.0;
    for(int i=0;i<n;++i){
        extent[i]=usable*e.strength[i]/grand;
        start[i]=angle;
        angle+=extent[i]+gap*2.0*M_PI;
    }
    // Where the next ribbon leaving each node begins, so ribbons sit side by
    // side inside their node's arc instead of on top of each other.
    QVector<double> cursor=start;

    p->save();
    const QFont label=font(spec,spec.style.tickSize);
    p->setFont(label);
    for(int m=0;m<e.from.size();++m){
        const int i=e.from[m],j=e.to[m];
        const double share=e.weight[m]/qMax(1e-12,grand)*usable;
        // Dropping the sub-pixel ribbons was tried here and reverted: 5.4x
        // faster at sixty nodes (295 ms to 54 ms) and 16.4% of the pixels
        // changed, because a ribbon that is thin where it meets the ring is
        // wide in the middle where it bundles with its neighbours. The speed
        // was real and so was the different picture.
        const double a0=cursor[i],a1=cursor[i]+share;
        const double b0=cursor[j],b1=cursor[j]+share;
        cursor[i]=a1; cursor[j]=b1;
        const auto at=[centre,inner](double t){
            return QPointF(centre.x()+inner*std::cos(t),centre.y()+inner*std::sin(t));
        };
        QPainterPath ribbon(at(a0));
        ribbon.arcTo(QRectF(centre.x()-inner,centre.y()-inner,inner*2,inner*2),
                     -a0*180.0/M_PI,-(a1-a0)*180.0/M_PI);
        ribbon.quadTo(centre,at(b0));
        ribbon.arcTo(QRectF(centre.x()-inner,centre.y()-inner,inner*2,inner*2),
                     -b0*180.0/M_PI,-(b1-b0)*180.0/M_PI);
        ribbon.quadTo(centre,at(a0));
        // Hue by source, so every flow out of one place reads as one family.
        QColor fill=categoryColour(spec,i,std::fmod(double(i)/double(n)+0.55,1.0),0.55,0.85);
        fill.setAlphaF(0.45);
        p->setBrush(fill);
        p->setPen(Qt::NoPen);
        p->drawPath(ribbon);
    }
    const QFontMetricsF fm(label,p->device());
    for(int i=0;i<n;++i){
        QColor arc=categoryColour(spec,i,std::fmod(double(i)/double(n)+0.55,1.0),0.6,0.95);
        p->setBrush(arc);
        p->setPen(Qt::NoPen);
        QPainterPath band;
        band.moveTo(centre.x()+inner*std::cos(start[i]),centre.y()+inner*std::sin(start[i]));
        band.arcTo(QRectF(centre.x()-inner,centre.y()-inner,inner*2,inner*2),
                   -start[i]*180.0/M_PI,-extent[i]*180.0/M_PI);
        band.arcTo(QRectF(centre.x()-radius,centre.y()-radius,radius*2,radius*2),
                   -(start[i]+extent[i])*180.0/M_PI,extent[i]*180.0/M_PI);
        band.closeSubpath();
        p->drawPath(band);

        const double mid=start[i]+extent[i]*0.5;
        const QPointF where(centre.x()+(radius+6.0)*std::cos(mid),
                            centre.y()+(radius+6.0)*std::sin(mid));
        p->setPen(spec.style.foreground);
        const QString text=QString::number(e.ids[i],'g',6);
        p->drawText(QRectF(where.x()-40,where.y()-fm.height()*0.5,80,fm.height()),
                    Qt::AlignHCenter|Qt::AlignVCenter,text);
    }
    p->restore();
}

// A Smith chart: normalised impedance on the reflection-coefficient plane.
//
// The chart IS its grid. Constant-resistance circles and constant-reactance
// arcs are what turn a dot inside a circle into a reading, so they are drawn
// here rather than assumed to be on the paper underneath - and a match at the
// centre, an open at the right and a short at the left are labelled, because
// those three points are how the chart is oriented.
//
// Two mapped columns: normalised resistance and normalised reactance.
void QtPlotBackend::drawSmith(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    drawFloatingTitle(p,target,spec);
    const double radius=0.42*qMin(target.width(),target.height());
    const QPointF centre=target.center();
    const auto at=[centre,radius](double re,double im){
        return QPointF(centre.x()+re*radius,centre.y()-im*radius);
    };

    p->save();
    QPen grid(spec.style.gridColor); grid.setWidthF(0.7);
    p->setPen(grid);
    p->setBrush(Qt::NoBrush);
    // Constant resistance: circles centred at r/(1+r) with radius 1/(1+r).
    for(const double r:{0.0,0.2,0.5,1.0,2.0,5.0}){
        const double cx=r/(1.0+r),rr=1.0/(1.0+r);
        p->drawEllipse(at(cx,0.0),rr*radius,rr*radius);
    }
    // Constant reactance: arcs centred at 1 + i/x with radius 1/|x|, clipped to
    // the unit circle - outside it they are not part of the chart.
    p->save();
    QPainterPath unit;
    unit.addEllipse(centre,radius,radius);
    p->setClipPath(unit);
    for(const double x:{0.2,0.5,1.0,2.0,5.0}){
        for(const double sign:{1.0,-1.0}){
            const double rr=1.0/std::abs(x);
            p->drawEllipse(at(1.0,sign/x),rr*radius,rr*radius);
        }
    }
    p->restore();
    QPen axis(spec.style.foreground); axis.setWidthF(0.9);
    p->setPen(axis);
    p->drawEllipse(centre,radius,radius);
    p->drawLine(at(-1.0,0.0),at(1.0,0.0));

    const QFont label=font(spec,spec.style.tickSize);
    p->setFont(label);
    const QFontMetricsF fm(label,p->device());
    const auto tag=[&](const QPointF& where,const QString& text,int flags){
        p->drawText(QRectF(where.x()-45,where.y()-fm.height()*0.5,90,fm.height()),flags,text);
    };
    tag(at(0.0,0.0)+QPointF(0,-6),QStringLiteral("match"),Qt::AlignHCenter|Qt::AlignVCenter);
    tag(at(1.0,0.0)+QPointF(-14,-10),QStringLiteral("open"),Qt::AlignRight|Qt::AlignVCenter);
    tag(at(-1.0,0.0)+QPointF(14,-10),QStringLiteral("short"),Qt::AlignLeft|Qt::AlignVCenter);

    if(spec.series.size()>=2){
        const QVector<double>& resistance=spec.series.at(0).y;
        const QVector<double>& reactance=spec.series.at(1).y;
        const int n=qMin(resistance.size(),reactance.size());
        QPen trace(spec.series.at(0).color);
        trace.setWidthF(qMax(1.0,spec.style.lineWidth));
        p->setPen(trace);
        p->setBrush(spec.series.at(0).color);
        QPointF previous;
        bool have=false;
        for(int i=0;i<n;++i){
            if(!finite(resistance[i])||!finite(reactance[i])) continue;
            // Gamma = (z - 1) / (z + 1), with z = r + jx.
            const double ar=resistance[i]-1.0,ai=reactance[i];
            const double br=resistance[i]+1.0,bi=reactance[i];
            const double denom=br*br+bi*bi;
            // finite(denom), not just denom > 0. An impedance near the top of
            // the double range squares to infinity, and infinity over infinity
            // is NaN - which reaches QPainter as a NaN coordinate and is drawn
            // as something undefined. Qt says so out loud
            // ("QPainterPath::arcTo: Adding arc where a parameter is NaN") and
            // the pathological-data sweep is where it said it.
            if(!(denom>1e-12)||!finite(denom)) continue;
            const QPointF here=at((ar*br+ai*bi)/denom,(ai*br-ar*bi)/denom);
            if(!finite(here.x())||!finite(here.y())) continue;
            p->drawEllipse(here,2.6,2.6);
            if(have&&finite(previous.x())&&finite(previous.y())) p->drawLine(previous,here);
            previous=here; have=true;
        }
    }
    p->restore();
}

// ======================================================================
// Skew-T log-P
//
// The first engine in this port that could not be a rewrite. Everything in
// batches 1 to 12 transformed its data and handed the result to geometry that
// already existed; this one cannot, because its COORDINATE SYSTEM is the
// content. Temperature runs diagonally, pressure runs logarithmically and
// downward, and the whole point of the arrangement is that four families of
// thermodynamic curve - isotherms, dry adiabats, saturated adiabats and lines
// of constant mixing ratio - all become readable at once on a single sheet.
// Nothing in a Cartesian frame can express that, so the frame, the chrome, the
// labels and the data are all drawn here.
//
// The skew is chosen so the isotherms come out at 45 degrees WHATEVER SHAPE
// the plot box is. A fixed skew constant is what most implementations use, and
// it makes the isotherms lie down when the window is wide and stand up when it
// is tall - at which point the diagram stops being a Skew-T, because the
// separation between a dry adiabat and an isotherm is the thing a forecaster
// reads slope differences against.

// ------------------------------------------------- the sounding diagram, in parts
//
// It was 546 lines and complexity 92 in one body, and it already said where its
// seams were: every section was introduced by a `// ----` banner. What held it
// together was a scope of a dozen locals and three lambdas, so a reader in the
// parcel arithmetic at the bottom could not be sure the `at` they were reading
// was the `at` they thought.
//
// `SoundingFrame` is the part that was implicit: the box, the fixed pressure
// range, the temperature range fitted to the data, and the one transform every
// curve on the diagram goes through. With the transform a member rather than a
// captured lambda, each section can be a function that takes the frame - and
// then the couplings are in the signatures instead of in a shared scope.
//
// FOUR CHARTS, ONE TRANSFORM. That is the property the whole file turns on and
// the reason `at()` is the only way anything is placed here: a tephigram is a
// rotation, so on it an isobar slopes and an isotherm is diagonal, and every
// bug this function has had was something positioned by an assumed direction
// instead of by asking the transform.
namespace {

struct SoundingFrame {
    SoundingChart chart=SoundingChart::SkewT;
    QRectF box;
    // Fixed to the atmosphere rather than fitted to the data. A sounding that
    // stops at 400 hPa is a sounding that stopped, and stretching the axis to
    // it would make two soundings from the same station uncomparable - which is
    // the one thing this diagram exists to allow.
    double lowest=1050.0,highest=100.0;
    double coolest=-40.0,warmest=40.0;
    double heightInLogs=1.0;
    // The skew, in degrees of temperature across the full height. Set from the
    // box's own aspect so an isotherm is at 45 degrees on the page.
    double skew=0.0;
    // The sampled extent of the chart's own transform, for the three charts
    // whose axes are not temperature and pressure directly.
    double chartLeft=0.0,chartRight=1.0,chartLow=0.0,chartHigh=1.0;

    double heightOf(double hPa) const {        // 0 at the bottom, 1 at the top
        return std::log(lowest/qMax(1e-6,hPa))/heightInLogs;
    }
    QPointF at(double celsius,double hPa) const {
        if(chart==SoundingChart::SkewT){
            const double up=heightOf(hPa);
            const double across=(celsius+skew*up-coolest)/(warmest-coolest);
            return QPointF(box.left()+across*box.width(),box.bottom()-up*box.height());
        }
        const QPointF where=placeOn(chart,celsius,hPa);
        const double across=(where.x()-chartLeft)/qMax(1e-12,chartRight-chartLeft);
        const double up=(where.y()-chartLow)/qMax(1e-12,chartHigh-chartLow);
        return QPointF(box.left()+across*box.width(),box.bottom()-up*box.height());
    }
};

// The pressure levels the chrome and the isobars both label. One list, because
// a tick without its line, or a line without its number, is what two lists
// drift into.
const double kSoundingIsobars[]={1000.0,925.0,850.0,700.0,600.0,500.0,400.0,
                                 300.0,250.0,200.0,150.0,100.0};

SoundingFrame soundingFrame(const QRectF& target,const PlotSpec& spec,
                            SoundingChart chart,const QFontMetricsF& fm,
                            bool hasTitle){
    SoundingFrame frame;
    frame.chart=chart;

    // Room for the pressure labels on the left, the temperature labels below,
    // and the mixing-ratio labels that run up the right-hand side.
    const double left=target.left()+fm.horizontalAdvance(QStringLiteral("1000"))+14.0;
    // TWO ROWS ARE DRAWN BELOW THE FRAME - the tick numbers, and then the note
    // saying what the axis is - and only one row and fourteen pixels were
    // reserved for them. The note therefore ran past the bottom of the canvas
    // on all four charts and had its descenders cut off; the tephigram's, being
    // the longest sentence, put the most ink on the last two rows of pixels.
    const double bottom=target.bottom()-fm.height()*2.4-10.0;
    const double right=target.right()-fm.horizontalAdvance(QStringLiteral("00"))-10.0;
    // Two rows above the frame: the title (in its own larger font) and then the
    // readout strip. Reserving two tick-heights for both was not enough and the
    // readout was painted over the title.
    const double top=target.top()+(hasTitle?fm.height()*3.2:fm.height()*1.4);
    frame.box=QRectF(left,top,qMax(40.0,right-left),qMax(40.0,bottom-top));

    double coolest=0.0,warmest=0.0;
    bool haveTemperature=false;
    if(spec.series.size()>=2){
        const QVector<double>& pressure=spec.series.at(0).y;
        for(int c=1;c<qMin(3,int(spec.series.size()));++c){
            const QVector<double>& values=spec.series.at(c).y;
            for(int i=0;i<qMin(pressure.size(),values.size());++i){
                if(!finite(pressure[i])||!finite(values[i])) continue;
                if(!(pressure[i]>0.0)) continue;
                if(!haveTemperature){ coolest=warmest=values[i]; haveTemperature=true; }
                coolest=qMin(coolest,values[i]); warmest=qMax(warmest,values[i]);
            }
        }
    }
    if(!haveTemperature){ coolest=-40.0; warmest=40.0; }
    // Widened and rounded outward to a multiple of ten, so the isotherm labels
    // land on round numbers and the profile is never against the edge.
    coolest=std::floor((coolest-12.0)/10.0)*10.0;
    warmest=std::ceil((warmest+12.0)/10.0)*10.0;
    if(warmest-coolest<50.0) warmest=coolest+50.0;
    frame.coolest=coolest; frame.warmest=warmest;

    frame.heightInLogs=std::log(frame.lowest/frame.highest);
    frame.skew=(warmest-coolest)*frame.box.height()/qMax(1.0,frame.box.width());

    // The other three charts place a point by their own transform, and where
    // that lands is not known in advance - a tephigram's axes are a rotation of
    // temperature and log potential temperature, so neither runs along the page.
    // The extent is found by SAMPLING the whole temperature-pressure domain,
    // which works for any transform and cannot be got wrong by assuming which
    // way something runs.
    if(chart!=SoundingChart::SkewT){
        bool first=true;
        for(int i=0;i<=24;++i){
            const double celsius=coolest+(warmest-coolest)*double(i)/24.0;
            for(int k=0;k<=24;++k){
                const double hPa=frame.lowest
                                *std::pow(frame.highest/frame.lowest,double(k)/24.0);
                const QPointF where=placeOn(chart,celsius,hPa);
                if(first){
                    frame.chartLeft=frame.chartRight=where.x();
                    frame.chartLow=frame.chartHigh=where.y();
                    first=false;
                }
                frame.chartLeft=qMin(frame.chartLeft,where.x());
                frame.chartRight=qMax(frame.chartRight,where.x());
                frame.chartLow=qMin(frame.chartLow,where.y());
                frame.chartHigh=qMax(frame.chartHigh,where.y());
            }
        }
    }
    return frame;
}

// The four families of background curve, drawn faintest first so the ones a
// forecaster reads against are on top of the ones they do not.
void drawSoundingBackground(QPainter* p,const SoundingFrame& frame,
                            const PlotSpec& spec,const QFont& chipFont){
    const QRectF& box=frame.box;
    const double lowest=frame.lowest,highest=frame.highest;

    p->save();
    p->setClipRect(box.adjusted(-0.5,-0.5,0.5,0.5));

    // Blended toward the foreground rather than set to fixed colours, so the
    // families stay distinguishable in a dark theme as well as a light one.
    const auto blend=[&](const QColor& toward,double amount){
        const QColor base=spec.style.gridColor;
        return QColor::fromRgbF(base.redF()+(toward.redF()-base.redF())*amount,
                                base.greenF()+(toward.greenF()-base.greenF())*amount,
                                base.blueF()+(toward.blueF()-base.blueF())*amount);
    };
    const QColor faint=spec.style.gridColor;                 // dry adiabats
    const QColor moist=blend(spec.style.positive,0.75);      // saturated adiabats
    const QColor humid=blend(spec.style.warning,0.65);       // mixing ratio

    // Lines of constant saturation mixing ratio: where air of this humidity
    // would become saturated. Dashed, because they are the only family that is
    // not a path any parcel takes - they are a labelling of moisture content.
    {
        QPen pen(humid); pen.setWidthF(0.7);
        pen.setDashPattern({4,4}); pen.setCapStyle(Qt::FlatCap);
        p->setPen(pen);
        p->setFont(chipFont);
        for(const double gramsPerKilo:{0.4,1.0,2.0,4.0,7.0,10.0,16.0,24.0,32.0}){
            QPolygonF path;
            for(double hPa=lowest;hPa>=highest;hPa*=0.97){
                const double w=gramsPerKilo/1000.0;
                const double vapour=w*hPa/(0.622+w);
                path.append(frame.at(dewpointFrom(vapour),hPa));
            }
            p->drawPolyline(path);
            // Labelled, because an unlabelled family of curves is decoration.
            // At 600 hPa, which is high enough to be clear of the profile in
            // most soundings and low enough to still be on the page.
            const double w=gramsPerKilo/1000.0;
            const QPointF where=frame.at(dewpointFrom(w*600.0/(0.622+w)),600.0);
            if(box.contains(where)){
                const QRectF chip(where.x()-13.0,where.y()-12.0,26.0,11.0);
                p->fillRect(chip,spec.style.background);
                p->setPen(humid);
                p->drawText(chip,Qt::AlignCenter,QString::number(gramsPerKilo,'g',2));
                p->setPen(pen);
            }
        }
    }

    // Dry adiabats: the path of an unsaturated parcel. Curved on this diagram,
    // which is the point - the angle between one of these and an isotherm is
    // what stability is read from.
    {
        QPen pen(faint); pen.setWidthF(0.7);
        p->setPen(pen);
        for(double theta=233.15;theta<=473.15;theta+=10.0){
            QPolygonF path;
            for(double hPa=lowest;hPa>=highest;hPa*=0.97)
                path.append(frame.at(dryAdiabatAt(theta,hPa)-273.15,hPa));
            p->drawPolyline(path);
        }
    }

    // Saturated adiabats, integrated downward in pressure from the top of each
    // curve. Solid and slightly stronger, because a parcel that has condensed
    // follows one of these and the difference between it and the environment
    // is the energy available.
    {
        QPen pen(moist); pen.setWidthF(0.8);
        p->setPen(pen);
        // Tabulated once. The adiabats depend only on the fixed pressure range
        // and not on the data, so building them on every repaint would be
        // sixty bisections a point for a picture that never changes.
        struct Adiabat { double thetaE; QVector<QPair<double,double>> path; };
        static const QVector<Adiabat> kAdiabats=[]{
            QVector<Adiabat> out;
            for(double surface=-20.0;surface<=40.0;surface+=5.0){
                Adiabat curve;
                curve.thetaE=equivalentPotential(surface,1050.0);
                for(double hPa=1050.0;hPa>=100.0;hPa*=0.97)
                    curve.path.append(qMakePair(hPa,
                        saturatedTemperatureAt(curve.thetaE,hPa)));
                out.append(curve);
            }
            return out;
        }();
        for(const Adiabat& curve:kAdiabats){
            QPolygonF path;
            for(const QPair<double,double>& point:curve.path){
                if(point.first>lowest||point.first<highest) continue;
                path.append(frame.at(point.second,point.first));
            }
            if(path.size()>=2) p->drawPolyline(path);
        }
    }

    // Isobars. Horizontal on three of the four charts, where the frame implies
    // them - and curved on a tephigram, where nothing else says where a
    // pressure is. Drawn through the transform in every case rather than as
    // horizontal rules, so the one chart that needs them is not a special case.
    {
        QPen pen(faint); pen.setWidthF(0.7);
        p->setPen(pen);
        for(const double hPa:kSoundingIsobars){
            if(hPa>lowest||hPa<highest) continue;
            QPolygonF path;
            for(int i=0;i<=24;++i)
                path.append(frame.at(frame.coolest
                                    +(frame.warmest-frame.coolest)*double(i)/24.0,hPa));
            p->drawPolyline(path);
        }
    }

    // Isotherms last of the background, and in the foreground colour at low
    // weight, because every other family is read as an angle AGAINST these.
    {
        QColor isotherm=spec.style.foreground; isotherm.setAlphaF(0.35);
        QPen pen(isotherm); pen.setWidthF(0.7);
        p->setPen(pen);
        const double first=std::ceil(frame.coolest/10.0)*10.0;
        const auto drawIsotherm=[&](double celsius){
            QPolygonF path;
            for(int i=0;i<=16;++i)
                path.append(frame.at(celsius,
                                     lowest*std::pow(highest/lowest,double(i)/16.0)));
            p->drawPolyline(path);
        };
        for(double celsius=first-frame.skew;celsius<=frame.warmest;celsius+=10.0)
            drawIsotherm(celsius);
        // Zero is where ice matters, so it is drawn again in a way that can be
        // picked out at a glance.
        QPen freezing(spec.style.foreground); freezing.setWidthF(1.1);
        freezing.setDashPattern({6,4});
        p->setPen(freezing);
        drawIsotherm(0.0);
    }
    p->restore();
}

// Chrome. Pressure up the left in hectopascals, temperature along the bottom,
// and the frame.
//
// Both label families are positioned by the TRANSFORM rather than by an assumed
// direction: a tephigram's isobars slope and its isotherms are diagonal, so a
// pressure label pinned to a fixed height and a temperature label pinned to the
// bottom edge would both be in the wrong place.
void drawSoundingChrome(QPainter* p,const QRectF& target,const SoundingFrame& frame,
                        const PlotSpec& spec,const QFont& tickFont){
    const QRectF& box=frame.box;
    const SoundingChart chart=frame.chart;
    const QFontMetricsF fm(tickFont,p->device());

    p->save();
    p->setFont(tickFont);
    QPen axis(spec.style.foreground); axis.setWidthF(0.9);
    p->setPen(axis);
    p->setBrush(Qt::NoBrush);
    p->drawRect(box);

    QRectF previousChip;
    for(const double hPa:kSoundingIsobars){
        if(hPa>frame.lowest||hPa<frame.highest) continue;
        // WHERE THE ISOBAR ACTUALLY IS, which is not the same question on all
        // four charts and was being answered as though it were.
        //
        // On the first three an isobar is a level line all the way to the left
        // edge of the frame, so a tick at `box.left()` is on it. A tephigram is
        // a rotation: its isobars SLOPE, and the left edge of the frame is a
        // place most of them never reach. Its twelve pressure labels were
        // consequently stacked down the left margin in a column no isobar
        // passed through, stopping two thirds of the way down a frame they are
        // meant to span - twelve numbers, none of them against its own line.
        //
        // The anchor is stated per chart rather than derived, because deriving
        // it is what went wrong at the first attempt: `at(coolest,hPa)` looks
        // like the left end of the isobar and is, on an emagram and a Stuve -
        // but a Skew-T applies its skew inside `at`, so there the cool end
        // climbs to the right with height and the labels walked off across the
        // plot. Asking each chart what shape its isobars are cannot make that
        // mistake.
        const QPointF end=(chart==SoundingChart::Tephigram)
                          ? frame.at(frame.coolest,hPa)
                          : QPointF(box.left(),frame.at(frame.coolest,hPa).y());
        if(end.y()<box.top()-1.0||end.y()>box.bottom()+1.0) continue;
        if(end.x()<box.left()-1.0||end.x()>box.right()+1.0) continue;
        p->drawLine(end+QPointF(-4.0,0.0),end);
        const double labelWidth=box.left()-target.left()-6.0;
        QRectF chip(end.x()-6.0-labelWidth,end.y()-fm.height()*0.5,
                    labelWidth,fm.height());
        // A LABEL THAT SITS INSIDE THE FRAME HAS TO STAY INSIDE IT. The
        // topmost isobar reaches the top of the box, so a number centred on it
        // was drawn half over the frame line and into the readout strip above.
        // Outside the frame - which is where the other three charts put all
        // twelve - the margin is empty and the overhang is the old behaviour,
        // so this only moves the ones that need moving.
        if(chip.right()>box.left()){
            if(chip.top()<box.top())       chip.moveTop(box.top());
            if(chip.bottom()>box.bottom()) chip.moveBottom(box.bottom());
        }
        // And it must not be painted over the last one. Isobars crowd together
        // toward the surface, which on a rotated chart brings 1000, 925 and 850
        // within a few pixels of each other; the lower pressure is the one that
        // keeps its place, because it is drawn first and its tick is already
        // down.
        if(!previousChip.isNull()&&chip.intersects(previousChip)) continue;
        previousChip=chip;
        // Cleared behind for the same reason the isotherm numbers are: on a
        // tephigram this lands inside the frame, over whatever passes through.
        if(chart==SoundingChart::Tephigram) p->fillRect(chip,spec.style.background);
        p->drawText(chip,Qt::AlignRight|Qt::AlignVCenter,QString::number(int(hPa)));
    }
    for(double celsius=std::ceil(frame.coolest/10.0)*10.0;
        celsius<=frame.warmest;celsius+=10.0){
        // At the foot of the isotherm itself, not on a fixed bottom row. The
        // two coincide on the three charts whose isotherms reach the bottom
        // edge; on a tephigram they do not, and the earlier version drew the
        // tick on the diagonal foot while putting its number below the frame,
        // so ticks and numbers disagreed by most of the width of the plot.
        const QPointF foot=frame.at(celsius,frame.lowest);
        if(foot.x()<box.left()-1.0||foot.x()>box.right()+1.0) continue;
        if(foot.y()<box.top()-1.0||foot.y()>box.bottom()+1.0) continue;
        p->drawLine(foot,foot+QPointF(0,4.0));
        const QRectF chip(foot.x()-24.0,foot.y()+5.0,48.0,fm.height());
        // Cleared behind, because on a tephigram this lands inside the frame on
        // top of whatever background family passes through.
        if(chart==SoundingChart::Tephigram) p->fillRect(chip,spec.style.background);
        p->drawText(chip,Qt::AlignHCenter|Qt::AlignTop,QString::number(int(celsius)));
    }
    const QString axisNote=
         (chart==SoundingChart::Emagram)?QStringLiteral("temperature (C), isotherms vertical")
        :(chart==SoundingChart::Stuve)  ?QStringLiteral("temperature (C), pressure as p^0.286 so dry adiabats are straight")
        :(chart==SoundingChart::Tephigram)?QStringLiteral("temperature (C) against log potential temperature, rotated; area is energy")
                                        :QStringLiteral("temperature (C), isotherms skewed 45 degrees");
    // Pinned to the bottom of the canvas rather than hung off the frame, so
    // that the space it needs is the space that was reserved for it.
    p->drawText(QRectF(box.left(),target.bottom()-fm.height()-2.0,
                       box.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignTop,axisNote);
    p->restore();
}

// One reported level of the sounding, in the order the diagram reads it.
struct SoundingLevel { double hPa,celsius,dew; };

// The reported profile, cleaned and ordered.
//
// Sorted by DECREASING pressure, which is upward. A sounding file that happens
// to be stored the other way round would otherwise draw a profile that runs the
// wrong way and a parcel lifted downward.
QVector<SoundingLevel> soundingLevels(const PlotSpec& spec){
    QVector<SoundingLevel> sounding;
    if(spec.series.size()<2) return sounding;
    const QVector<double>& pressure=spec.series.at(0).y;
    const QVector<double>& temperature=spec.series.at(1).y;
    const bool haveDew=spec.series.size()>=3;
    const QVector<double>& dewpoint=haveDew?spec.series.at(2).y:temperature;
    for(int i=0;i<qMin(pressure.size(),temperature.size());++i){
        if(!finite(pressure[i])||!finite(temperature[i])||!(pressure[i]>0.0)) continue;
        const double dew=(haveDew&&i<dewpoint.size()&&finite(dewpoint[i]))
                         ?qMin(dewpoint[i],temperature[i]):temperature[i];
        sounding.append({pressure[i],temperature[i],dew});
    }
    std::sort(sounding.begin(),sounding.end(),
              [](const SoundingLevel& a,const SoundingLevel& b){ return a.hPa>b.hPa; });
    return sounding;
}

// What the diagram is read FOR. Returned rather than printed, because the
// readout strip and the drawn parcel path are two presentations of one
// calculation and they must not be able to disagree.
struct ParcelResult {
    Condensation lcl;
    QPolygonF path;                    // the lifted parcel, in device coordinates
    double available=0.0;              // CAPE, J/kg
    double inhibition=0.0;             // CIN, J/kg
    double freeConvection=std::numeric_limits<double>::quiet_NaN();
    double equilibrium=std::numeric_limits<double>::quiet_NaN();
};

ParcelResult liftSurfaceParcel(const SoundingFrame& frame,
                               const QVector<SoundingLevel>& sounding){
    ParcelResult out;
    if(sounding.isEmpty()) return out;
    const SoundingLevel& surface=sounding.first();
    out.lcl=liftingCondensation(surface.celsius+273.15,
                                surface.dew+273.15,surface.hPa);
    if(!out.lcl.ok) return out;

    // Integrated on the SOUNDING's own levels rather than on a fixed pressure
    // step, so the areas are bounded by the reported environment instead of by
    // an interpolation of it.
    const double theta=(surface.celsius+273.15)
                      *std::pow(1000.0/surface.hPa,0.2854);
    const double parcelThetaE=equivalentPotential(out.lcl.kelvin-273.15,out.lcl.hPa);
    const double gasConstant=287.058;

    // ---- Pass one: the parcel's temperature at every reported level, and the
    // buoyancy it implies. NOTHING is accumulated here. Which layers count as
    // inhibition and which as available energy cannot be decided while walking
    // up the column, because both are defined relative to the free convection
    // and equilibrium levels and those are properties of the whole profile.
    //
    // The first version of this did accumulate as it walked - every negative
    // layer into CIN, every positive one into CAPE, all the way to the top of
    // the sounding. That is wrong above the equilibrium level, where the parcel
    // is tens of degrees colder than an air mass it has already stopped rising
    // through: on the self-test sounding it added about -4000 J/kg of
    // "inhibition" from the stratosphere and reported CIN -3214 where the real
    // figure is a few tens. It survived the constructed Skew-T test because that
    // sounding was built with CIN exactly zero and stopped at 300 hPa with no
    // equilibrium level, so the offending region did not exist in it.
    struct Rung { double hPa; double diff; };
    QVector<Rung> rungs;
    rungs.reserve(sounding.size());
    for(const SoundingLevel& level:sounding){
        // Above the condensation level the parcel is on the saturated adiabat
        // through the LCL, so its temperature at any pressure is one bisection
        // rather than an integration from the level below. That also means the
        // drawn path and the computed energy cannot disagree with the background
        // adiabats they are read against, because all three are the same curve.
        const double parcelC=(level.hPa>=out.lcl.hPa)
            ?dryAdiabatAt(theta,level.hPa)-273.15
            :saturatedTemperatureAt(parcelThetaE,level.hPa);
        if(!finite(parcelC)) break;
        out.path.append(frame.at(parcelC,level.hPa));
        rungs.append({level.hPa,parcelC-level.celsius});
    }

    // ---- Pass two: cut the column into pieces of a single buoyancy sign,
    // splitting any layer that changes sign at the crossing itself. The
    // crossing is interpolated in log pressure, which is the measure the
    // integral is taken in, so the two halves of a split layer add back to the
    // whole layer exactly.
    struct Piece { double bot,top,mean; };
    QVector<Piece> pieces;
    for(int i=1;i<rungs.size();++i){
        const double lower=rungs[i-1].hPa,upper=rungs[i].hPa;
        if(!(upper<lower)) continue;
        const double d0=rungs[i-1].diff,d1=rungs[i].diff;
        if((d0<0.0)!=(d1<0.0)&&std::abs(d1-d0)>1e-12){
            const double f=d0/(d0-d1);
            const double cut=std::exp(std::log(lower)
                                      +f*(std::log(upper)-std::log(lower)));
            pieces.append({lower,cut,0.5*d0});
            pieces.append({cut,upper,0.5*d1});
        }else{
            pieces.append({lower,upper,0.5*(d0+d1)});
        }
    }

    // The free convection level is the bottom of the lowest positive piece; the
    // equilibrium level is the top of the highest one. Between them the parcel
    // rises, so all positive area there is available energy. Below the free
    // convection level the negative area is what has to be supplied to get the
    // parcel there, which is the inhibition. Above the equilibrium level
    // nothing is counted at all.
    int firstPositive=-1,lastPositive=-1;
    for(int i=0;i<pieces.size();++i)
        if(pieces[i].mean>0.0){
            if(firstPositive<0) firstPositive=i;
            lastPositive=i;
        }
    // With no positive piece anywhere the parcel is never buoyant, so there is
    // no level of free convection to reach and neither quantity is defined.
    // Reporting the whole column's negative area as "CIN" in that case would be
    // a large, meaningless number; both stay zero and the readout says why.
    if(firstPositive>=0){
        out.freeConvection=pieces[firstPositive].bot;
        out.equilibrium=pieces[lastPositive].top;
        for(int i=firstPositive;i<=lastPositive;++i)
            if(pieces[i].mean>0.0)
                out.available+=gasConstant*pieces[i].mean
                              *std::log(pieces[i].bot/pieces[i].top);
        for(int i=0;i<firstPositive;++i)
            if(pieces[i].mean<0.0)
                out.inhibition+=gasConstant*pieces[i].mean
                               *std::log(pieces[i].bot/pieces[i].top);
    }
    return out;
}

// The profile, the dewpoint, the lifted parcel and its condensation level.
void drawSoundingProfile(QPainter* p,const SoundingFrame& frame,const PlotSpec& spec,
                         const QVector<SoundingLevel>& sounding,
                         const ParcelResult& parcel){
    p->save();
    p->setClipRect(frame.box.adjusted(-0.5,-0.5,0.5,0.5));
    const auto trace=[&](bool useDew,const QColor& colour){
        QPen pen(colour); pen.setWidthF(qMax(1.8,spec.style.lineWidth*1.4));
        pen.setJoinStyle(Qt::RoundJoin);
        p->setPen(pen);
        QPolygonF path;
        for(const SoundingLevel& level:sounding)
            path.append(frame.at(useDew?level.dew:level.celsius,level.hPa));
        p->drawPolyline(path);
    };
    if(spec.series.size()>=3) trace(true,spec.series.at(2).color);
    trace(false,spec.series.at(1).color);

    if(parcel.lcl.ok){
        QPen pen(spec.style.warning);
        pen.setWidthF(qMax(1.2,spec.style.lineWidth));
        pen.setDashPattern({7,4});
        p->setPen(pen);
        p->drawPolyline(parcel.path);

        const QPointF mark=frame.at(parcel.lcl.kelvin-273.15,parcel.lcl.hPa);
        p->setPen(QPen(spec.style.danger,1.4));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(mark,4.5,4.5);
    }
    p->restore();
}

// What the diagram is read for, stated rather than left to be measured off the
// page - and which colour is which, because two unlabelled lines on a Skew-T is
// the one ambiguity a forecaster cannot resolve from the picture: a dry sounding
// and a saturated one differ only in which line is where.
void drawSoundingReadout(QPainter* p,const SoundingFrame& frame,const PlotSpec& spec,
                         const ParcelResult& parcel,const QFont& tickFont){
    const QRectF& box=frame.box;
    const QFontMetricsF fm(tickFont,p->device());

    p->save();
    p->setFont(tickFont);
    p->setPen(spec.style.foreground);
    QStringList found;
    if(parcel.lcl.ok)
        found.append(QStringLiteral("LCL %1 hPa at %2 C")
                         .arg(parcel.lcl.hPa,0,'f',0)
                         .arg(parcel.lcl.kelvin-273.15,0,'f',1));
    if(finite(parcel.freeConvection)){
        found.append(QStringLiteral("CAPE %1 J/kg").arg(parcel.available,0,'f',0));
        found.append(QStringLiteral("CIN %1 J/kg").arg(parcel.inhibition,0,'f',0));
        found.append(QStringLiteral("LFC %1 hPa").arg(parcel.freeConvection,0,'f',0));
        found.append(QStringLiteral("EL %1 hPa").arg(parcel.equilibrium,0,'f',0));
    }else if(parcel.lcl.ok){
        found.append(QStringLiteral(
            "no LFC - the parcel is nowhere buoyant, so neither CAPE nor CIN "
            "is defined"));
    }
    // Temperature, not virtual temperature. The difference is a few per cent of
    // CAPE in a moist boundary layer and it is named here rather than implied,
    // because a CAPE quoted without saying which is not comparable with one
    // from anywhere else.
    found.append(QStringLiteral("from the surface parcel, temperature not virtual"));
    const QString readout=found.join(QStringLiteral("   "));
    // On its own strip below the frame rather than inside it. Sitting inside,
    // it landed on whichever background curves happened to pass through the top
    // left corner, and on a cold sounding that is where the profile is.
    const QRectF strip(box.left(),box.top()-fm.height()*1.15,
                       box.width(),fm.height()*1.05);
    p->fillRect(strip,spec.style.background);
    p->drawText(strip,Qt::AlignLeft|Qt::AlignVCenter,readout);

    const double swatch=fm.height()*0.6;
    const int last=qMin(2,int(spec.series.size()-1));
    const auto nameOf=[&](int c){
        return spec.series.at(c).label.isEmpty()
            ?(c==1?QStringLiteral("temperature"):QStringLiteral("dewpoint"))
            :spec.series.at(c).label;
    };
    double widest=0.0;
    for(int c=1;c<=last;++c) widest=qMax(widest,fm.horizontalAdvance(nameOf(c)));
    if(last>=1&&widest>0.0){
        const QRectF panel(box.right()-widest-swatch-16.0,box.top()+6.0,
                           widest+swatch+12.0,fm.height()*double(last)+6.0);
        p->fillRect(panel,spec.style.background);
        p->setPen(QPen(spec.style.gridColor,0.7));
        p->drawRect(panel);
        for(int c=1;c<=last;++c){
            const QRectF row(panel.left()+4.0,panel.top()+3.0+fm.height()*double(c-1),
                             panel.width()-8.0,fm.height());
            QPen pen(spec.series.at(c).color); pen.setWidthF(2.0);
            p->setPen(pen);
            p->drawLine(QPointF(row.left(),row.center().y()),
                        QPointF(row.left()+swatch,row.center().y()));
            p->setPen(spec.style.foreground);
            p->drawText(QRectF(row.left()+swatch+4.0,row.top(),
                               row.width()-swatch-4.0,row.height()),
                        Qt::AlignLeft|Qt::AlignVCenter,nameOf(c));
        }
    }
    p->restore();
}

} // namespace

// Three mapped columns: pressure, temperature and dewpoint. Pressure first
// because it is the independent variable of a sounding even though it is drawn
// on the vertical axis - the profile is a function of height, not of
// temperature, and two temperatures share the one pressure.
//
// The order below is the order of the picture: background, chrome, profile,
// readout. Nothing here computes anything - see the parts above.
void QtPlotBackend::drawSounding(QPainter* p,const QRectF& target,const PlotSpec& spec,
                                 int chartKind) const {
    const SoundingChart chart=static_cast<SoundingChart>(chartKind);
    drawFloatingTitle(p,target,spec);

    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    const SoundingFrame frame=
        soundingFrame(target,spec,chart,fm,!spec.title.isEmpty());

    drawSoundingBackground(p,frame,spec,font(spec,qMax(6.0,spec.style.tickSize-1.5)));
    drawSoundingChrome(p,target,frame,spec,tickFont);

    if(spec.series.size()<2) return;
    const QVector<SoundingLevel> sounding=soundingLevels(spec);
    if(sounding.isEmpty()) return;

    const ParcelResult parcel=liftSurfaceParcel(frame,sounding);
    drawSoundingProfile(p,frame,spec,sounding,parcel);
    drawSoundingReadout(p,frame,spec,parcel,tickFont);
}

// ======================================================================
// Piper diagram
//
// Two ternary triangles and a diamond, and the diamond is the whole point. The
// left triangle is the cations, the right is the anions, and every sample is
// projected UP from both into the rhombus between them - so a single mark in
// the diamond carries the full major-ion chemistry of that water, and samples
// that plot together there are waters of the same type whatever their total
// dissolved solids.
//
// This replaces an alias. "Piper Diagram" was routed to Ternary Scatter and
// drew one triangle from three columns, which is not a Piper diagram: it is a
// third of one, under a name that promises the rest. A wrong picture under a
// right name is the failure this port has spent its whole length removing, and
// it had been in the catalogue since the original two hundred and three.
//
// Six mapped columns, in milliequivalents: calcium, magnesium, sodium plus
// potassium, bicarbonate plus carbonate, sulphate, chloride. Milliequivalents
// rather than milligrams because the diagram is about CHARGE balance - a
// milligram of sodium and a milligram of calcium are not comparable quantities
// here, and normalising milligrams would silently plot the wrong point.
void QtPlotBackend::drawPiper(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    drawFloatingTitle(p,target,spec);

    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    p->setFont(tickFont);

    // The construction, in its own units: two unit triangles with a half-unit
    // gap, and the diamond that falls out of the projection above them.
    const double height=std::sqrt(3.0)/2.0;
    const double gap=0.5;
    const double leftBase=0.0,rightBase=1.0+gap;
    // The construction's real extent, not a guess at it. The top of the diamond
    // is the projection of the two apexes, and that lands at two and a half
    // triangle-heights: the apex contributes one, and the projection parameter
    // for two apexes a distance (1 + gap) apart contributes another one and a
    // half. Reserving three heights left a visible band of empty page above the
    // diamond and shrank everything to fit under it.
    const double spanX=2.0+gap,spanY=2.5*height;

    // Fitted to the target with one scale for both axes; a Piper diagram drawn
    // with different horizontal and vertical scales is not made of equilateral
    // triangles any more, and every angle in it stops meaning what it means.
    //
    // The area is worked out by SUBTRACTING what else has to fit rather than by
    // one margin all round. The first version anchored the construction to the
    // bottom of the target and left a band of empty page above it, and its
    // summary line landed on the vertex labels of the two lower triangles.
    const double titleSpace=spec.title.isEmpty()?fm.height()*0.4:fm.height()*2.0;
    const double summarySpace=fm.height()*1.4;
    const double sideSpace=fm.height()*3.4;      // room for the corner labels
    const double vertexSpace=fm.height()*1.3;    // above the apexes, below the bases
    const QRectF area(target.left()+sideSpace,target.top()+titleSpace+vertexSpace,
                      qMax(20.0,target.width()-2.0*sideSpace),
                      qMax(20.0,target.height()-titleSpace-summarySpace
                                -2.0*vertexSpace));
    const double scale=qMin(area.width()/spanX,area.height()/spanY);
    const double originX=area.center().x()-scale*spanX*0.5;
    const double originY=area.center().y()+scale*spanY*0.5;
    const auto at=[&](double x,double y){
        return QPointF(originX+x*scale,originY-y*scale);
    };

    // A point in a triangle whose left vertex is at `base`. The three fractions
    // are of the left vertex, the right vertex and the apex.
    const auto inTriangle=[&](double base,double leftShare,double rightShare,
                              double apexShare){
        Q_UNUSED(leftShare);
        return QPointF(base+rightShare+apexShare*0.5,apexShare*height);
    };

    // ---- The frame: two triangles, the diamond, and the ten per cent grid.
    p->save();
    QPen grid(spec.style.gridColor); grid.setWidthF(0.6);
    QPen edge(spec.style.foreground); edge.setWidthF(1.0);

    const auto triangleAt=[&](double base){
        p->setPen(grid);
        // Grid lines parallel to each of the three sides, every ten per cent.
        for(int step=1;step<10;++step){
            const double f=double(step)/10.0;
            // Parallel to the base.
            p->drawLine(at(base+f*0.5,f*height),at(base+1.0-f*0.5,f*height));
            // Parallel to the left side.
            p->drawLine(at(base+f,0.0),at(base+f+(1.0-f)*0.5,(1.0-f)*height));
            // Parallel to the right side.
            p->drawLine(at(base+f*0.5,f*height),at(base+f,0.0));
        }
        p->setPen(edge);
        QPolygonF outline;
        outline<<at(base,0.0)<<at(base+1.0,0.0)<<at(base+0.5,height)<<at(base,0.0);
        p->drawPolyline(outline);
    };
    triangleAt(leftBase);
    triangleAt(rightBase);

    // The diamond, from the same projection the data uses rather than from
    // typed-in corner coordinates - so if the projection is wrong the frame is
    // wrong in the same way and the error cannot hide.
    const auto project=[&](const QPointF& cation,const QPointF& anion){
        const double t=(anion.x()-cation.x())+(anion.y()-cation.y())/std::sqrt(3.0);
        return QPointF(cation.x()+0.5*t,cation.y()+height*t);
    };
    const QPointF calcium=inTriangle(leftBase,1,0,0);
    const QPointF sodium=inTriangle(leftBase,0,1,0);
    const QPointF magnesium=inTriangle(leftBase,0,0,1);
    const QPointF bicarbonate=inTriangle(rightBase,1,0,0);
    const QPointF chloride=inTriangle(rightBase,0,1,0);
    const QPointF sulphate=inTriangle(rightBase,0,0,1);
    const QPointF westCorner=project(calcium,bicarbonate);
    const QPointF northCorner=project(magnesium,sulphate);
    const QPointF eastCorner=project(sodium,chloride);
    const QPointF southCorner=project(sodium,bicarbonate);
    {
        p->setPen(grid);
        for(int step=1;step<10;++step){
            const double f=double(step)/10.0;
            p->drawLine(at(westCorner.x()+f*(northCorner.x()-westCorner.x()),
                           westCorner.y()+f*(northCorner.y()-westCorner.y())),
                        at(southCorner.x()+f*(eastCorner.x()-southCorner.x()),
                           southCorner.y()+f*(eastCorner.y()-southCorner.y())));
            p->drawLine(at(westCorner.x()+f*(southCorner.x()-westCorner.x()),
                           westCorner.y()+f*(southCorner.y()-westCorner.y())),
                        at(northCorner.x()+f*(eastCorner.x()-northCorner.x()),
                           northCorner.y()+f*(eastCorner.y()-northCorner.y())));
        }
        p->setPen(edge);
        QPolygonF diamond;
        diamond<<at(westCorner.x(),westCorner.y())<<at(northCorner.x(),northCorner.y())
               <<at(eastCorner.x(),eastCorner.y())<<at(southCorner.x(),southCorner.y())
               <<at(westCorner.x(),westCorner.y());
        p->drawPolyline(diamond);
    }

    // ---- Vertex labels. Without them the two triangles are interchangeable
    // and the diagram cannot be read at all.
    // The box is laid out so that a right-aligned label ENDS at the point it
    // names and a left-aligned one starts there. A box merely centred on the
    // point, which is what this was, puts a right-aligned label half a box to
    // the RIGHT of its vertex - so every label on the left-hand side of the
    // construction sat inside the figure naming the wrong thing.
    const auto tag=[&](const QPointF& where,const QString& text,int flags,
                       double dx,double dy){
        const QPointF screen=at(where.x(),where.y());
        const double width=110.0;
        double left=screen.x()-width*0.5+dx;
        if(flags&Qt::AlignRight)     left=screen.x()-width+dx;
        else if(flags&Qt::AlignLeft) left=screen.x()+dx;
        p->drawText(QRectF(left,screen.y()-fm.height()*0.5+dy,width,fm.height()),
                    flags,text);
    };
    p->setPen(spec.style.foreground);
    const double pad=fm.height()*0.7;
    tag(calcium,QStringLiteral("Ca"),Qt::AlignRight|Qt::AlignVCenter,-6.0,pad*0.6);
    tag(sodium,QStringLiteral("Na+K"),Qt::AlignLeft|Qt::AlignVCenter,6.0,pad*0.6);
    tag(magnesium,QStringLiteral("Mg"),Qt::AlignHCenter|Qt::AlignVCenter,0.0,-pad);
    tag(bicarbonate,QStringLiteral("HCO3"),Qt::AlignRight|Qt::AlignVCenter,-6.0,pad*0.6);
    tag(chloride,QStringLiteral("Cl"),Qt::AlignLeft|Qt::AlignVCenter,6.0,pad*0.6);
    tag(sulphate,QStringLiteral("SO4"),Qt::AlignHCenter|Qt::AlignVCenter,0.0,-pad);
    // The diamond's corners carry SUMS, not individual ions. Calcium with
    // chloride and magnesium with sulphate land on the same corner, because
    // both are a hundred per cent alkaline earths with a hundred per cent
    // strong acids - which is exactly why the two triangles are drawn as well,
    // and why labelling this corner "Ca + Cl" would be wrong.
    tag(westCorner,QStringLiteral("Ca+Mg / HCO3"),Qt::AlignRight|Qt::AlignVCenter,-8.0,0.0);
    tag(eastCorner,QStringLiteral("Na+K / SO4+Cl"),Qt::AlignLeft|Qt::AlignVCenter,8.0,0.0);
    p->restore();

    if(spec.series.size()<6){
        p->save();
        p->setPen(spec.style.foreground);
        p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.2,
                           target.width(),fm.height()),
                    Qt::AlignHCenter|Qt::AlignVCenter,
                    QStringLiteral("needs six columns in meq: Ca, Mg, Na+K, HCO3, SO4, Cl"));
        p->restore();
        return;
    }

    // ---- The samples.
    const QVector<double>& ca=spec.series.at(0).y;
    const QVector<double>& mg=spec.series.at(1).y;
    const QVector<double>& na=spec.series.at(2).y;
    const QVector<double>& hco3=spec.series.at(3).y;
    const QVector<double>& so4=spec.series.at(4).y;
    const QVector<double>& cl=spec.series.at(5).y;
    int rows=ca.size();
    for(const PlotSeries& s:spec.series) rows=qMin(rows,int(s.y.size()));

    p->save();
    int drawn=0,earthWeak=0,alkaliStrong=0,earthStrong=0,alkaliWeak=0;
    for(int i=0;i<rows;++i){
        if(!finite(ca[i])||!finite(mg[i])||!finite(na[i])) continue;
        if(!finite(hco3[i])||!finite(so4[i])||!finite(cl[i])) continue;
        const double cations=std::abs(ca[i])+std::abs(mg[i])+std::abs(na[i]);
        const double anions=std::abs(hco3[i])+std::abs(so4[i])+std::abs(cl[i]);
        if(!(cations>0.0)||!(anions>0.0)) continue;
        const double fCa=std::abs(ca[i])/cations,fMg=std::abs(mg[i])/cations;
        const double fNa=std::abs(na[i])/cations;
        const double fHco3=std::abs(hco3[i])/anions,fSo4=std::abs(so4[i])/anions;
        const double fCl=std::abs(cl[i])/anions;
        const QPointF cationPoint=inTriangle(leftBase,fCa,fNa,fMg);
        const QPointF anionPoint=inTriangle(rightBase,fHco3,fCl,fSo4);
        const QPointF diamondPoint=project(cationPoint,anionPoint);

        // Which quadrant of the diamond, which is the one reading of it that
        // every author agrees on. The named sub-fields inside each quadrant are
        // not: several incompatible schemes are in circulation, so the quadrant
        // is reported and the sub-field is not invented.
        const bool earths=(fCa+fMg)>0.5;
        const bool strong=(fSo4+fCl)>0.5;
        if(earths&&!strong) ++earthWeak;
        else if(!earths&&strong) ++alkaliStrong;
        else if(earths&&strong) ++earthStrong;
        else ++alkaliWeak;

        const QColor colour=spec.series.at(0).color;
        QPen pen(colour); pen.setWidthF(1.0);
        p->setPen(pen);
        p->setBrush(QColor(colour.red(),colour.green(),colour.blue(),150));
        const double size=qMax(2.2,spec.style.lineWidth*2.0);
        p->drawEllipse(at(cationPoint.x(),cationPoint.y()),size,size);
        p->drawEllipse(at(anionPoint.x(),anionPoint.y()),size,size);
        p->drawEllipse(at(diamondPoint.x(),diamondPoint.y()),size*1.25,size*1.25);
        ++drawn;
    }
    p->restore();

    p->save();
    p->setPen(spec.style.foreground);
    const QString summary=QStringLiteral(
        "%1 samples   Ca-HCO3 %2   Na-Cl %3   Ca-SO4/Cl %4   Na-HCO3 %5")
        .arg(drawn).arg(earthWeak).arg(alkaliStrong).arg(earthStrong).arg(alkaliWeak);
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.2,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,summary);
    p->restore();
}

// ======================================================================
// Stiff diagram
//
// The same six columns as the Piper, drawn so that one water is one SHAPE. A
// Piper tells you what type a water is; a Stiff tells you them apart at a
// glance and, laid out in a row, shows a trend along a flow path or down a
// well. They are complementary rather than alternatives, which is why the six
// columns feed both.
//
// Cations left of the centre line and anions right, three rows: sodium plus
// potassium, calcium, magnesium against chloride, bicarbonate, sulphate. In
// milliequivalents, and unlike the Piper the LENGTHS are absolute rather than
// normalised - a dilute water is a narrow shape and a concentrated one is a
// wide shape of the same form, and losing that would throw away half of what
// the diagram is for.
void QtPlotBackend::drawStiff(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    drawFloatingTitle(p,target,spec);

    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    p->setFont(tickFont);

    if(spec.series.size()<6){
        p->save();
        p->setPen(spec.style.foreground);
        p->drawText(target,Qt::AlignCenter,
                    QStringLiteral("needs six columns in meq: Ca, Mg, Na+K, HCO3, SO4, Cl"));
        p->restore();
        return;
    }

    const QVector<double>& ca=spec.series.at(0).y;
    const QVector<double>& mg=spec.series.at(1).y;
    const QVector<double>& na=spec.series.at(2).y;
    const QVector<double>& hco3=spec.series.at(3).y;
    const QVector<double>& so4=spec.series.at(4).y;
    const QVector<double>& cl=spec.series.at(5).y;
    int rows=ca.size();
    for(const PlotSeries& s:spec.series) rows=qMin(rows,int(s.y.size()));
    if(rows<1) return;

    // One scale for every shape on the page. Drawing each to its own width
    // would make them all the same size, which is precisely the comparison the
    // diagram exists to support - so the widest single ion sets the scale and
    // the rest are drawn against it.
    double widest=0.0;
    for(int i=0;i<rows;++i){
        for(const PlotSeries& s:spec.series)
            if(i<s.y.size()&&finite(s.y[i])) widest=qMax(widest,std::abs(s.y[i]));
    }
    if(!(widest>0.0)) return;

    // Laid out in a grid, as near square as the count allows, and read across
    // the rows. Small multiples rather than one shape per figure: a single
    // Stiff diagram is a picture of one sample, and nobody has ever wanted only
    // one.
    const int across=qMax(1,int(std::ceil(std::sqrt(double(rows)))));
    const int down=(rows+across-1)/across;
    const double titleRoom=spec.title.isEmpty()?0.0:fm.height()*2.0;
    const QRectF field(target.left()+fm.height()*0.5,target.top()+titleRoom,
                       target.width()-fm.height(),
                       target.height()-titleRoom-fm.height()*2.4);
    if(field.width()<20.0||field.height()<20.0) return;
    const double cellW=field.width()/double(across);
    const double cellH=field.height()/double(down);

    const QStringList leftNames{QStringLiteral("Na+K"),QStringLiteral("Ca"),
                                QStringLiteral("Mg")};
    const QStringList rightNames{QStringLiteral("Cl"),QStringLiteral("HCO3"),
                                 QStringLiteral("SO4")};
    // Whether there is room to name the ions on every shape. On a grid of
    // twenty they would overlap into a grey smear, so they go on the first
    // cell only and the rest are read against it.
    const double nameWidth=fm.horizontalAdvance(QStringLiteral("HCO3"))+6.0;

    for(int i=0;i<rows;++i){
        const double values[3][2]={{na[i],cl[i]},{ca[i],hco3[i]},{mg[i],so4[i]}};
        bool usable=true;
        for(int r=0;r<3;++r)
            for(int c=0;c<2;++c) if(!finite(values[r][c])) usable=false;
        if(!usable) continue;

        const QRectF cell(field.left()+cellW*double(i%across),
                          field.top()+cellH*double(i/across),cellW,cellH);
        const QRectF inner=cell.adjusted(nameWidth,fm.height()*0.9,
                                         -nameWidth,-fm.height()*1.1);
        if(inner.width()<12.0||inner.height()<12.0) continue;
        const double centre=inner.center().x();
        const double half=inner.width()*0.5;
        const double rowGap=inner.height()/3.0;
        const auto atRow=[&](int r){ return inner.top()+rowGap*(double(r)+0.5); };

        // The axis: the centre line the two halves are measured from, and the
        // three rows it crosses. Without it the polygon is a shape with no
        // origin and its width cannot be read.
        p->save();
        p->setPen(QPen(spec.style.gridColor,0.7));
        for(int r=0;r<3;++r)
            p->drawLine(QPointF(inner.left(),atRow(r)),QPointF(inner.right(),atRow(r)));
        p->setPen(QPen(spec.style.foreground,0.9));
        p->drawLine(QPointF(centre,atRow(0)),QPointF(centre,atRow(2)));

        QPolygonF shape;
        for(int r=0;r<3;++r)
            shape<<QPointF(centre-half*std::abs(values[r][0])/widest,atRow(r));
        for(int r=2;r>=0;--r)
            shape<<QPointF(centre+half*std::abs(values[r][1])/widest,atRow(r));
        const QColor colour=spec.series.at(0).color;
        p->setPen(QPen(colour,qMax(1.0,spec.style.lineWidth)));
        p->setBrush(QColor(colour.red(),colour.green(),colour.blue(),110));
        p->drawPolygon(shape);

        // The total, which is the one number the shape's width stands for and
        // the thing a reader would otherwise measure off the page.
        double total=0.0;
        for(int r=0;r<3;++r) total+=std::abs(values[r][0])+std::abs(values[r][1]);
        p->setPen(spec.style.foreground);
        p->drawText(QRectF(cell.left(),cell.bottom()-fm.height(),cell.width(),fm.height()),
                    Qt::AlignHCenter|Qt::AlignVCenter,
                    QStringLiteral("%1  %2 meq")
                        .arg(i+1).arg(total,0,'g',3));
        if(i==0&&nameWidth*2.0<cellW*0.7){
            p->setPen(spec.style.gridColor.darker(160));
            for(int r=0;r<3;++r){
                p->drawText(QRectF(cell.left(),atRow(r)-fm.height()*0.5,
                                   nameWidth-3.0,fm.height()),
                            Qt::AlignRight|Qt::AlignVCenter,leftNames.at(r));
                p->drawText(QRectF(inner.right()+3.0,atRow(r)-fm.height()*0.5,
                                   nameWidth-3.0,fm.height()),
                            Qt::AlignLeft|Qt::AlignVCenter,rightNames.at(r));
            }
        }
        p->restore();
    }

    p->save();
    p->setPen(spec.style.foreground);
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.2,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,
                QStringLiteral("cations left, anions right; one scale for all "
                               "shapes, widest ion %1 meq")
                    .arg(widest,0,'g',3));
    p->restore();
}

// ======================================================================
// Durov diagram
//
// The third reading of the same six columns, and the one that answers a
// question the other two cannot. A Piper's diamond tells you a water's type but
// not where it sits between two end members; a Stiff tells two waters apart but
// says nothing about mixing. A Durov projects both triangles into a SQUARE, and
// in a square the mixing line between two waters is a straight line - so a
// sequence of samples along a flow path falls on one, and departures from it
// are the reactions.
//
// The cation triangle sits above the square and projects straight down; the
// anion triangle sits to the left and projects straight across. Every sample
// therefore appears three times, and the two projections are what put it in the
// square - the same construction discipline as the Piper, where the frame is
// drawn from the projection the data uses so a wrong projection cannot hide.
//
// Six mapped columns in milliequivalents, the same order as the Piper and the
// Stiff: Ca, Mg, Na+K, HCO3, SO4, Cl.
void QtPlotBackend::drawDurov(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    drawFloatingTitle(p,target,spec);

    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    p->setFont(tickFont);

    const double height=std::sqrt(3.0)/2.0;
    const double spanX=1.0+height,spanY=1.0+height;
    const double margin=fm.height()*2.4;
    const double usableW=qMax(20.0,target.width()-2.0*margin);
    const double usableH=qMax(20.0,target.height()-2.0*margin
                              -(spec.title.isEmpty()?0.0:fm.height()*2.0));
    const double scale=qMin(usableW/spanX,usableH/spanY);
    // The origin is the square's bottom left corner; the anion triangle runs
    // into negative x from there and the cation triangle above y = 1.
    const double originX=target.center().x()-scale*spanX*0.5+scale*height;
    const double originY=target.bottom()-margin*0.7;
    const auto at=[&](double x,double y){
        return QPointF(originX+x*scale,originY-y*scale);
    };

    // The cation triangle: base along the top of the square from Ca on the left
    // to Na+K on the right, Mg at the apex. Its x is what falls into the square.
    const auto cationAt=[&](double fCa,double fNa,double fMg){
        Q_UNUSED(fCa);
        return QPointF(fNa+fMg*0.5,1.0+fMg*height);
    };
    // The anion triangle, the same triangle turned a quarter turn so that its
    // base runs UP the left side of the square - HCO3 at the bottom, Cl at the
    // top, SO4 at the apex. Its y is what falls into the square.
    const auto anionAt=[&](double fHco3,double fCl,double fSo4){
        Q_UNUSED(fHco3);
        return QPointF(-fSo4*height,fCl+fSo4*0.5);
    };

    p->save();
    QPen grid(spec.style.gridColor); grid.setWidthF(0.6);
    QPen edge(spec.style.foreground); edge.setWidthF(1.0);

    // The square, gridded every ten per cent. The grid is what makes it a
    // Durov rather than two triangles beside a box: a mixing line is read
    // against it.
    p->setPen(grid);
    for(int step=1;step<10;++step){
        const double f=double(step)/10.0;
        p->drawLine(at(f,0.0),at(f,1.0));
        p->drawLine(at(0.0,f),at(1.0,f));
    }
    p->setPen(edge);
    p->drawPolyline(QPolygonF()<<at(0,0)<<at(1,0)<<at(1,1)<<at(0,1)<<at(0,0));

    // The two triangles, drawn from the same two functions the samples use.
    const auto triangle=[&](bool cations){
        p->setPen(grid);
        for(int step=1;step<10;++step){
            const double f=double(step)/10.0;
            const auto place=[&](double a,double b,double c){
                return cations?cationAt(a,b,c):anionAt(a,b,c);
            };
            // The three families of grid line, each holding one fraction fixed.
            const QPointF p1=place(1.0-f,f,0.0),p2=place(1.0-f,0.0,f);
            const QPointF p3=place(0.0,1.0-f,f),p4=place(f,1.0-f,0.0);
            const QPointF p5=place(0.0,f,1.0-f),p6=place(f,0.0,1.0-f);
            p->drawLine(at(p1.x(),p1.y()),at(p2.x(),p2.y()));
            p->drawLine(at(p3.x(),p3.y()),at(p4.x(),p4.y()));
            p->drawLine(at(p5.x(),p5.y()),at(p6.x(),p6.y()));
        }
        p->setPen(edge);
        const QPointF a=cations?cationAt(1,0,0):anionAt(1,0,0);
        const QPointF b=cations?cationAt(0,1,0):anionAt(0,1,0);
        const QPointF c=cations?cationAt(0,0,1):anionAt(0,0,1);
        p->drawPolyline(QPolygonF()<<at(a.x(),a.y())<<at(b.x(),b.y())
                                   <<at(c.x(),c.y())<<at(a.x(),a.y()));
    };
    triangle(true);
    triangle(false);

    // The box is laid out so that a right-aligned label ENDS at the point it
    // names and a left-aligned one starts there. A box merely centred on the
    // point, which is what this was, puts a right-aligned label half a box to
    // the RIGHT of its vertex - so every label on the left-hand side of the
    // construction sat inside the figure naming the wrong thing.
    const auto tag=[&](const QPointF& where,const QString& text,int flags,
                       double dx,double dy){
        const QPointF screen=at(where.x(),where.y());
        const double width=110.0;
        double left=screen.x()-width*0.5+dx;
        if(flags&Qt::AlignRight)     left=screen.x()-width+dx;
        else if(flags&Qt::AlignLeft) left=screen.x()+dx;
        p->drawText(QRectF(left,screen.y()-fm.height()*0.5+dy,width,fm.height()),
                    flags,text);
    };
    p->setPen(spec.style.foreground);
    const double pad=fm.height()*0.75;
    // Ca and Cl land on the SAME point - the square's top left corner is the
    // cation triangle's left vertex and the anion triangle's top vertex at
    // once - so they are separated by hand rather than by the geometry: the
    // cation name goes up into the triangle it belongs to and the anion name
    // down beside its own. Likewise HCO3 is lifted clear of the summary line
    // that runs under the frame.
    tag(cationAt(1,0,0),QStringLiteral("Ca"),Qt::AlignRight|Qt::AlignVCenter,-6.0,-pad*0.9);
    tag(cationAt(0,1,0),QStringLiteral("Na+K"),Qt::AlignLeft|Qt::AlignVCenter,6.0,0.0);
    tag(cationAt(0,0,1),QStringLiteral("Mg"),Qt::AlignHCenter|Qt::AlignVCenter,0.0,-pad);
    tag(anionAt(1,0,0),QStringLiteral("HCO3"),Qt::AlignRight|Qt::AlignVCenter,-6.0,-pad*0.5);
    tag(anionAt(0,1,0),QStringLiteral("Cl"),Qt::AlignRight|Qt::AlignVCenter,-6.0,pad*0.9);
    tag(anionAt(0,0,1),QStringLiteral("SO4"),Qt::AlignRight|Qt::AlignVCenter,-6.0,0.0);
    p->restore();

    if(spec.series.size()<6){
        p->save();
        p->setPen(spec.style.foreground);
        p->drawText(target,Qt::AlignHCenter|Qt::AlignBottom,
                    QStringLiteral("needs six columns in meq: Ca, Mg, Na+K, HCO3, SO4, Cl"));
        p->restore();
        return;
    }

    const QVector<double>& ca=spec.series.at(0).y;
    const QVector<double>& mg=spec.series.at(1).y;
    const QVector<double>& na=spec.series.at(2).y;
    const QVector<double>& hco3=spec.series.at(3).y;
    const QVector<double>& so4=spec.series.at(4).y;
    const QVector<double>& cl=spec.series.at(5).y;
    int rows=ca.size();
    for(const PlotSeries& s:spec.series) rows=qMin(rows,int(s.y.size()));

    p->save();
    int drawn=0;
    double sumX=0.0,sumY=0.0;
    for(int i=0;i<rows;++i){
        if(!finite(ca[i])||!finite(mg[i])||!finite(na[i])) continue;
        if(!finite(hco3[i])||!finite(so4[i])||!finite(cl[i])) continue;
        const double cations=std::abs(ca[i])+std::abs(mg[i])+std::abs(na[i]);
        const double anions=std::abs(hco3[i])+std::abs(so4[i])+std::abs(cl[i]);
        if(!(cations>0.0)||!(anions>0.0)) continue;
        const QPointF cationPoint=cationAt(std::abs(ca[i])/cations,
                                           std::abs(na[i])/cations,
                                           std::abs(mg[i])/cations);
        const QPointF anionPoint=anionAt(std::abs(hco3[i])/anions,
                                         std::abs(cl[i])/anions,
                                         std::abs(so4[i])/anions);
        // The square point is the two projections meeting, which is the whole
        // construction: x from the cation triangle above, y from the anion
        // triangle beside.
        const QPointF square(cationPoint.x(),anionPoint.y());

        const QColor colour=spec.series.at(0).color;
        p->setPen(QPen(colour,1.0));
        p->setBrush(QColor(colour.red(),colour.green(),colour.blue(),150));
        const double size=qMax(2.2,spec.style.lineWidth*2.0);
        p->drawEllipse(at(cationPoint.x(),cationPoint.y()),size,size);
        p->drawEllipse(at(anionPoint.x(),anionPoint.y()),size,size);
        p->drawEllipse(at(square.x(),square.y()),size*1.25,size*1.25);
        // The projection lines. They are the difference between a Durov and
        // three unrelated plots sharing a page: without them a reader cannot
        // see which mark in the square belongs to which mark in the triangles.
        QPen thread(colour); thread.setWidthF(0.5);
        thread.setDashPattern({2,3});
        p->setPen(thread);
        p->setBrush(Qt::NoBrush);
        p->drawLine(at(cationPoint.x(),cationPoint.y()),at(square.x(),square.y()));
        p->drawLine(at(anionPoint.x(),anionPoint.y()),at(square.x(),square.y()));
        sumX+=square.x(); sumY+=square.y();
        ++drawn;
    }
    p->restore();

    p->save();
    p->setPen(spec.style.foreground);
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.2,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,
                drawn>0
                ?QStringLiteral("%1 samples; square centroid %2 across, %3 up - "
                                "a mixing line between two waters is straight here")
                     .arg(drawn).arg(sumX/double(drawn),0,'f',3)
                     .arg(sumY/double(drawn),0,'f',3)
                :QStringLiteral("no complete samples"));
    p->restore();
}

// ======================================================================
// Arc diagram
//
// The same edge list as the network graph, laid out on a line instead of in a
// plane. A force-directed layout puts nodes wherever the springs settle, which
// is unrepeatable across data sets and impossible to align with anything else;
// an arc diagram fixes the nodes in an order the reader chooses and spends the
// second dimension on the edges. That makes it the right picture when the order
// MEANS something - position along a sequence, time, rank - and when two graphs
// have to be compared node for node.
//
// Three mapped columns: source, target and an optional weight, exactly as the
// network graph reads them. Node identifiers are numbers, and they are sorted
// by identifier rather than by degree, because a number the user supplied is an
// order they chose and reordering it would discard it.
void QtPlotBackend::drawArc(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    const EdgeList e=edgesFrom(spec);
    drawFloatingTitle(p,target,spec);
    if(!e.valid){
        p->save();
        p->setPen(spec.style.foreground);
        p->drawText(target,Qt::AlignCenter,
                    QStringLiteral("needs an edge list: source, target, and an optional weight"));
        p->restore();
        return;
    }

    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    p->setFont(tickFont);
    const int n=e.ids.size();

    QVector<int> order(n);
    for(int i=0;i<n;++i) order[i]=i;
    std::sort(order.begin(),order.end(),
              [&e](int a,int b){ return e.ids[a]<e.ids[b]; });
    QVector<int> slot(n,0);
    for(int i=0;i<n;++i) slot[order[i]]=i;

    const double titleRoom=spec.title.isEmpty()?0.0:fm.height()*2.0;
    const QRectF field(target.left()+fm.height(),target.top()+titleRoom,
                       target.width()-fm.height()*2.0,
                       target.height()-titleRoom-fm.height()*1.6);
    if(field.width()<40.0||field.height()<40.0) return;
    // The baseline sits low, because the arcs go above it and the tallest is
    // half the width of the longest edge - so the space above is what the
    // picture needs and the space below is only labels.
    const double baseline=field.bottom()-fm.height()*1.4;
    const double step=(n>1)?field.width()/double(n-1):0.0;
    const auto seatX=[&](int node){
        return (n>1)?field.left()+step*double(slot[node])
                    :field.center().x();
    };

    double heaviest=0.0;
    for(double w:e.weight) heaviest=qMax(heaviest,w);

    // Arcs first, so the nodes sit on top of them and a node is never hidden
    // under the bundle of edges arriving at it.
    p->save();
    p->setBrush(Qt::NoBrush);
    const double ceiling=baseline-field.top();
    // One height scale for the whole picture, set so the longest edge just
    // reaches the top. Capping each arc at the ceiling instead - the obvious
    // thing, and the first thing here - flattens every long edge to the SAME
    // height and destroys the ordering that the heights carry; scaling them
    // together keeps every ratio and uses the page, which on a graph whose
    // longest edge is short is most of the page.
    double longest=0.0;
    for(int k=0;k<e.from.size();++k)
        longest=qMax(longest,std::abs(seatX(e.to[k])-seatX(e.from[k]))*0.5);
    const double heightScale=(longest>0.0)?ceiling*0.98/longest:1.0;
    for(int k=0;k<e.from.size();++k){
        const double x0=seatX(e.from[k]),x1=seatX(e.to[k]);
        if(qFuzzyCompare(x0,x1)) continue;              // a self-loop has no arc
        const double lo=qMin(x0,x1),hi=qMax(x0,x1);
        const double rise=(hi-lo)*0.5*heightScale;
        QPainterPath arc;
        arc.moveTo(lo,baseline);
        // A cubic whose control points are pushed out by 4/3, which is the
        // standard approximation to a half ellipse and is within a thousandth
        // of it - close enough that no eye distinguishes it from the circle.
        arc.cubicTo(QPointF(lo,baseline-rise*4.0/3.0),
                    QPointF(hi,baseline-rise*4.0/3.0),
                    QPointF(hi,baseline));
        QColor colour=spec.series.at(0).color;
        colour.setAlphaF(0.55);
        QPen pen(colour);
        pen.setWidthF(heaviest>0.0
            ?qBound(0.6,0.6+2.4*e.weight[k]/heaviest,3.0)
            :1.0);
        pen.setCapStyle(Qt::RoundCap);
        p->setPen(pen);
        p->drawPath(arc);
    }
    p->restore();

    // The baseline and the nodes. Node size is degree-weighted, which is the
    // one property of a node that an arc diagram cannot show by position.
    p->save();
    p->setPen(QPen(spec.style.foreground,0.9));
    p->drawLine(QPointF(field.left(),baseline),QPointF(field.right(),baseline));
    double busiest=0.0;
    for(double s:e.strength) busiest=qMax(busiest,s);
    for(int i=0;i<n;++i){
        const double size=(busiest>0.0)
            ?qBound(2.0,2.0+3.5*std::sqrt(e.strength[i]/busiest),6.0):3.0;
        p->setPen(Qt::NoPen);
        p->setBrush(spec.style.foreground);
        p->drawEllipse(QPointF(seatX(i),baseline),size,size);
    }
    // Identifiers only where they fit. Labelling every node of a hundred-node
    // graph produces a black band, and a band is not a label.
    if(step>fm.horizontalAdvance(QStringLiteral("0000"))+4.0){
        p->setPen(spec.style.foreground);
        for(int i=0;i<n;++i)
            p->drawText(QRectF(seatX(i)-step*0.5,baseline+6.0,step,fm.height()),
                        Qt::AlignHCenter|Qt::AlignTop,
                        QString::number(e.ids[i],'g',6));
    }
    p->restore();

    p->save();
    p->setPen(spec.style.foreground);
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.2,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,
                QStringLiteral("%1 nodes, %2 edges, in order of identifier; "
                               "arc height is proportional to span, thickness "
                               "to weight")
                    .arg(n).arg(e.from.size()));
    p->restore();
}

// ======================================================================
// A hierarchy from three mapped columns: node identifier, parent identifier,
// and one value per node. Identifiers are numbers because that is what a mapped
// column carries - the same convention the edge list uses for the network
// engines, and for the same reason.
//
// What the value MEANS is left to the caller: the icicle reads it as the amount
// the node holds itself, the cladogram as the length of the branch leading to
// it. The shape of the tree is the same object either way, and reading it twice
// would be two chances to disagree about what a cycle or a missing parent
// means.

// ======================================================================
// Alluvial diagram
//
// A flow through stages, from the same edge list the network engines read. The
// catalogue had a Sankey, and it drew ONE split - a flat list of magnitudes
// fanning out from a single source - which is a third of what a Sankey is for.
// A budget that passes through three departments, a cohort that moves between
// four states, a material that goes through five processes: all of those are
// edge lists, and none of them could be drawn.
//
// The stages are computed rather than asked for, as the LONGEST path from any
// node with nothing flowing into it. Longest and not shortest: a node fed by
// both a one-step and a three-step path belongs after both of them, and placing
// it by the shortest would make one of its inputs run backwards - which on a
// flow diagram means the opposite of what the data says.
//
// Three mapped columns: source, target, and an optional weight.
// ======================================================================
// Plot matrix
//
// Every mapped column against every other: scatters below the diagonal, the
// distribution of each variable ON the diagonal, and the correlation for each
// pair above it. That layout is not decoration - it is what makes the figure
// scannable. A reader sweeps the upper triangle for a number that stands out
// and then looks at the panel opposite it to see what shape produced it.
//
// The catalogue carried this entry for a long time and drew a single scatter
// of the first pair under it, on the reasoning that one frame cannot hold n^2
// figures. One FRAME cannot; one canvas can, and the marginal-scatter painter
// was already subdividing the plot area to prove it. Each panel here gets its
// own rectangle and its own range, because a matrix whose panels shared one
// range would be a matrix of one variable's units.
// ======================================================================
void QtPlotBackend::drawPlotMatrix(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    const int k=qMin(int(spec.series.size()),6);
    if(k<2) return;

    drawFloatingTitle(p,target,spec);
    const QFont tickFont=font(spec,qMax(6.0,spec.style.tickSize*0.85));
    const QFontMetricsF fm(tickFont,p->device());
    p->save();
    p->setFont(tickFont);

    // Each variable's own range, measured once. A panel is drawn against the
    // range of the two variables in it and nothing else.
    struct Span { double lo,hi; bool ok; };
    QVector<Span> span(k);
    for(int i=0;i<k;++i){
        double lo=std::numeric_limits<double>::infinity(),hi=-lo;
        for(double v:spec.series.at(i).y) if(finite(v)){ lo=qMin(lo,v); hi=qMax(hi,v); }
        if(!finite(lo)||!finite(hi)) { span[i]={0.0,1.0,false}; continue; }
        if(qFuzzyCompare(lo,hi)){ const double pad=qMax(0.5,std::abs(lo)*0.05); lo-=pad; hi+=pad; }
        span[i]={lo,hi,true};
    }

    const double titleRoom=spec.title.isEmpty()?6.0:fm.height()*2.4;
    // Room at the bottom for the caption drawn at the end of this function.
    // Without it the last row of panels was drawn under the sentence
    // explaining what the panels are.
    const double captionRoom=fm.height()*1.6;
    const QRectF grid=target.adjusted(10.0,titleRoom,-10.0,-captionRoom);
    if(grid.width()<40.0||grid.height()<40.0){ p->restore(); return; }
    const double cellW=grid.width()/double(k);
    const double cellH=grid.height()/double(k);

    const QColor ink=spec.style.foreground;
    QColor faint=spec.style.gridColor; faint.setAlphaF(0.9);

    for(int row=0;row<k;++row){
        for(int col=0;col<k;++col){
            // Inset, so neighbouring panels have a gap between them rather
            // than a shared edge that reads as one long box.
            const QRectF cell=QRectF(grid.left()+col*cellW,grid.top()+row*cellH,
                                     cellW,cellH).adjusted(2.0,2.0,-2.0,-2.0);
            p->setPen(QPen(faint,0.6));
            p->setBrush(Qt::NoBrush);
            p->drawRect(cell);
            if(cell.width()<8.0||cell.height()<8.0) continue;

            // The diagonal: the variable's own distribution, and its name.
            if(row==col){
                if(span[row].ok){
                    constexpr int kBins=16;
                    QVector<double> counts(kBins,0.0);
                    double peak=0.0;
                    for(double v:spec.series.at(row).y){
                        if(!finite(v)) continue;
                        const int b=qBound(0,int((v-span[row].lo)
                                                 /qMax(1e-300,span[row].hi-span[row].lo)*kBins),
                                           kBins-1);
                        peak=qMax(peak,counts[b]+=1.0);
                    }
                    if(peak>0.0){
                        QColor fill=spec.series.at(row).color; fill.setAlphaF(0.45);
                        p->setPen(Qt::NoPen); p->setBrush(fill);
                        const double bw=cell.width()/double(kBins);
                        for(int b=0;b<kBins;++b){
                            if(!(counts[b]>0.0)) continue;
                            const double h=cell.height()*0.72*(counts[b]/peak);
                            p->drawRect(QRectF(cell.left()+b*bw,cell.bottom()-h,bw,h));
                        }
                    }
                }
                p->setPen(ink);
                p->drawText(cell.adjusted(3,2,-3,-3),Qt::AlignHCenter|Qt::AlignTop,
                            fm.elidedText(spec.series.at(row).label,Qt::ElideRight,
                                          cell.width()-6.0));
                continue;
            }

            const PlotSeries& xs=spec.series.at(col);
            const PlotSeries& ys=spec.series.at(row);
            const int n=qMin(xs.y.size(),ys.y.size());

            // ABOVE the diagonal: the number. Below it: the picture.
            if(row<col){
                QVector<double> vx,vy;
                for(int i=0;i<n;++i){
                    if(!finite(xs.y[i])||!finite(ys.y[i])) continue;
                    vx.append(xs.y[i]); vy.append(ys.y[i]);
                }
                if(vx.size()<3) continue;
                const double r=pearson(momentsOf(vx,vy));
                p->setPen(ink);
                QFont big=tickFont;
                // Sized by |r|, so a strong correlation is visible from across
                // the figure and a weak one does not compete with it.
                big.setPointSizeF(qBound(7.0,tickFont.pointSizeF()*(1.0+1.6*std::abs(r)),
                                         qMax(7.0,cell.height()*0.42)));
                p->setFont(big);
                p->drawText(cell,Qt::AlignCenter,QString::number(r,'f',2));
                p->setFont(tickFont);
                continue;
            }

            if(!span[col].ok||!span[row].ok) continue;
            QColor dot=ys.color; dot.setAlphaF(0.65);
            p->setPen(Qt::NoPen); p->setBrush(dot);
            const double rad=qMax(0.7,qMin(2.0,cell.width()/90.0));
            const int stride=qMax(1,n/1200);
            for(int i=0;i<n;i+=stride){
                if(!finite(xs.y[i])||!finite(ys.y[i])) continue;
                const double tx=(xs.y[i]-span[col].lo)/qMax(1e-300,span[col].hi-span[col].lo);
                const double ty=(ys.y[i]-span[row].lo)/qMax(1e-300,span[row].hi-span[row].lo);
                p->drawEllipse(QPointF(cell.left()+tx*cell.width(),
                                       cell.bottom()-ty*cell.height()),rad,rad);
            }
        }
    }

    // What the figure IS, said once underneath, because a matrix with no axis
    // numbers has to say what its panels are measured against.
    p->setPen(faint);
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.1,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,
                QStringLiteral("%1 variables; scatters below the diagonal, "
                               "distributions on it, Pearson r above; each panel "
                               "on its own two ranges").arg(k));
    p->restore();
}

void QtPlotBackend::drawAlluvial(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    const EdgeList e=edgesFrom(spec);
    drawFloatingTitle(p,target,spec);
    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    p->setFont(tickFont);
    if(!e.valid){
        p->save();
        p->setPen(spec.style.foreground);
        p->drawText(target,Qt::AlignCenter,
                    QStringLiteral("needs an edge list: source, target, and an optional weight"));
        p->restore();
        return;
    }
    const int n=e.ids.size();

    // Stage by longest path, relaxed until it settles. A cycle cannot settle,
    // so the number of passes is capped at the node count and whatever is left
    // over is reported rather than drawn as if it were a flow - a cyclic
    // alluvial is not a thing, and silently laying one out would put edges
    // running backwards among edges running forwards with nothing to tell them
    // apart.
    //
    // AND CLAMPED, which the cap on the passes alone did not do. A longest
    // path through n nodes visits at most n of them, so no node can sit later
    // than stage n-1. Around a cycle each pass pushes every node on it one
    // stage further along, so six nodes with a cycle in them settled at
    // THIRTY-SEVEN stages: the diagram was laid out in thirty-seven columns,
    // the nodes crowded into the last two, and nineteen twentieths of the
    // canvas was empty. The report underneath said the graph has a cycle,
    // which is true and was not the thing that made the picture unreadable.
    QVector<int> stage(n,0);
    bool settled=false;
    for(int pass=0;pass<n&&!settled;++pass){
        settled=true;
        for(int k=0;k<e.from.size();++k)
            if(stage[e.to[k]]<qMin(n-1,stage[e.from[k]]+1)){
                stage[e.to[k]]=qMin(n-1,stage[e.from[k]]+1);
                settled=false;
            }
    }
    int stages=1;
    for(int i=0;i<n;++i) stages=qMax(stages,stage[i]+1);

    // A node is as tall as the larger of what enters and what leaves it: the
    // two differ wherever a flow is created or consumed, and sizing by only one
    // of them would hide exactly that.
    QVector<double> inflow(n,0.0),outflow(n,0.0);
    for(int k=0;k<e.from.size();++k){
        outflow[e.from[k]]+=e.weight[k];
        inflow[e.to[k]]+=e.weight[k];
    }
    QVector<double> weightOf(n,0.0);
    for(int i=0;i<n;++i) weightOf[i]=qMax(inflow[i],outflow[i]);

    QVector<QVector<int>> column(stages);
    for(int i=0;i<n;++i) column[stage[i]].append(i);
    for(QVector<int>& members:column)
        std::sort(members.begin(),members.end(),
                  [&e](int a,int b){ return e.ids[a]<e.ids[b]; });

    double heaviestColumn=0.0;
    for(const QVector<int>& members:column){
        double sum=0.0;
        for(int i:members) sum+=weightOf[i];
        heaviestColumn=qMax(heaviestColumn,sum);
    }
    if(!(heaviestColumn>0.0)) return;

    const double titleRoom=spec.title.isEmpty()?0.0:fm.height()*2.0;
    const QRectF field(target.left()+fm.height()*1.4,target.top()+titleRoom+fm.height()*0.4,
                       target.width()-fm.height()*2.8,
                       target.height()-titleRoom-fm.height()*2.6);
    if(field.width()<60.0||field.height()<40.0) return;
    const double barW=qMax(6.0,qMin(18.0,field.width()/double(qMax(1,stages))*0.12));
    const double columnX=(stages>1)?(field.width()-barW)/double(stages-1):0.0;

    // The tallest column fills the field less the gaps between its nodes, so
    // every column is drawn to the same scale and two columns can be compared
    // by eye - which is the one comparison an alluvial exists to support.
    int mostNodes=1;
    for(const QVector<int>& members:column) mostNodes=qMax(mostNodes,members.size());
    const double gap=qMin(12.0,field.height()*0.35/double(mostNodes));
    const double scale=(field.height()-gap*double(mostNodes-1))/heaviestColumn;

    QVector<double> nodeTop(n,0.0),nodeBottom(n,0.0),nodeX(n,0.0);
    for(int s=0;s<stages;++s){
        double sum=0.0;
        for(int i:column[s]) sum+=weightOf[i];
        // Each column centred on the field, so a stage that carries less than
        // the busiest one is short at both ends rather than hanging from the top.
        double y=field.top()+(field.height()-sum*scale
                              -gap*double(column[s].size()-1))*0.5;
        for(int i:column[s]){
            nodeX[i]=field.left()+columnX*double(s);
            nodeTop[i]=y;
            nodeBottom[i]=y+weightOf[i]*scale;
            y=nodeBottom[i]+gap;
        }
    }

    // ---- The ribbons, heaviest first so a thin flow is never buried under a
    // thick one drawn later.
    QVector<int> order(e.from.size());
    for(int k=0;k<order.size();++k) order[k]=k;
    std::sort(order.begin(),order.end(),
              [&e](int a,int b){ return e.weight[a]>e.weight[b]; });

    QVector<double> usedOut(n,0.0),usedIn(n,0.0);
    p->save();
    p->setPen(Qt::NoPen);
    for(int k:order){
        const int u=e.from[k],v=e.to[k];
        if(stage[v]<=stage[u]) continue;              // a backward edge is a cycle's leftover
        const double thickness=e.weight[k]*scale;
        const double y0=nodeTop[u]+usedOut[u],y1=nodeTop[v]+usedIn[v];
        usedOut[u]+=thickness; usedIn[v]+=thickness;
        const double x0=nodeX[u]+barW,x1=nodeX[v];
        if(!(x1>x0)) continue;
        const double bend=(x1-x0)*0.5;
        QPainterPath ribbon;
        ribbon.moveTo(x0,y0);
        ribbon.cubicTo(QPointF(x0+bend,y0),QPointF(x1-bend,y1),QPointF(x1,y1));
        ribbon.lineTo(x1,y1+thickness);
        ribbon.cubicTo(QPointF(x1-bend,y1+thickness),QPointF(x0+bend,y0+thickness),
                       QPointF(x0,y0+thickness));
        ribbon.closeSubpath();
        // Coloured by where the flow comes FROM, which is what a reader traces.
        QColor fill=categoryColour(spec,u,std::fmod(0.07+0.618034*double(u),1.0),0.45,0.85);
        fill.setAlphaF(0.55);
        p->setBrush(fill);
        p->drawPath(ribbon);
    }
    p->restore();

    // ---- The nodes over the ribbons.
    p->save();
    p->setPen(Qt::NoPen);
    p->setBrush(spec.style.foreground);
    for(int i=0;i<n;++i)
        p->drawRect(QRectF(nodeX[i],nodeTop[i],barW,qMax(1.0,nodeBottom[i]-nodeTop[i])));
    p->setPen(spec.style.foreground);
    for(int i=0;i<n;++i){
        if(nodeBottom[i]-nodeTop[i]<fm.height()*0.8) continue;
        const QString name=QStringLiteral("%1").arg(e.ids[i],0,'g',6);
        const bool onLeft=(stage[i]==stages-1);
        const QRectF box=onLeft
            ?QRectF(nodeX[i]-90.0-4.0,nodeTop[i],90.0,nodeBottom[i]-nodeTop[i])
            :QRectF(nodeX[i]+barW+4.0,nodeTop[i],90.0,nodeBottom[i]-nodeTop[i]);
        p->drawText(box,(onLeft?Qt::AlignRight:Qt::AlignLeft)|Qt::AlignVCenter,name);
    }
    p->restore();

    p->save();
    p->setPen(spec.style.foreground);
    int backward=0;
    for(int k=0;k<e.from.size();++k) if(stage[e.to[k]]<=stage[e.from[k]]) ++backward;
    double carried=0.0;
    for(double w:e.weight) carried+=w;

    // WHEN NOTHING COULD BE DRAWN, SAY SO IN THE MIDDLE.
    //
    // A graph that is one big cycle has no ordering for its stages to be, so
    // every edge runs backwards and not one flow is drawn. The figure was then
    // a row of bars against an empty canvas with the reason in eight-point type
    // along the bottom edge - which reads as a broken renderer rather than as
    // the answer it is. The same sentence, in the middle, reads as an answer.
    if(!e.from.isEmpty()&&backward==e.from.size()){
        p->drawText(target,Qt::AlignCenter,
                    QStringLiteral("every one of the %1 flows runs backwards: the graph "
                                   "is cyclic, and a flow through stages has no "
                                   "ordering to draw")
                        .arg(e.from.size()));
        p->restore();
        return;
    }
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.1,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,
                backward>0
                ?QStringLiteral("%1 nodes over %2 stages, %3 flows totalling %4; "
                                "%5 edge(s) run backwards and are not drawn - "
                                "the graph has a cycle")
                     .arg(n).arg(stages).arg(e.from.size()).arg(carried,0,'g',5)
                     .arg(backward)
                :QStringLiteral("%1 nodes over %2 stages, %3 flows totalling %4; "
                                "a node is as tall as the larger of what enters "
                                "and what leaves it")
                     .arg(n).arg(stages).arg(e.from.size()).arg(carried,0,'g',5));
    p->restore();
}

// ======================================================================
// Hive plot
//
// The third thing to do with the edge list, and the one that exists because the
// first is untrustworthy. A force-directed network graph places nodes wherever
// the springs settle, so the eye reads clusters and distances that are
// properties of the LAYOUT rather than of the graph - run it twice and the
// picture changes. A hive plot fixes every node by a rule: which axis it sits
// on and how far out it sits are both read off the graph itself, so the same
// edge list always draws the same figure and two graphs can be compared.
//
// The rule here is degree. Nodes are split into three groups by degree and each
// group gets an axis, with radial position set by degree within that group. So
// the middle of the figure is the sparsely connected part of the graph and the
// rim is the hubs, and a link running from rim to rim is a hub talking to a hub.
// Degree rather than an assignment column: a mapped column carries numbers, and
// a number that happened to be a group label would be indistinguishable from
// one that was a measurement.
//
// Three mapped columns: source, target, and an optional weight.
void QtPlotBackend::drawHive(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    const EdgeList e=edgesFrom(spec);
    drawFloatingTitle(p,target,spec);
    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    p->setFont(tickFont);
    if(!e.valid){
        p->save();
        p->setPen(spec.style.foreground);
        p->drawText(target,Qt::AlignCenter,
                    QStringLiteral("needs an edge list: source, target, and an optional weight"));
        p->restore();
        return;
    }
    const int n=e.ids.size();

    QVector<int> degree(n,0);
    for(int k=0;k<e.from.size();++k){ ++degree[e.from[k]]; ++degree[e.to[k]]; }

    QVector<int> byDegree(n);
    for(int i=0;i<n;++i) byDegree[i]=i;
    std::sort(byDegree.begin(),byDegree.end(),[&](int a,int b){
        if(degree[a]!=degree[b]) return degree[a]<degree[b];
        return e.ids[a]<e.ids[b];               // ties by identifier, so it is repeatable
    });
    QVector<int> axisOf(n,0);
    for(int r=0;r<n;++r) axisOf[byDegree[r]]=qMin(2,r*3/qMax(1,n));

    // Radial position by RANK in degree order within the axis, not by the
    // degree itself. Degree proportionally is the obvious thing and it hides
    // most of the graph: degree is a small integer, so ties are the rule rather
    // than the exception, and every node of the same degree lands on the same
    // point. On the test graph that put four leaves under one dot and left two
    // of the three axes looking almost empty. Rank keeps the ORDER - further
    // out is still better connected - and spreads every node so it can be seen
    // and its links followed, which is the whole purpose of fixing positions by
    // a rule in the first place.
    QVector<double> radius(n,0.5);
    for(int axis=0;axis<3;++axis){
        QVector<int> members;
        for(int i=0;i<n;++i) if(axisOf[i]==axis) members.append(i);
        if(members.isEmpty()) continue;
        std::sort(members.begin(),members.end(),[&](int a,int b){
            if(degree[a]!=degree[b]) return degree[a]<degree[b];
            return e.ids[a]<e.ids[b];               // ties broken repeatably
        });
        for(int slot=0;slot<members.size();++slot)
            radius[members[slot]]=(members.size()>1)
                ?double(slot)/double(members.size()-1):0.5;
    }

    const double titleRoom=spec.title.isEmpty()?0.0:fm.height()*2.0;
    const QRectF field(target.left(),target.top()+titleRoom,target.width(),
                       target.height()-titleRoom-fm.height()*1.6);
    const double outer=qMin(field.width(),field.height())*0.38;
    // Nudged DOWN by a quarter of the radius. Three axes at a hundred and
    // twenty degrees with one pointing up reach a full radius above the centre
    // and only half a radius below it, so a figure centred on its own centre
    // sits in the top two thirds of the page with a band of nothing under it.
    const QPointF centre=field.center()+QPointF(0.0,outer*0.25);
    const double inner=outer*0.16;
    const double angles[3]={-M_PI/2.0,-M_PI/2.0+2.0*M_PI/3.0,-M_PI/2.0+4.0*M_PI/3.0};
    const auto seat=[&](int node){
        const double r=inner+(outer-inner)*radius[node];
        const double a=angles[axisOf[node]];
        return QPointF(centre.x()+r*std::cos(a),centre.y()+r*std::sin(a));
    };

    // ---- The three axes.
    p->save();
    p->setPen(QPen(spec.style.gridColor,1.2));
    for(int axis=0;axis<3;++axis){
        const double a=angles[axis];
        p->drawLine(QPointF(centre.x()+inner*std::cos(a),centre.y()+inner*std::sin(a)),
                    QPointF(centre.x()+outer*std::cos(a),centre.y()+outer*std::sin(a)));
    }
    p->setPen(spec.style.foreground);
    static const char* kNames[3]={"fewest links","middle","most links"};
    for(int axis=0;axis<3;++axis){
        const double a=angles[axis];
        const double r=outer+fm.height()*1.6;
        const QPointF where(centre.x()+r*std::cos(a),centre.y()+r*std::sin(a));
        p->drawText(QRectF(where.x()-70.0,where.y()-fm.height()*0.5,140.0,fm.height()),
                    Qt::AlignCenter,QString::fromLatin1(kNames[axis]));
    }
    p->restore();

    // ---- The links. Curved by rotating each end's control point toward the
    // other axis, which is what gives a hive plot its readable bundles: two
    // links between the same pair of axes bow the same way and can be counted,
    // where two straight chords crossing the middle cannot.
    double heaviest=0.0;
    for(double w:e.weight) heaviest=qMax(heaviest,w);
    p->save();
    p->setBrush(Qt::NoBrush);
    for(int k=0;k<e.from.size();++k){
        const int u=e.from[k],v=e.to[k];
        if(u==v) continue;
        const QPointF a=seat(u),b=seat(v);
        double turn=angles[axisOf[v]]-angles[axisOf[u]];
        while(turn>M_PI) turn-=2.0*M_PI;
        while(turn<-M_PI) turn+=2.0*M_PI;
        // Two nodes on the SAME axis have no angle between them, so the curve
        // would be a straight line lying along the axis and invisible. Given a
        // fixed bow instead, on the side the identifiers order them.
        if(std::abs(turn)<1e-9) turn=(e.ids[u]<e.ids[v])?0.5:-0.5;
        // The control point is rotated toward the other axis AND pulled in
        // toward the middle. Rotating alone leaves it on the same circle as its
        // endpoint, and a cubic whose control points sit on a circle bulges
        // OUTSIDE it - which sent every link on a long sweep out past the ends
        // of the axes and through the space where the labels are. Pulled in,
        // the links curve through the interior, which is the shape that lets
        // two bundles between the same pair of axes be told apart.
        const auto swing=[&](const QPointF& from,double by){
            const double dx=from.x()-centre.x(),dy=from.y()-centre.y();
            const double rx=dx*std::cos(by)-dy*std::sin(by);
            const double ry=dx*std::sin(by)+dy*std::cos(by);
            return QPointF(centre.x()+rx*0.62,centre.y()+ry*0.62);
        };
        QPainterPath link;
        link.moveTo(a);
        link.cubicTo(swing(a,turn*0.35),swing(b,-turn*0.35),b);
        QColor colour=categoryColour(spec,axisOf[u],
                                     std::fmod(0.55+0.11*double(axisOf[u]),1.0),0.55,0.75);
        colour.setAlphaF(0.5);
        QPen pen(colour);
        pen.setWidthF(heaviest>0.0?qBound(0.6,0.6+2.0*e.weight[k]/heaviest,2.6):1.0);
        p->setPen(pen);
        p->drawPath(link);
    }
    p->restore();

    // ---- The nodes on top of the links.
    p->save();
    p->setPen(Qt::NoPen);
    p->setBrush(spec.style.foreground);
    int busiest=0;
    for(int d:degree) busiest=qMax(busiest,d);
    for(int i=0;i<n;++i){
        const double size=(busiest>0)
            ?qBound(2.0,2.0+3.0*std::sqrt(double(degree[i])/double(busiest)),5.5):3.0;
        p->drawEllipse(seat(i),size,size);
    }
    p->restore();

    p->save();
    p->setPen(spec.style.foreground);
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.1,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,
                QStringLiteral("%1 nodes, %2 edges; axis by degree group, radius "
                               "by rank in degree order within it, %3 to %4 "
                               "links a node")
                    .arg(n).arg(e.from.size())
                    .arg(degree.isEmpty()?0:*std::min_element(degree.begin(),degree.end()))
                    .arg(busiest));
    p->restore();
}

// ======================================================================
// Pourbaix diagram
//
// Potential against pH: which form of a metal is thermodynamically stable in
// water, and therefore whether it corrodes, passivates or is immune. The
// catalogue could draw the boundaries as an ordinary line chart and that is not
// a Pourbaix diagram, because a Pourbaix diagram is read AGAINST the stability
// field of water itself - a species that sits outside it cannot exist in
// aqueous solution however favourable its own thermodynamics look.
//
// The two water lines are drawn by the engine, from the Nernst equation and
// nothing else:
//
//     (a)  O2 / H2O    E = 1.229 - 0.0592 pH
//     (b)  H2O / H2    E = 0.000 - 0.0592 pH
//
// The 1.229 V is the standard potential of the oxygen electrode and the 0.0592
// is (RT/F) ln 10 at 25 degrees, so both lines are computed rather than
// tabulated - and both are named on the figure as (a) and (b), which is how
// they are referred to everywhere the diagrams are used. What the engine does
// NOT do is invent the species boundaries: those are the user's data, because
// they depend on the metal, the temperature and the assumed ion activity, and a
// boundary drawn from memory would look exactly as convincing as a correct one.
//
// x is pH from the mapping and each further mapped column is one boundary.
void QtPlotBackend::drawPourbaix(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    const QFont tickFont=font(spec,qMax(6.0,spec.style.tickSize-0.5));
    const QFontMetricsF fm(tickFont,p->device());

    // Both lines are straight in pH, so two points each - but they are computed
    // at the frame's own edges rather than over the data's range, because the
    // stability field of water does not stop where the samples do.
    const double slope=-0.0592;
    const auto lineAt=[&](double intercept,double pH){ return intercept+slope*pH; };
    const double left=f.xLog?std::pow(10.0,f.xLo):f.xLo;
    const double right=f.xLog?std::pow(10.0,f.xHi):f.xHi;

    p->save();
    p->setFont(tickFont);
    // The band between the two lines, shaded very lightly. It is a region, and
    // drawing it as two lines alone leaves the reader to work out which side of
    // each one is the inside.
    {
        QPolygonF band;
        band<<toDevice(f,left,lineAt(1.229,left))<<toDevice(f,right,lineAt(1.229,right))
            <<toDevice(f,right,lineAt(0.0,right))<<toDevice(f,left,lineAt(0.0,left));
        QColor wash=spec.style.positive;
        wash.setAlphaF(0.07);
        p->setPen(Qt::NoPen);
        p->setBrush(wash);
        p->drawPolygon(band);
    }
    p->setBrush(Qt::NoBrush);
    struct Water { double intercept; const char* tag; };
    static const Water kWater[2]={{1.229,"(a)  O2 / H2O"},{0.0,"(b)  H2O / H2"}};
    for(const Water& water:kWater){
        QPen pen(spec.style.foreground);
        pen.setWidthF(0.9);
        pen.setDashPattern({6,4});
        p->setPen(pen);
        const QPointF a=toDevice(f,left,lineAt(water.intercept,left));
        const QPointF b=toDevice(f,right,lineAt(water.intercept,right));
        p->drawLine(a,b);
        // Named at whichever end is on the page, since a frame that starts at
        // pH 7 has no left end for these lines to be labelled at.
        const QPointF where=f.plotArea.contains(b)?b:a;
        const bool atRight=(where==b);
        const QString tag=QString::fromLatin1(water.tag);
        const QRectF chip(atRight?where.x()-fm.horizontalAdvance(tag)-6.0
                                 :where.x()+4.0,
                          where.y()-fm.height()-1.0,
                          fm.horizontalAdvance(tag)+4.0,fm.height());
        p->fillRect(chip,spec.style.background);
        p->drawText(chip,Qt::AlignCenter,tag);
    }
    p->restore();

    drawLineChart(p,f,spec);

    p->save();
    p->setFont(tickFont);
    p->setPen(spec.style.foreground);
    p->drawText(QRectF(f.plotArea.left()+4.0,f.plotArea.bottom()-fm.height()-2.0,
                       f.plotArea.width()-8.0,fm.height()),
                Qt::AlignLeft|Qt::AlignVCenter,
                QStringLiteral("water is stable only between (a) and (b), at 25 C; "
                               "the species boundaries are the mapped columns"));
    p->restore();
}

// ======================================================================
// Sequence logo
//
// One stack of letters per position, where the HEIGHT of the stack is how much
// is known at that position and the height of each letter within it is that
// letter's share. A row of bar charts, one per position, shows the same
// frequencies and hides the thing everyone actually wants: which positions are
// conserved. A logo puts that on the vertical axis directly.
//
// The stack height is the information content in bits,
//
//     R = log2(s) - H,    H = -sum p log2 p
//
// with s the number of symbols. A position where every sequence agrees has
// H = 0 and a full stack; a position where all symbols are equally likely has
// H = log2(s) and no stack at all. That is why the axis is in bits and not in
// per cent: per cent would make a perfectly random position look as tall as a
// perfectly conserved one.
//
// x is the position from the mapping and each further mapped column is one
// symbol, named by its own column label - so four columns A, C, G, T give a
// nucleotide logo and twenty give a protein one, with no list of alphabets
// anywhere in the code.
void QtPlotBackend::drawSequenceLogo(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    const int symbols=spec.series.size();
    if(symbols<2) return;
    int steps=spec.series.at(0).y.size();
    for(const PlotSeries& s:spec.series) steps=qMin(steps,int(s.y.size()));
    if(steps<1) return;
    const QVector<double>& xs=spec.series.at(0).x;

    const double maxBits=std::log2(double(symbols));
    // One column's width, from the closest pair of neighbouring positions, so
    // positions that are not evenly spaced never overlap.
    QVector<double> centres;
    for(int j=0;j<steps&&j<xs.size();++j) if(finite(xs[j])) centres.append(xs[j]);
    const double slot=slotWidthFrom(centres,1.0,f.xHi-f.xLo);

    p->save();
    for(int j=0;j<steps;++j){
        if(j>=xs.size()||!finite(xs[j])) continue;
        double total=0.0;
        for(int i=0;i<symbols;++i){
            const double v=spec.series.at(i).y[j];
            if(finite(v)&&v>0.0) total+=v;
        }
        if(!(total>0.0)) continue;

        double entropy=0.0;
        QVector<QPair<double,int>> share;          // fraction, which symbol
        for(int i=0;i<symbols;++i){
            const double v=spec.series.at(i).y[j];
            const double fraction=(finite(v)&&v>0.0)?v/total:0.0;
            if(fraction>0.0) entropy-=fraction*std::log2(fraction);
            share.append(qMakePair(fraction,i));
        }
        const double bits=qMax(0.0,maxBits-entropy);
        // Largest at the top, which is the convention and the only ordering
        // that lets the dominant symbol be read without measuring.
        std::sort(share.begin(),share.end(),
                  [](const QPair<double,int>& a,const QPair<double,int>& b){
                      return a.first<b.first;
                  });

        const double xLeft=toDevice(f,xs[j]-slot*0.42,0.0).x();
        const double xRight=toDevice(f,xs[j]+slot*0.42,0.0).x();
        double base=0.0;
        for(const QPair<double,int>& part:share){
            const double letterBits=part.first*bits;
            if(!(letterBits>0.0)) continue;
            const double yBottom=toDevice(f,xs[j],base).y();
            const double yTop=toDevice(f,xs[j],base+letterBits).y();
            base+=letterBits;
            const double height=yBottom-yTop;
            if(height<0.6) continue;

            const QString name=spec.series.at(part.second).label;
            const QString glyph=name.isEmpty()?QStringLiteral("?"):name.left(1).toUpper();
            // Drawn at a fixed size and then STRETCHED to the box, rather than
            // choosing a point size per letter. A point size only comes in
            // whole steps and would make two letters of nearly equal share come
            // out visibly different, which is the one thing the reader is
            // comparing.
            QFont glyphFont=font(spec,40.0);
            glyphFont.setBold(true);
            const QFontMetricsF glyphMetrics(glyphFont,p->device());
            const QRectF tight=glyphMetrics.tightBoundingRect(glyph);
            if(tight.width()<=0.0||tight.height()<=0.0) continue;
            p->save();
            p->setFont(glyphFont);
            p->setPen(spec.series.at(part.second).color);
            p->translate(xLeft,yTop);
            p->scale((xRight-xLeft)/tight.width(),height/tight.height());
            p->drawText(QPointF(-tight.left(),-tight.top()),glyph);
            p->restore();
        }
    }
    p->restore();
}

// ======================================================================
// Karyotype ideogram
//
// A set of chromosomes drawn to scale with their banding, which is the
// reference picture every cytogenetic result is reported against: a band name
// like 17q21 means nothing without the figure that says where 17q21 is.
//
// Not the Gantt chart it superficially resembles. A Gantt row is a bar from a
// start to an end and that is all; a chromosome has a SHAPE - it is pinched at
// the centromere, which divides it into a short arm and a long arm, and the arm
// a band sits on is half of what its name says. Drawing these as bars would
// lose the centromere, and with it the only landmark on the figure.
//
// Four mapped columns: chromosome, band start, band end, and stain intensity
// from 0 to 100 - the Giemsa darkness, which is what the light and dark bands
// are. A NEGATIVE stain marks the centromere band, following the convention
// that a cytoband table names that band's stain differently from all the
// others; it is the one piece of information the four numeric columns cannot
// otherwise carry, and inferring the centromere from the band pattern would be
// a guess dressed as a landmark.
void QtPlotBackend::drawKaryotype(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    drawFloatingTitle(p,target,spec);
    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    p->setFont(tickFont);

    if(spec.series.size()<4){
        p->save();
        p->setPen(spec.style.foreground);
        p->drawText(target,Qt::AlignCenter,
                    QStringLiteral("needs four columns: chromosome, band start, "
                                   "band end, and stain 0-100 (negative marks the centromere)"));
        p->restore();
        return;
    }
    const QVector<double>& chrom=spec.series.at(0).y;
    const QVector<double>& from=spec.series.at(1).y;
    const QVector<double>& to=spec.series.at(2).y;
    const QVector<double>& stain=spec.series.at(3).y;
    int rows=chrom.size();
    for(const PlotSeries& s:spec.series) rows=qMin(rows,int(s.y.size()));

    struct Band { double from,to,stain; };
    struct Chromosome { double id=0.0,length=0.0,centromere=-1.0; QVector<Band> bands; };
    QVector<Chromosome> all;
    QHash<double,int> index;
    for(int i=0;i<rows;++i){
        if(!finite(chrom[i])||!finite(from[i])||!finite(to[i])) continue;
        const double lo=qMin(from[i],to[i]),hi=qMax(from[i],to[i]);
        if(!(hi>lo)) continue;
        int at=index.value(chrom[i],-1);
        if(at<0){
            Chromosome fresh; fresh.id=chrom[i];
            at=all.size(); index.insert(chrom[i],at); all.append(fresh);
        }
        const double level=finite(stain[i])?stain[i]:0.0;
        all[at].bands.append({lo,hi,level});
        all[at].length=qMax(all[at].length,hi);
        if(level<0.0) all[at].centromere=0.5*(lo+hi);
    }
    if(all.isEmpty()) return;
    std::sort(all.begin(),all.end(),
              [](const Chromosome& a,const Chromosome& b){ return a.id<b.id; });

    double longest=0.0;
    for(const Chromosome& c:all) longest=qMax(longest,c.length);
    if(!(longest>0.0)) return;

    const double titleRoom=spec.title.isEmpty()?0.0:fm.height()*2.0;
    const QRectF field(target.left()+fm.height()*0.6,target.top()+titleRoom+fm.height()*0.6,
                       target.width()-fm.height()*1.2,
                       target.height()-titleRoom-fm.height()*3.6);
    if(field.width()<40.0||field.height()<40.0) return;
    const double lane=field.width()/double(all.size());
    const double bodyW=qMin(lane*0.55,26.0);
    // Every chromosome to ONE scale, so the longest fills the field and the
    // rest are as short as they really are. Scaling each to its own lane is the
    // obvious thing and destroys the figure: relative length is half of what a
    // karyotype says, and it is how the numbering was assigned in the first
    // place.
    const double scale=field.height()/longest;

    for(int c=0;c<all.size();++c){
        const Chromosome& chr=all.at(c);
        const double cx=field.left()+lane*(double(c)+0.5);
        const double top=field.top();
        const double bottom=top+chr.length*scale;
        const double pinchAt=(chr.centromere>=0.0)?top+chr.centromere*scale:-1.0;
        // Half-width at a given height: full, tapering to just over a third
        // across the constriction. The taper is a fixed fraction of the
        // chromosome's own length so that a short chromosome is not pinched
        // over most of its body.
        const double reach=qMax(4.0,chr.length*scale*0.06);
        const auto halfWidth=[&](double y){
            if(pinchAt<0.0) return bodyW*0.5;
            const double away=std::abs(y-pinchAt);
            if(away>=reach) return bodyW*0.5;
            return bodyW*0.5*(0.38+0.62*away/reach);
        };

        // The outline, walked down one side and back up the other, with the
        // ends rounded. Built once and used both as the clip for the bands and
        // as the stroke around them, so a band can never sit outside the body
        // it belongs to.
        QPainterPath outline;
        const double cap=qMin(bodyW*0.5,(bottom-top)*0.5);
        {
            QVector<QPointF> right,left;
            for(double y=top+cap;y<=bottom-cap;y+=1.0){
                right.append(QPointF(cx+halfWidth(y),y));
                left.append(QPointF(cx-halfWidth(y),y));
            }
            if(right.isEmpty()){
                right.append(QPointF(cx+bodyW*0.5,top));
                left.append(QPointF(cx-bodyW*0.5,top));
            }
            outline.moveTo(QPointF(cx,top));
            outline.quadTo(QPointF(cx+bodyW*0.5,top),right.first());
            for(const QPointF& pt:right) outline.lineTo(pt);
            outline.quadTo(QPointF(cx+bodyW*0.5,bottom),QPointF(cx,bottom));
            outline.quadTo(QPointF(cx-bodyW*0.5,bottom),left.last());
            for(int i=left.size()-1;i>=0;--i) outline.lineTo(left[i]);
            outline.quadTo(QPointF(cx-bodyW*0.5,top),QPointF(cx,top));
            outline.closeSubpath();
        }

        p->save();
        p->setClipPath(outline);
        p->setPen(Qt::NoPen);
        for(const Band& band:chr.bands){
            const double y0=top+band.from*scale,y1=top+band.to*scale;
            if(band.stain<0.0){
                // The centromere, in the colour it is conventionally given so
                // that the landmark is visible rather than merely implied by
                // the constriction.
                p->setBrush(QColor(0xc0,0x40,0x40));
            }else{
                const int grey=int(qBound(0.0,255.0-band.stain*2.30,255.0));
                p->setBrush(QColor(grey,grey,grey));
            }
            p->drawRect(QRectF(cx-bodyW,y0,bodyW*2.0,qMax(0.6,y1-y0)));
        }
        p->restore();

        p->save();
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(spec.style.foreground,0.9));
        p->drawPath(outline);
        p->setPen(spec.style.foreground);
        p->drawText(QRectF(cx-lane*0.5,bottom+3.0,lane,fm.height()),
                    Qt::AlignHCenter|Qt::AlignTop,QString::number(chr.id,'g',6));
        p->restore();
    }

    p->save();
    p->setPen(spec.style.foreground);
    int withCentromere=0,bands=0;
    for(const Chromosome& c:all){
        bands+=c.bands.size();
        if(c.centromere>=0.0) ++withCentromere;
    }
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.1,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,
                QStringLiteral("%1 chromosomes, %2 bands, %3 with a centromere "
                               "given; all drawn to one scale, longest %4")
                    .arg(all.size()).arg(bands).arg(withCentromere)
                    .arg(longest,0,'g',6));
    p->restore();
}

// ======================================================================
// Circos plot
//
// Segments of DIFFERENT lengths around a circle, and links between positions
// INSIDE them. That is what separates it from the chord diagram already here: a
// chord diagram's arcs are categories whose only size is how much flows through
// them, so a link attaches to a category and nowhere in particular. A Circos
// segment is a coordinate space - a chromosome, a contig, a timeline - and a
// link attaches at a POSITION within it, which is the whole reason the picture
// is used for rearrangements and translocations.
//
// Five mapped columns: the source segment, the position within it, the target
// segment, the position within that, and an optional weight. Segment lengths
// are taken as the furthest position seen on each, because that is the only
// honest answer available from the data - a segment is at least as long as the
// furthest thing on it, and claiming more would be inventing a coordinate space.
void QtPlotBackend::drawCircos(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    drawFloatingTitle(p,target,spec);
    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    p->setFont(tickFont);

    if(spec.series.size()<4){
        p->save();
        p->setPen(spec.style.foreground);
        p->drawText(target,Qt::AlignCenter,
                    QStringLiteral("needs four columns: source segment, position, "
                                   "target segment, position (and an optional weight)"));
        p->restore();
        return;
    }
    const QVector<double>& fromSeg=spec.series.at(0).y;
    const QVector<double>& fromPos=spec.series.at(1).y;
    const QVector<double>& toSeg=spec.series.at(2).y;
    const QVector<double>& toPos=spec.series.at(3).y;
    const bool weighted=spec.series.size()>=5;
    const QVector<double> weight=weighted?spec.series.at(4).y:QVector<double>();
    int rows=fromSeg.size();
    for(const PlotSeries& s:spec.series) rows=qMin(rows,int(s.y.size()));

    QVector<double> ids;
    QVector<double> span;
    QHash<double,int> index;
    const auto seat=[&](double id,double pos){
        int at=index.value(id,-1);
        if(at<0){ at=ids.size(); ids.append(id); span.append(0.0); index.insert(id,at); }
        span[at]=qMax(span[at],pos);
        return at;
    };
    struct Link { int a,b; double pa,pb,w; };
    QVector<Link> links;
    for(int i=0;i<rows;++i){
        if(!finite(fromSeg[i])||!finite(toSeg[i])) continue;
        if(!finite(fromPos[i])||!finite(toPos[i])) continue;
        double w=1.0;
        if(weighted&&i<weight.size()){
            if(!finite(weight[i])||weight[i]<=0.0) continue;
            w=weight[i];
        }
        const int a=seat(fromSeg[i],std::abs(fromPos[i]));
        const int b=seat(toSeg[i],std::abs(toPos[i]));
        links.append({a,b,std::abs(fromPos[i]),std::abs(toPos[i]),w});
    }
    if(ids.isEmpty()||links.isEmpty()) return;

    // Segments in identifier order and sized by their own extent, with a gap
    // between them so the ring reads as separate coordinate spaces rather than
    // as one continuous one.
    QVector<int> order(ids.size());
    for(int i=0;i<order.size();++i) order[i]=i;
    std::sort(order.begin(),order.end(),
              [&ids](int a,int b){ return ids[a]<ids[b]; });
    double totalSpan=0.0;
    for(double s:span) totalSpan+=qMax(s,1e-9);
    if(!(totalSpan>0.0)) return;

    const double gapDegrees=qMin(3.0,120.0/double(ids.size()));
    const double usable=360.0-gapDegrees*double(ids.size());
    QVector<double> startAngle(ids.size(),0.0),sweep(ids.size(),0.0);
    {
        double at=90.0;                              // twelve o'clock, going clockwise
        for(int i:order){
            sweep[i]=usable*qMax(span[i],1e-9)/totalSpan;
            startAngle[i]=at;
            at-=sweep[i]+gapDegrees;
        }
    }
    const auto angleOf=[&](int segment,double position){
        const double f=(span[segment]>0.0)
            ?qBound(0.0,position/span[segment],1.0):0.5;
        return startAngle[segment]-sweep[segment]*f;
    };

    const double titleRoom=spec.title.isEmpty()?0.0:fm.height()*2.0;
    const QRectF field(target.left(),target.top()+titleRoom,target.width(),
                       target.height()-titleRoom-fm.height()*1.6);
    const QPointF centre=field.center();
    const double outer=qMin(field.width(),field.height())*0.40;
    const double ringW=qMax(6.0,outer*0.07);
    const double inner=outer-ringW;
    const auto onRing=[&](double degrees,double radius){
        const double a=degrees*M_PI/180.0;
        return QPointF(centre.x()+radius*std::cos(a),centre.y()-radius*std::sin(a));
    };

    // ---- The links first, so the ring is never hidden by them. Quadratic
    // through the centre: the deeper a chord dips the further apart its ends
    // are, which is the reading a straight chord cannot give because two very
    // different pairs can be the same straight line's length apart.
    double heaviest=0.0;
    for(const Link& link:links) heaviest=qMax(heaviest,link.w);
    p->save();
    p->setBrush(Qt::NoBrush);
    for(const Link& link:links){
        const QPointF a=onRing(angleOf(link.a,link.pa),inner);
        const QPointF b=onRing(angleOf(link.b,link.pb),inner);
        // The control point is pulled toward the middle by how far apart the
        // two ends are, so a link within one segment stays near the rim and one
        // across the circle passes close to the centre.
        double apart=std::abs(angleOf(link.a,link.pa)-angleOf(link.b,link.pb));
        while(apart>360.0) apart-=360.0;
        if(apart>180.0) apart=360.0-apart;
        const double pull=1.0-qBound(0.0,apart/180.0,1.0);
        QPainterPath chord;
        chord.moveTo(a);
        chord.quadTo(QPointF(centre.x()+(0.5*(a.x()+b.x())-centre.x())*pull,
                             centre.y()+(0.5*(a.y()+b.y())-centre.y())*pull),b);
        QColor colour=categoryColour(spec,link.a,std::fmod(0.06+0.618034*double(link.a),1.0),
                                       0.6,0.8);
        colour.setAlphaF(0.55);
        QPen pen(colour);
        pen.setWidthF(heaviest>0.0?qBound(0.7,0.7+2.3*link.w/heaviest,3.0):1.0);
        pen.setCapStyle(Qt::RoundCap);
        p->setPen(pen);
        p->drawPath(chord);
    }
    p->restore();

    // ---- The ring, with a tick every tenth of each segment so a position can
    // actually be read off it. Without those the segments are just coloured
    // arcs and the difference from a chord diagram is invisible.
    p->save();
    for(int i=0;i<ids.size();++i){
        QPainterPath arc;
        const QRectF outerBox(centre.x()-outer,centre.y()-outer,outer*2,outer*2);
        const QRectF innerBox(centre.x()-inner,centre.y()-inner,inner*2,inner*2);
        arc.arcMoveTo(outerBox,startAngle[i]);
        arc.arcTo(outerBox,startAngle[i],-sweep[i]);
        arc.arcTo(innerBox,startAngle[i]-sweep[i],sweep[i]);
        arc.closeSubpath();
        p->setPen(QPen(spec.style.background,1.0));
        p->setBrush(categoryColour(spec,i,std::fmod(0.06+0.618034*double(i),1.0),0.42,0.8));
        p->drawPath(arc);

        p->setPen(QPen(spec.style.background,0.8));
        for(int tick=1;tick<10;++tick){
            const double a=startAngle[i]-sweep[i]*double(tick)/10.0;
            p->drawLine(onRing(a,inner),onRing(a,inner+ringW*0.45));
        }
        p->setPen(spec.style.foreground);
        const double mid=startAngle[i]-sweep[i]*0.5;
        const QPointF where=onRing(mid,outer+fm.height()*0.8);
        p->drawText(QRectF(where.x()-40.0,where.y()-fm.height()*0.5,80.0,fm.height()),
                    Qt::AlignCenter,QString::number(ids[i],'g',6));
    }
    p->restore();

    p->save();
    p->setPen(spec.style.foreground);
    double carried=0.0;
    for(const Link& link:links) carried+=link.w;
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.1,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,
                QStringLiteral("%1 segments sized by their own extent, %2 links "
                               "totalling %3; ticks every tenth of a segment")
                    .arg(ids.size()).arg(links.size()).arg(carried,0,'g',5));
    p->restore();
}

// ======================================================================
// Alignment nomogram
//
// Three parallel scales, and a straight line laid across them reads off the
// third value from the other two. It is the calculator that needs no power, and
// it is still used where one is not wanted: field forms, cockpit cards,
// clinical dosing charts, anything laminated.
//
// The construction is the classical one. The outer scales carry u and v at
// heights u and v; the middle scale sits halfway between them, so a line from
// u on the left to v on the right crosses it at (u + v) / 2. That means an
// alignment nomogram of this shape represents exactly the relations
//
//     h(w) = u + v
//
// and no others. A product becomes a sum under logarithms, so a multiplicative
// relation is drawn by mapping the columns through log first - which is the
// user's choice to make and is stated on the figure rather than done silently.
//
// WHAT THIS ENGINE WILL NOT DO is invent the scale functions. Given three
// columns it cannot know whether the relation is additive, multiplicative or
// neither, and a nomogram built on the wrong assumption is not a wrong graph -
// it is a working calculator that returns wrong answers, which is worse than
// anything else in this catalogue. So it builds the figure the data supports
// and then CHECKS it: the middle scale is only single-valued if w is monotone
// in (u + v), and the largest violation of that is measured and printed. Zero
// means the nomogram is valid; anything else is the amount by which reading it
// would lie.
//
// Three mapped columns: u, v, and the w they determine.
void QtPlotBackend::drawNomogram(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    drawFloatingTitle(p,target,spec);
    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    p->setFont(tickFont);

    if(spec.series.size()<3){
        p->save();
        p->setPen(spec.style.foreground);
        p->drawText(target,Qt::AlignCenter,
                    QStringLiteral("needs three columns: the two scales read from "
                                   "and the one read off"));
        p->restore();
        return;
    }
    const QVector<double>& cu=spec.series.at(0).y;
    const QVector<double>& cv=spec.series.at(1).y;
    const QVector<double>& cw=spec.series.at(2).y;
    int rows=cu.size();
    for(const PlotSeries& s:spec.series) rows=qMin(rows,int(s.y.size()));

    struct Row { double u,v,w,middle; };
    QVector<Row> readings;
    for(int i=0;i<rows;++i){
        if(!finite(cu[i])||!finite(cv[i])||!finite(cw[i])) continue;
        readings.append({cu[i],cv[i],cw[i],0.5*(cu[i]+cv[i])});
    }
    if(readings.size()<2) return;

    Bounds uRange=boundsOf(cu),vRange=boundsOf(cv),wRange=boundsOf(cw);
    double midLo=readings.first().middle,midHi=midLo;
    for(const Row& row:readings){
        midLo=qMin(midLo,row.middle); midHi=qMax(midHi,row.middle);
    }
    // The three scales share ONE vertical measure, because the isopleth is a
    // straight line across all three and a line cannot be straight through
    // three axes drawn to different scales. So the frame spans everything any
    // of them needs.
    const double lo=qMin(uRange.lo,qMin(vRange.lo,midLo));
    const double hi=qMax(uRange.hi,qMax(vRange.hi,midHi));
    if(!(hi>lo)) return;

    const double titleRoom=spec.title.isEmpty()?0.0:fm.height()*2.0;
    const QRectF field(target.left()+fm.height()*3.2,target.top()+titleRoom+fm.height()*1.2,
                       target.width()-fm.height()*6.4,
                       target.height()-titleRoom-fm.height()*3.8);
    if(field.width()<60.0||field.height()<60.0) return;
    const auto at=[&](double which,double value){
        return QPointF(field.left()+field.width()*which,
                       field.bottom()-(value-lo)/(hi-lo)*field.height());
    };

    // ---- The isopleths, one per supplied row, behind the scales. They are the
    // evidence: every one of them is a straight line by construction, so if the
    // marks they leave on the middle scale do not form a scale, the relation is
    // not of this shape and the picture says so by being a mess.
    p->save();
    QColor thread=spec.series.at(2).color;
    thread.setAlphaF(0.35);
    QPen pen(thread); pen.setWidthF(0.7);
    p->setPen(pen);
    for(const Row& row:readings)
        p->drawLine(at(0.0,row.u),at(1.0,row.v));
    p->restore();

    // ---- The three scales.
    p->save();
    p->setPen(QPen(spec.style.foreground,1.1));
    for(double which:{0.0,0.5,1.0})
        p->drawLine(at(which,lo),at(which,hi));

    const auto scaleOf=[&](double which,const Bounds& range,const QString& name,
                           bool labelLeft){
        const double step=niceStep((range.hi-range.lo)/6.0);
        if(!(step>0.0)) return;
        for(double v=std::ceil(range.lo/step)*step;v<=range.hi+step*0.01;v+=step){
            const QPointF here=at(which,v);
            p->drawLine(here+QPointF(-4.0,0.0),here+QPointF(4.0,0.0));
            p->drawText(QRectF(labelLeft?here.x()-84.0:here.x()+6.0,
                               here.y()-fm.height()*0.5,78.0,fm.height()),
                        (labelLeft?Qt::AlignRight:Qt::AlignLeft)|Qt::AlignVCenter,
                        QString::number(v,'g',4));
        }
        p->drawText(QRectF(at(which,hi).x()-70.0,field.top()-fm.height()*1.3,
                           140.0,fm.height()),
                    Qt::AlignCenter,name);
    };
    scaleOf(0.0,uRange,spec.series.at(0).label.isEmpty()
                ?QStringLiteral("u"):spec.series.at(0).label,true);
    scaleOf(1.0,vRange,spec.series.at(1).label.isEmpty()
                ?QStringLiteral("v"):spec.series.at(1).label,false);

    // The middle scale is NOT a ruler. Its marks are where the supplied rows
    // put them, labelled with the w that row had - because the whole point is
    // that the spacing of this scale is whatever the relation makes it, and
    // drawing it as evenly spaced numbers would be drawing the answer the
    // engine wishes were true.
    QVector<Row> sorted=readings;
    std::sort(sorted.begin(),sorted.end(),
              [](const Row& a,const Row& b){ return a.middle<b.middle; });
    double lastLabelY=-1e9;
    p->setPen(spec.style.foreground);
    for(const Row& row:sorted){
        const QPointF here=at(0.5,row.middle);
        p->drawLine(here+QPointF(-5.0,0.0),here+QPointF(5.0,0.0));
        if(std::abs(here.y()-lastLabelY)<fm.height()*1.05) continue;
        lastLabelY=here.y();
        const QRectF chip(here.x()+7.0,here.y()-fm.height()*0.5,78.0,fm.height());
        p->fillRect(chip,spec.style.background);
        p->drawText(chip,Qt::AlignLeft|Qt::AlignVCenter,QString::number(row.w,'g',4));
    }
    p->drawText(QRectF(at(0.5,hi).x()-70.0,field.top()-fm.height()*1.3,140.0,fm.height()),
                Qt::AlignCenter,spec.series.at(2).label.isEmpty()
                    ?QStringLiteral("w"):spec.series.at(2).label);
    p->restore();

    // ---- The check. Sorted by where they land on the middle scale, the w
    // values must never go backwards; the largest step backwards is how badly a
    // reading of this nomogram could be wrong, in the units of w.
    double worstBackstep=0.0,runningMax=sorted.first().w;
    for(const Row& row:sorted){
        worstBackstep=qMax(worstBackstep,runningMax-row.w);
        runningMax=qMax(runningMax,row.w);
    }
    const double wSpan=qMax(1e-300,wRange.hi-wRange.lo);
    const double fraction=100.0*worstBackstep/wSpan;

    p->save();
    p->setFont(tickFont);
    p->setPen(fraction>1.0?spec.style.danger:spec.style.foreground);
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*2.2,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,
                fraction>1.0
                ?QStringLiteral("NOT a valid nomogram in these variables: reading "
                                "it could be wrong by %1 in %2, which is %3% of "
                                "the range")
                     .arg(worstBackstep,0,'g',4).arg(spec.series.at(2).label.isEmpty()
                         ?QStringLiteral("w"):spec.series.at(2).label)
                     .arg(fraction,0,'f',1)
                :QStringLiteral("valid: the middle scale is single-valued, largest "
                                "inconsistency %1% of the range")
                     .arg(fraction,0,'f',2));
    p->setPen(spec.style.foreground);
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.1,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,
                QStringLiteral("%1 readings; a straight line across the three "
                               "scales solves h(w) = u + v - map the columns "
                               "through log first for a product")
                    .arg(readings.size()));
    p->restore();
}

// ======================================================================
// Dalitz plot
//
// Two of the three pairwise invariant masses of a three-body decay, plotted
// against each other. What makes it a Dalitz plot rather than a scatter of two
// numbers is the KINEMATIC BOUNDARY: energy and momentum conservation confine
// every possible event to one closed region, and phase space is UNIFORM inside
// it. So structure in the picture is physics - a resonance is a band, an
// interference is a hole - and structure that would be there anyway is not,
// because the flat background has already been removed by the choice of
// variables. Without the boundary drawn, none of that is readable and the
// figure is a two-dimensional histogram wearing a physicist's name.
//
// The boundary needs the parent mass and the three daughter masses, and those
// are CONSTANTS rather than per-row measurements. There is no mechanism in this
// program for handing an engine a scalar, so they arrive as four more mapped
// columns holding the same value in every row. That is clumsy to set up and it
// is honest: the engine checks that they really are constant and says so when
// they are not, rather than quietly using whatever happened to be in the first
// row.
//
// Six mapped columns: m2(12), m2(23), then M, m1, m2, m3.
//
// The boundary is the standard one (PDG, Kinematics). At fixed s12, work in the
// rest frame of the (12) system, where
//
//     E2* = (s12 - m1^2 + m2^2) / (2 sqrt(s12))
//     E3* = (M^2  - s12  - m3^2) / (2 sqrt(s12))
//
// and s23 runs between the values taken when 2 and 3 are back to back and when
// they are aligned:
//
//     s23 max/min = (E2* + E3*)^2 - ( sqrt(E2*^2 - m2^2) -/+ sqrt(E3*^2 - m3^2) )^2
//
// which for three massless daughters collapses to the triangle s12 + s23 <= M^2
// - a case with an exact answer, and the one this is checked against.
void QtPlotBackend::drawDalitz(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    const QFont tickFont=font(spec,qMax(6.0,spec.style.tickSize-0.5));
    const QFontMetricsF fm(tickFont,p->device());

    if(spec.series.size()<2){
        p->save();
        p->setFont(tickFont);
        p->setPen(spec.style.danger);
        p->drawText(f.plotArea.adjusted(8,8,-8,-8),Qt::AlignLeft|Qt::AlignTop,
                    QStringLiteral("needs two columns: m2(12) and m2(23). The four "
                                   "masses are engine settings, beside the mapping"));
        p->restore();
        return;
    }

    const QVector<double>& s12=spec.series.at(0).y;
    const QVector<double>& s23=spec.series.at(1).y;
    int rows=s12.size();
    for(const PlotSeries& s:spec.series) rows=qMin(rows,int(s.y.size()));
    if(rows<1) return;

    // The four masses. They are properties of the decay rather than of an
    // event, so they come from the engine's settings - and from four extra
    // columns when six are mapped, which is the form this engine shipped as
    // and which any figure made before there were settings still carries.
    //
    // Columns win when they are there. A person who mapped six columns did so
    // deliberately and their file is the more specific answer; silently
    // preferring a settings default over a column they chose would change
    // their figure under them.
    static const char* kKeys[4]={"parentMass","daughter1","daughter2","daughter3"};
    const QMap<QString,double> fallback=engineParameterDefaults(spec.engine);
    double mass[4]={0,0,0,0};
    bool steady[4]={true,true,true,true};
    double drift[4]={0,0,0,0};
    const bool fromColumns=spec.series.size()>=6;
    for(int c=0;c<4;++c){
        const QString key=QString::fromLatin1(kKeys[c]);
        if(!fromColumns){
            mass[c]=spec.parameter(key,fallback.value(key,0.0));
            continue;
        }
        // A column that varies is not a parameter, and using its first row
        // would be inventing one.
        const QVector<double>& column=spec.series.at(2+c).y;
        double lo=std::numeric_limits<double>::infinity(),hi=-lo;
        for(int i=0;i<rows;++i){
            if(!finite(column[i])) continue;
            lo=qMin(lo,column[i]); hi=qMax(hi,column[i]);
        }
        if(!finite(lo)){ steady[c]=false; continue; }
        mass[c]=0.5*(lo+hi);
        drift[c]=hi-lo;
        // A tolerance rather than exact equality, because these will have come
        // through a spreadsheet and a text file.
        steady[c]=(hi-lo)<=1e-9*qMax(1.0,std::abs(mass[c]));
    }
    const double parent=mass[0],m1=mass[1],m2=mass[2],m3=mass[3];
    bool constantsOk=steady[0]&&steady[1]&&steady[2]&&steady[3];
    const bool massesUsable=(parent>m1+m2+m3)&&(m1>=0.0)&&(m2>=0.0)&&(m3>=0.0);

    // Where the two edges of the region sit at one value of s12. Used for BOTH
    // the drawing and the containment test, and evaluated at each event's own
    // s12 rather than interpolated between drawn samples.
    //
    // That distinction is not pedantic. The first version sampled the boundary
    // four hundred times and interpolated the test between those samples, which
    // on a curve that bulges outward puts every chord INSIDE the true edge - so
    // sixteen of three thousand simulated events, all of them legitimately near
    // the rim, were reported as impossible. The closed form costs two square
    // roots per event and is exact.
    const auto edgesAt=[&](double s,double& lo,double& hi){
        if(!(s>0.0)) return false;
        const double root=std::sqrt(s);
        const double e2=(s-m1*m1+m2*m2)/(2.0*root);
        const double e3=(parent*parent-s-m3*m3)/(2.0*root);
        const double p2sq=e2*e2-m2*m2,p3sq=e3*e3-m3*m3;
        if(!(p2sq>=0.0)||!(p3sq>=0.0)) return false;
        const double p2=std::sqrt(p2sq),p3=std::sqrt(p3sq);
        const double sum=(e2+e3)*(e2+e3);
        hi=sum-(p2-p3)*(p2-p3);
        lo=sum-(p2+p3)*(p2+p3);
        return true;
    };

    // ---- The boundary, when the masses allow one at all.
    QPolygonF upper,lower;
    double sFrom=0.0,sTo=0.0;
    if(massesUsable){
        sFrom=(m1+m2)*(m1+m2);
        sTo=(parent-m3)*(parent-m3);
        const int steps=600;
        for(int i=0;i<=steps;++i){
            const double s=sFrom+(sTo-sFrom)*double(i)/double(steps);
            double lo=0.0,hi=0.0;
            if(!edgesAt(s,lo,hi)) continue;
            upper.append(QPointF(s,hi));
            lower.append(QPointF(s,lo));
        }
    }

    // How many events fall outside it. This is the number that says whether the
    // masses supplied are the masses the data came from: with the right ones it
    // is zero, and with the wrong ones it is large. A picture alone cannot be
    // read that precisely near an edge, which is exactly where the interesting
    // events are.
    int outside=0;
    if(massesUsable&&!upper.isEmpty()){
        for(int i=0;i<rows;++i){
            if(!finite(s12[i])||!finite(s23[i])) continue;
            const double slack=1e-9*qMax(1.0,std::abs(s23[i]));
            if(s12[i]<sFrom-slack||s12[i]>sTo+slack){ ++outside; continue; }
            double lo=0.0,hi=0.0;
            if(!edgesAt(qBound(sFrom,s12[i],sTo),lo,hi)){ ++outside; continue; }
            if(s23[i]>hi+slack||s23[i]<lo-slack) ++outside;
        }
    }

    p->save();
    if(!upper.isEmpty()){
        QPainterPath region;
        region.moveTo(toDevice(f,upper.first().x(),upper.first().y()));
        for(const QPointF& pt:upper) region.lineTo(toDevice(f,pt.x(),pt.y()));
        for(int i=lower.size()-1;i>=0;--i)
            region.lineTo(toDevice(f,lower[i].x(),lower[i].y()));
        region.closeSubpath();
        // Filled faintly as well as outlined. Phase space is uniform inside it,
        // so the region is a statement about where events CAN be - a bare
        // outline reads as a curve drawn through the data rather than as a
        // limit imposed on it.
        QColor wash=spec.style.foreground;
        wash.setAlphaF(0.05);
        p->setPen(QPen(spec.style.foreground,1.1));
        p->setBrush(wash);
        p->drawPath(region);
    }

    // ---- The events. Small and translucent, because the density is the
    // reading and a filled marker large enough to see individually saturates
    // wherever there is anything to see.
    p->setPen(Qt::NoPen);
    QColor dot=spec.series.at(0).color;
    dot.setAlphaF(rows>4000?0.25:(rows>800?0.45:0.75));
    p->setBrush(dot);
    const double size=qMax(1.0,qMin(2.6,spec.style.lineWidth*1.4));
    for(int i=0;i<rows;++i){
        if(!finite(s12[i])||!finite(s23[i])) continue;
        p->drawEllipse(toDevice(f,s12[i],s23[i]),size,size);
    }
    p->restore();

    p->save();
    p->setFont(tickFont);
    QStringList notes;
    if(!constantsOk){
        QStringList named;
        static const char* kWhich[4]={"parent mass","m1","m2","m3"};
        for(int c=0;c<4;++c)
            if(!steady[c])
                named.append(QStringLiteral("%1 varies by %2")
                                 .arg(QString::fromLatin1(kWhich[c]))
                                 .arg(drift[c],0,'g',3));
        notes.append(QStringLiteral("NOT constant: %1 - these are parameters, "
                                    "so no boundary is drawn")
                         .arg(named.join(QStringLiteral(", "))));
    }else if(!massesUsable){
        notes.append(QStringLiteral("the parent mass must exceed the three "
                                    "daughters together; no boundary is drawn"));
    }else{
        // Which of the two it read matters: a figure whose masses look wrong
        // is fixed in one place or the other, and the person cannot tell
        // which without being told.
        notes.append(QStringLiteral("M %1, daughters %2 %3 %4 (%5)")
                         .arg(parent,0,'g',5).arg(m1,0,'g',4)
                         .arg(m2,0,'g',4).arg(m3,0,'g',4)
                         .arg(fromColumns?QStringLiteral("from columns 3-6")
                                         :QStringLiteral("engine settings")));
        notes.append(outside==0
            ?QStringLiteral("all %1 events inside the boundary").arg(rows)
            :QStringLiteral("%1 of %2 events OUTSIDE the boundary - the masses "
                            "or the variables do not match the data")
                 .arg(outside).arg(rows));
    }
    p->setPen((!constantsOk||!massesUsable||outside>0)?spec.style.danger
                                                     :spec.style.foreground);
    p->drawText(QRectF(f.plotArea.left()+4.0,f.plotArea.top()+2.0,
                       f.plotArea.width()-8.0,fm.height()),
                Qt::AlignLeft|Qt::AlignTop,notes.join(QStringLiteral("   ")));
    p->restore();
}

// ======================================================================
// Soil texture triangle
//
// A classification diagram, and the first thing in this catalogue that is one.
// Every other engine derives its picture from the data; this one is a fixed map
// of named regions with the sample placed on it, so the whole value is in the
// boundaries being right. A texture triangle drawn from memory would look
// entirely convincing and would tell somebody their soil is a clay loam when it
// is a silty clay loam, which is a different and worse failure than a wrong
// contour: the figure is not describing the data, it is answering a question.
//
// So neither set of boundaries below was drawn from memory.
//
// The UK regions are the Soil Survey of England and Wales boundaries - clay at
// 18, 35 and 55 per cent, and the sandy/silty splits - taken from the vertex
// table in the `soiltexture` R package (Moeys), which for this system sits on
// the published values rather than on a rounded plotting grid.
//
// The USDA regions were not transcribed from a drawing at all. They are
// COMPUTED from the class definitions in the NRCS Soil Survey Manual - "sand:
// 85 per cent or more sand, and the percentage of silt plus 1.5 times the
// percentage of clay not exceeding 15", and so on - by clipping the whole
// triangle with each inequality in turn. Nothing was read off a picture, and
// the two classes whose definition is a disjunction (sandy loam, silt loam)
// come out as two pieces each, which is what they are.
//
// Both sets were checked by the one identity a classification cannot escape:
// the regions must TILE the triangle. Summed in exact rational arithmetic, each
// system's classes come to 5000 against the triangle's own 5000, difference
// zero - so there is no composition that falls in no class and none that falls
// in two. A single mistyped vertex breaks that immediately.
//
// Three mapped columns: per cent sand, per cent silt, per cent clay, in the
// order a particle-size analysis reports them. Rows are normalised, because an
// analysis that sums to 99.7 is the normal case and not an error.

void QtPlotBackend::drawSoilTexture(QPainter* p,const QRectF& target,
                                    const PlotSpec& spec,bool uk) const {
    drawFloatingTitle(p,target,spec);
    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    p->setFont(tickFont);

    const Region* regions=uk?kUk:kUsda;
    const int regionCount=uk?int(sizeof(kUk)/sizeof(kUk[0]))
                            :int(sizeof(kUsda)/sizeof(kUsda[0]));

    const double height=std::sqrt(3.0)/2.0;
    const double margin=fm.height()*2.6;
    // TWO FOOTNOTE LINES ARE DRAWN BELOW THIS - the class census and the source
    // - and nothing reserved room for them, so the triangle's base sat at
    // almost exactly the height the census is written at. The result was the
    // census running straight through the bottom-left corner's own labels:
    // "sand" and "clay 1" on top of one another, at the one corner a reader
    // checks first.
    //
    // Reserved where the geometry is decided rather than patched afterwards.
    // Measuring the census and truncating it does not fix this on its own: the
    // line FITS the figure's width, it just lands on top of the diagram.
    const double footnotes=fm.height()*2.6;
    const double usableW=qMax(20.0,target.width()-2.0*margin);
    const double usableH=qMax(20.0,target.height()-2.0*margin-footnotes
                              -(spec.title.isEmpty()?0.0:fm.height()*2.0));
    const double scale=qMin(usableW,usableH/height);
    const double originX=target.center().x()-scale*0.5;
    const double originY=target.bottom()-margin*0.9-footnotes;
    // Sand at the bottom left, silt at the bottom right, clay at the apex,
    // which is how both systems are always printed.
    const auto at=[&](double clay,double silt){
        const double x=(silt+clay*0.5)/100.0;
        return QPointF(originX+x*scale,originY-(clay/100.0)*height*scale);
    };

    p->save();
    p->setPen(QPen(spec.style.gridColor,0.6));
    for(int step=1;step<10;++step){
        const double f=double(step)*10.0;
        p->drawLine(at(f,0.0),at(f,100.0-f));            // constant clay
        p->drawLine(at(0.0,f),at(100.0-f,f));            // constant silt
        p->drawLine(at(0.0,100.0-f),at(f,100.0-f));      // constant sand
    }
    p->restore();

    // ---- The regions, and their names at their own centroids.
    p->save();
    // Colour and label by CLASS, not by polygon. Two of the USDA classes are a
    // union of two pieces, and colouring each piece by its position in the
    // table gave sandy loam and silt loam two colours apiece and printed each
    // name twice - which on a classification diagram reads as four classes
    // where there are two.
    const auto firstWithName=[&](int r){
        for(int q=0;q<r;++q)
            if(qstrcmp(regions[q].name,regions[r].name)==0) return q;
        return r;
    };
    for(int r=0;r<regionCount;++r){
        QPolygonF shape;
        double cx=0.0,cy=0.0;
        for(int i=0;i<regions[r].count;++i){
            const double clay=regions[r].points[i*2],silt=regions[r].points[i*2+1];
            shape<<at(clay,silt);
            cx+=clay; cy+=silt;
        }
        cx/=double(regions[r].count); cy/=double(regions[r].count);
        const int hueFrom=firstWithName(r);
        QColor fill=categoryColour(spec,hueFrom,std::fmod(0.06+0.618034*double(hueFrom),1.0),
                                     0.16,0.99);
        p->setBrush(fill);
        p->setPen(QPen(spec.style.foreground,0.8));
        p->drawPolygon(shape);

        // The name goes on the LARGEST piece of its class, once.
        double widest=shape.boundingRect().width();
        bool biggest=true;
        for(int q=0;q<regionCount;++q){
            if(q==r||qstrcmp(regions[q].name,regions[r].name)!=0) continue;
            QPolygonF other;
            for(int i=0;i<regions[q].count;++i)
                other<<at(regions[q].points[i*2],regions[q].points[i*2+1]);
            if(other.boundingRect().width()>widest) biggest=false;
        }
        if(!biggest) continue;
        const QPointF middle=at(cx,cy);
        p->setPen(spec.style.foreground);
        const QString name=QString::fromLatin1(regions[r].name);
        // Only where it fits. A name wider than its own region, printed anyway,
        // labels its neighbours as well as itself.
        if(fm.horizontalAdvance(name)<shape.boundingRect().width()+8.0)
            p->drawText(QRectF(middle.x()-70.0,middle.y()-fm.height()*0.5,140.0,fm.height()),
                        Qt::AlignCenter,name);
    }
    p->setPen(QPen(spec.style.foreground,1.2));
    p->setBrush(Qt::NoBrush);
    p->drawPolyline(QPolygonF()<<at(0,0)<<at(0,100)<<at(100,0)<<at(0,0));

    p->setPen(spec.style.foreground);
    const double pad=fm.height()*0.8;
    p->drawText(QRectF(at(100,0).x()-70.0,at(100,0).y()-fm.height()-pad*0.4,140.0,fm.height()),
                Qt::AlignCenter,QStringLiteral("clay %"));
    p->drawText(QRectF(at(0,0).x()-140.0,at(0,0).y()+pad*0.3,140.0,fm.height()),
                Qt::AlignRight|Qt::AlignVCenter,QStringLiteral("sand %"));
    p->drawText(QRectF(at(0,100).x(),at(0,100).y()+pad*0.3,140.0,fm.height()),
                Qt::AlignLeft|Qt::AlignVCenter,QStringLiteral("silt %"));
    p->restore();

    if(spec.series.size()<3){
        p->save();
        p->setPen(spec.style.foreground);
        p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.2,
                           target.width(),fm.height()),
                    Qt::AlignHCenter|Qt::AlignVCenter,
                    QStringLiteral("needs three columns: per cent sand, silt and clay"));
        p->restore();
        return;
    }

    // ---- The samples.
    const QVector<double>& sandCol=spec.series.at(0).y;
    const QVector<double>& siltCol=spec.series.at(1).y;
    const QVector<double>& clayCol=spec.series.at(2).y;
    int rows=sandCol.size();
    for(const PlotSeries& s:spec.series) rows=qMin(rows,int(s.y.size()));

    QVector<int> tally(regionCount,0);
    int drawn=0,unclassified=0;
    p->save();
    for(int i=0;i<rows;++i){
        if(!finite(sandCol[i])||!finite(siltCol[i])||!finite(clayCol[i])) continue;
        const double total=std::abs(sandCol[i])+std::abs(siltCol[i])+std::abs(clayCol[i]);
        if(!(total>0.0)) continue;
        const double clay=100.0*std::abs(clayCol[i])/total;
        const double silt=100.0*std::abs(siltCol[i])/total;
        int found=-1;
        for(int r=0;r<regionCount&&found<0;++r)
            if(insideRegion(regions[r],clay,silt)) found=r;
        if(found>=0) ++tally[found]; else ++unclassified;
        const QColor colour=spec.series.at(0).color;
        p->setPen(QPen(colour,1.0));
        p->setBrush(QColor(colour.red(),colour.green(),colour.blue(),170));
        const double size=qMax(2.4,spec.style.lineWidth*2.0);
        p->drawEllipse(at(clay,silt),size,size);
        ++drawn;
    }
    p->restore();

    // ---- What each sample was called. The counts are the answer the diagram
    // exists to give; reading them off the dots is what it is meant to save.
    QVector<QPair<QString,int>> census;
    for(int r=0;r<regionCount;++r){
        if(tally[r]<=0) continue;
        const QString name=QString::fromLatin1(regions[r].name);
        // The two-piece classes appear twice in the table and must be reported
        // once: a sample in either piece of sandy loam is a sandy loam.
        bool merged=false;
        for(int q=0;q<r;++q)
            if(tally[q]>0&&QString::fromLatin1(regions[q].name)==name) merged=true;
        if(merged) continue;
        int count=0;
        for(int q=0;q<regionCount;++q)
            if(QString::fromLatin1(regions[q].name)==name) count+=tally[q];
        census.append(qMakePair(name,count));
    }
    // THE BUSIEST CLASSES FIRST, because the line may not hold all of them.
    // Ties by name, so the same data gives the same sentence every time.
    std::sort(census.begin(),census.end(),
              [](const QPair<QString,int>& a,const QPair<QString,int>& b){
                  if(a.second!=b.second) return a.second>b.second;
                  return a.first<b.first;
              });

    // AND IT HAS TO FIT. Every populated class was listed on one line the width
    // of the figure, with nothing measuring it - so a survey touching ten of
    // the eleven classes wrote straight through the triangle's own corner
    // labels, and the bottom left of the diagram read "sand" and "clay 1" on
    // top of one another. The census is the answer the diagram exists to give,
    // so it is truncated rather than dropped, and it says how many it left out.
    //
    // Measured with the font metrics rather than cut at a guessed number of
    // characters: the widths differ with the theme's font and with the class
    // names, and a character count that fits "sand 1" does not fit
    // "sandy clay loam 1".
    QString censusLine=QStringLiteral("%1 samples: ").arg(drawn);
    {
        const double room=qMax(40.0,target.width()-16.0);
        int shown=0;
        for(const QPair<QString,int>& entry:std::as_const(census)){
            const QString piece=QStringLiteral("%1%2 %3")
                .arg(shown?QStringLiteral(", "):QString())
                .arg(entry.first).arg(entry.second);
            const int remaining=census.size()-shown-1;
            const QString tail=remaining>0
                ? QStringLiteral(", and %1 more").arg(remaining) : QString();
            if(fm.horizontalAdvance(censusLine+piece+tail)>room) break;
            censusLine+=piece;
            ++shown;
        }
        if(shown<census.size())
            censusLine+=QStringLiteral(", and %1 more").arg(census.size()-shown);
    }
    p->save();
    p->setPen(unclassified>0?spec.style.danger:spec.style.foreground);
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*2.3,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,
                unclassified>0
                ?QStringLiteral("%1 samples, %2 in NO class - the regions should "
                                "tile the triangle, so this is a fault in them")
                     .arg(drawn).arg(unclassified)
                :censusLine);
    p->setPen(spec.style.foreground);
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.1,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,
                uk?QStringLiteral("Soil Survey of England and Wales; rows normalised to 100")
                  :QStringLiteral("USDA, regions computed from the NRCS Soil Survey Manual "
                                  "definitions; rows normalised to 100"));
    p->restore();
}

// ======================================================================
// QAPF diagram (Streckeisen)
//
// The double triangle that names an igneous rock from its modal mineralogy.
// Quartz at the top apex, feldspathoid at the bottom, alkali feldspar on the
// left and plagioclase on the right - two triangles sharing the A-P edge rather
// than a square, because quartz and feldspathoids are mutually exclusive: a
// rock silica-rich enough to crystallise free quartz cannot also crystallise a
// silica-undersaturated mineral. A composition therefore lives in one triangle
// or the other, never in both, and a row claiming both is a measurement error
// rather than a rock.
//
// Like the soil triangles, this is a classification diagram: the picture is a
// fixed map of named regions and the sample is a dot on it, so the whole value
// is in the names being on the right regions. These are not drawn from memory.
// They are read from Figures 11 and 12 of the BGS Rock Classification Scheme,
// Volume 1 (Gillespie and Styles, BGS Research Report RR 99-06), which is the
// IUGS scheme after Streckeisen (1976), and every name was then checked against
// that report's own appendix - "List of approved names for igneous rocks",
// which carries a QAPF field number beside each name. Two independent places in
// one document agreeing is the closest thing to a second source available here.
//
// Two boundary questions that secondary sources kept contradicting each other
// about, settled from the figure:
//
//   - the Q 20-60 band divides at 35 and 65, so "granite" is one name
//     subdivided into syenogranite and monzogranite, not a single field;
//   - the F 10-60 band divides at 10, 50 and 90 - FOUR fields - where every
//     other band divides at 10, 35, 65 and 90 into five.
//
// Four mapped columns: Q, A, P, F as modal percentages. Rows are normalised;
// mafic minerals are excluded before plotting, which is the recalculation the
// scheme requires and not something this engine can do for you.

void QtPlotBackend::drawQapf(QPainter* p,const QRectF& target,const PlotSpec& spec,
                             bool volcanic) const {
    drawFloatingTitle(p,target,spec);
    const QFont tickFont=font(spec,qMax(6.0,spec.style.tickSize-0.5));
    const QFontMetricsF fm(tickFont,p->device());
    p->setFont(tickFont);

    const QapfField* fields=volcanic?kQapfVolcanic:kQapfPlutonic;
    const int fieldCount=volcanic
        ?int(sizeof(kQapfVolcanic)/sizeof(kQapfVolcanic[0]))
        :int(sizeof(kQapfPlutonic)/sizeof(kQapfPlutonic[0]));
    // What a field is called when the scheme does not name it. The number is
    // the answer in that case, and saying so is better than a blank.
    const auto nameOf=[&](int k){
        return (fields[k].name[0]!='\0')
            ?QString::fromLatin1(fields[k].name)
            :QStringLiteral("field %1 (no approved volcanic name)")
                 .arg(QString::fromLatin1(fields[k].number));
    };
    const double height=std::sqrt(3.0)/2.0;
    const double margin=fm.height()*2.2;
    const double usableW=qMax(20.0,target.width()-2.0*margin);
    const double usableH=qMax(20.0,target.height()-2.0*margin
                              -(spec.title.isEmpty()?0.0:fm.height()*2.0));
    // The whole construction spans one unit across and two triangle heights up.
    const double scale=qMin(usableW,usableH/(2.0*height));
    const double originX=target.center().x()-scale*0.5;
    const double originY=target.center().y()
                        +(spec.title.isEmpty()?0.0:fm.height());
    // A is the origin of the construction and P is one unit to its right; the
    // apex is up for quartz and down for feldspathoid.
    const auto at=[&](double apex,double plagRatio,bool foid){
        const double base=(100.0-apex)/100.0;         // what A and P share
        const double x=base*(plagRatio/100.0)+apex/200.0;
        const double y=(apex/100.0)*height*(foid?-1.0:1.0);
        return QPointF(originX+x*scale,originY-y*scale);
    };

    // ---- The fields.
    p->save();
    for(int i=0;i<fieldCount;++i){
        const QapfField& f=fields[i];
        QPolygonF shape;
        shape<<at(f.lo,f.p0,f.foid)<<at(f.lo,f.p1,f.foid)
             <<at(f.hi,f.p1,f.foid)<<at(f.hi,f.p0,f.foid);
        QColor fill=categoryColour(spec,i,std::fmod(0.05+0.618034*double(i),1.0),0.13,0.99);
        p->setBrush(fill);
        p->setPen(QPen(spec.style.gridColor,0.7));
        p->drawPolygon(shape);
        // The FIELD NUMBER, not the name. Several of these names run to four
        // words and the fields they belong to are a few millimetres across, so
        // a name in each one would be a smear; the numbers are how the scheme
        // itself refers to them - "field 10" is what a petrologist says - and
        // the readout below names whatever was actually plotted.
        // Above the field's centre rather than on it, so a rock plotting near
        // the middle of its field does not sit on the number naming it.
        const QPointF middle=at(0.5*(f.lo+f.hi),0.5*(f.p0+f.p1),f.foid)
                            -QPointF(0.0,fm.height()*0.75);
        QColor ink=spec.style.foreground; ink.setAlphaF(0.55);
        p->setPen(ink);
        p->drawText(QRectF(middle.x()-30.0,middle.y()-fm.height()*0.5,60.0,fm.height()),
                    Qt::AlignCenter,QString::fromLatin1(f.number));
    }
    // The two triangles' own outlines over the field boundaries, so the shape of
    // the construction reads before its subdivisions do.
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(spec.style.foreground,1.2));
    p->drawPolyline(QPolygonF()<<at(0,0,false)<<at(100,0,false)<<at(0,100,false)
                               <<at(0,0,false));
    p->drawPolyline(QPolygonF()<<at(0,0,true)<<at(100,0,true)<<at(0,100,true)
                               <<at(0,0,true));
    p->setPen(spec.style.foreground);
    p->drawText(QRectF(at(100,0,false).x()-60.0,at(100,0,false).y()-fm.height()*1.6,
                       120.0,fm.height()),Qt::AlignCenter,QStringLiteral("Q"));
    p->drawText(QRectF(at(100,0,true).x()-60.0,at(100,0,true).y()+fm.height()*0.5,
                       120.0,fm.height()),Qt::AlignCenter,QStringLiteral("F"));
    p->drawText(QRectF(at(0,0,false).x()-64.0,at(0,0,false).y()-fm.height()*0.5,
                       60.0,fm.height()),Qt::AlignRight|Qt::AlignVCenter,
                QStringLiteral("A"));
    p->drawText(QRectF(at(0,100,false).x()+4.0,at(0,100,false).y()-fm.height()*0.5,
                       60.0,fm.height()),Qt::AlignLeft|Qt::AlignVCenter,
                QStringLiteral("P"));
    p->restore();

    if(spec.series.size()<4){
        p->save();
        p->setPen(spec.style.foreground);
        p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.2,
                           target.width(),fm.height()),
                    Qt::AlignHCenter|Qt::AlignVCenter,
                    QStringLiteral("needs four columns: modal per cent Q, A, P and F"));
        p->restore();
        return;
    }

    // ---- The samples.
    const QVector<double>& qCol=spec.series.at(0).y;
    const QVector<double>& aCol=spec.series.at(1).y;
    const QVector<double>& pCol=spec.series.at(2).y;
    const QVector<double>& fCol=spec.series.at(3).y;
    int rows=qCol.size();
    for(const PlotSeries& s:spec.series) rows=qMin(rows,int(s.y.size()));

    QVector<int> tally(fieldCount,0);
    int drawn=0,both=0,unnamed=0;
    p->save();
    for(int i=0;i<rows;++i){
        if(!finite(qCol[i])||!finite(aCol[i])||!finite(pCol[i])||!finite(fCol[i])) continue;
        const double q=std::abs(qCol[i]),a=std::abs(aCol[i]);
        const double pl=std::abs(pCol[i]),fo=std::abs(fCol[i]);
        // Quartz and feldspathoid together is not a rock this diagram can name,
        // and silently dropping one of them would name it anyway - wrongly.
        if(q>0.0&&fo>0.0){ ++both; continue; }
        const double apex=(fo>0.0)?fo:q;
        const bool foid=(fo>0.0);
        const double total=apex+a+pl;
        if(!(total>0.0)) continue;
        const double apexPct=100.0*apex/total;
        const double feldspar=a+pl;
        const double ratio=(feldspar>0.0)?100.0*pl/feldspar:50.0;

        int found=-1;
        for(int k=0;k<fieldCount&&found<0;++k){
            const QapfField& f=fields[k];
            if(f.foid!=foid) continue;
            if(apexPct<f.lo||apexPct>f.hi) continue;
            if(ratio<f.p0||ratio>f.p1) continue;
            found=k;
        }
        if(found>=0) ++tally[found]; else ++unnamed;
        const QColor colour=spec.series.at(0).color;
        p->setPen(QPen(colour,1.0));
        p->setBrush(QColor(colour.red(),colour.green(),colour.blue(),180));
        const double size=qMax(2.4,spec.style.lineWidth*2.0);
        p->drawEllipse(at(apexPct,ratio,foid),size,size);
        ++drawn;
    }
    p->restore();

    // ---- What they were called. The names are the answer; the dots are only
    // where the answer came from.
    QStringList named;
    int occupied=0;
    for(int k=0;k<fieldCount;++k){
        if(tally[k]<=0) continue;
        ++occupied;
        named.append(QStringLiteral("%1 %2").arg(nameOf(k)).arg(tally[k]));
    }
    // The names when there are few enough to read, the count of fields when
    // there are not - and the trouble ALONGSIDE either, never instead of it.
    // The first version replaced the names with the warning, so a set with one
    // bad row reported nothing about the good ones.
    QString summary=(occupied>0&&occupied<=6)
        ?QStringLiteral("%1 rocks: %2").arg(drawn).arg(named.join(QStringLiteral(", ")))
        :QStringLiteral("%1 rocks across %2 fields").arg(drawn).arg(occupied);
    QStringList trouble;
    if(both>0)
        trouble.append(QStringLiteral("%1 rows have BOTH quartz and feldspathoid, "
                                      "which no rock has").arg(both));
    if(unnamed>0)
        trouble.append(QStringLiteral("%1 in no field").arg(unnamed));
    if(!trouble.isEmpty())
        summary+=QStringLiteral("; ")+trouble.join(QStringLiteral("; "));
    p->save();
    p->setPen((both>0||unnamed>0)?spec.style.danger:spec.style.foreground);
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*2.3,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,summary);
    p->setPen(spec.style.foreground);
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.1,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,
                volcanic
                    ?QStringLiteral("volcanic; IUGS after Streckeisen (1978), fields "
                                    "as BGS RR 99-06 figure 19; M excluded, QAPF "
                                    "normalised to 100")
                    :QStringLiteral("plutonic; IUGS after Streckeisen (1976), fields as "
                                    "BGS RR 99-06 figures 11 and 12; M excluded, QAPF "
                                    "normalised to 100"));
    p->restore();
}

// ======================================================================
// Ternary contour
//
// Three components summing to a whole, and a fourth quantity measured over
// them: a phase field, a yield surface, a property of a mixture. The catalogue
// could scatter compositions and could contour a rectangular field, and had no
// way to do both at once - so a ternary experiment could show WHERE it was
// sampled or WHAT it found, but not both.
//
// The contours are of the piecewise-linear interpolant over the Delaunay
// triangulation of the samples, which is the choice that adds nothing. Inside
// each triangle the surface is the plane through its three measured corners, so
// a contour there is one straight segment and the arithmetic is exact; there is
// no smoothing parameter, no radius, and no kernel to pick - and nothing is
// drawn outside the hull, because a value at a composition that no three
// measurements surround would be an extrapolation wearing the same ink as a
// measurement.
//
// Four mapped columns: the three components and the value. Rows are normalised
// first - compositions that do not sum to one are the normal case, not an
// error - and the projection is the one Ternary Scatter uses, so a scatter and
// a contour of the same experiment lie on top of each other.
void QtPlotBackend::drawTernaryContour(QPainter* p,const QRectF& target,
                                       const PlotSpec& spec) const {
    drawFloatingTitle(p,target,spec);
    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    p->setFont(tickFont);

    const double height=std::sqrt(3.0)/2.0;
    const double margin=fm.height()*2.4;
    const double usableW=qMax(20.0,target.width()-2.0*margin);
    const double usableH=qMax(20.0,target.height()-2.0*margin
                              -(spec.title.isEmpty()?0.0:fm.height()*2.0));
    const double scale=qMin(usableW,usableH/height);
    const double originX=target.center().x()-scale*0.5;
    const double originY=target.bottom()-margin*0.8;
    const auto at=[&](double x,double y){
        return QPointF(originX+x*scale,originY-y*scale);
    };
    // The same placement as Ternary Scatter: the first component at the bottom
    // right, the second at the bottom left, the third at the apex.
    const auto place=[&](double pa,double pc){
        return QPointF(pa+pc*0.5,pc*height);
    };

    // ---- The triangle and its ten per cent grid.
    p->save();
    p->setPen(QPen(spec.style.gridColor,0.6));
    for(int step=1;step<10;++step){
        const double f=double(step)/10.0;
        const QPointF a1=place(f,0.0),a2=place(f,1.0-f);       // constant first
        const QPointF b1=place(0.0,f),b2=place(1.0-f,f);       // constant third
        const QPointF c1=place(1.0-f,0.0),c2=place(0.0,1.0-f); // constant second
        p->drawLine(at(a1.x(),a1.y()),at(a2.x(),a2.y()));
        p->drawLine(at(b1.x(),b1.y()),at(b2.x(),b2.y()));
        p->drawLine(at(c1.x(),c1.y()),at(c2.x(),c2.y()));
    }
    p->setPen(QPen(spec.style.foreground,1.0));
    p->drawPolyline(QPolygonF()<<at(0,0)<<at(1,0)<<at(0.5,height)<<at(0,0));
    p->restore();

    if(spec.series.size()<4){
        p->save();
        p->setPen(spec.style.foreground);
        p->drawText(target,Qt::AlignHCenter|Qt::AlignBottom,
                    QStringLiteral("needs four columns: three components and the value over them"));
        p->restore();
        return;
    }

    p->save();
    p->setPen(spec.style.foreground);
    const double pad=fm.height()*0.7;
    const auto corner=[&](const QPointF& where,const QString& text,int flags,
                          double dx,double dy){
        const QPointF screen=at(where.x(),where.y());
        const double width=140.0;
        double left=screen.x()-width*0.5+dx;
        if(flags&Qt::AlignRight)     left=screen.x()-width+dx;
        else if(flags&Qt::AlignLeft) left=screen.x()+dx;
        p->drawText(QRectF(left,screen.y()-fm.height()*0.5+dy,width,fm.height()),
                    flags,text);
    };
    corner(QPointF(1,0),spec.series.at(0).label.isEmpty()
               ?QStringLiteral("A"):spec.series.at(0).label,
           Qt::AlignLeft|Qt::AlignVCenter,6.0,pad*0.6);
    corner(QPointF(0,0),spec.series.at(1).label.isEmpty()
               ?QStringLiteral("B"):spec.series.at(1).label,
           Qt::AlignRight|Qt::AlignVCenter,-6.0,pad*0.6);
    corner(QPointF(0.5,height),spec.series.at(2).label.isEmpty()
               ?QStringLiteral("C"):spec.series.at(2).label,
           Qt::AlignHCenter|Qt::AlignVCenter,0.0,-pad);
    p->restore();

    // ---- The samples, in the projected plane, with their value attached.
    const QVector<double>& ca=spec.series.at(0).y;
    const QVector<double>& cb=spec.series.at(1).y;
    const QVector<double>& cc=spec.series.at(2).y;
    const QVector<double>& cv=spec.series.at(3).y;
    int rows=ca.size();
    for(const PlotSeries& s:spec.series) rows=qMin(rows,int(s.y.size()));

    QVector<ScatterPoint> points;
    for(int i=0;i<rows;++i){
        if(!finite(ca[i])||!finite(cb[i])||!finite(cc[i])||!finite(cv[i])) continue;
        const double total=std::abs(ca[i])+std::abs(cb[i])+std::abs(cc[i]);
        if(!(total>0.0)) continue;
        const QPointF where=place(std::abs(ca[i])/total,std::abs(cc[i])/total);
        ScatterPoint point;
        point.x=where.x(); point.y=where.y(); point.v=cv[i];
        points.append(point);
    }
    if(points.size()<3){
        p->save();
        p->setPen(spec.style.foreground);
        p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.2,
                           target.width(),fm.height()),
                    Qt::AlignHCenter|Qt::AlignVCenter,
                    QStringLiteral("three compositions at least are needed to contour between them"));
        p->restore();
        return;
    }

    double lowValue=points.first().v,highValue=lowValue;
    for(const ScatterPoint& point:points){
        lowValue=qMin(lowValue,point.v); highValue=qMax(highValue,point.v);
    }

    // ASKED BEFORE CALLING, so that too many compositions is a sentence rather
    // than an empty triangle. The triangulation is quadratic (see `delaunay`),
    // and this engine used to hand it every row: 24,000 compositions ran past
    // three quarters of a minute and were still going when the probe killed
    // it. A contour map of 24,000 samples over a triangle 500 pixels on a side
    // would not have been readable in any case.
    if(points.size()>kDelaunayLimit){
        p->save();
        p->setPen(spec.style.foreground);
        p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.2,
                           target.width(),fm.height()),
                    Qt::AlignHCenter|Qt::AlignVCenter,
                    QStringLiteral("%1 compositions; contouring between more than "
                                   "%2 is not drawn - the triangulation costs the "
                                   "square of the count. Aggregate or filter first.")
                        .arg(points.size()).arg(kDelaunayLimit));
        p->restore();
        return;
    }
    const QVector<Triangle> mesh=delaunay(points);
    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);

    // Levels on a round step through the range, so the numbers on the figure
    // are ones somebody would have chosen.
    QVector<double> levels;
    const double step=niceStep((highValue-lowValue)/7.0);
    if(step>0.0)
        for(double level=std::ceil(lowValue/step)*step;level<=highValue;level+=step)
            levels.append(level);
    if(levels.isEmpty()) levels.append(0.5*(lowValue+highValue));

    p->save();
    int segments=0;
    struct Label { QPointF where; QString text; QColor colour; };
    QVector<Label> labels;
    for(double level:levels){
        const double t=(highValue>lowValue)
                       ? rampPosition(spec.style,level,lowValue,highValue) : 0.5;
        QPen pen(colourMapStyled(cmap,spec.style,t));
        pen.setWidthF(qMax(1.1,spec.style.lineWidth));
        pen.setCapStyle(Qt::RoundCap);
        p->setPen(pen);
        QVector<QPointF> midpoints;
        for(const Triangle& tri:mesh){
            const ScatterPoint* corners[3]={&points[tri.a],&points[tri.b],&points[tri.c]};
            // Where the plane through the three measured corners crosses this
            // level: at most two of the triangle's three edges, and exactly two
            // whenever the level passes through it at all.
            QPointF crossing[3]; int found=0;
            for(int e=0;e<3;++e){
                const ScatterPoint& u=*corners[e];
                const ScatterPoint& v=*corners[(e+1)%3];
                const double du=u.v-level,dv=v.v-level;
                if((du<0.0)==(dv<0.0)) continue;      // no crossing on this edge
                if(std::abs(dv-du)<1e-300) continue;
                const double f=du/(du-dv);
                if(found<3)
                    crossing[found++]=QPointF(u.x+f*(v.x-u.x),u.y+f*(v.y-u.y));
            }
            if(found!=2) continue;
            p->drawLine(at(crossing[0].x(),crossing[0].y()),
                        at(crossing[1].x(),crossing[1].y()));
            ++segments;
            midpoints.append(at(0.5*(crossing[0].x()+crossing[1].x()),
                                0.5*(crossing[0].y()+crossing[1].y())));
        }
        // The MIDDLE segment of the contour, not the first one. The first is
        // wherever the triangulation happens to start, which for a level that
        // runs to the edge of the triangle is on the edge - so every label sat
        // half outside the frame, on top of the sample point that put it there.
        if(!midpoints.isEmpty())
            labels.append({midpoints.at(midpoints.size()/2),
                           QString::number(level,'g',4),p->pen().color()});
    }

    // The samples over the contours. A contour map without its measurements
    // cannot be told from a smooth invention, and here the triangulation makes
    // the difference visible: the lines bend at the sample points and only
    // there.
    p->setPen(Qt::NoPen);
    p->setBrush(spec.style.foreground);
    for(const ScatterPoint& point:points)
        p->drawEllipse(at(point.x,point.y),2.0,2.0);

    // Labels last of all, over both the contours and the samples: a number
    // hidden under the dot that produced it is not a label.
    p->setBrush(Qt::NoBrush);
    for(const Label& label:labels){
        const QRectF chip(label.where.x()-fm.horizontalAdvance(label.text)*0.5-2.0,
                          label.where.y()-fm.height()*0.5,
                          fm.horizontalAdvance(label.text)+4.0,fm.height());
        p->fillRect(chip,spec.style.background);
        p->setPen(label.colour);
        p->drawText(chip,Qt::AlignCenter,label.text);
    }
    p->restore();

    p->save();
    p->setPen(spec.style.foreground);
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.2,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,
                QStringLiteral("%1 compositions, %2 triangles, %3 to %4 in steps "
                               "of %5; linear within each triangle, nothing "
                               "outside the hull")
                    .arg(points.size()).arg(mesh.size())
                    .arg(lowValue,0,'g',4).arg(highValue,0,'g',4)
                    .arg(step>0.0?QString::number(step,'g',3)
                                 :QStringLiteral("-")));
    Q_UNUSED(segments);
    p->restore();
}

// ======================================================================
// Streamgraph
//
// A stacked area whose baseline is free. The stack is centred and then allowed
// to wander, which sounds like decoration and is not: on an ordinary stacked
// area every band above the first is distorted by the ones below it, so a
// steady band sitting on a rising one appears to rise, and the eye cannot
// separate a band's own shape from its neighbours'. Letting the baseline move
// spends that distortion where it does least harm.
//
// The baseline is Byron and Wattenberg's "wiggle": the one that minimises the
// sum of the squared slopes of the bands, weighted by thickness. It is
//
//     g0' = -1/(n+1) * sum_i (n - i + 1) f_i'
//
// integrated across the series, started at minus half the total so the figure
// opens centred. Not simply centring at every step - that is a different and
// worse baseline, which throws all of the movement into the outermost bands.
//
// There is no y axis, and that is not an omission. The vertical position of a
// band carries nothing at all here; only its THICKNESS is the value, and an
// axis with numbers on it would invite exactly the misreading the chart is
// built to prevent. The x axis is drawn, because time is still time.
void QtPlotBackend::drawStream(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    drawFloatingTitle(p,target,spec);
    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    p->setFont(tickFont);
    if(spec.series.isEmpty()) return;

    // Every band on the shared x of the first series. A streamgraph adds its
    // bands at each x, so they must be sampled at the same x - bands on
    // different grids cannot be stacked, and interpolating them onto one would
    // invent values the data does not have.
    const QVector<double>& xs=spec.series.at(0).x;
    const int bands=spec.series.size();
    int steps=xs.size();
    for(const PlotSeries& s:spec.series) steps=qMin(steps,int(s.y.size()));
    if(steps<2||bands<1) return;

    QVector<double> x;
    QVector<QVector<double>> value(bands);
    for(int j=0;j<steps;++j){
        if(!finite(xs[j])) continue;
        bool usable=true;
        for(int i=0;i<bands;++i) if(!finite(spec.series.at(i).y[j])) usable=false;
        if(!usable) continue;
        x.append(xs[j]);
        // Negative contributions have no meaning in a stack: a band cannot be
        // less than nothing thick. Clamped rather than dropped, so a column that
        // dips below zero loses only the dip.
        for(int i=0;i<bands;++i) value[i].append(qMax(0.0,spec.series.at(i).y[j]));
    }
    const int n=x.size();
    if(n<2) return;

    QVector<double> baseline(n,0.0);
    {
        double total=0.0;
        for(int i=0;i<bands;++i) total+=value[i][0];
        baseline[0]=-0.5*total;
        for(int j=1;j<n;++j){
            double move=0.0;
            for(int i=0;i<bands;++i)
                move+=double(bands-i)*(value[i][j]-value[i][j-1]);
            baseline[j]=baseline[j-1]-move/double(bands+1);
        }
    }

    double lo=baseline.first(),hi=baseline.first();
    for(int j=0;j<n;++j){
        double top=baseline[j];
        for(int i=0;i<bands;++i) top+=value[i][j];
        lo=qMin(lo,baseline[j]); hi=qMax(hi,top);
    }
    Bounds across=boundsOf(x);
    if(!(across.hi>across.lo)||!(hi>lo)) return;

    // Margins wide enough for the tick labels at BOTH ends, which are centred
    // on their ticks and so need half a label of room outside the frame, and
    // deep enough at the bottom for the axis name and the summary to sit on
    // separate lines.
    const double titleRoom=spec.title.isEmpty()?0.0:fm.height()*2.0;
    const QRectF field(target.left()+fm.height()*1.8,
                       target.top()+titleRoom+fm.height()*0.4,
                       target.width()-fm.height()*3.6,
                       target.height()-titleRoom-fm.height()*4.8);
    if(field.width()<40.0||field.height()<20.0) return;
    const auto at=[&](double xv,double yv){
        return QPointF(field.left()+(xv-across.lo)/(across.hi-across.lo)*field.width(),
                       field.bottom()-(yv-lo)/(hi-lo)*field.height());
    };

    p->save();
    QVector<double> running=baseline;
    for(int i=0;i<bands;++i){
        QPolygonF shape;
        for(int j=0;j<n;++j) shape<<at(x[j],running[j]);
        for(int j=n-1;j>=0;--j) shape<<at(x[j],running[j]+value[i][j]);
        QColor fill=spec.series.at(i).color;
        fill.setAlphaF(0.9);
        p->setBrush(fill);
        // Hairline in the background colour rather than no pen: two adjacent
        // bands of similar colour otherwise merge into one shape whose middle
        // boundary is invisible, which is the one thing that must always be
        // readable here.
        p->setPen(QPen(spec.style.background,0.8));
        p->drawPolygon(shape);
        // Named inside the band, at its thickest point. A legend beside the
        // figure would make the reader carry a colour across the page and back
        // for every band, and a streamgraph has more bands than that survives.
        // The MIDDLE of where the band is thickest, not the first such point.
        // A band that is constant is thickest everywhere, and taking the first
        // index put its name hard against the left edge of the figure with half
        // of it outside.
        double peak=0.0;
        for(int j=0;j<n;++j) peak=qMax(peak,value[i][j]);
        int firstWide=-1,lastWide=-1;
        for(int j=0;j<n;++j)
            if(value[i][j]>=peak*0.99){ if(firstWide<0) firstWide=j; lastWide=j; }
        const int fattest=(firstWide<0)?0:(firstWide+lastWide)/2;
        const QPointF middle=at(x[fattest],running[fattest]+value[i][fattest]*0.5);
        const double thickness=std::abs(at(x[fattest],0.0).y()
                                        -at(x[fattest],value[i][fattest]).y());
        const QString name=spec.series.at(i).label;
        if(!name.isEmpty()&&thickness>fm.height()*1.1
           &&fm.horizontalAdvance(name)<field.width()*0.4){
            p->setPen(fill.lightness()<128?Qt::white:QColor(0x18,0x18,0x18));
            const double half=fm.horizontalAdvance(name)*0.5+2.0;
            const double cx=qBound(field.left()+half,middle.x(),field.right()-half);
            p->drawText(QRectF(cx-60.0,middle.y()-fm.height()*0.5,120.0,fm.height()),
                        Qt::AlignCenter,name);
        }
        for(int j=0;j<n;++j) running[j]+=value[i][j];
    }
    p->restore();

    // The x axis, and only the x axis.
    p->save();
    p->setPen(QPen(spec.style.foreground,0.9));
    const double axisY=field.bottom()+fm.height()*0.5;
    p->drawLine(QPointF(field.left(),axisY),QPointF(field.right(),axisY));
    for(const AxisTick& tick:linearTicks(across.lo,across.hi,6)){
        if(tick.minor) continue;
        if(tick.value<across.lo||tick.value>across.hi) continue;
        const double px=at(tick.value,lo).x();
        p->drawLine(QPointF(px,axisY),QPointF(px,axisY+4.0));
        p->drawText(QRectF(px-40.0,axisY+5.0,80.0,fm.height()),
                    Qt::AlignHCenter|Qt::AlignTop,tick.label);
    }
    if(!spec.xAxis.label.isEmpty())
        p->drawText(QRectF(field.left(),axisY+fm.height()*1.4,field.width(),fm.height()),
                    Qt::AlignHCenter|Qt::AlignTop,spec.xAxis.label);

    // The thickest each band ever gets, which is the number the picture cannot
    // be measured for: there is no scale to measure it against, by design.
    QStringList widest;
    for(int i=0;i<bands&&i<6;++i){
        double top=0.0;
        for(int j=0;j<n;++j) top=qMax(top,value[i][j]);
        widest.append(QStringLiteral("%1 peaks at %2")
                          .arg(spec.series.at(i).label.isEmpty()
                                   ?QStringLiteral("band %1").arg(i+1)
                                   :spec.series.at(i).label)
                          .arg(top,0,'g',4));
    }
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.1,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,
                QStringLiteral("thickness is the value; there is no y scale.   ")
                    +widest.join(QStringLiteral("   ")));
    p->restore();
}

// ======================================================================
// Cladogram
//
// The same three columns as the icicle, read as a tree rather than as a
// division of a whole: node, parent, and the LENGTH of the branch leading to
// that node. Tips stack down the page and a node's horizontal position is its
// distance from the root, so the picture is a statement about how far apart two
// tips are - which is what a tree is for, and what a dendrogram built from a
// distance matrix cannot say in the same way because its heights come from the
// clustering rather than from the data.
//
// Drawn square rather than as slanted vees. On a slanted tree the length of the
// drawn line depends on how far apart its two children happen to fall on the
// page, so two equal branches look different and two unequal ones can look the
// same. With square corners the horizontal run IS the branch length and nothing
// else is.
void QtPlotBackend::drawCladogram(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    drawFloatingTitle(p,target,spec);
    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    p->setFont(tickFont);

    Tree tree=treeFrom(spec);
    if(!tree.valid){
        p->save();
        p->setPen(spec.style.foreground);
        p->drawText(target,Qt::AlignCenter,
                    QStringLiteral("needs three columns: node, parent, and the "
                                   "length of the branch leading to it"));
        p->restore();
        return;
    }
    const QVector<TreeNode>& nodes=tree.nodes;
    const int n=nodes.size();

    // Distance from the root, parents first - which is the order the pre-order
    // walk gives. A negative length is taken as zero rather than allowed to walk
    // backwards: a negative branch is a data error, and drawing it would put a
    // child to the LEFT of its parent, which on this diagram means the child is
    // closer to the root than the thing it descends from.
    QVector<double> distance(n,0.0);
    for(int at:tree.preorder)
        distance[at]=(nodes[at].parent<0)?0.0
                    :distance[nodes[at].parent]+qMax(0.0,nodes[at].value);
    double furthest=0.0;
    for(double d:distance) furthest=qMax(furthest,d);

    // Tips down the page in the order the walk reaches them, then internal
    // nodes at the midpoint of their children: the standard layout, and the one
    // that keeps a subtree contiguous so a clade reads as a block.
    QVector<double> row(n,-1.0);
    int tips=0;
    for(int i=tree.preorder.size()-1;i>=0;--i){
        const int at=tree.preorder[i];
        if(nodes[at].kids.isEmpty()){ row[at]=double(tips++); continue; }
        double lo=row[nodes[at].kids.first()],hi=lo;
        for(int kid:nodes[at].kids){ lo=qMin(lo,row[kid]); hi=qMax(hi,row[kid]); }
        row[at]=0.5*(lo+hi);
    }
    if(tips<1) return;
    // No flip. The stack reverses siblings once on the way down and reading the
    // walk backwards reverses them again, so tips come out top to bottom in the
    // order the table gave them - which is the order somebody chose. The first
    // version turned them upside down to "correct" a reversal that had already
    // cancelled itself out.

    const double titleRoom=spec.title.isEmpty()?0.0:fm.height()*2.0;
    // Room on the right for the tip labels, which are the point of the figure: a
    // tree whose tips are cut off names nothing.
    double widestTip=0.0;
    for(int i=0;i<n;++i)
        if(nodes[i].kids.isEmpty())
            widestTip=qMax(widestTip,
                           fm.horizontalAdvance(QString::number(nodes[i].id,'g',6)));
    const QRectF field(target.left()+fm.height()*0.6,
                       target.top()+titleRoom+fm.height()*0.4,
                       target.width()-fm.height()*1.2-widestTip-12.0,
                       target.height()-titleRoom-fm.height()*4.0);
    if(field.width()<40.0||field.height()<20.0) return;
    const double rowGap=field.height()/double(tips);
    const auto at=[&](double d,double r){
        return QPointF(field.left()+(furthest>0.0?d/furthest:0.0)*field.width(),
                       field.top()+rowGap*(r+0.5));
    };

    p->save();
    QPen branch(spec.series.at(0).color);
    branch.setWidthF(qMax(1.0,spec.style.lineWidth));
    branch.setCapStyle(Qt::FlatCap);
    p->setPen(branch);
    for(int i=0;i<n;++i){
        if(nodes[i].parent<0) continue;
        const int up=nodes[i].parent;
        // Two strokes: down the parent's column to this child's row, then out
        // along the row by the branch length. The horizontal run is the length;
        // the vertical one carries no meaning at all, which is exactly why the
        // two are kept separate.
        p->drawLine(at(distance[up],row[up]),at(distance[up],row[i]));
        p->drawLine(at(distance[up],row[i]),at(distance[i],row[i]));
    }

    p->setPen(spec.style.foreground);
    if(rowGap>fm.height()*0.95){
        for(int i=0;i<n;++i){
            if(!nodes[i].kids.isEmpty()) continue;
            const QPointF tip=at(distance[i],row[i]);
            p->drawText(QRectF(tip.x()+4.0,tip.y()-fm.height()*0.5,
                               widestTip+8.0,fm.height()),
                        Qt::AlignLeft|Qt::AlignVCenter,
                        QString::number(nodes[i].id,'g',6));
        }
    }
    p->restore();

    // A distance scale along the bottom. Without one the horizontal direction is
    // a picture of nothing - the whole difference between this and a bare
    // topology is that the run means something, and it cannot mean something
    // unnamed.
    p->save();
    p->setPen(QPen(spec.style.foreground,0.9));
    const double axisY=field.bottom()+fm.height()*0.7;
    p->drawLine(QPointF(field.left(),axisY),QPointF(field.right(),axisY));
    const double step=niceStep(furthest/5.0);
    if(step>0.0&&furthest>0.0)
        for(double d=0.0;d<=furthest+step*0.5;d+=step){
            const double x=at(qMin(d,furthest),0.0).x();
            p->drawLine(QPointF(x,axisY),QPointF(x,axisY+4.0));
            p->drawText(QRectF(x-30.0,axisY+5.0,60.0,fm.height()),
                        Qt::AlignHCenter|Qt::AlignTop,QString::number(d,'g',4));
        }
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.1,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,
                QStringLiteral("%1 tips over %2 nodes, deepest %3 branches from "
                               "the root, furthest tip at %4")
                    .arg(tips).arg(n).arg(tree.deepest).arg(furthest,0,'g',4));
    p->restore();
}

// ======================================================================
// Icicle plot and flame graph
//
// A hierarchy drawn as nested bars: one row per level, each parent exactly as
// wide as its children put together. A treemap divides the same total by area
// and reads better for one level; an icicle keeps DEPTH on its own axis, so the
// shape of the tree - deep and narrow against broad and shallow - is the thing
// you see first. A flame graph is the same picture drawn upward and sorted by
// name, which is the form a profiler prints.
//
// Three mapped columns: node identifier, parent identifier, and the value the
// node holds ITSELF, not counting its children. A node whose parent is not in
// the identifier column, or is its own parent, is a root. Identifiers are
// numbers because that is what a mapped column can carry - the same convention
// as the network engines, and for the same reason.
//
// Self value rather than total, because the total is derivable and the self
// value is not: given totals, a parent whose children do not account for all of
// it cannot be told from one whose data is inconsistent.
void QtPlotBackend::drawIcicle(QPainter* p,const QRectF& target,const PlotSpec& spec,
                               bool upward) const {
    drawFloatingTitle(p,target,spec);
    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    p->setFont(tickFont);

    if(spec.series.size()<3){
        p->save();
        p->setPen(spec.style.foreground);
        p->drawText(target,Qt::AlignCenter,
                    QStringLiteral("needs three columns: node, parent, and the node's own value"));
        p->restore();
        return;
    }
    Tree tree=treeFrom(spec);
    if(!tree.valid) return;
    QVector<TreeNode>& nodes=tree.nodes;
    QVector<double> total(nodes.size(),0.0);
    // Subtree totals, deepest first - which the pre-order walk gives when read
    // backwards, since a node is always appended before any of its children.
    for(int i=tree.preorder.size()-1;i>=0;--i){
        const int at=tree.preorder[i];
        double sum=qMax(0.0,nodes[at].value);
        for(int kid:nodes[at].kids) sum+=total[kid];
        total[at]=sum;
    }
    double grand=0.0;
    for(int root:tree.roots) grand+=total[root];
    const int deepest=tree.deepest;
    if(!(grand>0.0)) return;

    // A flame graph sorts siblings by name so that two profiles of the same
    // program line up; an icicle keeps the order the table gave, because that
    // order is often the thing being shown.
    if(upward)
        for(TreeNode& node:nodes)
            std::sort(node.kids.begin(),node.kids.end(),
                      [&nodes](int a,int b){ return nodes[a].id<nodes[b].id; });

    const double titleRoom=spec.title.isEmpty()?0.0:fm.height()*2.0;
    const QRectF field(target.left()+fm.height()*0.5,target.top()+titleRoom,
                       target.width()-fm.height(),
                       target.height()-titleRoom-fm.height()*1.6);
    if(field.width()<40.0||field.height()<20.0) return;
    const double levelH=field.height()/double(deepest+1);

    p->save();
    // Laid out left to right in one walk: each node is given the slice of its
    // parent's width that its total is of the parent's total, and its children
    // divide what is left after the parent's own value.
    struct Job { int node; double x,width; };
    QVector<Job> queue;
    { double x=field.left();
      for(int root:tree.roots){
          const double w=field.width()*total[root]/grand;
          queue.append({root,x,w});
          x+=w;
      } }
    int drawn=0;
    while(!queue.isEmpty()){
        const Job job=queue.takeLast();
        const TreeNode& node=nodes[job.node];
        const double top=upward
            ?field.bottom()-levelH*double(node.depth+1)
            :field.top()+levelH*double(node.depth);
        const QRectF block(job.x,top+0.5,qMax(0.0,job.width-0.5),levelH-1.0);
        // Hue by depth, so a level is a colour band and the eye can follow one
        // row across; lightness by share, so the heavy blocks are the solid
        // ones. Not hue by identifier: adjacent identifiers are usually
        // unrelated and that would be a rainbow carrying nothing.
        const double hue=std::fmod(0.08+0.13*double(node.depth),1.0);
        p->setBrush(categoryColour(spec,node.depth,hue,upward?0.62:0.45,
                                   upward?0.95:0.88,0.92));
        p->setPen(QPen(spec.style.background,1.0));
        p->drawRect(block);
        ++drawn;
        if(block.width()>fm.horizontalAdvance(QStringLiteral("000"))+6.0
           &&block.height()>fm.height()){
            p->setPen(QColor(0x20,0x20,0x20));
            p->drawText(block.adjusted(3,0,-3,0),Qt::AlignVCenter|Qt::AlignLeft,
                        QString::number(node.id,'g',6));
        }
        double at=job.x;
        for(int kid:node.kids){
            const double w=job.width*total[kid]/qMax(1e-12,total[job.node]);
            queue.append({kid,at,w});
            at+=w;
        }
    }
    p->restore();

    p->save();
    p->setPen(spec.style.foreground);
    p->drawText(QRectF(target.left(),target.bottom()-fm.height()*1.2,
                       target.width(),fm.height()),
                Qt::AlignHCenter|Qt::AlignVCenter,
                QStringLiteral("%1 nodes over %2 levels, total %3; "
                               "width is the subtree total, %4")
                    .arg(drawn).arg(deepest+1).arg(grand,0,'g',6)
                    .arg(upward?QStringLiteral("deepest at the top")
                               :QStringLiteral("roots at the top")));
    p->restore();
}

// ======================================================================
// Tripartite response spectrum
//
// Four quantities on two axes. Period runs across and pseudo-velocity up, both
// logarithmic, and because D = VT/2pi and A = 2piV/T are products and quotients
// of those two, constant displacement and constant acceleration are STRAIGHT
// LINES on the same picture - one family sloping down, one up. A designer reads
// all four off one curve: the acceleration governs the short-period end, the
// displacement the long-period end, and where the curve leaves each family is
// where the structure stops behaving like one and starts behaving like the
// other. Drawing it as an ordinary log-log line chart throws that away and
// leaves two of the four quantities to be worked out with a calculator.
//
// Two mapped columns: period and pseudo-velocity. The units are the data's own
// - the two derived families are labelled in whatever they imply, since the
// engine has no way to know whether the velocities are cm/s or in/s and
// guessing would put a wrong number on the page.
void QtPlotBackend::drawTripartite(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    const QFont tickFont=font(spec,qMax(6.0,spec.style.tickSize-1.0));
    const QFontMetricsF fm(tickFont,p->device());

    if(!f.xLog||!f.yLog){
        p->save();
        p->setFont(tickFont);
        p->setPen(spec.style.danger);
        p->drawText(f.plotArea.adjusted(6,6,-6,-6),Qt::AlignLeft|Qt::AlignTop,
                    QStringLiteral("both axes must be logarithmic for the "
                                   "displacement and acceleration families to "
                                   "be straight"));
        p->restore();
        drawLineChart(p,f,spec);
        return;
    }

    // The two families, in decades, over exactly the range the frame can show.
    // Computed from the corners rather than from the data: a family line that
    // crosses the frame belongs on it whether or not a sample sits near it.
    const double tLo=f.xLo,tHi=f.xHi,vLo=f.yLo,vHi=f.yHi;   // already log10
    const double twoPi=std::log10(2.0*M_PI);
    // log D = log V + log T - log 2pi, and log A = log V - log T + log 2pi.
    const double dLo=std::floor(vLo+tLo-twoPi),dHi=std::ceil(vHi+tHi-twoPi);
    const double aLo=std::floor(vLo-tHi+twoPi),aHi=std::ceil(vHi-tLo+twoPi);

    p->save();
    p->setFont(tickFont);
    p->setClipRect(f.plotArea);
    const auto onPage=[&](double logT,double logV){
        return QPointF(f.plotArea.left()
                       +(logT-tLo)/qMax(1e-300,tHi-tLo)*f.plotArea.width(),
                       f.plotArea.bottom()
                       -(logV-vLo)/qMax(1e-300,vHi-vLo)*f.plotArea.height());
    };
    const auto decade=[](double power){
        // 1e3 rather than 1000 past three decades, because the alternative is a
        // label wider than the gap between two lines.
        return (power>=-3.0&&power<=4.0)
            ?QString::number(std::pow(10.0,power),'g',6)
            :QStringLiteral("1e%1").arg(int(power));
    };

    QColor displacement=spec.style.positive; displacement.setAlphaF(0.75);
    QColor acceleration=spec.style.warning;  acceleration.setAlphaF(0.75);

    for(double d=dLo;d<=dHi;d+=1.0){
        QPen pen(displacement); pen.setWidthF(0.7);
        pen.setDashPattern({5,4});
        p->setPen(pen);
        // log V = log D + log 2pi - log T: one straight line, two endpoints.
        p->drawLine(onPage(tLo,d+twoPi-tLo),onPage(tHi,d+twoPi-tHi));
    }
    for(double a=aLo;a<=aHi;a+=1.0){
        QPen pen(acceleration); pen.setWidthF(0.7);
        pen.setDashPattern({2,3});
        p->setPen(pen);
        // log V = log A + log T - log 2pi.
        p->drawLine(onPage(tLo,a+tLo-twoPi),onPage(tHi,a+tHi-twoPi));
    }

    // Labels along the two edges each family leaves by, so a line is named
    // where it is least likely to be crossing the data.
    // Full strength for the numbers even though the lines they name are faint:
    // a grid line is background and its label is not.
    p->setPen(spec.style.positive);
    for(double d=dLo;d<=dHi;d+=1.0){
        // Displacement lines fall to the right, so they leave by the right edge
        // or the bottom. Whichever the line reaches first is where it is named.
        const double vAtRight=d+twoPi-tHi;
        const QPointF where=(vAtRight>=vLo)?onPage(tHi,vAtRight)
                                           :onPage(d+twoPi-vLo,vLo);
        if(!f.plotArea.contains(where)) continue;
        p->drawText(QRectF(where.x()-58.0,where.y()-fm.height()-1.0,54.0,fm.height()),
                    Qt::AlignRight|Qt::AlignVCenter,decade(d));
    }
    p->setPen(spec.style.warning);
    for(double a=aLo;a<=aHi;a+=1.0){
        const double vAtRight=a+tHi-twoPi;
        const QPointF where=(vAtRight<=vHi)?onPage(tHi,vAtRight)
                                           :onPage(vHi-a+twoPi,vHi);
        if(!f.plotArea.contains(where)) continue;
        p->drawText(QRectF(where.x()-58.0,where.y()+1.0,54.0,fm.height()),
                    Qt::AlignRight|Qt::AlignVCenter,decade(a));
    }
    p->restore();

    drawLineChart(p,f,spec);

    p->save();
    p->setFont(tickFont);
    p->setPen(spec.style.foreground);
    // Along the bottom of the frame rather than the top, where the legend is.
    p->drawText(QRectF(f.plotArea.left()+4.0,f.plotArea.bottom()-fm.height()-2.0,
                       f.plotArea.width()-8.0,fm.height()),
                Qt::AlignLeft|Qt::AlignVCenter,
                QStringLiteral("dashed, falling: constant displacement V*T/2pi   "
                               "dotted, rising: constant acceleration 2pi*V/T   "
                               "both in the data's own units"));
    p->restore();
}

// ======================================================================
// Voronoi diagram
//
// The region nearer to each site than to any other. It is the picture behind
// nearest-neighbour interpolation, service-area and catchment questions, and
// the sampling density of a scattered survey - and the port had none, so a set
// of scattered points could be shown as dots and nothing else.
//
// Built by half-plane clipping rather than as the dual of the Delaunay
// triangulation, which the estimators already compute. The dual is faster and
// is the wrong tool here: its unbounded cells have to be closed against the
// frame as a special case, degenerate (cocircular) inputs need the
// triangulation to be perturbed, and both failure modes produce a plausible
// picture rather than an obvious one. Clipping the frame rectangle by each
// bisector in turn is O(n^2) and cannot go wrong: every cell is bounded by
// construction and duplicate or cocircular sites are ordinary cases.
//
// Two mapped columns, the positions. Cells are shaded by their own AREA rather
// than by an index or a third column: a large cell is a place where the survey
// sampled thinly, which is a real question about scattered data and the one a
// Voronoi diagram answers without being asked. Hue by identifier would have
// been a rainbow carrying nothing.
void QtPlotBackend::drawVoronoi(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    // A VORONOI DIAGRAM OF EVERY ROW IS NOT A VORONOI DIAGRAM. The clipping
    // below is quadratic deliberately - it is the construction that cannot go
    // wrong, argued above - and a deliberate quadratic still needs a limit, or
    // a scattered survey of 24,000 rows grinds for a quarter of an hour to
    // produce cells seven pixels across. Two thousand cells is already more
    // than a reader can tell apart in a figure; the count stops one past the
    // limit, so refusing costs nothing, and `explainEmpty` names the same
    // number so the empty figure is not a mystery.
    constexpr int kMaxSites=2000;
    QVector<double> sx,sy;
    // Coincident sites share one region rather than owning one each, and
    // WHICH ONES ARE ALREADY TAKEN IS A LOOKUP. Asking the list itself meant a
    // scan per row - the same fault the confusion matrix had - so a survey of
    // 120,000 rows spent seven billion comparisons on the duplicate check
    // before any tessellation began. Zero is normalised because a negative
    // zero compares equal to a positive one and would not hash to the same
    // place, which would let a duplicate through that the old scan caught.
    QSet<QPair<double,double>> taken;
    bool tooMany=false;
    for(const PlotSeries& s:spec.series){
        if(tooMany) break;
        const int n=qMin(s.x.size(),s.y.size());
        for(int i=0;i<n;++i){
            if(!finite(s.x[i])||!finite(s.y[i])) continue;
            if(f.xLog&&s.x[i]<=0.0) continue;
            if(f.yLog&&s.y[i]<=0.0) continue;
            // In the space the axes are drawn in, so a logarithmic axis
            // tessellates log positions - which is what the picture shows and
            // therefore what a reader will measure off it.
            const double px=f.xLog?std::log10(s.x[i]):s.x[i];
            const double py=f.yLog?std::log10(s.y[i]):s.y[i];
            // Kept out here rather than handled in the clipping, because a
            // duplicate that reaches the loop below produces the same cell
            // twice - drawn twice, and counted twice in the areas, so the
            // reported total came out larger than the frame it tiles.
            const QPair<double,double> key((px==0.0)?0.0:px,(py==0.0)?0.0:py);
            if(taken.contains(key)) continue;
            taken.insert(key);
            sx.append(px);
            sy.append(py);
            if(sx.size()>kMaxSites){ tooMany=true; break; }
        }
    }
    if(tooMany) return;
    const int n=sx.size();
    if(n<1) return;
    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);

    // The frame in data space, which is what the cells are clipped to.
    QVector<QPointF> frame;
    frame<<QPointF(f.xLo,f.yLo)<<QPointF(f.xHi,f.yLo)
         <<QPointF(f.xHi,f.yHi)<<QPointF(f.xLo,f.yHi);

    // Sutherland-Hodgman against one half plane: keep the part of the polygon
    // where normal . point <= offset, cutting each crossing edge exactly on the
    // line so that neighbouring cells share their boundary to the last bit.
    const auto clip=[](const QVector<QPointF>& poly,double nx,double ny,double offset){
        QVector<QPointF> out;
        const int m=poly.size();
        for(int i=0;i<m;++i){
            const QPointF& a=poly[i];
            const QPointF& b=poly[(i+1)%m];
            const double da=nx*a.x()+ny*a.y()-offset;
            const double db=nx*b.x()+ny*b.y()-offset;
            if(da<=0.0) out.append(a);
            if((da<0.0&&db>0.0)||(da>0.0&&db<0.0)){
                const double t=da/(da-db);
                out.append(QPointF(a.x()+t*(b.x()-a.x()),a.y()+t*(b.y()-a.y())));
            }
        }
        return out;
    };

    // Both passes need every cell, and the areas have to be known before the
    // first one can be given its colour, so the tessellation is built once and
    // kept rather than walked twice.
    QVector<QPolygonF> shapes(n);
    QVector<double> areas(n,0.0);
    for(int i=0;i<n;++i){
        QVector<QPointF> cell=frame;
        for(int j=0;j<n&&!cell.isEmpty();++j){
            if(j==i) continue;
            const double nx=sx[j]-sx[i],ny=sy[j]-sy[i];
            const double offset=0.5*((sx[j]*sx[j]+sy[j]*sy[j])
                                    -(sx[i]*sx[i]+sy[i]*sy[i]));
            cell=clip(cell,nx,ny,offset);
        }
        if(cell.size()<3) continue;
        // The shoelace area, in data units, before the cell is put on the page:
        // the answer must not depend on the size of the window.
        double twice=0.0;
        for(int k=0;k<cell.size();++k){
            const QPointF& a=cell[k];
            const QPointF& b=cell[(k+1)%cell.size()];
            twice+=a.x()*b.y()-b.x()*a.y();
        }
        areas[i]=std::abs(twice)*0.5;
        QPolygonF onPage;
        for(const QPointF& v:cell)
            onPage<<QPointF(f.plotArea.left()
                            +(v.x()-f.xLo)/qMax(1e-300,f.xHi-f.xLo)*f.plotArea.width(),
                            f.plotArea.bottom()
                            -(v.y()-f.yLo)/qMax(1e-300,f.yHi-f.yLo)*f.plotArea.height());
        shapes[i]=onPage;
    }

    double smallest=std::numeric_limits<double>::infinity(),largest=0.0,total=0.0;
    for(int i=0;i<n;++i){
        if(shapes[i].size()<3) continue;
        smallest=qMin(smallest,areas[i]);
        largest=qMax(largest,areas[i]);
        total+=areas[i];
    }
    if(!finite(smallest)) return;

    // On the square root of the area, so the ramp reads as a length rather than
    // an area: a cell four times the size of another is twice as far along the
    // scale, which is how the eye compares two regions anyway.
    //
    // The roots are taken ONCE and the spread tested against a tolerance rather
    // than against zero. Sites on a regular lattice - a survey grid, which is a
    // common input - give cells that are equal to the last bit, and there
    // largest is a rounding step above smallest while their square roots are
    // identical: the difference survives the comparison and dies in the
    // division, and every cell came out 0/0. NaN then landed at the bottom of
    // the map, so a perfectly regular grid rendered as one flat colour that
    // looked deliberate. Found by putting four sites on the corners of a square
    // and asking for four equal cells.
    const double lowRoot=std::sqrt(smallest),highRoot=std::sqrt(largest);
    const bool graded=(highRoot-lowRoot)>1e-9*qMax(1.0,highRoot);

    p->save();
    for(int i=0;i<n;++i){
        if(shapes[i].size()<3) continue;
        const double t=graded?(std::sqrt(areas[i])-lowRoot)/(highRoot-lowRoot)
                             :0.5;
        QColor fill=colourMapStyled(cmap,spec.style,t);
        fill.setAlphaF(0.88);
        p->setBrush(fill);
        p->setPen(QPen(spec.style.foreground,0.7));
        p->drawPolygon(shapes[i]);
    }
    // Sites on top of every cell, not just their own: a boundary that does not
    // sit halfway between two dots is the one visible sign that the
    // tessellation is wrong, and it can only be checked if the dots are
    // visible over the fills.
    p->setPen(Qt::NoPen);
    p->setBrush(spec.style.foreground);
    for(int i=0;i<n;++i){
        const QPointF at(f.plotArea.left()
                         +(sx[i]-f.xLo)/qMax(1e-300,f.xHi-f.xLo)*f.plotArea.width(),
                         f.plotArea.bottom()
                         -(sy[i]-f.yLo)/qMax(1e-300,f.yHi-f.yLo)*f.plotArea.height());
        p->drawEllipse(at,2.0,2.0);
    }
    p->restore();

    drawColourBar(p,f,spec,smallest,largest,QStringLiteral("cell area"));

    // What the shading stands for, in the axes' own units. Without it the ramp
    // is a decoration - and the ratio is the number a sampling question is
    // actually asking for.
    p->save();
    p->setFont(font(spec,spec.style.tickSize));
    p->setPen(spec.style.foreground);
    const QFontMetricsF fm(p->font(),p->device());
    const QString note=QStringLiteral("%1 cells, area %2 to %3 axis units "
                                      "(%4x), mean %5")
        .arg(n).arg(smallest,0,'g',3).arg(largest,0,'g',3)
        .arg(smallest>0.0?largest/smallest:0.0,0,'f',1)
        .arg(total/double(n),0,'g',3);
    // Cleared behind: it sits inside the frame, and whether the cell under it
    // is the pale end of the ramp or the dark end is decided by the data.
    const QRectF strip(f.plotArea.right()-fm.horizontalAdvance(note)-6.0,
                       f.plotArea.top()+2.0,
                       fm.horizontalAdvance(note)+5.0,fm.height());
    p->fillRect(strip,spec.style.background);
    p->drawText(strip,Qt::AlignRight|Qt::AlignVCenter,note);
    p->restore();
}

// A mosaic plot: a contingency table drawn to scale. Column widths are the
// column totals and each column is divided by its own proportions, so an
// association shows as tiles that fail to line up across columns - which is
// exactly what a chi-squared test is testing and what a grid of numbers hides.
//
// Three mapped columns: the column category, the row category, and the count.
void QtPlotBackend::drawMosaic(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    drawFloatingTitle(p,target,spec);
    if(spec.series.size()<2) return;
    const QVector<double>& columnOf=spec.series.at(0).y;
    const QVector<double>& rowOf=spec.series.at(1).y;
    const bool counted=spec.series.size()>=3;
    const QVector<double> weight=counted?spec.series.at(2).y:QVector<double>();
    const int n=qMin(columnOf.size(),rowOf.size());

    // THE CAP IS CHECKED BEFORE THE TABLE IS BUILT, not after it.
    //
    // The guard below - "more than forty of either and give up" - used to sit
    // after the loop that builds the table, which meant a continuous column
    // mapped by mistake built a 24,000 by 24,000 contingency table and then
    // threw it away. Every new column appends a cell to every existing row and
    // every new row allocates a vector as long as the column list, so the work
    // is quadratic in the row count and the memory is quadratic in it too: a
    // spreadsheet with 24,000 distinct values froze the figure for nineteen
    // seconds, which was the slowest thing in the whole catalogue.
    //
    // Counting the levels first is linear, stops as soon as it has seen one too
    // many, and reaches the same verdict. A guard that runs after the work it
    // exists to prevent is not a guard.
    constexpr int kMaxLevels=40;
    {
        const auto tooMany=[&](const QVector<double>& column){
            QSet<double> seen;
            for(int i=0;i<n&&i<column.size();++i){
                if(!finite(column[i])) continue;
                seen.insert(column[i]);
                if(seen.size()>kMaxLevels) return true;
            }
            return false;
        };
        if(tooMany(columnOf)||tooMany(rowOf)) return;
    }

    QVector<double> columns,rows;
    QHash<double,int> columnIndex,rowIndex;
    QVector<QVector<double>> table;
    for(int i=0;i<n;++i){
        if(!finite(columnOf[i])||!finite(rowOf[i])) continue;
        double amount=1.0;
        if(counted){
            if(i>=weight.size()||!finite(weight[i])||weight[i]<=0) continue;
            amount=weight[i];
        }
        if(!columnIndex.contains(columnOf[i])){
            columnIndex.insert(columnOf[i],columns.size());
            columns.append(columnOf[i]);
            for(QVector<double>& r:table) r.append(0.0);
        }
        if(!rowIndex.contains(rowOf[i])){
            rowIndex.insert(rowOf[i],rows.size());
            rows.append(rowOf[i]);
            table.append(QVector<double>(columns.size(),0.0));
        }
        table[rowIndex.value(rowOf[i])][columnIndex.value(columnOf[i])]+=amount;
    }
    if(columns.isEmpty()||rows.isEmpty()
       ||columns.size()>kMaxLevels||rows.size()>kMaxLevels) return;

    QVector<double> columnTotal(columns.size(),0.0);
    double grand=0.0;
    for(const QVector<double>& row:table)
        for(int c=0;c<row.size();++c){ columnTotal[c]+=row[c]; grand+=row[c]; }
    if(!(grand>0)) return;

    const QRectF area=target.adjusted(target.width()*0.08,target.height()*0.12,
                                      -target.width()*0.04,-target.height()*0.10);
    const double gap=qMin(3.0,area.width()/double(qMax(1,columns.size()))*0.06);
    const QFont label=font(spec,spec.style.tickSize);
    p->save();
    p->setFont(label);
    const QFontMetricsF fm(label,p->device());
    double x=area.left();
    for(int c=0;c<columns.size();++c){
        const double width=(area.width()-gap*double(columns.size()-1))*columnTotal[c]/grand;
        double y=area.top();
        for(int r=0;r<rows.size();++r){
            const double share=(columnTotal[c]>0)?table[r][c]/columnTotal[c]:0.0;
            const double height=area.height()*share;
            if(height>0){
                QColor fill=categoryColour(spec,r,
                                           std::fmod(double(r)/double(qMax(1,rows.size()))+0.08,1.0),0.5,0.9);
                fill.setAlphaF(0.85);
                p->setBrush(fill);
                p->setPen(QPen(spec.style.background,0.8));
                p->drawRect(QRectF(x,y,width,height));
                if(height>fm.height()+2&&width>fm.horizontalAdvance(QStringLiteral("00"))+4){
                    p->setPen(spec.style.background);
                    p->drawText(QRectF(x,y,width,height),Qt::AlignCenter,
                                QString::number(table[r][c],'g',4));
                }
            }
            // THE ROW'S OWN NAME, beside the first column.
            //
            // The columns were labelled along the bottom and the rows were not
            // labelled at all, which leaves a two-way contingency table showing
            // counts without saying what they are counts OF. The left margin
            // was already being reserved - eight per cent of the width - and
            // nothing was drawn in it.
            //
            // Against the FIRST column because that is the only place a row is
            // a single band at a known height; every later column gives the
            // same row a different height, so there is no shared baseline to
            // label. This is where R's mosaicplot puts them for the same
            // reason. Skipped when the band is too short for the text rather
            // than drawn overlapping its neighbour.
            if(c==0&&height>=fm.height()){
                p->setPen(spec.style.foreground);
                p->drawText(QRectF(target.left(),y,area.left()-target.left()-4,height),
                            Qt::AlignRight|Qt::AlignVCenter,
                            QString::number(rows[r],'g',6));
            }
            y+=height;
        }
        p->setPen(spec.style.foreground);
        p->drawText(QRectF(x,area.bottom()+2,width,fm.height()),
                    Qt::AlignHCenter|Qt::AlignTop,QString::number(columns[c],'g',6));
        x+=width+gap;
    }
    p->restore();
}

// An UpSet plot: how big each combination of sets is, as bars, with a matrix of
// dots underneath saying which combination each bar is.
//
// A Venn diagram stops being readable at four sets and cannot be drawn at all
// past five; this is the standard answer to that, and the combinations are
// sorted by size so the ones that matter are on the left.
//
// One mapped column per set, holding 1 for a member and 0 for a non-member.
void QtPlotBackend::drawUpSet(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    drawFloatingTitle(p,target,spec);
    const int sets=spec.series.size();
    if(sets<2||sets>12) return;
    int rows=std::numeric_limits<int>::max();
    for(const PlotSeries& s:spec.series) rows=qMin(rows,int(s.y.size()));
    if(rows<=0) return;

    QHash<quint32,int> tally;
    for(int i=0;i<rows;++i){
        quint32 mask=0;
        bool usable=true;
        for(int k=0;k<sets;++k){
            const double v=spec.series.at(k).y[i];
            if(!finite(v)){ usable=false; break; }
            if(v>=0.5) mask|=(1u<<k);
        }
        if(!usable||mask==0) continue;      // a row in no set is not an intersection
        tally[mask]+=1;
    }
    if(tally.isEmpty()) return;
    QVector<QPair<quint32,int>> ordered;
    for(auto it=tally.constBegin();it!=tally.constEnd();++it) ordered.append({it.key(),it.value()});
    // A TOTAL order. Sorting on the count alone leaves words that appear the
    // same number of times in whatever order the QHash produced them, and Qt
    // randomises its hash seed per process - so the cloud's ties changed place
    // between runs and the figure was not reproducible. The key breaks them.
    std::sort(ordered.begin(),ordered.end(),
              [](const QPair<quint32,int>& a,const QPair<quint32,int>& b){
                  if(a.second!=b.second) return a.second>b.second;
                  return a.first<b.first; });
    if(ordered.size()>24) ordered.resize(24);

    const QFont label=font(spec,spec.style.tickSize);
    p->save();
    p->setFont(label);
    const QFontMetricsF fm(label,p->device());
    double widest=0;
    for(const PlotSeries& s:spec.series) widest=qMax(widest,fm.horizontalAdvance(s.label));
    const double left=target.left()+qMin(target.width()*0.32,widest+14.0);
    const double matrixHeight=qMin(target.height()*0.42,double(sets)*18.0);
    const QRectF bars(left,target.top()+22.0,
                      target.right()-left-8.0,
                      qMax(20.0,target.height()-matrixHeight-52.0));
    const double column=bars.width()/double(ordered.size());
    int biggest=0;
    for(const auto& entry:ordered) biggest=qMax(biggest,entry.second);
    if(biggest<=0){ p->restore(); return; }

    for(int i=0;i<ordered.size();++i){
        const double height=bars.height()*double(ordered[i].second)/double(biggest);
        const QRectF bar(bars.left()+column*double(i)+column*0.15,
                         bars.bottom()-height,column*0.7,height);
        p->setBrush(spec.series.at(0).color);
        p->setPen(Qt::NoPen);
        p->drawRect(bar);
        p->setPen(spec.style.foreground);
        p->drawText(QRectF(bar.left()-6,bar.top()-fm.height()-1,bar.width()+12,fm.height()),
                    Qt::AlignHCenter|Qt::AlignBottom,QString::number(ordered[i].second));
    }

    const double rowHeight=matrixHeight/double(sets);
    for(int k=0;k<sets;++k){
        const double y=bars.bottom()+14.0+rowHeight*(double(k)+0.5);
        p->setPen(spec.style.foreground);
        p->drawText(QRectF(target.left()+4,y-fm.height()*0.5,left-target.left()-10,fm.height()),
                    Qt::AlignRight|Qt::AlignVCenter,spec.series.at(k).label);
        // A band behind alternate rows, so a dot can be traced back to its set.
        if(k%2==0){
            QColor band=spec.style.gridColor; band.setAlphaF(0.35);
            p->fillRect(QRectF(left,y-rowHeight*0.5,bars.width(),rowHeight),band);
        }
    }
    for(int i=0;i<ordered.size();++i){
        const double cx=bars.left()+column*(double(i)+0.5);
        double firstY=-1,lastY=-1;
        for(int k=0;k<sets;++k){
            const double y=bars.bottom()+14.0+rowHeight*(double(k)+0.5);
            const bool member=(ordered[i].first&(1u<<k))!=0;
            p->setPen(Qt::NoPen);
            QColor dot=member?spec.series.at(0).color:spec.style.gridColor;
            if(!member) dot.setAlphaF(0.55);
            p->setBrush(dot);
            p->drawEllipse(QPointF(cx,y),qMin(4.0,rowHeight*0.3),qMin(4.0,rowHeight*0.3));
            if(member){ if(firstY<0) firstY=y; lastY=y; }
        }
        // The line joining the members is what makes a combination readable as
        // one thing rather than as a column of unrelated dots.
        if(firstY>=0&&lastY>firstY){
            QPen join(spec.series.at(0).color);
            join.setWidthF(1.6);
            p->setPen(join);
            p->drawLine(QPointF(cx,firstY),QPointF(cx,lastY));
        }
    }
    p->restore();
}

void QtPlotBackend::drawComposition(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    // Each mapped series contributes its total as one part of the whole.
    struct Part { QString label; double value; QColor colour; };
    QVector<Part> parts;
    for(const PlotSeries& s:spec.series){
        double total=0.0;
        for(double v:s.y) if(finite(v)) total+=std::abs(v);
        // finite(total), not just total>0. Each value can be finite while the
        // SUM overflows to infinity - 240 values near 1e308 does it - and an
        // infinite total then divides by itself below to produce NaN, which
        // reaches QPainter as a NaN radius. Qt says so out loud
        // ("QPainterPath::arcTo: Adding arc where a parameter is NaN") and
        // draws something undefined.
        if(total>0.0&&finite(total)) parts.append({s.label,total,s.color});
    }
    if(parts.isEmpty()) return;
    std::sort(parts.begin(),parts.end(),
              [](const Part& a,const Part& b){ return a.value>b.value; });
    double grand=0.0;
    for(const Part& part:parts) grand+=part.value;
    if(!(grand>0.0)) return;

    const QRectF area=target.adjusted(28,44,-28,-28);
    p->save();
    p->setFont(font(spec,spec.style.legendSize));

    const QString& engine=spec.engine;

    if(engine==QLatin1String("Treemap")){
        // Slice-and-dice, alternating direction. Not squarified - that needs a
        // second pass and buys aspect ratio, not correctness - but the areas
        // are exactly proportional, which is the property that matters.
        QRectF remaining=area;
        double left=grand;
        bool horizontal=remaining.width()>=remaining.height();
        for(int i=0;i<parts.size();++i){
            const double fraction=parts[i].value/qMax(1e-12,left);
            QRectF cell;
            if(i==parts.size()-1){
                cell=remaining;
            }else if(horizontal){
                const double w=remaining.width()*fraction;
                cell=QRectF(remaining.left(),remaining.top(),w,remaining.height());
                remaining.setLeft(remaining.left()+w);
            }else{
                const double h=remaining.height()*fraction;
                cell=QRectF(remaining.left(),remaining.top(),remaining.width(),h);
                remaining.setTop(remaining.top()+h);
            }
            left-=parts[i].value;
            horizontal=remaining.width()>=remaining.height();
            p->setBrush(parts[i].colour);
            QPen edge(spec.style.background); edge.setWidthF(1.5);
            p->setPen(edge);
            p->drawRect(cell);
            if(cell.width()>52&&cell.height()>18){
                p->setPen(spec.style.background.lightness()<128?Qt::white:Qt::black);
                p->drawText(cell.adjusted(5,4,-5,-4),Qt::AlignLeft|Qt::AlignTop|Qt::TextWordWrap,
                            QStringLiteral("%1\n%2%").arg(parts[i].label)
                                .arg(parts[i].value/grand*100.0,0,'f',1));
            }
        }
    }else if(engine==QLatin1String("Sunburst")){
        // One ring, because a flat list of series has one level of hierarchy.
        // Drawing a second ring would invent a structure the data does not have.
        const QPointF centre=area.center();
        const double outer=qMin(area.width(),area.height())*0.46;
        const double inner=outer*0.42;
        double start=90.0;
        for(const Part& part:parts){
            const double sweep=-360.0*part.value/grand;
            QPainterPath ring;
            ring.arcMoveTo(QRectF(centre.x()-outer,centre.y()-outer,2*outer,2*outer),start);
            ring.arcTo(QRectF(centre.x()-outer,centre.y()-outer,2*outer,2*outer),start,sweep);
            ring.arcTo(QRectF(centre.x()-inner,centre.y()-inner,2*inner,2*inner),start+sweep,-sweep);
            ring.closeSubpath();
            p->setBrush(part.colour);
            QPen edge(spec.style.background); edge.setWidthF(1.2);
            p->setPen(edge);
            p->drawPath(ring);
            start+=sweep;
        }
    }else if(engine==QLatin1String("Venn Diagram")){
        // Two or three circles, sized by their totals and overlapped by a fixed
        // amount. The overlap is not computed from the data - a Venn diagram
        // with true proportional intersections is not generally constructible,
        // and pretending otherwise would be a lie drawn to scale.
        const QPointF centre=area.center();
        const double r=qMin(area.width(),area.height())*0.26;
        const int count=qMin(3,parts.size());
        const double offsets[3][2]={{-0.55,0.0},{0.55,0.0},{0.0,0.9}};
        for(int i=0;i<count;++i){
            // parts is sorted descending and every value is finite and
            // positive, so the ratio is in (0,1] - but the division is guarded
            // anyway, because a NaN here becomes a NaN circle radius and the
            // failure is silent apart from a Qt warning nobody reads.
            const double largest=parts[0].value;
            const double share=largest>0.0?parts[i].value/largest:1.0;
            const double scale=0.7+0.6*(finite(share)?qBound(0.0,share,1.0):1.0);
            QColor fill=parts[i].colour; fill.setAlphaF(0.45);
            p->setBrush(fill);
            QPen edge(parts[i].colour); edge.setWidthF(1.2);
            p->setPen(edge);
            const QPointF at=centre+QPointF(offsets[i][0]*r,offsets[i][1]*r*0.8);
            p->drawEllipse(at,r*scale,r*scale);
            p->setPen(spec.style.foreground);
            // OUTWARD FROM THE CENTRE, not always above.
            //
            // Every label was drawn above its own circle, which is right for
            // the two side by side and wrong for the third: that one sits
            // BELOW the centre, so its name landed inside the overlap of the
            // other two - unreadable against two translucent fills, and
            // pointing at the wrong region. A three-set Venn is the case the
            // engine exists for and it was the only one that came out wrong.
            const bool below=offsets[i][1]>0.0;
            const double lift=r*scale+16.0;
            p->drawText(QRectF(at.x()-r,at.y()+(below?lift:-lift),2*r,14),
                        Qt::AlignCenter,parts[i].label);
        }
    }else if(engine==QLatin1String("Word Cloud")||engine==QLatin1String("Bubble Cloud")){
        // Size by magnitude. Placement is a spiral with overlap rejection,
        // which is what every word cloud does and is honest about being
        // arbitrary: position carries no meaning here, only size does.
        QVector<QRectF> placed;
        const QPointF centre=area.center();
        for(const Part& part:parts){
            // Same guard as the Venn circles: this weight sets a font size
            // and a bubble radius, and NaN in either is undefined behaviour
            // inside QPainter rather than a visible mistake.
            const double biggest=parts.first().value;
            const double ratio=biggest>0.0?part.value/biggest:1.0;
            const double weight=finite(ratio)?qBound(0.0,ratio,1.0):1.0;
            const bool bubble=(engine==QLatin1String("Bubble Cloud"));
            QFont f=font(spec,spec.style.legendSize);
            f.setPointSizeF(qBound(7.0,spec.style.legendSize*(0.9+2.8*weight),34.0));
            f.setBold(weight>0.55);
            p->setFont(f);
            const QFontMetricsF fm(f,p->device());
            const double w=bubble?qMax(18.0,60.0*weight):fm.horizontalAdvance(part.label)+8.0;
            const double h=bubble?w:fm.height();
            QRectF box;
            bool ok=false;
            for(double t=0.0;t<64.0;t+=0.28){
                const double radius=6.0*t;
                const QPointF at=centre+QPointF(std::cos(t)*radius,std::sin(t)*radius*0.62);
                box=QRectF(at.x()-w/2.0,at.y()-h/2.0,w,h);
                if(!area.contains(box)) continue;
                bool clash=false;
                for(const QRectF& other:placed) if(box.intersects(other)){ clash=true; break; }
                if(!clash){ ok=true; break; }
            }
            if(!ok) continue;
            placed.append(box);
            if(bubble){
                QColor fill=part.colour; fill.setAlphaF(0.7);
                p->setBrush(fill); p->setPen(Qt::NoPen);
                p->drawEllipse(box);
                p->setPen(spec.style.foreground);
                // THE LABEL HAS TO FIT INSIDE THE BUBBLE.
                //
                // A word cloud sizes its box to its text; a bubble sizes its
                // circle to its VALUE and then wrote the text at a size chosen
                // from the same value independently. A small share with a long
                // name came out as a word lying across its own circle and its
                // neighbours - "signal" drawn as "igna" with the ends outside
                // the bubble, which reads as two different labels.
                //
                // The widest text a circle can hold is its inscribed square,
                // d/sqrt(2). Shrink to that, and if the name still will not go
                // at the smallest readable size, elide it - a short name that
                // is inside its bubble beats a long one that is not.
                const double usable=box.width()*0.70;
                QFont inner=f;
                QFontMetricsF ifm(inner,p->device());
                while(inner.pointSizeF()>6.0
                      &&ifm.horizontalAdvance(part.label)>usable){
                    inner.setPointSizeF(inner.pointSizeF()-0.5);
                    ifm=QFontMetricsF(inner,p->device());
                }
                p->setFont(inner);
                p->drawText(QRectF(box.center().x()-usable/2.0,
                                   box.center().y()-ifm.height()/2.0,
                                   usable,ifm.height()),
                            Qt::AlignCenter,
                            ifm.elidedText(part.label,Qt::ElideRight,usable));
                p->setFont(f);
            }else{
                p->setPen(part.colour);
                p->drawText(box,Qt::AlignCenter,part.label);
            }
        }
    }else{
        // Sankey. One stage, because a flat list of series is a split rather
        // than a network: a single source flowing into one band per part, with
        // the band's thickness the magnitude.
        const double leftX=area.left()+area.width()*0.14;
        const double rightX=area.right()-area.width()*0.18;
        double y=area.top();
        const double sourceTop=area.top();
        double sourceCursor=sourceTop;
        for(const Part& part:parts){
            const double thickness=area.height()*(part.value/grand)*0.92;
            QPainterPath band;
            const double midX=(leftX+rightX)/2.0;
            band.moveTo(leftX,sourceCursor);
            band.cubicTo(midX,sourceCursor,midX,y,rightX,y);
            band.lineTo(rightX,y+thickness);
            band.cubicTo(midX,y+thickness,midX,sourceCursor+thickness,leftX,sourceCursor+thickness);
            band.closeSubpath();
            QColor fill=part.colour; fill.setAlphaF(0.62);
            p->setBrush(fill); p->setPen(Qt::NoPen);
            p->drawPath(band);
            p->setPen(spec.style.foreground);
            p->drawText(QRectF(rightX+6,y+thickness/2.0-8,area.width()*0.17,16),
                        Qt::AlignLeft|Qt::AlignVCenter,part.label);
            sourceCursor+=thickness;
            y+=thickness+area.height()*0.02;
        }
        p->setBrush(spec.style.foreground);
        p->setPen(Qt::NoPen);
        p->drawRect(QRectF(leftX-10,sourceTop,10,sourceCursor-sourceTop));
    }

    drawFloatingTitle(p,target,spec);
    p->restore();
}

// ---------------------------------------------------------------------------
// 3-D fields: quiver, cones, stream tubes and ribbons, tensor glyphs, and the
// volume family.
//
// Six mapped columns for a vector field - x, y, z and the three components -
// or four for a scalar volume. Everything is projected by the same orthographic
// transform as the other 3-D engines and painted back to front, because a glyph
// drawn out of depth order reads as a field pointing the wrong way.
//
// VTK remains the right tool for a rotatable, lit, million-cell volume. This is
// the version that exports as vectors and does not need a plugin.
// ---------------------------------------------------------------------------
// The volume family: a scalar sampled through a box, and the five ways of
// showing one.
//
// All six used to share one branch - `startsWith("Volume") || startsWith("Iso")
// || == "Contour Slice"` - and drew the same cloud of faded points. The code's
// own comment called it "an isosurface without the surface", which was honest
// and is exactly the problem: somebody choosing "Isosurface" got a point cloud
// and had no way to learn that this backend drew all six alike.
//
// A SCALAR FIELD ON A LATTICE, binned from the scattered samples. Everything
// below reads this and nothing re-derives it.
struct VolumeGrid {
    int side=0;
    double xLo=0,xHi=1,yLo=0,yHi=1,zLo=0,zHi=1,vLo=0,vHi=1;
    QVector<double> cells;                 // side^3, NaN where unsampled
    bool valid=false;
    int index(int i,int j,int k) const { return (k*side+j)*side+i; }
    double at(int i,int j,int k) const {
        return cells[index(qBound(0,i,side-1),qBound(0,j,side-1),qBound(0,k,side-1))];
    }
    double x(int i) const { return xLo+(xHi-xLo)*double(i)/double(qMax(1,side-1)); }
    double y(int j) const { return yLo+(yHi-yLo)*double(j)/double(qMax(1,side-1)); }
    double z(int k) const { return zLo+(zHi-zLo)*double(k)/double(qMax(1,side-1)); }
};

VolumeGrid volumeGridFrom(const QVector<double>& xs,const QVector<double>& ys,
                          const QVector<double>& zs,const QVector<double>& vs){
    VolumeGrid g;
    const int n=qMin(qMin(xs.size(),ys.size()),qMin(zs.size(),vs.size()));
    if(n<8) return g;
    double xLo=std::numeric_limits<double>::infinity(),xHi=-xLo;
    double yLo=xLo,yHi=xHi,zLo=xLo,zHi=xHi;
    for(int i=0;i<n;++i){
        if(!finite(xs[i])||!finite(ys[i])||!finite(zs[i])||!finite(vs[i])) continue;
        xLo=qMin(xLo,xs[i]); xHi=qMax(xHi,xs[i]);
        yLo=qMin(yLo,ys[i]); yHi=qMax(yHi,ys[i]);
        zLo=qMin(zLo,zs[i]); zHi=qMax(zHi,zs[i]);
    }
    if(!finite(xLo)||!(xHi>xLo)||!(yHi>yLo)||!(zHi>zLo)) return g;

    // Cube root of the sample count, so a lattice of N^3 points reconstructs
    // at about N. Bounded below because a surface needs a few cells to be a
    // surface, and above because the cost is the cube of this.
    const int side=qBound(6,int(std::cbrt(double(n))+0.5),40);
    g.side=side; g.xLo=xLo; g.xHi=xHi; g.yLo=yLo; g.yHi=yHi; g.zLo=zLo; g.zHi=zHi;
    g.cells.fill(std::numeric_limits<double>::quiet_NaN(),side*side*side);
    QVector<double> sums(side*side*side,0.0);
    QVector<int> counts(side*side*side,0);
    for(int i=0;i<n;++i){
        if(!finite(xs[i])||!finite(ys[i])||!finite(zs[i])||!finite(vs[i])) continue;
        const int ci=qBound(0,int((xs[i]-xLo)/(xHi-xLo)*(side-1)+0.5),side-1);
        const int cj=qBound(0,int((ys[i]-yLo)/(yHi-yLo)*(side-1)+0.5),side-1);
        const int ck=qBound(0,int((zs[i]-zLo)/(zHi-zLo)*(side-1)+0.5),side-1);
        const int at=g.index(ci,cj,ck);
        sums[at]+=vs[i]; counts[at]+=1;
    }
    double vLo=std::numeric_limits<double>::infinity(),vHi=-vLo;
    for(int k=0;k<side*side*side;++k){
        if(counts[k]==0) continue;
        g.cells[k]=sums[k]/double(counts[k]);
        vLo=qMin(vLo,g.cells[k]); vHi=qMax(vHi,g.cells[k]);
    }
    if(!finite(vLo)||!(vHi>vLo)) return g;

    // A lattice binned from scattered samples has holes, and marching through
    // a hole makes a surface with a bite out of it. Filled with the mean of
    // whatever neighbours a cell has, repeatedly - which is the same
    // gap-closing the 2-D fields already do, in one more dimension.
    for(int pass=0;pass<3;++pass){
        QVector<double> filled=g.cells;
        for(int k=0;k<side;++k) for(int j=0;j<side;++j) for(int i=0;i<side;++i){
            if(finite(g.cells[g.index(i,j,k)])) continue;
            double sum=0.0; int seen=0;
            for(int dk=-1;dk<=1;++dk) for(int dj=-1;dj<=1;++dj) for(int di=-1;di<=1;++di){
                if(i+di<0||j+dj<0||k+dk<0||i+di>=side||j+dj>=side||k+dk>=side) continue;
                const double v=g.cells[g.index(i+di,j+dj,k+dk)];
                if(finite(v)){ sum+=v; ++seen; }
            }
            if(seen>0) filled[g.index(i,j,k)]=sum/double(seen);
        }
        g.cells=filled;
    }
    // Whatever is still unsampled sits at the low end rather than being a
    // hole: an unvisited corner of the box is outside the thing being drawn.
    for(int k=0;k<side*side*side;++k) if(!finite(g.cells[k])) g.cells[k]=vLo;

    g.vLo=vLo; g.vHi=vHi; g.valid=true;
    return g;
}

// MARCHING TETRAHEDRA, not marching cubes.
//
// Cubes needs a 256-entry triangle table, and one wrong entry produces a
// surface that looks entirely plausible and is wrong in a way nobody notices -
// which is not a thing to write from memory. A tetrahedron has four corners, so
// there are sixteen sign patterns and every one is either empty, one corner cut
// off (a triangle) or two-and-two (a quad). That is derivable at the point of
// use rather than recited.
//
// Verified before it was written into the renderer, against x^2+y^2+z^2-1 = 0,
// whose answers are arithmetic:
//
//     n    triangles     area    4*pi*r^2     error   open edges
//     8          768   11.7991    12.5664    -6.11%            0
//    16         3168   12.3851    12.5664    -1.44%            0
//    32        12768   12.5200    12.5664    -0.37%            0
//    64        51336   12.5549    12.5664    -0.09%            0
//
// Area converging at second order, and the surface CLOSED at every resolution.
// The first closure check hashed vertex coordinates and reported holes that
// grew with n; that instrument was wrong, not the mesh. Two tetrahedra cutting
// the same grid edge compute the same point from the same corner values but
// may walk the corners the other way round, so it is lerp(a,b,t) against
// lerp(b,a,1-t) - equal in exact arithmetic, not bitwise. Labelling each vertex
// by the GRID EDGE it lies on makes identity exact and the holes disappeared.
struct IsoTriangle { QVector3D a,b,c; };

void marchTetrahedron(const QVector3D p[4],const double val[4],double iso,
                      QVector<IsoTriangle>& out){
    int inside=0;
    for(int i=0;i<4;++i) if(val[i]<iso) ++inside;
    if(inside==0||inside==4) return;
    static const int kEdges[6][2]={{0,1},{0,2},{0,3},{1,2},{1,3},{2,3}};
    QVector3D cut[4]; int which[4]; int found=0;
    for(int e=0;e<6&&found<4;++e){
        const int i=kEdges[e][0],j=kEdges[e][1];
        if((val[i]<iso)==(val[j]<iso)) continue;
        const double d=val[j]-val[i];
        const double s=(std::abs(d)<1e-300)?0.5:(iso-val[i])/d;
        cut[found]=p[i]+(p[j]-p[i])*float(s);
        which[found]=e;
        ++found;
    }
    if(found==3){ out.append({cut[0],cut[1],cut[2]}); return; }
    if(found!=4) return;
    // Four cut points are a quadrilateral and THE ORDER MATTERS: joining them
    // in the order they were collected can cross the quad and leave a bow-tie
    // with a hole in it. Two edges that share no corner are opposite sides, so
    // ordering by "shares a corner with the previous one" walks the rim.
    int order[4]={0,1,2,3};
    const auto shares=[&](int a,int b){
        return kEdges[which[a]][0]==kEdges[which[b]][0]
             ||kEdges[which[a]][0]==kEdges[which[b]][1]
             ||kEdges[which[a]][1]==kEdges[which[b]][0]
             ||kEdges[which[a]][1]==kEdges[which[b]][1];
    };
    for(int k=1;k<4;++k){
        if(shares(order[k-1],order[k])) continue;
        for(int m=k+1;m<4;++m)
            if(shares(order[k-1],order[m])){ std::swap(order[k],order[m]); break; }
    }
    out.append({cut[order[0]],cut[order[1]],cut[order[2]]});
    out.append({cut[order[0]],cut[order[2]],cut[order[3]]});
}

// The whole lattice, one cube at a time. The six tetrahedra share the 0-6
// diagonal, and this particular split TILES: neighbouring cubes divided the
// same way agree on the face between them, which is what makes the mesh
// watertight rather than a pile of shells.
QVector<IsoTriangle> isosurfaceOf(const VolumeGrid& g,double iso){
    QVector<IsoTriangle> out;
    if(!g.valid||g.side<2) return out;
    static const int dx[8]={0,1,1,0,0,1,1,0};
    static const int dy[8]={0,0,1,1,0,0,1,1};
    static const int dz[8]={0,0,0,0,1,1,1,1};
    static const int kTets[6][4]={{0,5,1,6},{0,1,2,6},{0,2,3,6},
                                  {0,3,7,6},{0,7,4,6},{0,4,5,6}};
    for(int k=0;k+1<g.side;++k)
      for(int j=0;j+1<g.side;++j)
        for(int i=0;i+1<g.side;++i){
            QVector3D corner[8]; double value[8];
            for(int q=0;q<8;++q){
                corner[q]=QVector3D(float(i+dx[q]),float(j+dy[q]),float(k+dz[q]));
                value[q]=g.at(i+dx[q],j+dy[q],k+dz[q]);
            }
            bool ok=true;
            for(int q=0;q<8&&ok;++q) ok=finite(value[q]);
            if(!ok) continue;
            for(int tet=0;tet<6;++tet){
                QVector3D p[4]; double val[4];
                for(int m=0;m<4;++m){ p[m]=corner[kTets[tet][m]]; val[m]=value[kTets[tet][m]]; }
                marchTetrahedron(p,val,iso,out);
            }
        }
    return out;
}

// The five readings of a scalar volume.
//
//   Isosurface     the surface at one level, filled and depth-sorted
//   Isonormals     the same surface as a wireframe, with a normal off each
//                  face - which is how you see whether a reconstruction is
//                  smooth or faceted, and which way it is oriented
//   Isocaps        the surface, plus the CAPS where the enclosed region is cut
//                  by the walls of the box. Without caps an isosurface that
//                  runs out of the data looks like an open shell; with them it
//                  reads as a solid that has been sliced.
//   Volume Slice   three orthogonal planes through the middle, coloured by the
//                  scalar - the whole field, not one level of it
//   Contour Slice  the same three planes, drawn as level curves instead
//
// One grid, five readings. The grid is the expensive part and none of these
// recomputes it.
void drawVolumeFamily(QPainter* p,const Projection& proj,const PlotSpec& spec,
                      const Bounds& bx,const Bounds& by,const Bounds& bz,
                      const QVector<double>& xs,const QVector<double>& ys,
                      const QVector<double>& zs,const QVector<double>& scalar,
                      ColourMapKind cmap){
    const VolumeGrid g=volumeGridFrom(xs,ys,zs,scalar);
    if(!g.valid) return;
    const QString& engine=spec.engine;
    const bool caps=engine==QLatin1String("Isocaps");
    const bool normals=engine==QLatin1String("Isonormals");
    const bool slice=engine==QLatin1String("Volume Slice");
    const bool contourSlice=engine==QLatin1String("Contour Slice");
    const bool show=engine==QLatin1String("Volume Show");

    // A lattice index, back to where it belongs in the cube. The grid runs
    // 0..side-1 in each direction and the cube runs -0.5..0.5, so this is the
    // one conversion everything below shares.
    const auto place=[&](double i,double j,double k,double* depth=nullptr){
        const double span=double(qMax(1,g.side-1));
        return project(proj,i/span-0.5,j/span-0.5,k/span-0.5,depth);
    };

    if(show){
        // Unchanged, and correct for what it is: a projection of the samples,
        // shaded and sized by the scalar. It is the only one of the six that
        // makes no claim to a surface.
        struct Speck { double depth; QPointF at; double t; };
        QVector<Speck> specks;
        const int n=qMin(qMin(xs.size(),ys.size()),qMin(zs.size(),scalar.size()));
        const int stride=qMax(1,n/2200);
        for(int i=0;i<n;i+=stride){
            if(!finite(xs[i])||!finite(ys[i])||!finite(zs[i])||!finite(scalar[i])) continue;
            double d=0;
            const QPointF at=project(proj,bx.norm(xs[i]),by.norm(ys[i]),bz.norm(zs[i]),&d);
            specks.append({d,at,rampPosition(spec.style,scalar[i],g.vLo,g.vHi)});
        }
        std::sort(specks.begin(),specks.end(),
                  [](const Speck& a,const Speck& b){ return a.depth<b.depth; });
        p->setPen(Qt::NoPen);
        for(const Speck& s:std::as_const(specks)){
            QColor fill=colourMapStyled(cmap,spec.style,s.t);
            fill.setAlphaF(0.25+0.65*s.t);
            p->setBrush(fill);
            p->drawEllipse(s.at,qMax(0.9,2.2+3.4*s.t),qMax(0.9,2.2+3.4*s.t));
        }
        return;
    }

    if(slice||contourSlice){
        // Three planes through the middle, one per axis. Three rather than one
        // because a single slice cannot show whether a feature is a sphere or
        // a tube, and that is the first question anyone asks of a volume.
        const int mid=g.side/2;
        struct Plane { int axis; };
        static const Plane kPlanes[3]={{0},{1},{2}};
        for(const Plane& plane:kPlanes){
            for(int b=0;b+1<g.side;++b){
                for(int a=0;a+1<g.side;++a){
                    const auto sample=[&](int ia,int ib){
                        return (plane.axis==0)?g.at(mid,ia,ib)
                              :(plane.axis==1)?g.at(ia,mid,ib)
                                              :g.at(ia,ib,mid);
                    };
                    const auto corner=[&](int ia,int ib,double* d=nullptr){
                        return (plane.axis==0)?place(mid,ia,ib,d)
                              :(plane.axis==1)?place(ia,mid,ib,d)
                                              :place(ia,ib,mid,d);
                    };
                    const double v00=sample(a,b),v10=sample(a+1,b);
                    const double v11=sample(a+1,b+1),v01=sample(a,b+1);
                    if(!finite(v00)||!finite(v10)||!finite(v11)||!finite(v01)) continue;
                    if(contourSlice) continue;              // lines, drawn below
                    const double mean=(v00+v10+v11+v01)*0.25;
                    QPolygonF quad;
                    quad<<corner(a,b)<<corner(a+1,b)<<corner(a+1,b+1)<<corner(a,b+1);
                    QColor fill=colourMapStyled(cmap,spec.style,
                                    rampPosition(spec.style,mean,g.vLo,g.vHi));
                    fill.setAlphaF(0.72);
                    p->setPen(Qt::NoPen);
                    p->setBrush(fill);
                    p->drawPolygon(quad);
                }
            }
            if(!contourSlice) continue;
            // Level curves on the plane, by the same marching squares the 2-D
            // contour uses - eight levels, each in its own colour.
            constexpr int kLevels=8;
            for(int level=1;level<=kLevels;++level){
                const double frac=double(level)/double(kLevels+1);
                const double iso=g.vLo+(g.vHi-g.vLo)*frac;
                QPen pen(colourMapStyled(cmap,spec.style,frac));
                pen.setWidthF(qMax(0.5,spec.style.lineWidth*0.8));
                p->setPen(pen); p->setBrush(Qt::NoBrush);
                for(int b=0;b+1<g.side;++b) for(int a=0;a+1<g.side;++a){
                    const auto sample=[&](int ia,int ib){
                        return (plane.axis==0)?g.at(mid,ia,ib)
                              :(plane.axis==1)?g.at(ia,mid,ib)
                                              :g.at(ia,ib,mid);
                    };
                    const auto corner=[&](double ia,double ib){
                        return (plane.axis==0)?place(mid,ia,ib)
                              :(plane.axis==1)?place(ia,mid,ib)
                                              :place(ia,ib,mid);
                    };
                    const double v00=sample(a,b),v10=sample(a+1,b);
                    const double v11=sample(a+1,b+1),v01=sample(a,b+1);
                    if(!finite(v00)||!finite(v10)||!finite(v11)||!finite(v01)) continue;
                    const int code=(v00>iso?1:0)|(v10>iso?2:0)|(v11>iso?4:0)|(v01>iso?8:0);
                    if(code==0||code==15) continue;
                    const auto lerp=[&](double va,double vb,double lo,double hi){
                        const double d=vb-va;
                        return std::abs(d)<1e-15? lo : lo+(hi-lo)*((iso-va)/d);
                    };
                    const QPointF bottom=corner(lerp(v00,v10,a,a+1),b);
                    const QPointF right =corner(a+1,lerp(v10,v11,b,b+1));
                    const QPointF top   =corner(lerp(v01,v11,a,a+1),b+1);
                    const QPointF left  =corner(a,lerp(v00,v01,b,b+1));
                    switch(code){
                    case 1: case 14: p->drawLine(left,bottom); break;
                    case 2: case 13: p->drawLine(bottom,right); break;
                    case 3: case 12: p->drawLine(left,right); break;
                    case 4: case 11: p->drawLine(right,top); break;
                    case 6: case 9:  p->drawLine(bottom,top); break;
                    case 7: case 8:  p->drawLine(left,top); break;
                    case 5:  p->drawLine(left,bottom); p->drawLine(right,top); break;
                    case 10: p->drawLine(left,top); p->drawLine(bottom,right); break;
                    default: break;
                    }
                }
            }
        }
        return;
    }

    // The surface itself. Halfway up the range unless the figure says
    // otherwise - a level at the minimum encloses everything and a level at the
    // maximum encloses nothing, and neither is a picture.
    const double level=qBound(0.0,spec.parameter(QStringLiteral("isoLevel"),0.5),1.0);
    const double iso=g.vLo+(g.vHi-g.vLo)*level;
    const QVector<IsoTriangle> mesh=isosurfaceOf(g,iso);
    if(mesh.isEmpty()) return;

    // The caps first, so the surface is drawn over them.
    //
    // A cap is where the ENCLOSED REGION meets a wall of the box: on that wall
    // the field is a 2-D scalar, and the cap is the part of it the surface
    // encloses. Filled cell by cell rather than outlined, because the point of
    // a cap is that the solid looks solid where it has been cut.
    //
    // WHICH SIDE IS ENCLOSED was the fault. An isosurface at a level encloses
    // the region where the field EXCEEDS it - that is what everybody means by
    // an isosurface of a blob, and what the surface above draws. The cap filled
    // the cells that were BELOW it instead, which for a volume with its
    // features in the middle is very nearly the whole wall: the figure came out
    // as a solid cube with the isosurface invisible inside it.
    //
    // The surface itself is unaffected either way, because the boundary between
    // the two regions is the same set of triangles - which is exactly why this
    // could be wrong without anything else looking wrong.
    if(caps){
        QColor capColour=colourMapStyled(cmap,spec.style,level);
        capColour.setAlphaF(0.85);
        p->setPen(Qt::NoPen);
        p->setBrush(capColour);
        for(int axis=0;axis<3;++axis){
            for(int face=0;face<2;++face){
                const int at=face?g.side-1:0;
                for(int b=0;b+1<g.side;++b) for(int a=0;a+1<g.side;++a){
                    const auto sample=[&](int ia,int ib){
                        return (axis==0)?g.at(at,ia,ib)
                              :(axis==1)?g.at(ia,at,ib)
                                        :g.at(ia,ib,at);
                    };
                    const auto corner=[&](int ia,int ib){
                        return (axis==0)?place(at,ia,ib)
                              :(axis==1)?place(ia,at,ib)
                                        :place(ia,ib,at);
                    };
                    const double v00=sample(a,b),v10=sample(a+1,b);
                    const double v11=sample(a+1,b+1),v01=sample(a,b+1);
                    if(!finite(v00)||!finite(v10)||!finite(v11)||!finite(v01)) continue;
                    // Enclosed on all four corners. A cell straddling the
                    // level is left to the surface, which cuts it properly.
                    if(!(v00>=iso&&v10>=iso&&v11>=iso&&v01>=iso)) continue;
                    QPolygonF quad;
                    quad<<corner(a,b)<<corner(a+1,b)<<corner(a+1,b+1)<<corner(a,b+1);
                    p->drawPolygon(quad);
                }
            }
        }
    }

    // Painter's algorithm over the triangles. Without it a face behind another
    // is drawn over it and the surface reads inside out - the same reason the
    // quads of a 3-D surface are sorted.
    struct Facet { double depth; QPolygonF shape; QPointF centre; QPointF normalTip; };
    QVector<Facet> facets;
    facets.reserve(mesh.size());
    for(const IsoTriangle& tri:mesh){
        double d0=0,d1=0,d2=0;
        const QPointF a=place(tri.a.x(),tri.a.y(),tri.a.z(),&d0);
        const QPointF b=place(tri.b.x(),tri.b.y(),tri.b.z(),&d1);
        const QPointF c=place(tri.c.x(),tri.c.y(),tri.c.z(),&d2);
        Facet facet;
        facet.depth=(d0+d1+d2)/3.0;
        facet.shape<<a<<b<<c;
        facet.centre=QPointF((a.x()+b.x()+c.x())/3.0,(a.y()+b.y()+c.y())/3.0);
        if(normals){
            // The face normal, in the lattice's own space, then projected -
            // so what is drawn is the projection of the direction rather than
            // an angle guessed on screen.
            const QVector3D n=QVector3D::crossProduct(tri.b-tri.a,tri.c-tri.a).normalized();
            const QVector3D mid=(tri.a+tri.b+tri.c)/3.0f;
            const QVector3D tip=mid+n*float(qMax(1.0,double(g.side))*0.06);
            facet.normalTip=place(tip.x(),tip.y(),tip.z());
        }
        facets.append(facet);
    }
    std::sort(facets.begin(),facets.end(),
              [](const Facet& a,const Facet& b){ return a.depth<b.depth; });

    const QColor base=colourMapStyled(cmap,spec.style,level);
    for(const Facet& facet:std::as_const(facets)){
        if(normals){
            // A wireframe, so the normals are visible against it. A filled
            // surface with sticks coming out of it hides most of them.
            p->setBrush(Qt::NoBrush);
            QPen wire(base); wire.setWidthF(qMax(0.3,spec.style.lineWidth*0.4));
            p->setPen(wire);
            p->drawPolygon(facet.shape);
            QPen quill(spec.style.foreground);
            quill.setWidthF(qMax(0.3,spec.style.lineWidth*0.4));
            p->setPen(quill);
            p->drawLine(facet.centre,facet.normalTip);
        }else{
            QColor fill=base;
            fill.setAlphaF(caps?0.9:0.82);
            p->setBrush(fill);
            QPen edge(fill.darker(118)); edge.setWidthF(0.25);
            p->setPen(edge);
            p->drawPolygon(facet.shape);
        }
    }
}

// The 3-D field engines that read a SCALAR fourth column rather than three
// vector components - the volume family. They have something to draw with four
// columns; the glyph engines do not.
static bool engineNeedsScalarOnly(const QString& engine){
    return engine.startsWith(QLatin1String("Volume"))
        || engine.startsWith(QLatin1String("Iso"))
        || engine==QLatin1String("Contour Slice");
}

void QtPlotBackend::draw3DField(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    // The field colour map, read once. Viridis unless the style asks for
    // another - see PlotStyle::colourMap.
    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);
    const int columns=spec.series.size();
    if(columns<4) return;
    const QVector<double>& xs=spec.series.at(0).y;
    const QVector<double>& ys=spec.series.at(1).y;
    const QVector<double>& zs=spec.series.at(2).y;
    const int n=qMin(xs.size(),qMin(ys.size(),zs.size()));
    if(n<2) return;

    const bool haveVector=(columns>=6);

    // Nothing to draw means nothing drawn - INCLUDING the cube.
    //
    // This used to draw the cube and the axes first and discover afterwards
    // that there were no vectors, which made five engines look like a figure
    // when they were an empty box. It also defeated the sweep's blank check:
    // that compares the render against the same spec with its series cleared,
    // and a cleared spec returns at `columns<4` above without drawing a cube -
    // so the cube itself counted as data, and "every engine drew data" passed
    // on five engines that had drawn none.
    //
    // Returning here instead lets explainEmpty say what is missing, which is
    // the whole point of that function: a figure that comes out empty is the
    // most confusing thing this program can do, and an empty box with axes on
    // it is MORE confusing than an empty canvas, because it looks deliberate.
    if(!haveVector&&!(engineNeedsScalarOnly(spec.engine))) return;
    const QVector<double>& us=spec.series.at(qMin(3,columns-1)).y;
    const QVector<double>& vs=spec.series.at(qMin(4,columns-1)).y;
    const QVector<double>& ws=spec.series.at(qMin(5,columns-1)).y;
    // With only four columns the fourth is a scalar - the volume family - and
    // there is no direction to draw, only a magnitude.
    const QVector<double>& scalar=spec.series.at(3).y;

    const Bounds bx=boundsOf(xs), by=boundsOf(ys), bz=boundsOf(zs);
    if(!bx.valid||!by.valid||!bz.valid) return;

    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFont nameFont=font(spec,spec.style.axisLabelSize);
    const double margin=cubeMargin(p,tickFont,nameFont,spec.style.scaleLabelsVisible);
    const Projection proj=makeProjection(target,spec.view3d.azimuth,spec.view3d.elevation,
                                         spec.view3d.zoom,margin,
                                         spec.view3d.panX,spec.view3d.panY);

    p->save();
    drawBoundingCube(p,proj,spec.style.gridColor);
    drawCubeAxes(p,proj,spec,bx,by,bz,
                 spec.series.at(0).label,spec.series.at(1).label,spec.series.at(2).label,
                 tickFont,nameFont);

    const QString& engine=spec.engine;

    // The volume family gets its own painter: five different readings of one
    // scalar field, where before all six drew the same cloud of faded points.
    if(engineNeedsScalarOnly(engine)){
        drawVolumeFamily(p,proj,spec,bx,by,bz,xs,ys,zs,scalar,cmap);
        drawFloatingTitle(p,target,spec);
        p->restore();
        return;
    }
    const bool tensor=engine==QLatin1String("Tensor Glyph Field");
    const bool cones=engine==QLatin1String("Cone Plot");
    const bool tubes=engine==QLatin1String("Stream Tube")
                   ||engine==QLatin1String("Stream Ribbon");
    // The same test the early return above uses, read from one place. Two
    // copies of "which engines are the volume family" is how the guard and the
    // painter come to disagree about who needs six columns.
    const bool volume=engineNeedsScalarOnly(engine);

    // Magnitude for colour and length, from whichever channels exist.
    double magMax=0.0;
    QVector<double> magnitude(n,0.0);
    for(int i=0;i<n;++i){
        if(haveVector&&i<us.size()&&i<vs.size()&&i<ws.size()
           &&finite(us[i])&&finite(vs[i])&&finite(ws[i])){
            magnitude[i]=std::sqrt(us[i]*us[i]+vs[i]*vs[i]+ws[i]*ws[i]);
        }else if(i<scalar.size()&&finite(scalar[i])){
            magnitude[i]=std::abs(scalar[i]);
        }
        magMax=qMax(magMax,magnitude[i]);
    }
    if(!(magMax>0.0)) magMax=1.0;

    // Depth-sorted, painter's algorithm. Without it a glyph behind another is
    // drawn over it and the field reads inside out.
    struct Glyph { double depth; int index; QPointF at; };
    QVector<Glyph> glyphs;
    glyphs.reserve(n);
    // A field of ten thousand arrows is a grey rectangle. Thin to a readable
    // count, evenly through the record so the sampling is not biased to one end.
    const int budget=volume?2200:900;
    const int stride=qMax(1,n/budget);
    for(int i=0;i<n;i+=stride){
        if(!finite(xs[i])||!finite(ys[i])||!finite(zs[i])) continue;
        double d=0;
        const QPointF at=project(proj,bx.norm(xs[i]),by.norm(ys[i]),bz.norm(zs[i]),&d);
        glyphs.append({d,i,at});
    }
    std::sort(glyphs.begin(),glyphs.end(),
              [](const Glyph& a,const Glyph& b){ return a.depth<b.depth; });

    for(const Glyph& g:glyphs){
        const int i=g.index;
        const double t=rampPosition(spec.style,magnitude[i],0.0,magMax);
        const QColor colour=colourMapStyled(cmap,spec.style,t);

        if(volume){
            // Points shaded and sized by the scalar, with the low end faded
            // out: an isosurface without the surface, which is honest about
            // being a projection of samples rather than a reconstructed mesh.
            QColor fill=colour;
            fill.setAlphaF(0.25+0.65*t);
            p->setPen(Qt::NoPen); p->setBrush(fill);
            const double r=qMax(0.9,2.2+3.4*t);
            p->drawEllipse(g.at,r,r);
            continue;
        }

        if(!haveVector) continue;
        const double u=us.value(i), v=vs.value(i), w=ws.value(i);
        if(!finite(u)||!finite(v)||!finite(w)) continue;
        const double mag=magnitude[i];
        if(!(mag>0.0)) continue;

        // The vector's own tip, projected: the direction on screen is the
        // projection of the direction in space, not an angle guessed in 2-D.
        const QPointF tip=project(proj,
                                  bx.norm(xs[i])+u/magMax*0.16,
                                  by.norm(ys[i])+v/magMax*0.16,
                                  bz.norm(zs[i])+w/magMax*0.16);
        const QPointF along=tip-g.at;
        const double len=std::hypot(along.x(),along.y());
        if(!(len>0.4)) continue;
        const QPointF dir=along/len;
        const QPointF normal(-dir.y(),dir.x());

        if(tensor){
            // A tensor glyph shows magnitude and orientation together: an
            // ellipse whose long axis is the vector and whose short axis is
            // what is left of the magnitude across it.
            p->save();
            QColor fill=colour; fill.setAlphaF(0.7);
            p->setBrush(fill);
            p->setPen(QPen(colour.darker(130),0.4));
            p->translate(g.at);
            p->rotate(std::atan2(dir.y(),dir.x())*180.0/3.14159265358979323846);
            p->drawEllipse(QPointF(0,0),qMax(1.2,len*0.5),qMax(0.7,len*0.22));
            p->restore();
        }else if(cones){
            QPolygonF cone;
            cone<<tip
                <<(g.at+normal*qMax(1.0,len*0.28))
                <<(g.at-normal*qMax(1.0,len*0.28));
            p->setPen(Qt::NoPen); p->setBrush(colour);
            p->drawPolygon(cone);
        }else if(tubes){
            // A tube in projection is a thick line; a ribbon is a flat one. The
            // width carries the magnitude either way.
            QPen pen(colour);
            pen.setWidthF(qMax(0.8,(engine==QLatin1String("Stream Tube"))?2.0+3.0*t:1.0+1.4*t));
            pen.setCapStyle(Qt::RoundCap);
            p->setPen(pen);
            p->drawLine(g.at,tip);
        }else{
            // 3D Quiver.
            QPen pen(colour);
            pen.setWidthF(qMax(0.5,spec.style.lineWidth));
            p->setPen(pen);
            p->drawLine(g.at,tip);
            const double head=qMin(5.0,len*0.34);
            p->drawLine(tip,tip-dir*head+normal*head*0.5);
            p->drawLine(tip,tip-dir*head-normal*head*0.5);
        }
    }

    drawFloatingTitle(p,target,spec);
    p->restore();
}

bool QtPlotBackend::engineHasAxes(const QString& engine){
    // Polar joins pie and donut: a rectangular frame would be meaningless
    // chrome around a radial projection.
    return engine!=QLatin1String("Pie")
        && engine!=QLatin1String("Donut")
        && engine!=QLatin1String("Polar Line")
        && engine!=QLatin1String("Polar Scatter")
        && engine!=QLatin1String("Radiation Pattern")
        // A skewed, logarithmic frame with four families of thermodynamic
        // curve in it. A rectangular one drawn around that would be a
        // second set of axes disagreeing with the first.
        && engine!=QLatin1String("Skew-T Log-P")
        && engine!=QLatin1String("Emagram")
        && engine!=QLatin1String("Stuve Diagram")
        && engine!=QLatin1String("Tephigram")
        // Two triangles and a diamond, all in one construction. A
        // rectangular frame around them would put numbers on axes that
        // nothing in the diagram is measured against.
        && engine!=QLatin1String("Piper Diagram")
        // A grid of shapes measured from their own centre lines, a line of
        // nodes with the arcs above it, and a hierarchy whose two directions
        // are share and depth. None of the three has a coordinate to put on an
        // axis, and Voronoi is deliberately not in this list: its positions
        // ARE the data.
        && engine!=QLatin1String("Stiff Diagram")
        && engine!=QLatin1String("Arc Diagram")
        && engine!=QLatin1String("Icicle Plot")
        && engine!=QLatin1String("Flame Graph")
        // A tree: its horizontal direction is a distance and its vertical one
        // is only an ordering, so a y axis with numbers on it would be a
        // measurement of nothing.
        && engine!=QLatin1String("Cladogram")
        // Thickness is the value and vertical position is nothing, so a y axis
        // would be numbers against a quantity that does not exist.
        && engine!=QLatin1String("Streamgraph")
        // A triangle, like the ternary scatter it shares its projection with.
        // A TRIANGLE IS NOT A RECTANGLE, and the plain ternary scatter was
        // the one triangular engine still getting a rectangular frame drawn
        // behind it: a box ruled 0.0 to 1.0 on both sides, around a diagram
        // whose three axes run along the triangle's edges and are measured in
        // none of those numbers. Every other member of the family - the
        // contour version right below, the Piper diagram, the soil triangles,
        // the QAPF diagrams - was already excluded; this one was missed.
        // Its panels carry their own frames and their own ranges; one
        // rectangle round the outside would be an axis for none of them.
        && engine!=QLatin1String("Plot Matrix")
        && engine!=QLatin1String("Ternary Scatter")
        && engine!=QLatin1String("Ternary Contour")
        // Stages across and flow down: neither direction is a coordinate.
        && engine!=QLatin1String("Alluvial Diagram")
        // Three radial axes, so a rectangular frame would measure nothing.
        && engine!=QLatin1String("Hive Plot")
        // Chromosomes side by side: the horizontal direction is an ordering
        // and the vertical one is a coordinate that restarts on every body.
        && engine!=QLatin1String("Karyotype Ideogram")
        // A ring of coordinate spaces, so nothing on it is a Cartesian
        // position.
        && engine!=QLatin1String("Circos Plot")
        // Three parallel scales sharing one vertical measure and nothing
        // horizontal at all.
        && engine!=QLatin1String("Alignment Nomogram")
        // A named map of regions on a triangle. Nothing on it is a Cartesian
        // coordinate.
        && engine!=QLatin1String("Soil Texture Triangle (UK)")
        && engine!=QLatin1String("Soil Texture Triangle (USDA)")
        // A double triangle of named fields, like the Piper's construction.
        && engine!=QLatin1String("QAPF Diagram (Plutonic)")
        && engine!=QLatin1String("QAPF Diagram (Volcanic)")
        // Two triangles projecting into a square, the same case as the Piper.
        && engine!=QLatin1String("Durov Diagram")
        // 3-D draws its own projected cube; a 2-D frame around it would be
        // chrome that means nothing.
        && engine!=QLatin1String("3D Line")
        && engine!=QLatin1String("3D Scatter")
        && engine!=QLatin1String("3D Topography / Surface")
        && engine!=QLatin1String("3D Mesh")
        && !engine.startsWith(QLatin1String("3D "))
        && engine!=QLatin1String("Surface + Contours")
        && engine!=QLatin1String("Comet 3D")
        && engine!=QLatin1String("Ribbon")
        && engine!=QLatin1String("Polar Histogram")
        && engine!=QLatin1String("Wind Rose")
        && engine!=QLatin1String("Radar Chart")
        && engine!=QLatin1String("Compass")
        && engine!=QLatin1String("Polar Bubble")
        && engine!=QLatin1String("Treemap")
        && engine!=QLatin1String("Sunburst")
        && engine!=QLatin1String("Venn Diagram")
        && engine!=QLatin1String("Word Cloud")
        && engine!=QLatin1String("Bubble Cloud")
        && engine!=QLatin1String("Sankey Diagram")
        // One number against its range, drawn as a dial, a strip, or a panel
        // of columns whose widths are the measurement. None of the three has a
        // Cartesian coordinate, so a rectangular frame would rule numbers
        // against a quantity that is not there.
        && engine!=QLatin1String("Gauge")
        && engine!=QLatin1String("Bullet Chart")
        && engine!=QLatin1String("Marimekko Chart")
        // A force-directed layout and a ring of arcs are both positions this
        // backend invented, not coordinates the data has. An axis around
        // either would be chrome with numbers on it that mean nothing.
        && engine!=QLatin1String("Network Graph")
        && engine!=QLatin1String("Chord Diagram")
        && engine!=QLatin1String("Smith Chart")
        && engine!=QLatin1String("Mosaic Plot")
        && engine!=QLatin1String("UpSet Plot")
        && engine!=QLatin1String("3D Quiver")
        && engine!=QLatin1String("Cone Plot")
        && engine!=QLatin1String("Stream Tube")
        && engine!=QLatin1String("Stream Ribbon")
        && engine!=QLatin1String("Tensor Glyph Field")
        && engine!=QLatin1String("Volume Show")
        && engine!=QLatin1String("Volume Slice")
        && engine!=QLatin1String("Isosurface")
        && engine!=QLatin1String("Isonormals")
        && engine!=QLatin1String("Isocaps")
        && engine!=QLatin1String("Contour Slice");
}

// Histogram and Box Plot describe the distribution of the mapped column rather
// than plotting it against X, so they are rewritten into drawable geometry
// before the axes are computed.
// ------------------------------------------------------------ prepared cache

// See the declaration. This is makeProjection called with exactly what draw3D
// calls it with, and nothing else - the moment it computes its own margin or
// its own fitted size, the anchor stops matching the picture.
QtPlotBackend::CameraFrame QtPlotBackend::cameraFrameFor(const PlotSpec& spec,
                                                         const QRectF& target) const {
    CameraFrame out;
    if(!(target.width()>1.0)||!(target.height()>1.0)) return out;
    // The margin, measured the way cubeMargin measures it. In logical pixels,
    // which is the space the item hands its pointer position in.
    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFont nameFont=font(spec,spec.style.axisLabelSize);
    const QFontMetricsF tick(tickFont);
    const QFontMetricsF name(nameFont);
    const double lines=(spec.style.scaleLabelsVisible?tick.height():0.0)+name.height();
    const double margin=qMax(18.0,lines+10.0+(spec.style.scaleLabelsVisible
                                 ? tick.horizontalAdvance(QStringLiteral("00000"))*0.5
                                 : 0.0));
    const Projection proj=makeProjection(target,spec.view3d.azimuth,spec.view3d.elevation,
                                         spec.view3d.zoom,margin,
                                         spec.view3d.panX,spec.view3d.panY);
    out.origin=proj.origin;
    out.scale=proj.scale;
    out.valid=proj.scale>0.0;
    return out;
}

quint64 QtPlotBackend::specFingerprint(const PlotSpec& spec){
    quint64 h=0xcbf29ce484222325ULL;
    fnvString(h,spec.engine);
    fnvString(h,spec.variant);
    fnvString(h,spec.expression);
    fnvString(h,spec.title);
    for(const PlotAxis* a:{&spec.xAxis,&spec.yAxis}){
        fnvString(h,a->label);
        const unsigned char lg=(a->log10?1:0)|(a->inverted?2:0); fnvBytes(h,&lg,1);
        // The transform rewrites the values, so a prepared spec built under one
        // is not the prepared spec for another.
        fnvBytes(h,&a->transform,sizeof(a->transform));
        // Deliberately NOT the limits. No rewrite reads them - the only code
        // that touches them copies them onto the result - and they now change
        // on every frame of a pan or a pinch. Hashing them meant dragging a
        // violin plot re-derived the violin sixty times a second.
    }
    for(const PlotSeries& s:spec.series){
        fnvString(h,s.label);
        fnvColour(h,s.color);
        fnvDouble(h,s.lineWidth);
        fnvDouble(h,s.markerSize);
        fnvDouble(h,s.opacity);
        // WHICH AXIS THIS SERIES IS DRAWN AGAINST IS PART OF WHAT IT IS.
        //
        // secondaryAxis was missing here, and the omission was invisible until
        // a mapping role could set it: the flag rides through prepareSpec onto
        // the prepared series, so two specs differing only in it hashed the
        // same, the cache returned the figure prepared under the OTHER setting,
        // and moving a column to the right-hand axis drew a pixel-identical
        // picture. The self-test above renders a series four decades from its
        // neighbour with and without the second axis and refuses to accept the
        // same image twice.
        const unsigned char flags=(s.drawLine?1:0)|(s.drawMarkers?2:0)
                                 |(s.secondaryAxis?4:0);
        fnvBytes(h,&flags,1);
        const qsizetype nx=s.x.size(), ny=s.y.size();
        fnvBytes(h,&nx,sizeof(nx)); fnvBytes(h,&ny,sizeof(ny));
        // The bulk of the work, and a straight linear scan of memory that
        // render() is about to copy anyway.
        for(double v:s.x) fnvDouble(h,v);
        for(double v:s.y) fnvDouble(h,v);
        for(qreal v:s.dashPattern) fnvDouble(h,double(v));
    }
    // Only the style fields a rewrite in prepareSpec can read. Font sizes and
    // grid visibility are drawing decisions and must not invalidate the cache.
    fnvColour(h,spec.style.positive);
    fnvColour(h,spec.style.warning);
    fnvColour(h,spec.style.danger);
    fnvColour(h,spec.style.foreground);
    fnvColour(h,spec.style.background);
    // The field grid IS cached on this fingerprint - see cachedGrid - and both
    // of these change what it contains rather than how it is painted. Left out,
    // switching from Nearest to Linear would return the grid built under the
    // previous setting and the control would appear to do nothing until
    // something else forced a rebuild.
    fnvBytes(h,&spec.style.fieldInterpolation,sizeof(spec.style.fieldInterpolation));
    fnvBytes(h,&spec.style.fieldResolution,sizeof(spec.style.fieldResolution));
    // Every one of the scattered-estimator settings, for exactly the same
    // reason: each changes what the grid CONTAINS, and a control that silently
    // returns the previous grid is a control that appears to do nothing.
    fnvBytes(h,&spec.style.fieldEstimator,sizeof(spec.style.fieldEstimator));
    fnvBytes(h,&spec.style.fieldExtrapolation,sizeof(spec.style.fieldExtrapolation));
    fnvBytes(h,&spec.style.fieldValuePolicy,sizeof(spec.style.fieldValuePolicy));
    fnvBytes(h,&spec.style.fieldResponseSpace,sizeof(spec.style.fieldResponseSpace));
    fnvBytes(h,&spec.style.fieldNeighbours,sizeof(spec.style.fieldNeighbours));
    fnvBytes(h,&spec.style.fieldIdwPower,sizeof(spec.style.fieldIdwPower));
    fnvBytes(h,&spec.style.fieldSmoothing,sizeof(spec.style.fieldSmoothing));
    fnvBytes(h,&spec.style.fieldFootprint,sizeof(spec.style.fieldFootprint));
    fnvBytes(h,&spec.style.fieldBridging,sizeof(spec.style.fieldBridging));
    fnvBytes(h,&spec.style.fieldBridgeMaxCells,sizeof(spec.style.fieldBridgeMaxCells));
    fnvBytes(h,&spec.style.fieldInvalidDisplay,sizeof(spec.style.fieldInvalidDisplay));
    fnvBytes(h,&spec.style.fieldKrigingVariogram,sizeof(spec.style.fieldKrigingVariogram));
    fnvBytes(h,&spec.style.fieldLoessFraction,sizeof(spec.style.fieldLoessFraction));
    // Engine parameters. These are constants an engine draws with, so a change
    // to one has to invalidate the prepared figure exactly as a change of
    // column does - without this a new parent mass would return the cached
    // figure and the control would appear to do nothing.
    for(auto it=spec.parameters.constBegin();it!=spec.parameters.constEnd();++it){
        const QByteArray key=it.key().toUtf8();
        fnvBytes(h,key.constData(),size_t(key.size()));
        const double value=it.value();
        fnvBytes(h,&value,sizeof(value));
    }
    return h;
}


const PlotSpec& QtPlotBackend::preparedCached(const PlotSpec& in) const {
    int seriesCount=in.series.size();
    qsizetype pointCount=0;
    for(const PlotSeries& s:in.series) pointCount+=s.x.size()+s.y.size();
    const quint64 hash=specFingerprint(in);

    if(prepCache_.valid
       && prepCache_.hash==hash
       && prepCache_.seriesCount==seriesCount
       && prepCache_.pointCount==pointCount
       && prepCache_.engine==in.engine
       && prepCache_.variant==in.variant
       && prepCache_.expression==in.expression)
        return applyLimits(prepCache_.prepared,in);

    // The core, not the wrapper: the wrapper's only job is to put the caller's
    // limits back on top, and that is done below on every call, hit or miss.
    //
    // Colours first, so what the rewrite chain copies onto its derived series
    // is the colour the figure will actually be drawn in.
    PlotSpec coloured=in;
    assignDefaultSeriesColours(coloured);
    prepCache_.prepared=prepareSpecCore(coloured);
    prepCache_.engine=in.engine;
    prepCache_.variant=in.variant;
    prepCache_.expression=in.expression;
    prepCache_.seriesCount=seriesCount;
    prepCache_.pointCount=pointCount;
    prepCache_.hash=hash;
    prepCache_.valid=true;
    return applyLimits(prepCache_.prepared,in);
}


// A caller's axis limits outrank the engine's own.
//
// Many of the rewrites below set out.xAxis/yAxis themselves, because a
// histogram is drawn against counts and a correlation matrix against variable
// indices - ranges the mapped columns know nothing about. Those assignments are
// unconditional, so once the canvas gained an interactive zoom, every one of
// those engines would have thrown the user's zoom away on the next repaint and
// snapped back to the full range. The limits that arrive on the spec are the
// ones the user is looking at, and they win.

PlotSpec QtPlotBackend::prepareSpec(const PlotSpec& in) const {
    // Colours before the rewrites, for the reason given at
    // assignDefaultSeriesColours: a rewrite copies its source series' colour
    // onto the series it derives, so filling them in afterwards would leave
    // every derived figure carrying the default.
    PlotSpec coloured=in;
    assignDefaultSeriesColours(coloured);
    PlotSpec out=prepareSpecCore(coloured);


    // The axis transforms, on the drawn geometry.
    if(in.xAxis.transform>AxisLog10||in.yAxis.transform>AxisLog10
       ||in.zAxis.transform>AxisLog10){
        // Column-shaped engines carry one MAPPED COLUMN per series, so the
        // transform for an axis belongs to that series' values - not to the
        // .x/.y of every series.
        //
        // Doing it the other way round was quietly wrong for every 3-D engine
        // in the catalogue: series 0, 1 and 2 are the x, y and z columns and
        // all three keep their values in .y, so a y-axis z-score standardised
        // the x column and the z column as well and drew a figure of three
        // columns that had each been divided by a different column's standard
        // deviation. It still looked like a plot.
        const ColumnPlan plan=columnPlan(in.engine);
        if(plan.asSeries&&plan.maximum>=3){
            // A fixed third column: x, y and z, or x, y and the value a colour
            // map runs over. One transform each.
            const int transforms[3]={in.xAxis.transform,in.yAxis.transform,
                                     in.zAxis.transform};
            for(int i=0;i<out.series.size()&&i<3;++i)
                transformValues(out.series[i].y,transforms[i]);
            // Columns beyond the third are the companions of one of the first
            // three - the two components of a vector at (x,y), the three of one
            // at (x,y,z) - and standardising a component on its own turns a
            // direction into a different direction. Left alone deliberately.
        }else if(plan.asSeries){
            // maximum 0: as many columns as are mapped, and they are PEERS -
            // one spoke of a radar chart, one axis of parallel coordinates, one
            // damping curve of a response spectrum. There is no third axis to
            // give its own scale to; standardising column two and not column
            // three would put two of the peers in different units.
            if(!out.series.isEmpty()) transformValues(out.series[0].y,in.xAxis.transform);
            for(int i=1;i<out.series.size();++i)
                transformValues(out.series[i].y,in.yAxis.transform);
        }else{
            for(PlotSeries& s:out.series){
                transformValues(s.x,in.xAxis.transform);
                transformValues(s.y,in.yAxis.transform);
            }
        }
        out.xAxis.label=transformedLabel(out.xAxis.label,in.xAxis.transform);
        out.yAxis.label=transformedLabel(out.yAxis.label,in.yAxis.transform);
        out.zAxis.label=transformedLabel(out.zAxis.label,in.zAxis.transform);
        // The 3-D painters take their axis names from the series labels rather
        // than from the axes, so a transformed z column has to say so there or
        // the cube is labelled with the units it no longer has.
        if(plan.asSeries&&plan.maximum>=3&&out.series.size()>=3&&in.zAxis.transform>AxisLog10)
            out.series[2].label=transformedLabel(out.series.at(2).label,in.zAxis.transform);
        if(plan.asSeries&&out.series.size()>=1&&in.xAxis.transform>AxisLog10)
            out.series[0].label=transformedLabel(out.series.at(0).label,in.xAxis.transform);
        if(plan.asSeries&&out.series.size()>=2&&in.yAxis.transform>AxisLog10)
            out.series[1].label=transformedLabel(out.series.at(1).label,in.yAxis.transform);
        // A limit set in the untransformed space means nothing afterwards, and
        // a zoom kept across a change of transform would clip the new figure to
        // a range that belongs to the old one. Dropped rather than converted:
        // the quantile transform is not invertible without the original sample.
        out.xAxis.min=unsetValue(); out.xAxis.max=unsetValue();
        out.yAxis.min=unsetValue(); out.yAxis.max=unsetValue();
        PlotSpec limits=in;
        if(in.xAxis.transform>AxisLog10){ limits.xAxis.min=unsetValue(); limits.xAxis.max=unsetValue(); }
        if(in.yAxis.transform>AxisLog10){ limits.yAxis.min=unsetValue(); limits.yAxis.max=unsetValue(); }
        return applyLimits(out,limits);
    }

    return applyLimits(out,in);
}

// A floor under the rewrite chain.
//
// Most of the catalogue is expressed as a rewrite: an engine retargets
// `spec.engine` at geometry that already exists and hands the spec back
// through prepareSpec, so an ECDF becomes a staircase and a Probability Plot
// becomes a Q-Q Plot. The chains are normally one or two links long.
//
// Nothing bounded them. A rewrite that produces the engine it started from -
// directly, around a longer loop, or by failing to change the engine on some
// branch - recurses until the stack is gone, and Windows kills the process
// with 0xC00000FD and no message at all. That is the worst failure this
// program can have: no verdict, no log line, nothing to read. It is also the
// failure most likely to be introduced by ordinary work, because adding a
// catalogue engine means adding a rewrite.
//
// So the chain is counted, and past a depth no legitimate rewrite reaches, the
// spec is returned unchanged rather than followed further. An engine that hits
// this draws as whatever it had last become, which is wrong - but it is wrong
// on screen, where it can be seen and reported, instead of taking the
// application down with it. The counter is per-thread because the full render
// runs off the GUI thread.
//
// The depth is 8, not a comfortable-looking larger number, and the reason is
// measured rather than guessed. `g++ -fstack-usage` puts this function's frame
// at **47,472 bytes** - it has a branch per catalogue engine, and the frame
// carries the locals of all of them. Windows gives the main thread 1 MB by
// default, so a chain about 21 deep exhausts the stack on its own. A guard set
// at 24 would never fire: the process would die first, which is the exact
// failure it was added to prevent. Legitimate chains observed here are one or
// two links long, so 8 is generous and lands at roughly 380 KB.

PlotSpec QtPlotBackend::prepareSpecCore(const PlotSpec& in) const {
    // Named for what it is, and not `depth`: three rewrites further down this
    // function already use that name for a borehole/beam depth column.
    RewriteDepthGuard rewriteDepth;
    if(rewriteDepth.tooDeep){
        qWarning("GraphVis: the rewrite chain for engine '%s' did not settle "
                 "after %d steps and was stopped. That is a cycle in "
                 "prepareSpecCore, not a fault in the data.",
                 qPrintable(in.engine),kMaxRewriteDepth);
        return in;
    }

    // THE CHAIN, IN SIX PIECES, IN ORDER.
    //
    // This was one function of 19,600 lines. Its content is unchanged and it
    // is still one ordered chain - the pieces are contiguous runs of the
    // original sequence, called in the original sequence, because the order is
    // load-bearing: several engines are matched by a `startsWith` that a later
    // and more specific test would also match, so re-ordering the groups would
    // silently re-order the catalogue.
    //
    // std::optional and not a bool with an out parameter: every block in there
    // ends in `return out;` or `return in;`, and those statements are left
    // exactly as they were. A bool signature would have meant rewriting 312 of
    // them by hand - while leaving alone the many `return`s that belong to
    // lambdas inside those same blocks, which look identical.
    if(auto r=prepareEngineGroup1(in)) return *r;
    if(auto r=prepareEngineGroup2(in)) return *r;
    if(auto r=prepareEngineGroup3(in)) return *r;
    if(auto r=prepareEngineGroup4(in)) return *r;
    if(auto r=prepareEngineGroup5(in)) return *r;
    if(auto r=prepareEngineGroup6(in)) return *r;

    return in;
}

void QtPlotBackend::drawStem(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    const double baseY=f.plotArea.bottom()-((qBound(f.yLo,0.0,f.yHi)-f.yLo)/qMax(1e-300,f.yHi-f.yLo))*f.plotArea.height();
    for(const PlotSeries& s:spec.series){
        const int n=qMin(s.x.size(),s.y.size());
        p->save();
        QPen pen(s.color); pen.setWidthF(qMax(0.4,s.lineWidth)); applySeriesDash(pen,s); p->setPen(pen);
        for(int i=0;i<n;++i){
            const double x=s.x[i],y=s.y[i];
            if(!finite(x)||!finite(y)||(f.xLog&&x<=0)||(f.yLog&&y<=0)) continue;
            const QPointF pt=toDevice(f,x,y);
            p->drawLine(QPointF(pt.x(),baseY),pt);
        }
        p->setPen(Qt::NoPen); p->setBrush(s.color);
        drawSeriesMarkers(p,f,s,1.0);
        p->restore();
    }
}

void QtPlotBackend::drawHorizontalBar(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    // In a horizontal bar chart the axes really are swapped: x carries the
    // VALUE and y carries the CATEGORY. This used to read the value out of y
    // and take the row from the point's position in the array, so the bar
    // lengths were scaled against an axis fitted to the category column and
    // every row landed in one thin band in the middle of the plot. Both
    // engines that reach it emit one series per bar, which made the fault
    // total rather than partial.
    int total=0;
    for(const PlotSeries& s:spec.series) total=qMax(total,qMin(s.x.size(),s.y.size()));
    if(total<=0) return;

    // How many bars share a row, and which of them this one is. One series per
    // bar is the normal shape here - a population pyramid emits eighty - so the
    // series index is NOT the group index: eighty series across forty rows are
    // forty pairs, not eighty groups.
    auto rowKey=[](double v){ return QString::number(v,'g',12); };
    QHash<QString,int> perRow;
    QVector<double> rows;
    for(const PlotSeries& s:spec.series){
        const int n=qMin(s.x.size(),s.y.size());
        for(int i=0;i<n;++i){
            if(!finite(s.y[i])||(f.yLog&&s.y[i]<=0)) continue;
            if(perRow[rowKey(s.y[i])]++==0) rows.push_back(toDevice(f,f.xLo,s.y[i]).y());
        }
    }
    if(rows.isEmpty()) return;
    int groups=1;
    for(int count:std::as_const(perRow)) groups=qMax(groups,count);

    const double slot=slotWidthFrom(rows,f.plotArea.height()/double(qMax(1,rows.size())),
                                    f.plotArea.height());
    const double barH=qMax(1.0,(slot*0.78)/double(groups));
    const double baseX=toDevice(f,qBound(f.xLo,0.0,f.xHi),f.yLo).x();

    QHash<QString,int> ordinal;
    for(const PlotSeries& s:spec.series){
        const int n=qMin(s.x.size(),s.y.size());
        p->save(); p->setPen(Qt::NoPen); p->setBrush(s.color);
        p->setOpacity(qBound(0.0,s.opacity,1.0));
        for(int i=0;i<n;++i){
            const double value=s.x[i], category=s.y[i];
            if(!finite(value)||!finite(category)) continue;
            if((f.xLog&&value<=0)||(f.yLog&&category<=0)) continue;
            const int g=ordinal[rowKey(category)]++;
            const QPointF pt=toDevice(f,value,category);
            const double top=pt.y()-(groups*barH)/2.0+g*barH;
            p->drawRect(QRectF(qMin(baseX,pt.x()),top,std::abs(pt.x()-baseX),barH));
        }
        p->restore();
    }
}

void QtPlotBackend::drawStackedLines(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    // Cumulative bands: each series is drawn on top of the running total.
    if(spec.series.isEmpty()) return;
    int n=spec.series.first().x.size();
    for(const PlotSeries& s:spec.series) n=qMin(n,qMin(s.x.size(),s.y.size()));
    if(n<2) return;
    QVector<double> lower(n,0.0);
    for(const PlotSeries& s:spec.series){
        // A SERIES THAT REPORTS THE WHOLE IS NOT ONE OF THE PARTS - see
        // PlotSeries::stacked. The VFA profile appends a "total VFA" line
        // carrying the sum of the species it has just handed over, and adding
        // that to the running total drew the band at twice its height with the
        // annotation naming the true peak stranded in the middle of it.
        //
        // Drawn as a line in its own values, with no fill: a band here would
        // add its own height to a figure whose entire content is height.
        if(!s.stacked){
            QPainterPath line; bool started=false;
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])){ started=false; continue; }
                const QPointF pt=toDevice(f,s.x[i],s.y[i]);
                if(!started){ line.moveTo(pt); started=true; } else line.lineTo(pt);
            }
            QPen pen(s.color,qMax(0.3,s.lineWidth)); applySeriesDash(pen,s);
            p->save(); p->setPen(pen); p->setBrush(Qt::NoBrush);
            p->drawPath(line); p->restore();
            continue;
        }
        QPainterPath band;
        for(int i=0;i<n;++i){
            const double y=lower[i]+(finite(s.y[i])?s.y[i]:0.0);
            const QPointF pt=toDevice(f,s.x[i],y);
            if(i==0) band.moveTo(pt); else band.lineTo(pt);
        }
        for(int i=n-1;i>=0;--i) band.lineTo(toDevice(f,s.x[i],lower[i]));
        band.closeSubpath();
        QColor fill=s.color; fill.setAlphaF(0.55);
        QPen pen(s.color,qMax(0.3,s.lineWidth)); applySeriesDash(pen,s);
        p->save(); p->setPen(pen); p->setBrush(fill);
        p->drawPath(band); p->restore();
        for(int i=0;i<n;++i) lower[i]+=finite(s.y[i])?s.y[i]:0.0;
    }
}

void QtPlotBackend::drawErrorBar(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    // First series is the value; a second, if present, is the uncertainty.
    if(spec.series.isEmpty()) return;
    const PlotSeries& v=spec.series.first();
    const bool haveErr=spec.series.size()>1;
    const PlotSeries& e=haveErr?spec.series[1]:v;
    const int n=qMin(v.x.size(),v.y.size());
    p->save();
    QPen pen(v.color); pen.setWidthF(qMax(0.4,v.lineWidth)); applySeriesDash(pen,v); p->setPen(pen);
    QPainterPath line; bool started=false;
    for(int i=0;i<n;++i){
        if(!finite(v.x[i])||!finite(v.y[i])) { started=false; continue; }
        const QPointF pt=toDevice(f,v.x[i],v.y[i]);
        if(!started){ line.moveTo(pt); started=true; } else line.lineTo(pt);
        if(haveErr&&i<e.y.size()&&finite(e.y[i])){
            const double err=std::abs(e.y[i]);
            const QPointF hi=toDevice(f,v.x[i],v.y[i]+err), lo=toDevice(f,v.x[i],v.y[i]-err);
            p->drawLine(hi,lo);
            p->drawLine(QPointF(hi.x()-3,hi.y()),QPointF(hi.x()+3,hi.y()));
            p->drawLine(QPointF(lo.x()-3,lo.y()),QPointF(lo.x()+3,lo.y()));
        }
    }
    p->setBrush(Qt::NoBrush); p->drawPath(line);
    p->restore();
}

// The same bar lying down: one row per series, spanning from a start to an end
// along x. A schedule, an availability timeline and a stratigraphic log are all
// this shape, and none of them can be drawn by drawFloatingBar - a Gantt with
// time on the vertical axis is not a Gantt, it is a puzzle.
//
// One series per bar, as above, but transposed: y = {row}, x = {start, end}.
void QtPlotBackend::drawFloatingRow(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    const double halfHeight=halfSlotAcross(f,spec,true,1.0);
    if(!(halfHeight>0)) return;

    for(const PlotSeries& s:spec.series){
        if(s.y.isEmpty()||s.x.size()<2) continue;
        // TWO ROWS AND ONE ABSCISSA IS A RULE, NOT A BAR.
        //
        // Every other series here is a bar: one row, a start and an end. The
        // tornado diagram's base case is the other shape - one abscissa, and
        // the two rows it should span - and handed to the bar path it became a
        // bar on row zero from the base case to the base case, drawn at the
        // minimum width a milestone is allowed. So a tornado whose whole
        // reading depends on which side of the base case a bar falls showed a
        // one-and-a-half pixel tick under the shortest bar, and the funnel had
        // no centre line at all.
        //
        // Gantt, Availability and Swimmer all build bars as y={row}, so none
        // of them can arrive here; the shape is unambiguous.
        if(s.y.size()==2&&s.x.size()==2&&s.x[0]==s.x[1]){
            if(!finite(s.x[0])||!finite(s.y[0])||!finite(s.y[1])) continue;
            p->save();
            QPen pen(s.color); pen.setWidthF(qMax(0.6,s.lineWidth)); applySeriesDash(pen,s);
            p->setPen(pen);
            p->drawLine(toDevice(f,s.x[0],s.y[0]),toDevice(f,s.x[1],s.y[1]));
            p->restore();
            continue;
        }
        const double row=s.y.first();
        const double from=s.x[s.x.size()-2],to=s.x[s.x.size()-1];
        if(!finite(row)||!finite(from)||!finite(to)) continue;
        const double cy=toDevice(f,f.xLo,row).y();
        const double xFrom=toDevice(f,from,row).x();
        const double xTo=toDevice(f,to,row).x();
        QColor fill=s.color; fill.setAlphaF(qBound(0.05,s.opacity,1.0)*0.85);
        p->save();
        QPen pen(s.color); pen.setWidthF(qMax(0.4,spec.style.lineWidth*0.8)); p->setPen(pen);
        p->setBrush(fill);
        // A zero-length task is a milestone, and a milestone that is one pixel
        // wide is still a milestone. It must not vanish.
        const double w=qMax(1.5,std::abs(xTo-xFrom));
        p->drawRect(QRectF(qMin(xFrom,xTo),cy-halfHeight,w,halfHeight*2));
        p->restore();
    }
}

// Open, high, low and close, drawn the way a price series is read: the body is
// the open-to-close range and the wick is the whole range, so a long wick over
// a short body says the period moved and came back. Colour carries direction,
// which is the one thing a reader takes from the chart at a glance.
//
// One series per period: x = {slot}, y = {open, high, low, close}.
void QtPlotBackend::drawCandlestick(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    // A candle is a little narrower than a bar, and may be thinner than a
    // pixel and a half when a year of daily prices is on screen.
    const double halfWidth=halfSlotAcross(f,spec,false,0.75,0.34);
    if(!(halfWidth>0)) return;

    for(const PlotSeries& s:spec.series){
        if(s.x.isEmpty()||s.y.size()<4) continue;
        const double slotX=s.x.first();
        const double open=s.y[0],high=s.y[1],low=s.y[2],close=s.y[3];
        if(!finite(slotX)||!finite(open)||!finite(high)||!finite(low)||!finite(close)) continue;
        const double cx=toDevice(f,slotX,f.yLo).x();
        const double yOpen=toDevice(f,slotX,open).y();
        const double yClose=toDevice(f,slotX,close).y();
        const double yHigh=toDevice(f,slotX,high).y();
        const double yLow=toDevice(f,slotX,low).y();
        const bool up=(close>=open);
        const QColor colour=up?spec.style.positive:spec.style.danger;
        p->save();
        QPen pen(colour); pen.setWidthF(qMax(0.6,spec.style.lineWidth)); p->setPen(pen);
        p->drawLine(QPointF(cx,yHigh),QPointF(cx,yLow));
        // A hollow body for a rise and a filled one for a fall, so the chart
        // still reads in monochrome and in the colour-vision modes.
        p->setBrush(up?QBrush(Qt::NoBrush):QBrush(colour));
        const double h=qMax(1.0,std::abs(yClose-yOpen));
        p->drawRect(QRectF(cx-halfWidth,qMin(yOpen,yClose),halfWidth*2,h));
        p->restore();
    }
}

// Half the width of one bar, from the closest neighbouring pair of slots, so
// bars never overlap however unevenly the slots are spaced.
//
// The three floating painters - the vertical bar, the horizontal row and the
// candle - each measured this for themselves, with the same loop, the same
// fallback and the same clamp written out three times. The only differences
// were the axis it is measured along and how much of the slot a mark fills.
double QtPlotBackend::halfSlotAcross(const Frame& f,const PlotSpec& spec,bool alongY,
                                     double minimum,double fraction) const {
    QVector<double> centres;
    for(const PlotSeries& s:spec.series){
        // Not "slots": Qt defines that as a keyword macro, and the error it
        // produces points at the assignment rather than at the name.
        const QVector<double>& seats=alongY?s.y:s.x;
        const QVector<double>& values=alongY?s.x:s.y;
        if(seats.isEmpty()||values.size()<2) continue;
        if(!finite(seats.first())) continue;
        centres.push_back(alongY?toDevice(f,f.xLo,seats.first()).y()
                                :toDevice(f,seats.first(),f.yLo).x());
    }
    if(centres.isEmpty()) return 0.0;
    const double across=alongY?f.plotArea.height():f.plotArea.width();
    const double slot=slotWidthFrom(centres,across/double(qMax(1,centres.size())),across);
    return qBound(minimum,slot*fraction,across*0.45);
}

void QtPlotBackend::drawFloatingBar(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    const double halfWidth=halfSlotAcross(f,spec,false,1.0);
    if(!(halfWidth>0)) return;

    for(const PlotSeries& s:spec.series){
        if(s.x.isEmpty()||s.y.size()<2) continue;
        const double slotX=s.x.first();
        // y = {low, high}; a three-element form carrying a centre first, the
        // shape the waterfall rewrite already produces, is read from the back.
        const double lo=s.y[s.y.size()-2], hi=s.y[s.y.size()-1];
        if(!finite(slotX)||!finite(lo)||!finite(hi)) continue;
        const double cx=toDevice(f,slotX,f.yLo).x();
        const double yLo=toDevice(f,slotX,lo).y();
        const double yHi=toDevice(f,slotX,hi).y();
        QColor fill=s.color; fill.setAlphaF(qBound(0.05,s.opacity,1.0)*0.85);
        p->save();
        QPen pen(s.color); pen.setWidthF(qMax(0.4,spec.style.lineWidth*0.8)); p->setPen(pen);
        p->setBrush(fill);
        // A zero-height step still has to be visible, or a no-change entry in a
        // waterfall silently disappears.
        const double h=qMax(1.0,std::abs(yHi-yLo));
        p->drawRect(QRectF(cx-halfWidth,qMin(yLo,yHi),halfWidth*2,h));
        p->restore();
    }
}

void QtPlotBackend::drawBoxPlot(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    for(const PlotSeries& s:spec.series){
        if(s.x.isEmpty()||s.y.size()<5) continue;
        const double slot=s.x.first();
        const double halfWidth=qMin(28.0,f.plotArea.width()/qMax(2,spec.series.size())*0.28);
        const double cx=toDevice(f,slot,f.yLo).x();
        const double wlo=toDevice(f,slot,s.y[0]).y();
        const double q1 =toDevice(f,slot,s.y[1]).y();
        const double med=toDevice(f,slot,s.y[2]).y();
        const double q3 =toDevice(f,slot,s.y[3]).y();
        const double whi=toDevice(f,slot,s.y[4]).y();
        p->save();
        QPen pen(s.color); pen.setWidthF(qMax(0.6,spec.style.lineWidth)); applySeriesDash(pen,s); p->setPen(pen);
        p->drawLine(QPointF(cx,wlo),QPointF(cx,q1));
        p->drawLine(QPointF(cx,q3),QPointF(cx,whi));
        p->drawLine(QPointF(cx-halfWidth*0.5,wlo),QPointF(cx+halfWidth*0.5,wlo));
        p->drawLine(QPointF(cx-halfWidth*0.5,whi),QPointF(cx+halfWidth*0.5,whi));
        QColor fill=s.color; fill.setAlphaF(0.35);
        p->setBrush(fill);
        p->drawRect(QRectF(cx-halfWidth,qMin(q1,q3),halfWidth*2,std::abs(q3-q1)));
        pen.setWidthF(qMax(1.0,spec.style.lineWidth*1.6)); p->setPen(pen);
        p->drawLine(QPointF(cx-halfWidth,med),QPointF(cx+halfWidth,med));
        p->restore();
    }
}

void QtPlotBackend::drawPie(QPainter* p,const QRectF& target,const PlotSpec& spec,bool donut) const {
    if(spec.series.isEmpty()) return;
    const PlotSeries& s=spec.series.first();
    double total=0;
    for(double v:s.y) if(finite(v)&&v>0) total+=v;
    if(!(total>0)) return;

    const double side=qMin(target.width(),target.height())*0.62;
    const QRectF circle(target.center().x()-side/2,target.center().y()-side/2,side,side);
    // The sector colours.
    //
    // These were eight hard-coded QColors and nothing else, so a pie chart was
    // the one engine in the program that ignored the colour-vision setting
    // completely: a person who had selected the deuteranopia palette got a
    // deuteranopia-safe figure for every engine except this one, with nothing
    // to say so. The chosen colours win where there are any, and the series
    // palette - which is measured through a dichromat projection, see
    // ColourVision.h - is what they fall back to.
    static const QColor kWedge[]={QColor(0x4f,0x9d,0xf7),QColor(0xf2,0x8e,0x2b),QColor(0x59,0xa1,0x4f),
                                  QColor(0xe1,0x5f,0x99),QColor(0x76,0xb7,0xb2),QColor(0xed,0xc9,0x48),
                                  QColor(0xb0,0x7a,0xa1),QColor(0x9c,0x75,0x5f)};
    QVector<QColor> wedge=spec.style.customColours;
    if(wedge.isEmpty()) wedge=spec.style.categoryPalette;
    if(wedge.isEmpty()){
        // ONE COLOUR PER SERIES WAS THE WRONG SOURCE, and it made every pie in
        // the program a single flat colour.
        //
        // The line here used to collect `ps.color` from each series, on the
        // reasoning that the canvas puts a palette entry on each one. That is
        // true, and it is not what a pie is: a pie's slices are the POINTS OF
        // ONE SERIES, not separate series. So the loop collected exactly one
        // colour and every slice was drawn in it - wedge.at(idx % 1).
        //
        // It looked fine in the sweep only by accident: the shared fixture
        // hands over five columns, so five colours came out and the slices
        // alternated. An ordinary pie, built from one category column and one
        // value column, has one series and came out monochrome.
        //
        // categoryPalette above is the right source and already existed for
        // exactly this: buildPlotSeries sets it for "the engines that lay out a
        // whole - treemap, icicle, sunburst, Sankey, chord - which never get a
        // PlotSeries to take a colour from". A pie is one of those and was left
        // off the list.
        for(const PlotSeries& ps:spec.series)
            if(ps.color.isValid()) wedge.append(ps.color);
    }
    // DISTINCT colours, not merely several.
    //
    // Counting them is not enough: a pie built from a category column and a
    // value column has two series and both carry PlotSeries' default blue, so
    // "at least two colours" was satisfied by two copies of one colour and the
    // pie came out flat anyway. The per-series path is only meaningful when the
    // series genuinely differ, so that is what is asked.
    {
        QSet<QRgb> distinct;
        for(const QColor& c:wedge) distinct.insert(c.rgb());
        if(distinct.size()<2){
            wedge.clear();
            for(const QColor& c:kWedge) wedge.append(c);
        }
    }
    // SAYING WHICH SLICE IS WHICH.
    //
    // A pie was drawn as coloured wedges with no names and no key, which makes
    // it a picture of some proportions rather than a figure anybody can read -
    // and unlike a line chart there is no axis to fall back on.
    //
    // The name of a slice is its x value: the person maps a category column to
    // x and an amount to y, and buildPlotSeries keeps them together in one
    // series. Falling back to the position is deliberate - a slice called "3"
    // is still better than a slice called nothing.
    const int mode=qBound(0,spec.style.pieLabels,3);
    const bool wantSliceLabels=(mode==1||mode==2);
    const bool wantLegend=(mode==0||mode==2);
    const auto sliceName=[&s](int i){
        return (i<s.x.size()&&finite(s.x[i]))
                   ? QString::number(s.x[i],'g',6)
                   : QString::number(i+1);
    };

    p->save();
    const QFont labelFont=font(spec,spec.style.legendSize);
    const QFontMetricsF lfm(labelFont,p->device());
    double start=90.0*16.0;   // start at 12 o'clock, Qt uses 1/16th degrees
    int idx=0;
    // Kept so the legend lists the slices in the order they were drawn and in
    // their own colours.
    QVector<QPair<QString,QColor>> drawn;
    for(int i=0;i<s.y.size();++i){
        const double v=s.y[i];
        if(!finite(v)||v<=0) continue;
        const double fraction=v/total;
        const double span=-fraction*360.0*16.0;   // clockwise
        const QColor fill=wedge.at(idx%int(wedge.size()));
        p->setPen(QPen(spec.style.background,1.0));
        p->setBrush(fill);
        p->drawPie(circle,int(start),int(span));
        drawn.append({sliceName(i),fill});

        if(wantSliceLabels){
            // At the middle of the slice, two thirds of the way out - inside
            // the wedge, where the label belongs to a slice unambiguously
            // rather than floating between two of them.
            const double midDeg=(start+span/2.0)/16.0;
            const double rad=midDeg*M_PI/180.0;
            const QPointF at=circle.center()
                             +QPointF(std::cos(rad),-std::sin(rad))*(side*0.33);
            const QString text=QStringLiteral("%1  %2%")
                                   .arg(sliceName(i))
                                   .arg(fraction*100.0,0,'f',fraction<0.1?1:0);
            const QRectF box=lfm.boundingRect(text).translated(at)
                                 .adjusted(-lfm.horizontalAdvance(text)/2.0,
                                           -lfm.height()/2.0,
                                           -lfm.horizontalAdvance(text)/2.0,
                                           -lfm.height()/2.0);
            // Only where the slice can actually hold it.
            //
            // Measured against the wedge's CHORD at the radius the label sits
            // at, not against a fraction of the figure: the text is horizontal
            // and centred in the wedge, so what has to fit is the straight
            // distance across the wedge there. A first version compared the arc
            // length to the text height, which a 3% slice passes comfortably -
            // and its label still lay across both neighbours, because a 15 px
            // wedge cannot hold 34 px of text however long its arc is.
            const double radius=side*0.33;
            const double arc=fraction*2.0*M_PI*radius;
            const double chord=2.0*radius*std::sin(fraction*M_PI);
            if(arc>=lfm.height()*1.1&&box.width()<=chord){
                // Ink chosen against the wedge, not against the figure: a dark
                // label on a dark slice is the same as no label.
                const double lum=0.2126*fill.redF()+0.7152*fill.greenF()
                                +0.0722*fill.blueF();
                p->setPen(lum<0.5?QColor(Qt::white):QColor(0x14,0x18,0x1d));
                p->setFont(labelFont);
                p->drawText(box,Qt::AlignCenter,text);
            }
        }
        start+=span; ++idx;
    }
    if(donut){
        const double inner=side*0.55;
        p->setPen(Qt::NoPen);
        p->setBrush(spec.style.background);
        p->drawEllipse(target.center(),inner/2,inner/2);
    }
    if(wantLegend&&!drawn.isEmpty()){
        // Down the right-hand side, where the circle is not. Capped so a pie of
        // forty slices does not produce a legend taller than the figure; the
        // slices it cannot list are counted rather than silently dropped.
        p->setFont(labelFont);
        const double swatch=lfm.height()*0.75;
        const double lineH=lfm.height()*1.25;
        const int room=qMax(1,int((target.height()*0.86)/lineH)-1);
        const int listed=qMin(drawn.size(),room);
        double widest=0;
        for(int i=0;i<listed;++i)
            widest=qMax(widest,lfm.horizontalAdvance(drawn.at(i).first));
        const double boxW=swatch+6.0+widest+12.0;
        double y=target.center().y()-(listed*lineH)/2.0;
        const double x=qMin(circle.right()+14.0,target.right()-boxW-4.0);
        for(int i=0;i<listed;++i){
            p->setPen(Qt::NoPen);
            p->setBrush(drawn.at(i).second);
            p->drawRect(QRectF(x,y+(lineH-swatch)/2.0,swatch,swatch));
            p->setPen(spec.style.foreground);
            p->drawText(QRectF(x+swatch+6.0,y,widest+4.0,lineH),
                        Qt::AlignLeft|Qt::AlignVCenter,drawn.at(i).first);
            y+=lineH;
        }
        if(drawn.size()>listed){
            p->setPen(spec.style.foreground);
            p->drawText(QRectF(x,y,boxW,lineH),Qt::AlignLeft|Qt::AlignVCenter,
                        QStringLiteral("+%1 more").arg(drawn.size()-listed));
        }
    }
    drawFloatingTitle(p,target,spec);
    p->restore();
}

// Which engines build a cached grid out of their input. Draft mode must not
// thin their series: the grid is keyed on a fingerprint of the data, so a spec
// that changes every frame misses the cache every frame and the thinning costs
// far more than it saves. They are bounded by the grid resolution anyway, which
// is what a grid is for.
static bool engineUsesGrid(const QString& engine){
    if(engine==QLatin1String("3D Topography / Surface")
       ||engine==QLatin1String("3D Mesh")) return true;
    if(engine.startsWith(QLatin1String("3D "))) return false;
    return QtPlotBackend::usesColourMap(engine);
}

// THINNING, for the live preview only.
//
// Only in draft, only for the engines that draw their series point by point,
// and only when there are more points than the frame can show anything with.
// First and last are always kept, so the line still starts and ends where the
// data does.
//
// Returns whether it thinned anything; `out` holds the thinned copy when it
// did. An out-parameter rather than a return value because the spec the rest
// of the render draws from is a REFERENCE into either this copy or the
// prepared one, and a returned temporary would dangle the moment it was bound.
bool QtPlotBackend::thinForDraft(const PlotSpec& prepared,PlotSpec& out) const {
    if(!draft_||engineUsesGrid(prepared.engine)) return false;
    bool any=false;
    for(const PlotSeries& series:prepared.series)
        if(series.y.size()>draftCap_){ any=true; break; }
    if(!any) return false;
    out=prepared;
    for(PlotSeries& series:out.series){
        const int n=series.y.size();
        if(n<=draftCap_) continue;
        const int stride=(n+draftCap_-1)/draftCap_;
        QVector<double> xs,ys;
        xs.reserve(n/stride+2); ys.reserve(n/stride+2);
        for(int i=0;i<n;i+=stride){
            if(i<series.x.size()) xs.append(series.x[i]);
            ys.append(series.y[i]);
        }
        if(!series.x.isEmpty()&&series.x.size()==n&&xs.last()!=series.x.last())
            xs.append(series.x.last());
        if(ys.last()!=series.y.last()) ys.append(series.y.last());
        series.x=xs; series.y=ys;
    }
    return true;
}

// Every engine that has no axes: the pies, the diagrams, the compositions and
// the 3-D family. One line per engine and nothing else - the frame view, which
// applies to all of them, is applied by the caller before this runs.
void QtPlotBackend::drawAxislessEngine(QPainter* painter,const QRectF& target,
                                       const PlotSpec& spec) const {
    // THE FRAME VIEW IS THE CALLER'S, and applying it here as well is what this
    // comment is for. When this function was cut out of render() the transform
    // came with it, so every axis-less engine had it applied twice - and
    // applyFrameView COMPOSES onto the painter's matrix rather than setting it,
    // so a zoom of 2 drew at 4, a pan of 0.15 moved 0.30, and the second
    // setClipRect was evaluated in the already-transformed space and came out
    // larger than the frame it exists to clip to.
    //
    // Nothing caught it. The gallery renders every figure with the default
    // view, where applyFrameView returns before touching the painter, so both
    // calls were no-ops and 440 figures were byte-identical. The self-test
    // asserted only that panning right moved the ink right and that zooming
    // increased it - both still true of a doubled transform.


    if(spec.engine==QLatin1String("3D Quiver")
       ||spec.engine==QLatin1String("Cone Plot")
       ||spec.engine==QLatin1String("Stream Tube")
       ||spec.engine==QLatin1String("Stream Ribbon")
       ||spec.engine==QLatin1String("Tensor Glyph Field")
       ||spec.engine==QLatin1String("Volume Show")
       ||spec.engine==QLatin1String("Volume Slice")
       ||spec.engine==QLatin1String("Isosurface")
       ||spec.engine==QLatin1String("Isonormals")
       ||spec.engine==QLatin1String("Isocaps")
       ||spec.engine==QLatin1String("Contour Slice")){
        draw3DField(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Gauge")){
        drawGauge(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Bullet Chart")){
        drawBullet(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Marimekko Chart")){
        drawMarimekko(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Network Graph")){
        drawNetwork(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Chord Diagram")){
        drawChord(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Piper Diagram")){
        drawPiper(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Stiff Diagram")){
        drawStiff(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Durov Diagram")){
        drawDurov(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Arc Diagram")){
        drawArc(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("QAPF Diagram (Plutonic)")
       ||spec.engine==QLatin1String("QAPF Diagram (Volcanic)")){
        drawQapf(painter,target,spec,
                 spec.engine.endsWith(QLatin1String("(Volcanic)")));
        return;
    }
    if(spec.engine==QLatin1String("Soil Texture Triangle (UK)")
       ||spec.engine==QLatin1String("Soil Texture Triangle (USDA)")){
        drawSoilTexture(painter,target,spec,
                        spec.engine.endsWith(QLatin1String("(UK)")));
        return;
    }
    if(spec.engine==QLatin1String("Alignment Nomogram")){
        drawNomogram(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Circos Plot")){
        drawCircos(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Karyotype Ideogram")){
        drawKaryotype(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Hive Plot")){
        drawHive(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Plot Matrix")){
        drawPlotMatrix(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Alluvial Diagram")){
        drawAlluvial(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Ternary Contour")){
        drawTernaryContour(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Streamgraph")){
        drawStream(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Cladogram")){
        drawCladogram(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Icicle Plot")
       ||spec.engine==QLatin1String("Flame Graph")){
        drawIcicle(painter,target,spec,
                   spec.engine==QLatin1String("Flame Graph"));
        return;
    }
    if(spec.engine==QLatin1String("Skew-T Log-P")
       ||spec.engine==QLatin1String("Emagram")
       ||spec.engine==QLatin1String("Stuve Diagram")
       ||spec.engine==QLatin1String("Tephigram")){
        const int kind=(spec.engine==QLatin1String("Emagram"))?1
                      :(spec.engine==QLatin1String("Stuve Diagram"))?2
                      :(spec.engine==QLatin1String("Tephigram"))?3:0;
        drawSounding(painter,target,spec,kind);
        return;
    }
    if(spec.engine==QLatin1String("Smith Chart")){
        drawSmith(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Mosaic Plot")){
        drawMosaic(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("UpSet Plot")){
        drawUpSet(painter,target,spec);
        return;
    }
    if(spec.engine==QLatin1String("Treemap")
       ||spec.engine==QLatin1String("Sunburst")
       ||spec.engine==QLatin1String("Venn Diagram")
       ||spec.engine==QLatin1String("Word Cloud")
       ||spec.engine==QLatin1String("Bubble Cloud")
       ||spec.engine==QLatin1String("Sankey Diagram"))
        drawComposition(painter,target,spec);
    else if(spec.engine.startsWith(QLatin1String("3D "))
       ||spec.engine==QLatin1String("Surface + Contours")
       ||spec.engine==QLatin1String("Comet 3D")
       ||spec.engine==QLatin1String("Ribbon"))
        draw3D(painter,target,spec);
    // Asked of the shared list, so the interface and the painter can never
    // disagree about what is a polar figure. markersOnly is the only thing
    // that still differs between the two groups.
    else if(isPolarEngine(spec.engine))
        drawPolar(painter,target,spec,
                  spec.engine!=QLatin1String("Polar Line")
                  &&spec.engine!=QLatin1String("Radar Chart")
                  &&spec.engine!=QLatin1String("Compass"));
    else
        drawPie(painter,target,spec,spec.engine==QLatin1String("Donut"));
    return;
}

// Every engine that draws inside an axis frame. One line per engine; the
// frame, the chrome, the clip, the legend and the annotations belong to the
// caller, because every one of these needs all of them.
void QtPlotBackend::drawFramedEngine(QPainter* painter,const Frame& f,
                                     const PlotSpec& spec) const {
    if(spec.engine==QLatin1String("Bar"))                    drawBar(painter,f,spec);
    else if(spec.engine==QLatin1String("Histogram"))         drawBar(painter,f,spec);
    else if(spec.engine==QLatin1String("Horizontal Bar"))    drawHorizontalBar(painter,f,spec);
    else if(spec.engine==QLatin1String("Floating Bar"))      drawFloatingBar(painter,f,spec);
    else if(spec.engine==QLatin1String("Floating Row"))      drawFloatingRow(painter,f,spec);
    else if(spec.engine==QLatin1String("Candlestick"))       drawCandlestick(painter,f,spec);
    else if(spec.engine==QLatin1String("Stem"))              drawStem(painter,f,spec);
    else if(spec.engine==QLatin1String("Stacked Lines"))     drawStackedLines(painter,f,spec);
    else if(spec.engine==QLatin1String("Error Bar"))         drawErrorBar(painter,f,spec);
    else if(spec.engine==QLatin1String("Box Plot"))          drawBoxPlot(painter,f,spec);
    else if(spec.engine==QLatin1String("Dumbbell Plot")) drawDumbbell(painter,f,spec);
    else if(spec.engine==QLatin1String("Scatter + Marginals")) drawScatterMarginals(painter,f,spec);
    else if(spec.engine==QLatin1String("Area"))              drawArea(painter,f,spec);
    else if(spec.engine==QLatin1String("Fill Between"))      drawFillBetween(painter,f,spec);
    else if(spec.engine==QLatin1String("Stairs"))            drawStairs(painter,f,spec);
    else if(spec.engine==QLatin1String("4D / 5D Scatter"))   drawScatter(painter,f,spec);
    else if(spec.engine==QLatin1String("2D Heatmap"))        drawHeatmap(painter,f,spec);
    else if(spec.engine==QLatin1String("2D Histogram"))      drawHeatmap(painter,f,spec);
    else if(spec.engine==QLatin1String("Hexbin Density"))    drawHeatmap(painter,f,spec);
    else if(spec.engine==QLatin1String("Correlation Matrix"))drawHeatmap(painter,f,spec);
    else if(spec.engine==QLatin1String("Covariance Matrix")) drawHeatmap(painter,f,spec);
    else if(spec.engine==QLatin1String("Spy Matrix"))        drawHeatmap(painter,f,spec);
    else if(spec.engine==QLatin1String("Quiver Field")
            ||spec.engine==QLatin1String("Feather")
            ||spec.engine==QLatin1String("Stream Field")
            ||spec.engine==QLatin1String("Stream Particles")
            ||spec.engine==QLatin1String("Phase Portrait")
            ||spec.engine==QLatin1String("Flow Texture (LIC)")
            ||spec.engine==QLatin1String("Divergence Map")
            ||spec.engine==QLatin1String("Vorticity Map"))    drawVectorField(painter,f,spec);
    else if(spec.engine==QLatin1String("Choropleth"))            drawChoropleth(painter,f,spec);
    else if(spec.engine==QLatin1String("2D Contour"))        drawContour(painter,f,spec);
    else if(spec.engine==QLatin1String("Voronoi Diagram"))   drawVoronoi(painter,f,spec);
    else if(spec.engine==QLatin1String("Pourbaix Diagram"))  drawPourbaix(painter,f,spec);
    else if(spec.engine==QLatin1String("Dalitz Plot"))       drawDalitz(painter,f,spec);
    else if(spec.engine==QLatin1String("Sequence Logo"))     drawSequenceLogo(painter,f,spec);
    else if(spec.engine==QLatin1String("Tripartite Response Spectrum"))
                                                             drawTripartite(painter,f,spec);
    else if(spec.engine==QLatin1String("Violin Plot"))       drawViolin(painter,f,spec);
    else if(spec.engine==QLatin1String("Raincloud"))         drawRaincloud(painter,f,spec);
    else if(spec.engine==QLatin1String("Forest Plot"))       drawForest(painter,f,spec);
    else                                                     drawLineChart(painter,f,spec);
}

void QtPlotBackend::render(QPainter* painter,const QRectF& target,const PlotSpec& inSpec){
    painter->save();
    // Antialiasing off while the figure is being moved. See setDraft in the
    // header for the measurement this is here for - it is the difference
    // between 54 ms a frame and 6,667.
    painter->setRenderHint(QPainter::Antialiasing,!draft_);
    // Text stays antialiased either way. Labels are a handful of glyphs, they
    // cost nothing to smooth, and a tick that goes jagged the moment you touch
    // the figure reads as the application breaking rather than as a preview.
    painter->setRenderHint(QPainter::TextAntialiasing,true);
    painter->fillRect(target,inSpec.style.background);

    // Reference, not a copy: the cache owns the prepared spec and it is stable
    // for the whole of this render.
    const PlotSpec& prepared=preparedCached(inSpec);

    QElapsedTimer draftClock;
    // Moves the cap toward the target as this frame ends, however it ends -
    // render() returns from several places and a guard cannot be forgotten at
    // one of them the way a line before each return can.
    struct DraftPace {
        const bool on; const QElapsedTimer& clock; int& cap;
        ~DraftPace(){
            if(!on) return;
            const double ms=double(clock.elapsed());
            if(ms<1.0) return;                       // nothing to learn from
            const double factor=qBound(0.4,double(kDraftTargetMs)/ms,2.0);
            cap=qBound(kDraftCapMin,int(cap*factor),kDraftCapMax);
        }
    } pace{draft_,draftClock,draftCap_};
    if(draft_) draftClock.start();

    // `thinned` is declared here and not inside thinForDraft because `spec`
    // below is a REFERENCE into whichever of the two is used, and it has to
    // outlive the call.
    PlotSpec thinned;
    const bool useThinned=thinForDraft(prepared,thinned);
    const PlotSpec& spec=useThinned?thinned:prepared;

    // Pie and donut have no axes at all, so they skip the frame entirely.
    if(!engineHasAxes(spec.engine)){
        // THE IN-FRAME VIEW, for the engines with no axis range to slide.
        //
        // Applied here, once, rather than inside each of the thirty-odd
        // painters: every one of them draws into `target`, so transforming the
        // painter before the dispatch moves all of them and there is no list of
        // engines to keep in step. A rule that has to be repeated per engine is
        // a rule that is missing from the engine added next week.
        //
        // NO save() OF ITS OWN, deliberately. render() saved on entry and this
        // branch restores exactly once below, so a second save here would need
        // a second restore - and the DraftPace guard above exists because a
        // line before each return in this function is a line that gets
        // forgotten at one of them. Riding on the existing save is the version
        // that cannot be unbalanced.
        //
        // The background was filled before this, so it is not transformed: a
        // zoomed figure sits on a full, still background rather than on a
        // scaled rectangle of it.
        applyFrameView(painter,target,spec);
        drawAxislessEngine(painter,target,spec);
        painter->restore();
        return;
    }

    QVector<AxisTick> xTicks,yTicks;
    const Frame f=computeFrame(painter,target,spec,xTicks,yTicks);
    drawChrome(painter,f,spec,xTicks,yTicks);

    painter->save();
    painter->setClipRect(f.plotArea.adjusted(-0.5,-0.5,0.5,0.5));
    drawFramedEngine(painter,f,spec);
    painter->restore();

    drawLegend(painter,f,spec);
    // Last, over the legend as well: a note is about the figure rather than
    // part of it, and one hidden behind the legend is one nobody reads.
    drawAnnotations(painter,f,spec);
    painter->restore();
}

} // namespace graphvis
