#pragma once

// The window shapes, as DATA.
//
// There were six of them and they did not differ. Every one ran through the
// same workspace with a handful of conditionals - `layout === 1` mirrored the
// split, `layout === 3` made the sidebar seventy pixels narrower, `layout === 5`
// floated it - so the catalogue stayed on the left and Publish stayed on top
// whichever one you picked, and picking one was very nearly a no-op. The person
// using it said so plainly: "the how visualise etc is still on the left and the
// publish and stuff is still on the top".
//
// A layout that is a branch in a QML file can only ever differ by what someone
// remembered to branch on. A layout that is a RECORD - which edge holds what,
// what band runs across the top, where the figure lives, what the bottom strip
// is - differs by construction, and adding one is a row in a table rather than
// another conditional in a 400-line file that already has six.
//
// The shapes are taken from applications that are known to work, named in each
// entry, rather than invented here: an interface people have already learned
// somewhere else is one they do not have to learn again.

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace graphvis {

// Where the main tool panel sits.
enum class SidebarEdge { None, Left, Right };

// How that panel presents itself.
//   Tabs      the eight-tab bar (Graphs, Project, Data, Map, ...)
//   Inspector one scrolling column, no tabs, nothing behind anything
//   Rail      an icon strip chooses which single panel is showing
//   Stack     several panels open at once in one scrolling column
enum class SidebarMode { Tabs, Inspector, Rail, Stack };

// The band across the top of the figure, if any.
enum class TopBand { None, Ribbon, Shelves, WorkspaceTabs, AddToolbar };

// The strip along the bottom of the window, if any.
enum class BottomStrip { None, DataTable, Log, PageBar, SectionTabs };

// What occupies the middle.
enum class CanvasMode { Single, Notebook, Quadrants, Centred, Zen };

struct UiLayout {
    QString id;
    int group = 0;              // index into the group table below
    QString name;
    QString icon;               // one or two characters, drawn in the picker
    QString from;               // the application the shape is taken from
    QString description;        // what a person would pick it for

    SidebarEdge sidebar = SidebarEdge::Left;
    SidebarMode sidebarMode = SidebarMode::Tabs;
    int sidebarWidth = 400;
    // A second panel on the opposite edge. Empty means none; otherwise it names
    // the panel that lives there permanently - "map", "analysis", "publish".
    QString secondaryPanel;
    int secondaryWidth = 300;
    // An icon strip, and which edge it is on. The rail is what makes a
    // twelve-panel application navigable without twelve visible panels.
    bool rail = false;
    bool railRightToo = false;
    // The rail survives the panel being collapsed, so switching panels still
    // works with the figure at full width. Obsidian's trick.
    bool railPersists = false;
    // The opposite edge expands by itself when something on the figure is
    // selected, while the browse side stays shut. Figma's asymmetry.
    bool inspectorSummons = false;

    TopBand topBand = TopBand::None;
    BottomStrip bottomStrip = BottomStrip::None;
    CanvasMode canvas = CanvasMode::Single;

    // Panels float over the figure instead of being docked beside it.
    bool floatingTools = false;
    // Everything but the figure - or the panel being worked in - is dimmed.
    bool focusDim = false;
    // A thin line of numbers about what is on screen, along the bottom.
    bool statsStrip = false;
};

struct UiLayoutGroup {
    QString name;
    QString description;
};

// The groups, in the order they are offered.
const QVector<UiLayoutGroup>& uiLayoutGroups();
// Every layout, in group order.
const QVector<UiLayout>& uiLayouts();
// One layout as a map QML can read, with the enums as plain integers.
QVariantMap uiLayoutAsMap(const UiLayout& layout);

} // namespace graphvis
