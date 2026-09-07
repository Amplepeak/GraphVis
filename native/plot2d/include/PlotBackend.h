#pragma once
// =========================================================================
// PlotBackend - the renderer adapter layer.
//
// Every one of the 318 catalogue engines is written once against this
// interface. Backends implement it; the Renderer selector in TopBar.qml
// chooses which one draws. See docs/PORT-PLAN.md.
//
// Rule: vector export always goes through the Qt backend, whichever backend
// is selected for interaction, because a rasterising backend cannot emit
// vectors. A backend therefore reports whether it can draw to a vector
// paint device.
// =========================================================================
#include "PlotSpec.h"
#include <QRectF>
#include <QStringList>

class QPainter;

namespace graphvis {

class PlotBackend {
public:
    virtual ~PlotBackend() = default;

    // Name as shown in the Renderer selector.
    virtual QString name() const = 0;

    // Catalogue engine keys this backend can draw. An engine the backend does
    // not list stays visible in the Graph Library but is disabled, exactly as
    // GraphVis 17's compatibility checker disables entries the active dataset
    // cannot support.
    virtual QStringList supportedEngines() const = 0;
    // Overridable so a backend with a long engine list can answer from a set
    // rather than rebuilding and scanning the list on every question.
    virtual bool supports(const QString& engine) const { return supportedEngines().contains(engine); }

    // True when this backend can draw into a vector paint device such as
    // QPdfWriter or QSvgGenerator with real text rather than rasterised glyphs.
    virtual bool supportsVectorOutput() const = 0;

    // Draw the spec into target on the given painter. The painter may be
    // backed by a widget, an image, or a vector device; a backend must not
    // assume pixels.
    virtual void render(QPainter* painter, const QRectF& target, const PlotSpec& spec) = 0;
};

} // namespace graphvis
