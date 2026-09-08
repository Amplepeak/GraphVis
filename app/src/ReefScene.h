#pragma once
// =========================================================================
// ReefScene - the reef the otter swims through while the application starts.
//
// A hundred different scenes, and not a hundred pictures: everything here is
// drawn from a seed, so seed 0 to 99 give a hundred reliably different reefs
// and the same seed always gives the same one. A hundred image files would be
// a hundred things to ship, keep in step with the palette, and re-export the
// day the splash changes size.
//
// The otter is not drawn here and is not touched. It is the logo, it stays
// exactly the logo, and this only ever draws behind and around it - the
// composition deliberately leaves the middle of the frame quiet so the artwork
// reads as the otter in a reef rather than as an otter lost in one.
// =========================================================================
#include <QRectF>

class QPainter;

namespace graphvis {

// Draws the reef into `box`. `seed` selects which one; anything is accepted and
// is reduced to the hundred.
void drawReefScene(QPainter& painter, const QRectF& box, unsigned seed);

// Where the otter and the text sit, as a fraction of the box. Nothing dense is
// drawn inside it. Exposed so the splash and the scene cannot disagree about
// where the quiet part of the picture is.
QRectF reefQuietZone(const QRectF& box);

// How many distinct scenes there are.
constexpr unsigned kReefSceneCount = 100;

} // namespace graphvis
