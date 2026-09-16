// The main workspace loads.
//
// It could not be instantiated by these tests at all until now, for two
// separate reasons, and neither was a defect in the file:
//
//   * it contains a FigureCanvas, which contains a PlotCanvas, which is a C++
//     type the staged module had no answer for - "Type VisualizeWorkspace
//     unavailable";
//   * it embeds the native renderer with `WindowContainer`, which QtQuick added
//     in 6.8, so on an older Qt it fails with "WindowContainer is not a type".
//
// Both are handled in tools/run_ui_tests.py - the first by a stand-in generated
// from PlotCanvas.h, the second by a shim staged only when the running Qt does
// not have the real type. This is the test that says so, and that would go red
// if either arrangement broke.
//
// WHAT IT ASSERTS, AND WHAT IT DELIBERATELY DOES NOT.
//
// The fatal classes: a type that cannot be resolved, a delegate that cannot be
// created, a required property nobody filled. Each of those means something in
// the tree does not exist at runtime, and each has happened here - the notebook
// delegate was the last one and it cost every figure its identity.
//
// NOT "Unable to assign [undefined]" and not TypeError. Five of those remain
// and every one is the stand-in, not the workspace: they come from invokables
// that return a QVariantMap or a QUrl - `publicationProfile()`,
// `colourState()` - and a header says a method returns a map without saying
// what is in it. Failing on those here would mean this test could never pass,
// which is the same as not having it.
//
// It was eight, and the first version of this note said "no amount of
// generating fixes that". That was wrong, and worth recording as wrong: three
// of them were `uiLayoutSpec`, whose keys ARE declared in one place both sides
// can read - `uiLayoutAsMap` in UiLayouts.cpp builds the map key by key, and
// every value is a member of the `UiLayout` struct with a declared type. The
// stand-in generates them from there now, which removed those three and,
// more to the point, made it possible to drive the seven layout dimensions at
// all: before it, setting a layout changed nothing, because every `spec.x` the
// workspace reads was undefined either way.
//
// The five that are left are the same shape of problem with no such builder to
// read. If one turns up, they go the same way.
import QtQuick
import QtTest
import GraphVis

TestCase {
    id: suite
    name: "workspace_loads"
    when: windowShown
    // A real working size. A workspace crammed into a postage stamp runs out of
    // room legitimately, and the layout warnings that produces would be about
    // the test canvas rather than about the file.
    width: 1200
    height: 800

    // Generated from AppController.h, with ProjectWorkspace generated from its
    // own header and handed to `app.project` - thirty-odd TypeErrors in
    // ProjectPanel were nothing but that one pointer property reading
    // `undefined`.
    AppController { id: fakeApp }

    Component {
        id: workspaceFactory
        VisualizeWorkspace { }
    }

    // ONE STAND-IN, SHARED, SO IT HAS TO BE RESET.
    //
    // `fakeApp` is a single object and the shape test writes a different layout
    // into it on every iteration - so without this, whichever test ran next
    // inherited the LAST shape that test happened to leave behind, which was
    // the Zen canvas. A figure-visibility assertion then measured a layout
    // nobody had asked for and came back with an answer that was about the
    // previous test.
    //
    // QtTest calls init() before every test function. The defaults here are the
    // ones the generated stand-in starts with - each type's default, which is
    // value 0 of every enum in UiLayouts.h, i.e. the ordinary docked layout
    // with a single canvas.
    property var pristineSpec: ({})
    function initTestCase() {
        var copy = {}
        for (var k in fakeApp.uiLayoutSpec) copy[k] = fakeApp.uiLayoutSpec[k]
        suite.pristineSpec = copy
    }
    function init() {
        var copy = {}
        for (var k in suite.pristineSpec) copy[k] = suite.pristineSpec[k]
        fakeApp.uiLayoutSpec = copy
        fakeApp.notebookLayout = false
        // THE FIGURE AREA IS ONE ARM OF A StackLayout, and the stand-in's
        // default picks a different one.
        //
        //   currentIndex: rendererMode === "Qt 2-D" ? 2
        //               : (rendererMode === "VTK / PBR" ? 1 : 0)
        //
        // A generated stand-in serves "" for a QString, so `currentIndex` came
        // out 0 - the native WGPU viewport - and every test above was building
        // that arm while the Qt 2-D one, which is where the figures are, stayed
        // hidden behind a StackLayout that sets `visible: false` on the arms it
        // is not showing. Nothing failed, because nothing asked about the
        // figures; the first assertion that did came back "0 figures visible"
        // and looked like a defect in the workspace.
        //
        // Named explicitly rather than left to the default, so which arm these
        // tests exercise is a decision written down rather than a side effect
        // of what QString defaults to.
        fakeApp.rendererMode = "Qt 2-D"
    }

    // A POINTER PROPERTY HAS TO BE AN OBJECT, and this is the only thing that
    // says so.
    //
    // `Q_PROPERTY(ProjectWorkspace* project ...)` came out of the generator as
    // `undefined` at first, and ProjectPanel reads `root.project.hasProject`
    // and friends about thirty times - each one a TypeError rather than a
    // missing value. Fixing it removed thirty-odd warnings and broke no test,
    // which is exactly the state in which a fix gets undone by the next person
    // tidying the generator. So it is asserted here, on the stand-in itself,
    // rather than inferred from how quiet the workspace is.
    function test_a_pointer_property_stands_in_as_an_object() {
        verify(fakeApp.project !== undefined && fakeApp.project !== null,
               "app.project is not an object, so every panel that reads through it raises a TypeError")
        verify(fakeApp.project.hasProject !== undefined,
               "app.project carries none of ProjectWorkspace's own properties, so it is not standing in for it")
    }

    // A MAP-VALUED PROPERTY CARRIES ITS KEYS.
    //
    // `uiLayoutSpec` is a QVariantMap, and a header says a property is a map
    // without saying what is in it - so the stand-in served `{}`, every
    // `root.spec.<key>` in the workspace was undefined, and three of them are
    // assigned straight into a `bool`. It also meant the shape test below could
    // set a layout and change nothing at all.
    //
    // The keys are generated from `uiLayoutAsMap` in UiLayouts.cpp, which is
    // where the real map is built, with the types taken from the `UiLayout`
    // struct in UiLayouts.h. Asserted here because nothing else notices when it
    // stops: dropping the generation broke no test and left the shape test
    // quietly driving an empty map.
    //
    // The three named are the ones that were producing warnings. The count is
    // there so that a builder reduced to a couple of keys does not pass.
    function test_the_layout_spec_stands_in_with_its_keys() {
        var spec = fakeApp.uiLayoutSpec
        verify(spec !== undefined && spec !== null, "uiLayoutSpec is not a map")

        var named = ["rail", "railRightToo", "floatingTools", "canvas",
                     "sidebar", "topBand", "bottomStrip"]
        for (var i = 0; i < named.length; ++i)
            verify(spec[named[i]] !== undefined,
                   "the layout spec has no '" + named[i] + "', which the workspace reads")

        var count = 0
        for (var k in spec) ++count
        verify(count >= 20,
               "the layout spec stands in with only " + count + " keys, so it is not "
               + "being generated from the map the application actually builds")
    }

    function test_the_visualize_workspace_instantiates() {
        failOnWarning(/unavailable/)
        failOnWarning(/is not a type/)
        failOnWarning(/Cannot create delegate/)
        failOnWarning(/Required property/)

        var ws = workspaceFactory.createObject(suite, { app: fakeApp })
        verify(ws !== null, "VisualizeWorkspace did not instantiate")
        // Let the loaders, repeaters and deferred bindings run. A tree that
        // resolves at construction and falls over a moment later has not
        // loaded.
        wait(150)
        verify(ws !== null, "the workspace went away while it was settling")
        ws.destroy()
    }

    // NO LOADER IN THE TREE IS IN ERROR.
    //
    // A Loader whose component will not build sets `status` to `Loader.Error`,
    // logs NOTHING unless the Loader itself has a handler that says so, and
    // leaves a hole where its content should be. From the outside that is
    // indistinguishable from the content legitimately being empty.
    //
    // The notebook's Loader was in exactly that state for the length of one
    // experiment - the component reported Ready with an empty errorString and
    // the Loader reported Error - and nothing anywhere said a word. The only
    // way to find out was to read `status` from a test. This is that read, made
    // permanent and asked of every Loader rather than the one that happened to
    // break.
    //
    // Asked in every window shape, because a Loader is `active` in some shapes
    // and not others: the notebook's is only built when the canvas mode is
    // Notebook, so checking the default shape alone would have missed it.
    function test_no_loader_in_any_shape_is_in_error() {
        var shapes = [
            { name: "default",  canvas: 0, notebook: false },
            { name: "notebook", canvas: 1, notebook: true  },
            { name: "zen",      canvas: 4, notebook: false }
        ]
        for (var s = 0; s < shapes.length; ++s) {
            var shape = {}
            for (var k in fakeApp.uiLayoutSpec) shape[k] = fakeApp.uiLayoutSpec[k]
            shape.canvas = shapes[s].canvas
            fakeApp.uiLayoutSpec = shape
            fakeApp.notebookLayout = shapes[s].notebook

            var ws = workspaceFactory.createObject(suite, { app: fakeApp })
            verify(ws !== null, "the workspace did not build for " + shapes[s].name)
            wait(120)
            var broken = []
            findBrokenLoaders(ws, 0, broken)
            compare(broken.length, 0,
                    "a Loader is in Error in the " + shapes[s].name + " shape, which "
                    + "leaves a hole in the window and says nothing: " + broken.join(", "))
            ws.destroy()
        }
    }

    // How many FigureCanvases are actually showing. A FigureCanvas is the thing
    // with a `figureState` and an `app`; `visible` on a QML item is false when
    // any ancestor is hidden, so this is what a person would see rather than
    // what the item was asked to be.
    function visibleFigures(item, depth) {
        if (item === null || item === undefined || depth > 16) return 0
        var kids = item.children
        if (kids === undefined) return 0
        var n = 0
        for (var i = 0; i < kids.length; ++i) {
            var kid = kids[i]
            if (kid.applyFigureState !== undefined && kid.visible === true) ++n
            n += visibleFigures(kid, depth + 1)
        }
        return n
    }

    // Loader.Error is 3. Compared as a number rather than against the enum,
    // because `Loader` is not in scope as a type here.
    function findBrokenLoaders(item, depth, out) {
        if (item === null || item === undefined || depth > 16) return
        var kids = item.children
        if (kids === undefined) return
        for (var i = 0; i < kids.length; ++i) {
            var kid = kids[i]
            if (kid.sourceComponent !== undefined && kid.status === 3)
                out.push("a Loader at depth " + depth)
            findBrokenLoaders(kid, depth + 1, out)
        }
    }

    // THE SHELL CONTRACT: what Main.qml calls on whatever workspace is loaded.
    //
    // The shell reaches its workspace through a Loader, so every one of these
    // is written as `shellLoader.item.<name>`, and a Loader's `item` is a plain
    // QObject as far as anything static is concerned. That has two
    // consequences and both of them bite:
    //
    //   * the linter reports sixteen "Member ... not found on type QObject"
    //     warnings here that are pure false positives, which teaches the reader
    //     to skip the whole category;
    //   * the calls are guarded - `if (shellLoader.item && shellLoader.item.openExport)`
    //     - so a member that is genuinely missing does not raise anything. The
    //     menu item just does nothing, for ever, in silence.
    //
    // `openExport` was in exactly that state. It was declared four levels deep
    // inside a SplitView instead of on the root, so it was not a member of the
    // component: `typeof workspace.openExport` was `undefined`, File > Export
    // did nothing, and the header button raised "not a function" when pressed.
    // The linter DID say so, in the seventeenth warning of that wording.
    //
    // This is the assertion that no longer lets a name go missing quietly. Each
    // entry is something Main.qml calls, and the test reads as the list of what
    // the shell is entitled to expect.
    function test_the_workspace_offers_what_the_shell_calls_on_it() {
        var ws = workspaceFactory.createObject(suite, { app: fakeApp })
        verify(ws !== null, "VisualizeWorkspace did not instantiate")
        wait(120)

        var callable = ["openExport", "selectFigure", "addFigure"]
        for (var i = 0; i < callable.length; ++i)
            compare(typeof ws[callable[i]], "function",
                    "Main.qml calls " + callable[i] + "() on the loaded workspace, "
                    + "behind a guard that silently does nothing when it is absent")

        var readable = ["canvas", "figureTitleList", "figureIndex"]
        for (var j = 0; j < readable.length; ++j)
            verify(ws[readable[j]] !== undefined,
                   "Main.qml reads " + readable[j] + " off the loaded workspace")

        ws.destroy()
    }


    // EVERY SHAPE THE WINDOW CAN TAKE, not just the one the defaults pick.
    //
    // UiLayouts.h calls these "the window shapes, as DATA", and the note there
    // says why they are a record rather than a chain of `layout === n` tests: a
    // layout that is a branch can only differ by what someone remembered to
    // branch on. The record has seven dimensions - which edge holds the panel,
    // how that panel presents itself, what band runs across the top, what strip
    // runs along the bottom, what occupies the middle, and how the window
    // offers its six workspaces - and the default values exercise exactly one
    // value of each.
    //
    // So the branches for a Zen canvas, a Quadrants canvas, a rail sidebar, a
    // shelves band and a page-bar strip had never been built by anything. Not
    // by the sweep, which never loads QML; not by the other tests here, which
    // could not instantiate this file at all until the stand-ins landed.
    //
    // Each dimension is walked on its own rather than as a cross product. The
    // product is 3x4x5x5x5x3x3 = 6,750 windows, and what this is looking for is
    // a branch that was never built - which one value of one dimension is
    // enough to find. A combination that is only wrong together is a different
    // question and would need a different test.
    //
    // Possible because the spec's keys are now generated from uiLayoutAsMap in
    // UiLayouts.cpp. Before that `spec` was an empty map, every `spec.x` was
    // undefined, and setting a layout changed nothing at all.
    function test_every_window_shape_builds() {
        failOnWarning(/unavailable/)
        failOnWarning(/is not a type/)
        failOnWarning(/Cannot create delegate/)
        failOnWarning(/Required property/)

        // name, key, and how many values the enum in UiLayouts.h has.
        var dimensions = [
            ["sidebar edge",  "sidebar",     3],
            ["sidebar mode",  "sidebarMode", 4],
            ["top band",      "topBand",     5],
            ["bottom strip",  "bottomStrip", 5],
            ["canvas",        "canvas",      5],
            ["nav style",     "navStyle",    3],
            ["nav edge",      "navEdge",     3]
        ]

        for (var d = 0; d < dimensions.length; ++d) {
            var key = dimensions[d][1]
            for (var v = 0; v < dimensions[d][2]; ++v) {
                var shape = {}
                for (var k in fakeApp.uiLayoutSpec) shape[k] = fakeApp.uiLayoutSpec[k]
                shape[key] = v
                // The rail is a separate flag from the sidebar mode, and the
                // rail branches are among the ones never built.
                if (key === "sidebarMode") shape.rail = (v === 2)
                fakeApp.uiLayoutSpec = shape
                // DERIVED THE WAY THE APPLICATION DERIVES IT.
                //
                // `notebookLayout` is not an independent switch:
                // AppController::notebookLayout() returns
                // `uiLayouts()[uiLayout_].canvas == CanvasMode::Notebook`, off
                // the same record this spec comes from. Setting one without the
                // other would drive the workspace into a state it can never be
                // in, and a fault found there would be about the test.
                fakeApp.notebookLayout = (shape.canvas === 1)

                var ws = workspaceFactory.createObject(suite, { app: fakeApp })
                verify(ws !== null,
                       "the workspace did not build with " + dimensions[d][0] + " = " + v)
                wait(30)
                verify(ws !== null,
                       "the workspace fell over while settling with " + dimensions[d][0] + " = " + v)
                // AND THE SHAPE ACTUALLY CHANGED.
                //
                // Without this the test builds the same tree twenty-eight times
                // and reports it as coverage of seven dimensions - which is the
                // failure mode of every test that sets an input and asserts
                // only that nothing crashed. The notebook is the one shape with
                // an unmistakable signature in the tree, so it is the one
                // checked: present when the canvas is Notebook, absent
                // otherwise.
                if (key === "canvas")
                    compare(hasNotebook(ws), v === 1,
                            "canvas = " + v + " did not change what is in the middle "
                            + "of the window, so this dimension is not being driven at all")
                ws.destroy()
            }
        }
    }
    // MORE THAN ONE FIGURE, which nothing had ever built.
    //
    // `extraFigures` has `model: Math.max(0, root.figureCount - 1)`, and a new
    // workspace has exactly one figure - so its delegate was instantiated zero
    // times by every test here, and the whole multi-figure path went unbuilt.
    // The default state of a component is the one state that gets exercised by
    // accident; everything else has to be asked for.
    //
    // WHAT IT ASSERTS is that exactly one figure is visible at a time, which is
    // the property the whole multi-figure arrangement rests on: `extraFigures`
    // gives each canvas `visible: root.figureIndex === index + 1`, and figure
    // zero has the matching rule. Get the index wrong in that binding - which is
    // precisely what the notebook defect did to its own cells - and you see two
    // figures at once, or none, with the sidebar editing something you cannot
    // see.
    //
    // The first version of this test asserted only that selecting a figure did
    // not throw and that `canvas` was non-null. Both were true with the
    // delegate's `index` declaration deleted, so it guarded nothing it claimed
    // to: `canvas` is resolved through `figureItem()` rather than through the
    // delegate's own index. Counting what is visible is the assertion that
    // actually reaches the binding.
    //
    // (The delegate declaring `required property int index` is NOT the notebook
    // defect wearing a different hat: FigureCanvas declares only `app`, so
    // there is nothing to shadow. Settled by reading FigureCanvas. The linter's
    // twelve "missing required property index from FigureCanvas" warnings are
    // its confusion about a required property declared inline in a delegate.)
    function test_more_than_one_figure_builds() {
        failOnWarning(/Required property/)
        failOnWarning(/Cannot create delegate/)
        failOnWarning(/unavailable/)

        // DETACHED, not parented to the TestCase.
        //
        // An item's `visible` is false when any ancestor is hidden, and the
        // TestCase item itself reports `visible: false` under the offscreen
        // platform - so a workspace parented to it has every descendant
        // invisible whatever the bindings say, and "how many figures are
        // showing" is always zero. With no parent there is no hidden ancestor
        // and the bindings are what decides.
        var ws = workspaceFactory.createObject(null, { app: fakeApp })
        verify(ws !== null, "VisualizeWorkspace did not instantiate")
        ws.width = 1200
        ws.height = 800
        wait(120)
        compare(ws.figureCount, 1, "a new workspace starts with one figure")

        ws.addFigure()
        ws.addFigure()
        wait(120)
        compare(ws.figureCount, 3, "adding figures did not extend the model")

        // Each of them is selectable, and selecting one does not throw. This is
        // the path the extra canvases' `visible: root.figureIndex === index + 1`
        // binding is on - the binding that would have been wrong had the
        // delegate's index been shadowed the way the notebook's was.
        for (var i = 0; i < 3; ++i) {
            ws.selectFigure(i)
            compare(ws.figureIndex, i, "selecting figure " + i + " did not take")
            wait(20)
            verify(ws.canvas !== undefined && ws.canvas !== null,
                   "there is no current canvas with figure " + i + " selected, so "
                   + "every control in the sidebar is editing nothing")
            compare(visibleFigures(ws, 0), 1,
                    "with figure " + i + " selected, " + visibleFigures(ws, 0)
                    + " figures are visible instead of one")
        }

        ws.destroy()
    }

    // The notebook is reached through this workspace, and it is the component
    // the index defect was in. Asserted here as well as in tst_notebook so that
    // the path a user actually takes to it is covered, not only the component
    // on its own.
    function test_the_notebook_inside_the_workspace_instantiates() {
        failOnWarning(/Cannot create delegate/)
        failOnWarning(/Required property/)

        var ws = workspaceFactory.createObject(suite, { app: fakeApp })
        verify(ws !== null, "VisualizeWorkspace did not instantiate")
        wait(150)
        ws.destroy()
    }
    // Whether a NotebookCanvas is anywhere in this tree. Depth-first, by the
    // property that identifies one, because how deeply it is nested is layout
    // detail this test has no business pinning.
    function hasNotebook(item) {
        if (item === null || item === undefined) return false
        var kids = item.children
        if (kids === undefined) return false
        for (var i = 0; i < kids.length; ++i) {
            var kid = kids[i]
            if (kid.figureCount !== undefined && kid.addFigure !== undefined
                && kid.removeFigure !== undefined)
                return true
            if (hasNotebook(kid)) return true
        }
        return false
    }

}
