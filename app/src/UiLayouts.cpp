#include "UiLayouts.h"

namespace graphvis {

const QVector<UiLayoutGroup>& uiLayoutGroups(){
    static const QVector<UiLayoutGroup> kGroups{
        {QStringLiteral("Rail"),
         QStringLiteral("A strip of symbols down one edge chooses which single panel "
                        "is showing. The figure keeps the rest of the window, and a "
                        "dozen panels cost one narrow strip.")},
        {QStringLiteral("Tree and inspector"),
         QStringLiteral("A structure tree on one edge and a properties panel on "
                        "another, with the figure between them. The shape most "
                        "scientific software already uses.")},
        {QStringLiteral("Modes and pages"),
         QStringLiteral("The whole window changes with the task. The figure stays; "
                        "everything around it is replaced.")},
        {QStringLiteral("Fixed frames"),
         QStringLiteral("Permanent slots, no docking and no floating. Nothing is ever "
                        "hidden, so nothing is ever forgotten.")},
        {QStringLiteral("Focus"),
         QStringLiteral("The figure fills the window and the rest is summoned. For "
                        "reading, presenting and the last look before export.")},
        {QStringLiteral("No top bar"),
         QStringLiteral("The bar of words across the top is gone. Home, Literature, "
                        "Visualize, Data, Analysis and Publish move to a strip of "
                        "symbols down one edge, or to Ctrl+K, and the window looks "
                        "like a different application.")},
    };
    return kGroups;
}

const QVector<UiLayout>& uiLayouts(){
    static const QVector<UiLayout> kLayouts=[]{
        QVector<UiLayout> out;
        UiLayout l;

        // ================================================================= 0
        // Rail shells. One panel at a time, chosen from a strip of symbols.
        // The rail is what lets an application carry twelve panels without
        // showing twelve panels, which is the problem this one has: eight tabs
        // that are all equally invisible until clicked.
        l={}; l.id=QStringLiteral("notebook"); l.group=3;
        // (Notebook is defined below with the fixed frames; the default index
        // has to stay 0, so it is listed first and grouped fourth.)
        l.name=QStringLiteral("Notebook");
        l.icon=QStringLiteral("▤");
        l.from=QStringLiteral("Jupyter, Observable");
        l.description=QStringLiteral("Several figures stacked as a document, so two graphs "
                                     "can be compared side by side rather than one at a time.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Tabs;
        l.canvas=CanvasMode::Notebook;
        out.append(l);

        l={}; l.id=QStringLiteral("activity-rail"); l.group=0;
        l.name=QStringLiteral("Activity rail");
        l.icon=QStringLiteral("▮");
        l.from=QStringLiteral("VS Code");
        l.description=QStringLiteral("Symbols down the left edge, one panel open at a time, "
                                     "and the data table along the bottom where a table's "
                                     "shape actually fits.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Rail;
        l.rail=true; l.sidebarWidth=330;
        l.bottomStrip=BottomStrip::DataTable;
        out.append(l);

        l={}; l.id=QStringLiteral("opposed-rails"); l.group=0;
        l.name=QStringLiteral("Opposed rails");
        l.icon=QStringLiteral("◫");
        l.from=QStringLiteral("JupyterLab, VS Code's secondary side bar");
        l.description=QStringLiteral("Browse on the left, edit on the right. The graph "
                                     "catalogue and the axis mapping are both visible at "
                                     "once instead of taking turns.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Rail;
        l.rail=true; l.railRightToo=true; l.sidebarWidth=320;
        l.secondaryPanel=QStringLiteral("map"); l.secondaryWidth=300;
        out.append(l);

        l={}; l.id=QStringLiteral("persistent-rail"); l.group=0;
        l.name=QStringLiteral("Persistent rail");
        l.icon=QStringLiteral("⋮▮");
        l.from=QStringLiteral("Obsidian");
        l.description=QStringLiteral("The rail stays when the panel is shut, so you can "
                                     "give the figure the whole window and still switch "
                                     "panels without bringing one back.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Stack;
        l.rail=true; l.railPersists=true; l.sidebarWidth=360;
        out.append(l);

        l={}; l.id=QStringLiteral("summoning"); l.group=0;
        l.name=QStringLiteral("Summoning inspector");
        l.icon=QStringLiteral("◧");
        l.from=QStringLiteral("Figma");
        l.description=QStringLiteral("The browse side hides itself and the edit side calls "
                                     "itself: click something on the figure and its "
                                     "settings appear on the right.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Rail;
        l.rail=true; l.railPersists=true; l.sidebarWidth=320;
        l.secondaryPanel=QStringLiteral("map"); l.inspectorSummons=true;
        out.append(l);

        // ================================================================= 1
        // Tree and inspector. The shape Origin, Grapher, Veusz and LabPlot all
        // arrived at independently, which is a reason to offer it rather than
        // to dismiss it as old-fashioned.
        l={}; l.id=QStringLiteral("project-and-log"); l.group=1;
        l.name=QStringLiteral("Project and log");
        l.icon=QStringLiteral("⌸");
        l.from=QStringLiteral("Origin");
        l.description=QStringLiteral("Project tree on the left, figure in the middle, "
                                     "solver and import messages along the bottom where "
                                     "output belongs.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Tabs;
        l.bottomStrip=BottomStrip::Log;
        out.append(l);

        l={}; l.id=QStringLiteral("object-properties"); l.group=1;
        l.name=QStringLiteral("Object and properties");
        l.icon=QStringLiteral("⊟");
        l.from=QStringLiteral("Grapher");
        l.description=QStringLiteral("The list of things above the settings for the "
                                     "selected thing, both on one edge. The cheapest "
                                     "layout in width, so the figure keeps the most of it.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Stack;
        l.sidebarWidth=340;
        out.append(l);

        l={}; l.id=QStringLiteral("free-docks"); l.group=1;
        l.name=QStringLiteral("Free docks");
        l.icon=QStringLiteral("❏");
        l.from=QStringLiteral("LabPlot, Qt dock widgets");
        l.description=QStringLiteral("Nothing is docked. Every panel is a window you put "
                                     "where you want it, including on a second monitor.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Tabs;
        l.floatingTools=true;
        out.append(l);

        l={}; l.id=QStringLiteral("tree-and-data"); l.group=1;
        l.name=QStringLiteral("Tree and datasets");
        l.icon=QStringLiteral("⊞");
        l.from=QStringLiteral("Veusz");
        l.description=QStringLiteral("Structure and settings on the left, the columns you "
                                     "have loaded on the right, and a row of add-this "
                                     "buttons across the top.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Stack;
        l.sidebarWidth=340;
        l.secondaryPanel=QStringLiteral("data"); l.secondaryWidth=280;
        l.topBand=TopBand::AddToolbar;
        out.append(l);

        // ================================================================= 2
        // Modes and pages. The document stays; the application around it is
        // replaced. Resolve is the strongest version of this and its Deliver
        // page is exactly a publication panel given a page of its own.
        l={}; l.id=QStringLiteral("pipeline"); l.group=2;
        l.name=QStringLiteral("Pipeline pages");
        l.icon=QStringLiteral("▭▭");
        l.from=QStringLiteral("DaVinci Resolve");
        l.description=QStringLiteral("Data, Map, Plot, Analyse, Read, Publish - as pages "
                                     "along the bottom, left to right in the order the "
                                     "work happens. Each page is a different window.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Inspector;
        l.sidebarWidth=360;
        l.bottomStrip=BottomStrip::PageBar;
        out.append(l);

        l={}; l.id=QStringLiteral("workspace-tabs"); l.group=2;
        l.name=QStringLiteral("Workspace tabs");
        l.icon=QStringLiteral("⊤");
        l.from=QStringLiteral("Blender");
        l.description=QStringLiteral("Named arrangements as tabs across the top, over "
                                     "panes that tile rather than float. Nothing ever "
                                     "covers the figure.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Tabs;
        l.topBand=TopBand::WorkspaceTabs;
        out.append(l);

        l={}; l.id=QStringLiteral("personas"); l.group=2;
        l.name=QStringLiteral("Personas");
        l.icon=QStringLiteral("◑");
        l.from=QStringLiteral("Affinity");
        l.description=QStringLiteral("The figure stays exactly where it is and the whole "
                                     "toolset around it changes between plotting, fitting "
                                     "and annotating.");
        l.sidebar=SidebarEdge::Right; l.sidebarMode=SidebarMode::Rail;
        l.rail=true; l.railPersists=true; l.sidebarWidth=340;
        out.append(l);

        l={}; l.id=QStringLiteral("ribbon"); l.group=2;
        l.name=QStringLiteral("Ribbon");
        l.icon=QStringLiteral("▬");
        l.from=QStringLiteral("Excel, SigmaPlot, Origin");
        l.description=QStringLiteral("A band across the top that changes with the task. "
                                     "Everything is visible and nothing is behind a menu, "
                                     "at the cost of the height a figure wants.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Tabs;
        l.topBand=TopBand::Ribbon;
        out.append(l);

        // ================================================================= 3
        // Fixed frames. Permanent slots. Teachable in one screenshot, which is
        // a real property - no two IntelliJ installs look alike and that is why
        // its screenshots go stale.
        l={}; l.id=QStringLiteral("shelves"); l.group=3;
        l.name=QStringLiteral("Shelves");
        l.icon=QStringLiteral("⌷");
        l.from=QStringLiteral("Tableau");
        l.description=QStringLiteral("Columns on the left, and the X, Y and Z shelves in a "
                                     "strip above the figure. The mapping is always on "
                                     "screen instead of being a panel you go and visit.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Stack;
        l.sidebarWidth=280;
        l.topBand=TopBand::Shelves;
        out.append(l);

        l={}; l.id=QStringLiteral("quadrants"); l.group=3;
        l.name=QStringLiteral("Quadrants");
        l.icon=QStringLiteral("田");
        l.from=QStringLiteral("RStudio");
        l.description=QStringLiteral("Four panes, all permanent: figure, data, settings "
                                     "and output. Any one can be blown up to fill the "
                                     "window and dropped back.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Tabs;
        l.canvas=CanvasMode::Quadrants;
        l.sidebarWidth=360;
        // The four panes come from the slots rather than from a special canvas:
        // settings on the left, figure in the middle, the table on the right and
        // the messages underneath. RStudio's promise is that all four are
        // permanent, which this keeps; it does not promise a 2x2 grid.
        l.secondaryPanel=QStringLiteral("data"); l.secondaryWidth=320;
        l.bottomStrip=BottomStrip::Log;
        out.append(l);

        l={}; l.id=QStringLiteral("sections"); l.group=3;
        l.name=QStringLiteral("Sections");
        l.icon=QStringLiteral("⊐");
        l.from=QStringLiteral("GraphPad Prism");
        l.description=QStringLiteral("Data, Results, Graphs and Layouts as typed sections "
                                     "along the bottom, with a navigator that groups a "
                                     "figure with the table and the fit it came from.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Stack;
        l.sidebarWidth=300;
        l.bottomStrip=BottomStrip::SectionTabs;
        out.append(l);

        l={}; l.id=QStringLiteral("inspector"); l.group=3;
        l.name=QStringLiteral("Inspector");
        l.icon=QStringLiteral("◨");
        l.from=QStringLiteral("Illustrator's Properties panel");
        l.description=QStringLiteral("One scrolling column of settings on the right and no "
                                     "tabs at all. Nothing is ever hidden behind another "
                                     "tab, at the cost of scrolling.");
        l.sidebar=SidebarEdge::Right; l.sidebarMode=SidebarMode::Inspector;
        out.append(l);

        // ================================================================= 4
        // Focus. Every one of these is a mode over a shell rather than a shell,
        // which is the lesson from VS Code putting Zen and Centred in the same
        // menu as the panel positions.
        l={}; l.id=QStringLiteral("zen"); l.group=4;
        l.name=QStringLiteral("Zen");
        l.icon=QStringLiteral("○");
        l.from=QStringLiteral("VS Code Zen mode");
        l.description=QStringLiteral("The figure and nothing else. Controls come back on "
                                     "Ctrl+K or by moving to an edge.");
        l.sidebar=SidebarEdge::None; l.canvas=CanvasMode::Zen;
        out.append(l);

        l={}; l.id=QStringLiteral("centred"); l.group=4;
        l.name=QStringLiteral("Centred");
        l.icon=QStringLiteral("▯");
        l.from=QStringLiteral("VS Code centred layout");
        l.description=QStringLiteral("The figure held to a column in the middle of the "
                                     "window, at the proportions it will have on the page, "
                                     "with margins either side.");
        l.sidebar=SidebarEdge::Right; l.sidebarMode=SidebarMode::Inspector;
        l.canvas=CanvasMode::Centred;
        out.append(l);

        l={}; l.id=QStringLiteral("reader"); l.group=4;
        l.name=QStringLiteral("Reader");
        l.icon=QStringLiteral("▤▏");
        l.from=QStringLiteral("Notion, iA Writer");
        l.description=QStringLiteral("One list on the left and the document beside it, "
                                     "with nothing else competing. For working through a "
                                     "paper rather than a dataset.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Inspector;
        l.sidebarWidth=280;
        l.focusDim=true; l.statsStrip=true;
        out.append(l);

        // The command bar keeps the top bar as icons: with no sidebar and no
        // rail, a hidden bar would leave nothing on screen to switch with but
        // a keyboard shortcut nobody has been told about.
        l={}; l.id=QStringLiteral("command"); l.group=4;
        l.name=QStringLiteral("Command bar");
        l.icon=QStringLiteral("⌕");
        l.from=QStringLiteral("Superhuman, Raycast");
        l.description=QStringLiteral("Almost no permanent chrome. The figure gets the "
                                     "whole window and every control is reached by typing "
                                     "for it.");
        l.sidebar=SidebarEdge::None;
        l.navStyle=NavStyle::Icons;
        out.append(l);

        // ================================================================= 5
        // Shapes that change the chrome rather than the panels.
        //
        // Every layout above rearranges what is around the figure and leaves
        // the same wide bar of words across the top, so two very different
        // shapes still look like the same application from the neck up. These
        // five move that bar, shrink it to symbols, or remove it.

        l={}; l.id=QStringLiteral("symbols"); l.group=5;
        l.name=QStringLiteral("Symbols only");
        l.icon=QStringLiteral("◌");
        l.from=QStringLiteral("Blender, Krita");
        l.description=QStringLiteral("The same arrangement with the words taken out: the "
                                     "top bar becomes a row of symbols about a third the "
                                     "height, and the panel keeps the space it gives up.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Tabs;
        l.navStyle=NavStyle::Icons;
        out.append(l);

        l={}; l.id=QStringLiteral("left-spine"); l.group=5;
        l.name=QStringLiteral("Left spine");
        l.icon=QStringLiteral("▌");
        l.from=QStringLiteral("Slack, Discord");
        l.description=QStringLiteral("No bar across the top at all. The six workspaces "
                                     "become a spine of symbols down the left edge, with "
                                     "the panel beside it and the figure filling the rest.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Rail;
        l.rail=true; l.railPersists=true; l.sidebarWidth=330;
        l.navStyle=NavStyle::Hidden; l.navEdge=NavEdge::Left;
        out.append(l);

        l={}; l.id=QStringLiteral("right-spine"); l.group=5;
        l.name=QStringLiteral("Right spine");
        l.icon=QStringLiteral("▐");
        l.from=QStringLiteral("Photoshop, Affinity");
        l.description=QStringLiteral("The same again on the other edge, for a left-handed "
                                     "pointer or a second screen on the left: workspaces "
                                     "and panels both on the right, figure hard left.");
        l.sidebar=SidebarEdge::Right; l.sidebarMode=SidebarMode::Tabs;
        l.navStyle=NavStyle::Hidden; l.navEdge=NavEdge::Right;
        out.append(l);

        l={}; l.id=QStringLiteral("spine-and-table"); l.group=5;
        l.name=QStringLiteral("Spine and table");
        l.icon=QStringLiteral("▙");
        l.from=QStringLiteral("Excel, Tableau");
        l.description=QStringLiteral("Workspaces down the left edge, the data table along "
                                     "the bottom, and nothing above the figure - for "
                                     "working between the numbers and the picture.");
        l.sidebar=SidebarEdge::Left; l.sidebarMode=SidebarMode::Rail;
        l.rail=true; l.railPersists=true; l.sidebarWidth=320;
        l.bottomStrip=BottomStrip::DataTable;
        l.navStyle=NavStyle::Hidden; l.navEdge=NavEdge::Left;
        l.statsStrip=true;
        out.append(l);

        l={}; l.id=QStringLiteral("bare"); l.group=5;
        l.name=QStringLiteral("Bare");
        l.icon=QStringLiteral("□");
        l.from=QStringLiteral("Preview, Quick Look");
        l.description=QStringLiteral("Nothing but the figure: no top bar, no spine, no "
                                     "panel. Everything is reached with Ctrl+K, which is "
                                     "the one thing this shape depends on you knowing.");
        l.sidebar=SidebarEdge::None;
        l.navStyle=NavStyle::Hidden;
        l.canvas=CanvasMode::Zen;
        l.focusDim=true;
        out.append(l);

        return out;
    }();
    return kLayouts;
}

QVariantMap uiLayoutAsMap(const UiLayout& l){
    QVariantMap m;
    m.insert(QStringLiteral("id"),l.id);
    m.insert(QStringLiteral("group"),l.group);
    m.insert(QStringLiteral("groupName"),uiLayoutGroups().value(l.group).name);
    m.insert(QStringLiteral("name"),l.name);
    m.insert(QStringLiteral("icon"),l.icon);
    m.insert(QStringLiteral("from"),l.from);
    m.insert(QStringLiteral("description"),l.description);
    m.insert(QStringLiteral("sidebar"),int(l.sidebar));
    m.insert(QStringLiteral("sidebarMode"),int(l.sidebarMode));
    m.insert(QStringLiteral("sidebarWidth"),l.sidebarWidth);
    m.insert(QStringLiteral("secondaryPanel"),l.secondaryPanel);
    m.insert(QStringLiteral("secondaryWidth"),l.secondaryWidth);
    m.insert(QStringLiteral("rail"),l.rail);
    m.insert(QStringLiteral("railRightToo"),l.railRightToo);
    m.insert(QStringLiteral("railPersists"),l.railPersists);
    m.insert(QStringLiteral("inspectorSummons"),l.inspectorSummons);
    m.insert(QStringLiteral("navStyle"),int(l.navStyle));
    m.insert(QStringLiteral("navEdge"),int(l.navEdge));
    m.insert(QStringLiteral("topBand"),int(l.topBand));
    m.insert(QStringLiteral("bottomStrip"),int(l.bottomStrip));
    m.insert(QStringLiteral("canvas"),int(l.canvas));
    m.insert(QStringLiteral("floatingTools"),l.floatingTools);
    m.insert(QStringLiteral("focusDim"),l.focusDim);
    m.insert(QStringLiteral("statsStrip"),l.statsStrip);
    return m;
}

} // namespace graphvis
