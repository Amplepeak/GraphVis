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

// Property checks: things that must hold for EVERY engine, asked mechanically
// rather than one engine at a time.
//
// A sweep proves an engine drew something. These ask whether what it drew is
// consistent with itself - shuffle the rows and a picture with no notion of
// row order must not change; hand it a column that never varies and it must
// not produce non-finite geometry; nothing may be painted hard against the
// edge of the canvas, because that is what a label running off the figure
// looks like. Each has already caught a shipped defect of its class.
//
// `report` prints every engine's answer rather than only the failures, which
// is how the exception lists below were built.
bool runPropertyChecks(bool report=false);
}
