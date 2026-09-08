#pragma once
// =========================================================================
// TouchGesture - what a set of fingers is doing, in the few numbers a view
// needs to act on it.
//
// Two views take touch: the 2-D figure, where one finger pans and two pinch,
// and the 3-D viewport, where one finger rotates and two pinch and pan. What
// they do with the gesture is entirely different; how they read it is exactly
// the same - the centroid, how far apart the fingers are, and the fact that
// adding or lifting a finger has to restart the measurement rather than jump
// the view by the distance between them.
//
// That last rule is the one worth having in one place. Written out twice, it is
// two places for "a second finger landed" to be handled one way in the figure
// and another in the viewport.
// =========================================================================
#include <QEventPoint>
#include <QList>
#include <QPointF>
#include <cmath>

namespace graphvis {

class TouchGesture {
public:
    struct Step {
        QPointF centre;        // where the fingers are, as one point
        QPointF movement;      // how far that point moved since the last event
        double scale = 1.0;    // how much the fingers spread, 1.0 for no change
        int fingers = 0;
        bool usable = false;   // false for the event that starts or restarts it
    };

    // Feed it the points from a touch event. `restart` is for TouchBegin.
    Step update(const QList<QEventPoint>& points, bool restart) {
        Step step;
        step.fingers = points.size();
        if (points.isEmpty()) { reset(); return step; }

        QPointF centre;
        for (const QEventPoint& point : points) centre += point.position();
        centre /= double(points.size());
        step.centre = centre;

        double span = 0.0;
        if (points.size() >= 2) {
            const QPointF a = points.at(0).position(), b = points.at(1).position();
            span = std::hypot(b.x() - a.x(), b.y() - a.y());
        }

        if (restart || points.size() != fingers_) {
            fingers_ = points.size();
            centre_ = centre;
            span_ = span;
            return step;                       // deliberately not usable
        }

        step.movement = centre - centre_;
        // Only when both spans are big enough to mean anything: two fingers a
        // pixel apart give a ratio that is mostly noise.
        if (span > 1.0 && span_ > 1.0) step.scale = span / span_;
        step.usable = true;
        centre_ = centre;
        span_ = span;
        return step;
    }

    void reset() { fingers_ = 0; span_ = 0.0; }

private:
    QPointF centre_;
    double span_ = 0.0;
    int fingers_ = 0;
};

} // namespace graphvis
