#pragma once
// Renders a known figure through QtPlotBackend to a vector PDF and reports
// what it produced. Used by --selftest-plot so the export path is verified on
// every build instead of by hand.
#include <QString>

namespace graphvis {
// The sweep renders every catalogue engine and asks whether each put data on
// the page. It already has the picture in hand and then throws it away.
//
// `galleryDir`, when given, keeps them: one PNG per engine, numbered in sweep
// order. That is the whole of what the remaining visual pass needs. "Did it
// draw something" is answerable by a machine and is answered on every build;
// "is the legend covering the data, is that axis labelled in the wrong units,
// is this readable at 89 mm" is not, and there has been no way to look at 434
// figures without opening each one by hand.
//
// Empty by default, so the check that runs on every build is unchanged.
bool runEngineSweep(const QString& galleryDir=QString());
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
