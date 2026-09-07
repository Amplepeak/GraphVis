#pragma once
// Renders a known figure through QtPlotBackend to a vector PDF and reports
// what it produced. Used by --selftest-plot so the export path is verified on
// every build instead of by hand.
#include <QString>

namespace graphvis {
bool runEngineSweep();
// Targeted regression checks for defects that a "did it draw anything" sweep
// cannot see: a stale prepared-spec cache, bars positioned by array index
// instead of by their x value, and a waterfall rewritten onto geometry the
// draw step cannot read. Each of these shipped, and each drew a picture.
bool runRegressionChecks();
bool runPlotSelfTest(const QString& outputPdf);
}
