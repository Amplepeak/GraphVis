# Changelog

Notable changes to GraphVis. Versions follow the `VERSION` file.

The entries below record what changed and, where it matters, what was wrong
before — a changelog that only lists additions is no use to someone deciding
whether a result they already have is affected.

## Unreleased

### Fixed — results that were wrong or mislabelled

- **`fit_nonlinear` fitted one model and reported another.** The fitter ended in
  a bare `else` that fitted *exponential saturation* for any model name it did
  not recognise, then labelled the result with the name that had been asked for.
  Its registered default was `gaussian`, a name it had no branch for, so the
  default invocation was always mislabelled: on a clean Gaussian it returned
  R² = 0.078 with parameters named `asymptote`, `amplitude` and `rate`, reported
  as `"model": "gaussian"`. Gaussian and Lorentzian are now real fits, and an
  unrecognised name is refused with the list of models that exist.
  **If you have quoted a "Gaussian" fit from an earlier build, re-run it.**
- **`PlotCanvas::setXTransform` and `applyVariant` had no definition.** Removed
  as collateral by an earlier clean-up that matched a one-line setter and
  swallowed the function after it. Restored.

### Fixed — features that could not be reached or used

- **The expression engine was unreachable.** Seven catalogue engines (Function
  Plot, Surface, Mesh, Contour, 3D Parametric, Implicit Function, Implicit
  Surface) plot a formula, and nothing anywhere could set it — so all seven drew
  the renderer's hard-coded demonstration formula, permanently. There is now a
  Formula field in the mapping panel that compiles as you type and names what is
  wrong.
- **Extractions did not survive the session.** `literature.extractions` wrote
  sidecar files that nothing ever read back, so closing the program discarded
  every extraction it had made. Now listed under "Extracted before".
- **`full_factorial` could not read its own documented input.** The field hint
  asked for `{"temperature": 3, "pH": 2}`, which raised
  `TypeError: 'int' object is not iterable` from inside itertools. Both that
  shape and explicit level lists now work.
- **`bootstrap_ci` crashed on any statistic.** The parameter was typed as a
  Python callable, but requests arrive as JSON, so the only value a caller could
  send was a string — and a string raised `TypeError: 'str' object is not
  callable`. It now names a statistic: mean, median, std, var, min or max.
- **Thirteen further controls wired up**, including `literature.heatmap`, close
  and reopen project, restoring removed datasets, canvas zoom buttons, the
  column-requirement panel and `FullRenderPolicyBox`, which had been built and
  placed in no window.
- **`fit.modified_gompertz` removed.** It imported a function that no longer
  existed and raised `ImportError` on every call; nothing called it, so nothing
  noticed. "Modified gompertz" remains available through `fit_model`.

### Fixed — advice that was not supported by the data

- **The catalogue scan gave one column two jobs.** With two numeric columns and
  no declared response, the response fell back to a column that was already a
  parameter, so the top suggestion for a `(voltage, current)` table was a heatmap
  of `x=voltage, y=current, z=current` — a response surface whose height is one
  of its own axes.
- **Every relationship was described as "strong".** The wording was hard-coded
  regardless of the number printed beside it, so eight independent random columns
  produced "Strong dependency detected … (score 0.20)". Mutual information from a
  finite histogram is biased upward and floors near 0.20 for independent data, so
  the scanner was reporting its own noise as a finding. Weak pairs are now not
  reported at all, and the wording follows the measurement.
- **Grouped comparisons did not exist.** The scan ranked only numeric columns, so
  a table of `(catalyst, yield)` was offered a histogram of yield and nothing
  else. Box, violin, bar and grouped-scatter recommendations are now made, ranked
  by eta-squared — the share of variance the grouping explains — and the reason
  quotes it, so a grouping that explains 41% ranks first and one that explains 2%
  ranks below the plain distribution views and says the groups will overlap.

### Fixed — figures that were unreadable or misleading

- **A wind rose put a bearing of zero due east.** Every polar engine — wind rose,
  compass, stereonet, radar, polar scatter, polar bubble — was drawn by one
  painter measuring angles the way mathematics does: zero at three o'clock,
  increasing anticlockwise. For a polar scatter of phase against amplitude that
  is right; for anything read as a *bearing* it is ninety degrees out and turning
  the wrong way. Neither convention is right for every figure, so it is now a
  per-figure setting the operator chooses, offered on the polar engines and
  nowhere else. **A wind rose or stereonet read off an earlier build has its
  directions rotated.**
- **Polar figures had no title and no angle scale.** The painter labelled four
  rings with radius values and drew twelve unlabelled spokes, so a compass had
  nothing to read a heading against, and it never drew a title on any of its
  three exits. Twelve angular labels now sit outside the rim, in whichever
  convention the figure is set to, and all three exits draw the title.
- **The colour-blind and monochrome settings appeared to do nothing.** Two
  separate faults. A map was judged safe by one test — monotonic lightness —
  which is the reading task for a *sequential* map and not for a diverging or
  cyclic one, so those were failed for normal vision and substituted away from.
  And Plasma was recorded as monochrome-safe on a measured colour distance of
  16.9 dE76, when monochrome is a rendering request rather than a question about
  the map: only achromatic maps satisfy it. Maps are now measured against their
  own category's reading task, the substitution is stated on screen rather than
  made silently, and there is a preview that simulates the deficiency so the
  setting can be seen working.
- **A pie could be drawn entirely in one colour.** `drawPie` took one colour per
  *series*, and a pie is one series, so the slices came out as a single disc
  unless a palette happened to be set. Slices now fall back to the category
  palette; a pie whose colours are not distinct is repainted from it.
- **A pie said which slice was which nowhere.** No axis, no labels, no legend.
  Slices now carry their name and percentage where the slice is wide enough to
  hold the text against its chord, with a legend down the right for the rest —
  labels, legend, both or neither, per figure.
- **4D/5D Scatter and several others drew markers with no line, or a line with no
  markers,** because `drawScatter` ignored the series' own `drawLine` flag.
- **A figure could be clipped by its own note**, and the x-axis had no padding
  where the y-axis had 5%, so the leftmost and rightmost points sat on the frame.
  Both fixed, with zero-pinned axes left pinned.
- **Implicit Surface drew its demonstration formula over the wrong box**, a
  confusion matrix had no class names on either axis, and a mosaic plot's row
  labels were drawn against whichever column happened to be last.

- **Every box plot was clipped above its lower whisker.** The frame paired each
  series' x with its y and stopped at the shorter — right for a curve, wrong for
  the shape a box plot uses, which is ONE position (`x = {slot}`) and a list of
  five summary values in `y`. The minimum was one, so the axis was fitted to the
  lower whisker alone and the median, upper quartile and top whisker of every
  box were drawn above the frame and discarded. A Gantt row is the same shape
  transposed and lost the end of every bar; a forest plot is drawn sideways on
  top of that, so its effect axis was scaled to the *number of studies*.
- **A Horizontal Bar chosen from the library drew its category numbers as the
  bar lengths.** The painter reads the value from x and the category row from y
  — deliberately, because the axes really are swapped in that chart — and three
  rewrites hand it that shape. Nothing gave it to a Horizontal Bar the person
  chose, so it drew the data transposed. It drew *something*, which is why no
  automated check caught it.
- **A stated axis limit was widened by 5%.** The headroom that keeps the topmost
  marker off the frame was applied after the explicit limits, so typing 100 into
  the axis maximum produced an axis reading 105.
- **A single zero p-value took the whole axis.** Both `-log10(p)` engines clamped
  to 1e-300, which draws a zero at y = 300 on an axis whose content lies between
  0 and 8 — the genome-wide threshold a Manhattan plot is read against sits at
  7.3 and was a pixel above the baseline. A zero is now drawn a decade below the
  smallest p-value the data contains.
- **A Michaelis-Menten fit with a negative Km was drawn.** The hyperbola's pole
  fell inside the sampled range and the divide-by-zero guard turned it into a
  spike of 1e8, which then set the axis: every real curve on the figure became a
  flat line along the bottom. A fit whose parameters are impossible is now
  refused, with the reason in the label.
- **Grids were differentiated with a clamped central difference**, so every
  boundary cell of a divergence or vorticity map got half its true slope — a
  bright stripe along two edges that set the colour scale for the whole map. The
  slope, aspect and hillshade maps avoided that by dropping their outer ring and
  then labelling their axes for the whole grid, so the map was drawn inset
  inside its own frame. Both now use a one-sided difference at the edge.

- **A caterpillar plot drew three of its studies and dropped the rest.** The
  rewrite sorted the studies and handed `drawForest` three full-length columns,
  but that painter reads x as a single row and y as `{estimate, low, high}` — so
  it made three bars out of the first three studies, on one row, and the sorting
  the engine exists for was invisible.
- **Isocaps filled the whole box.** An isosurface encloses the region where the
  field *exceeds* the level; the caps filled the cells below it, which for a
  volume with its features in the middle is nearly the entire wall. The surface
  is the same either way — the boundary between the two regions is one set of
  triangles — so nothing else looked wrong.
- **A grid sample sat on the edge of its cell rather than at its centre.**
  Invisible on a 160-cell heatmap; on a four-class confusion matrix it is a
  quarter of a cell, and the matrix engines label their axes at cell *edges*, so
  cells and ticks described different places.
- **Mohr's circle was drawn as an ellipse.** Both axes carry stress in the same
  unit and the radius *is* the maximum shear, so the roundness is the reading.
  Equal aspect now also applies to the impedance loci (Nyquist, Cole-Cole), the
  shaft orbit, the hodograph and the Poincaré plot.
- **A ternary scatter was drawn inside a rectangular frame** ruled 0.0 to 1.0 on
  both sides, measuring nothing that appears in the diagram. It rewrites onto a
  Line Chart, so the frame decision was being taken on the name "Line Chart".
- **A rating curve fitted to a falling column** produced a power law running to
  340 on a figure whose discharges are under 8, and the axis followed it.

- **Six nodes drawn over thirty-seven stages.** An alluvial diagram's stages
  come from a longest-path relaxation capped at *n passes* — but a cap on the
  passes is not a cap on the answer, and around a cycle every pass pushes each
  node one stage further. Nineteen twentieths of the canvas was empty. A graph
  that is entirely cyclic now says so in the middle of the figure rather than in
  small type along the bottom edge.
- **A log axis spanning thirty-six decades labelled every one of them** — a
  column of numbers with no gaps, which reads as a broken renderer. Past a dozen
  decades only every *n*th is labelled; the tick itself stays, because the
  gridlines are the sense of scale on a log axis.
- **The Nyquist stability plot's unit circle and the Youden plot's 95% circle
  were ellipses**, and the Youden plot's 45° systematic-direction line was not
  at 45°. Same equal-aspect fix as Mohr's circle.

### Fixed — catalogue entries that were the same picture under two names

A perceptual pass over the whole 434-figure gallery — every figure reduced to
64 × 40 greyscale with its title cropped, then compared pair by pair on mean
absolute difference — found pairs far closer than the byte comparison the sweep
already ran could see. Anything under about two grey levels out of 255 is the
same picture to a reader. Every finding below came from that pass, and each one
turned out to be one of two faults: an engine that was not doing the thing its
entry is named for, or a *demonstration* that could not reach what the engine
actually does. The second is not a lesser fault. A catalogue figure exists to
show what an engine does, so demonstrating one on data that cannot reach any of
its behaviour is the same failure as the engine not having the behaviour.

- **Lollipop and Stem were 0.03 grey levels apart.** The lollipop asked for a
  5pt marker against the stem plot's 4pt default and said nothing about the
  stalk, so the whole of the difference between the two entries was half a pixel
  of radius. A lollipop now has a stalk you can see and a head about three times
  the stem plot's — it reads as a head on a stalk, which is the distinction.
- **Zero pinned the x axis on every figure whose data happened to start there.**
  The pin exists so a horizontal bar is not lifted off the axis its lengths are
  measured from, and it was written as a test on the number rather than on the
  engine — so a stem plot's sample index, a scatter's first category and a time
  axis measured from the start of a run were all pinned too, and their leftmost
  mark was drawn half outside the frame.
- **A legend key was an 18-pixel stroke whatever the series was.** On a scatter
  that is a line the figure does not contain; on a bar chart it is a hairline
  standing for a solid block of colour. The key now draws what the series draws,
  and the row grows to fit the marker it has to show.
- **Geo Line, Ground Track and Great Circle Route were literally the same
  picture** — 0.000 and 0.002 grey levels apart, the closest pair in the
  gallery. The eight geographic engines had no demonstration data of their own,
  so the sweep was handing them a decaying exponential as a longitude. They now
  run on coordinates: a route across a hemisphere, a low-Earth ground track that
  crosses the antimeridian twice, and a cloud of sightings.
- **A great circle drawn alone is not an argument.** The entry exists because
  the obvious line — straight in longitude and latitude, which is what plotting
  the two columns gives — is longer. That line is now drawn beside it, dashed,
  and both carry their length on the ground. London to Tokyo: 9,569 km against
  11,373.
- **Geo Bubble ignored the column it was given to size its bubbles by**, drawing
  every point at one fixed size. It is now a graduated-symbol map: classes
  across the column's range, sized by area rather than radius, and a legend that
  says what each size is worth.
- **The drag polar, both flight envelopes and both wind figures were fitted
  against a row index.** They read a point out of one series' own x and y, and
  were being handed a numbered column — so the drag polar's parabola fit
  returned a negative zero-lift drag, failed its own sanity test, and fell
  through to drawing the raw columns. The engine was right to refuse; it was
  being asked to fit a signal against its own subscript. On real coefficients it
  now recovers CD0 0.0208 and k 0.0479 from data generated at 0.021 and 0.047.
- **Five polar entries all pointed the same way.** Polar Line, Polar Scatter,
  Polar Bubble, Compass and Stereonet were reading the shared signal column as
  an angle in degrees. It runs from 2 to 5, so every point in every one of those
  figures lay in a three-degree sliver just above due east.
- **A drag coefficient on an axis labelled `time_h`.** The sweep stamped the
  shared time base on every figure, including the ones with data of their own.
  Each engine sets its own axis names only where the caller left them empty —
  which is right, because the label is the name of the column a person mapped —
  so the sweep was naming a column it had not supplied.
- **Comet 3D's tail did not fade**, so it was a 3D Line with a dot on the end
  and the head had nothing to be meaningful against. The whole content of a
  comet is recency.
- **Stairs and Step Mid were smooth curves.** They differ by half a sample, and
  on a 240-point base over twelve units that is a quarter of a pixel. Twelve
  readings is what a step chart is for.
- **Isocaps had nothing to cap.** The caps are where the enclosed region is cut
  by the walls of the box, and no feature in the demonstration volume reached a
  wall — so the engine correctly found none and drew the bare isosurface.
- **3D Swarm had no ties to spread.** A swarm fans rows that share a height
  apart; the demonstration was a smooth curve where almost no two rows share
  one.
- **A streamline said nothing about which way it went**, so the same set of
  curves described the flow and its exact reverse. It mattered most on the
  phase portrait, where a stable spiral and an unstable one draw identical
  trajectories and differ only in which way they are travelled — the
  classification in the labels was asserting something the picture did not
  show. Each line now carries one direction head at its midpoint.
- **Streamlines were integrated to a fixed number of steps, not to a length.**
  160 steps of six tenths of a cell is about fourteen units of arc and the
  demonstration field is three and a half units across, so every line wrapped
  three times round the same closed orbit and the middle of the figure went
  solid. The budget is now measured in the domain, which is also why the
  direction heads — added first, on their own — could not be seen.
- **The Divergence Map was one flat colour.** The demonstration field is the
  Duffing system, whose divergence is the same number everywhere: a correct
  picture of a constant. Both curl maps now have a field with a source and a
  vortex in it, with no change to either engine's arithmetic.

### Fixed — build

- **The empty look-alike table would not compile under `-Wpedantic`.**
  `Adjudicated kAdjudicated[]={}` is a zero-size array, a GCC extension and an
  *error* under `-Wpedantic`. The cloud harness compiles with `-w` and let it
  through; the first strict build would have failed on a line whose whole
  content is "there is nothing left to excuse". It is a `QVector` now — an
  empty table has to be expressible without a dialect extension, or the table
  can never be emptied.

### Fixed — a figure that was different every time you drew it

**One figure in the 434-figure gallery had a different checksum between two
runs of the same binary.** Qt randomises its `QHash` seed per process, so an
engine that iterates a hash to decide what to draw — or in what order —
produces a different picture on every run, and nothing inside a single run can
see it. The hexbin painter iterated its cell counts straight out of a `QHash`;
neighbouring hexagons share an edge and, with antialiasing, the shared pixels
belong to whichever was drawn last. The word cloud's sort was not a total
order, so words with equal counts changed place. The sunflower plot emitted its
cells in hash order too.

A figure that is not reproducible cannot be compared with itself, which is most
of what the sweep does — and someone re-exporting a figure for a paper should
get the file they had before.

**The sweep now renders every engine a second time under a deterministic hash
seed and fails on any that differ**, which is a check no amount of re-running a
single build could make. The whole gallery is now byte-identical across
independent runs.

### Fixed — a beeswarm of 24,000 points took 38 seconds

Placing each point at the nearest free offset needs a collision test, and the
test scanned every point already placed whose value was within `reach` of this
one. `reach` is a fraction of the *span*, so the number of points inside it
grows with the row count and the scan was O(n) per candidate offset per point.

Every offset is a multiple of one step and consecutive offsets are a whole step
apart — more than the 0.9 of a step the test compared against — so "clashes"
means "sits at the same offset", and nothing else. And because the values are
sorted, the only point at that offset that can still be within reach is the
last one placed there. The whole backward scan collapses to one lookup: the
same predicate, evaluated directly instead of searched for.

**38.5 seconds to 159 ms, and the rendered figure is byte-identical.**

### Fixed — two more quadratic searches

- **A titration logged at 24,000 points took 12.8 seconds.** The endpoint is
  found by searching for the split that two straight lines fit best, and the
  search copied both halves out, fitted each, then walked every point again to
  total the residuals — four allocations and three O(n) passes per split, for n
  splits. A least-squares fit and its residual sum both come from six running
  numbers; moving the split one place right moves one point from the right side
  to the left. **12,804 ms to 94 ms, byte-identical.**
- **A master curve made 24,000 isotherms of one point each.** It groups its
  readings by temperature and shifts each isotherm against everything placed so
  far — so a temperature *logged alongside* the sweep, rather than a list of
  the temperatures the sweep was run at, made one group per row and an assembly
  quadratic in their number. An isotherm of one point has no overlap to be
  shifted against, so the figure could not mean anything either.
  **19,856 ms to 2.0 ms.**

### Fixed — a Voronoi diagram of 24,000 rows never came back

This one was not in the scaling table because the probe never got past it: the
engine ran for **over fifteen minutes on a single figure** and was still going
when it was killed.

The clipping is quadratic *deliberately*. The source argues, correctly, that
half-plane clipping is the construction that cannot go wrong, where the dual of
the Delaunay triangulation fails on unbounded cells and on cocircular sites —
and fails by drawing a plausible picture rather than an obvious one. That
argument stands and the construction is unchanged. **But a deliberate quadratic
still needs a limit**, or a defensible construction becomes an engine that
never returns, which is the one failure worse than a wrong picture. Two
thousand cells is already more than a reader can tell apart; past that the
figure refuses, and — unlike the category caps when they were first written —
it says why, in a sentence naming the same number the guard tests.

A second quadratic was hiding in the same function with no argument behind it
at all: the duplicate-site check asked the list of sites collected so far
whether it already held this position. That is a scan per row — seven billion
comparisons on a survey of 120,000 rows, spent before any tessellation began,
and the identical fault the confusion matrix had. It is now a lookup. Zero is
normalised before hashing, because a negative zero compares equal to a positive
one but would not hash to the same place, and a duplicate the old scan caught
would otherwise have been given its own cell.

**Over 15 minutes to 4 ms at 24,000 rows**, and every figure in the gallery
byte-identical.

### Fixed — a comment that claimed a speed nobody had measured

`delaunay` carried the note *"this is O(n^1.5) in practice and the caller caps
n"*. Both halves were false, and both were cheap to check.

Every inserted point tests the circumcircle of **every** triangle standing —
there is no adjacency and no point location — so the work is one full scan per
point, and doubling the sample quadruples the time exactly as that predicts:
4.8 ms at 500 samples, 92 ms at 2,000, 1,555 ms at 8,000, 8.7 seconds at
16,000. And of the two callers, neither capped anything.

Three things followed from that one sentence going unchecked:

- **The ternary contour handed it every row.** 24,000 compositions ran past
  three quarters of a minute and were still going when the probe killed them —
  which is why this engine never appeared in the scaling table at all: it was
  the wall the whole-catalogue probe stopped at. It now asks the limit before
  calling, and says on the figure why nothing is contoured. **Over 45 seconds
  to 5 ms.**
- **`estimateField`'s Auto ladder chose this method for LARGE samples**, on
  its own stated grounds that a large sample "gets the local method that stays
  O(n)". A triangulation is local to *evaluate* and quadratic to *build*, so
  for exactly the samples the rule existed to protect it selected the one
  method whose cost grows fastest — 8.7 seconds at 16,000 samples, against 28
  ms for the modified Shepard weighting that really is local in both senses,
  and that is flat from 400 samples to 16,000 because its cost is the number
  of grid nodes rather than the sample count. The ladder named the right idea
  and picked the wrong method.
- **A triangulating estimator asked for explicitly** on too large a sample now
  falls back the same way a structured method asked for on scattered data
  already did, rather than returning an empty field — which a figure reads as
  "nothing was measured here".

The construction itself is unchanged: a slow triangulation that is right is
worth more than a fast one that is subtly wrong, and the alternative is an
adjacency rewrite whose output order would move every contour label. What
changed is that the limit the comment *claimed* existed now actually exists, in
the header, where a third caller cannot miss it — and the measurements sit
beside it, so the next person to read a claim about its speed is reading a
number rather than an assurance.

### Fixed — the same window search, written out three times

The Tauc plot's steepest rise, the Kubelka-Munk absorption edge and the creep
curve's flattest secondary stage are one search with two sign conventions, and
each had its own copy — each copying the window out with `mid` at every start
position and fitting it fresh. Three copies means a fault has to be found three
times, which is how the first two quadratics in this catalogue were fixed and
the third was not. There is now one sliding search and three callers.

**The Tauc figure moved, for the same reason the growth window did.** The
fixture's absorption edge is exactly linear above 2.40 eV, so 32 of the 44
candidate windows tie at *precisely* the steepest slope, and every one of them
reports a gap of 2.400000 eV. Which of the 32 won was decided by the last bits
of the accumulation. It is now decided deliberately, and the earliest wins — so
the tangent is drawn from the absorption onset, which is where a Tauc gap is
read, rather than from wherever along the straight part the arithmetic happened
to land. **Tauc 1,314 ms, Kubelka-Munk 1,220 ms and the creep curve 1,497 ms at
24,000 points, all now within a factor of 90 of their 240-point cost rather
than 300.**

### Fixed — the canvas-edge check never looked at the bottom

`edgesTouched` reported `left`, `right` and `top`. There was no `bottom` — and
the bottom is where the x tick labels, the axis note and every caption live, so
it is the likeliest of the four edges to be overrun.

It was dropped deliberately. The record says asking about all four named
seventeen engines, asking about three named one, and the smaller number was
written down as the catalogue being *"in better shape than the first pass
suggested"*. **That conclusion was drawn from having stopped looking.** The four
sounding charts then ran their axis note off the bottom of the canvas and
survived three visual-pass rounds and every property run since.

The fix is one row rather than two, and that is what makes it assertable.
Measured over the whole gallery, **eleven figures deliberately sit a footnote
flush against the bottom margin** and put ink in the second-from-last row —
tight, complete, and not a fault; none of them reaches the last row. The four
clipped soundings all did, by 7 to 25 pixels. So the final row separates "cut
off" from "flush" exactly: it fires on the real fault and is silent on the
eleven.

Proven by reverting the sounding fix: the check then names all four charts and
fails the property run. With the fix in place, 0 of 427 engines touch any edge.

### Changed — the background swatch grid is gone

Twenty-four swatches in two rows of permanent panel height, offering six
near-whites, six greys, six darks and six tinted papers — most of them hard to
tell apart at 20 pixels square, and every one of them already reachable from the
colour picker or the preset drop-down directly above. Panel height is the scarce
thing in that sidebar. The hex field, the picker and the way back to White stay.

The controller's `figureBackgroundSwatches` went with it rather than being left
as a property nothing reads.

**The guard behind it had to become a better one rather than be deleted.** It
asserted that each of the twenty-four offered grounds gave at least 4.5:1
against the ink derived for it. With no list of offered grounds, the property
that matters is the stronger one the derivation was written for: *any* colour
someone picks gets readable ink. It is now swept over the RGB cube — which
immediately showed the source comment was quoting the wrong number.

**Two worst cases, and they are not the same.** Over the whole cube the worst
any background gets is exactly **4.500:1** (at #645fa0, where a tuned ink only
just clears the threshold and is therefore used). The **fallback branch** alone
bottoms out at **4.583:1** (at #5d60ff, the grey where black and white are
equally bad). The comment quoted the second as though it were the first. Both
are now measured and asserted.

Three of that guard's reversions initially passed, and fixing them is the more
useful part:

- Dulling either tuned ink left it green, because the 4.5 floor is *guaranteed
  by the black/white fallback whatever the tuned pair is*. The floor cannot
  detect a weakened ink. What the pair actually buys is keeping the harsh
  fallback rare — as shipped it carries **83%** of the colour cube on its own,
  against 63% with the light ink dulled and 40% with the dark one. That is now
  the assertion.
- The inks, the threshold and the WCAG luminance weights were **copied** into
  the test rather than read out of the source, so the sweep was checking a
  private copy of constants the application no longer used. They are parsed from
  `figureForeground` now. A mirror of a constant is not a check on it.

### Fixed — twelve pressure labels, none of them against its own line

Found by screening all 434 gallery figures for ink touching the outer two
pixels of the canvas. Four of the top six hits were the same family — the four
sounding charts — which is the signature of a systemic fault rather than four
coincidences.

**The tephigram's pressure labels were in a column no isobar passed through.**
Four sounding charts share one drawing function, and three of them have isobars
that run level to the left edge of the frame, so a tick at `box.left()` is on
the line it labels. A tephigram is a *rotation*: its isobars slope, and the left
edge of the frame is a place most of them never reach. Its twelve labels were
therefore stacked down the left margin, stopping two thirds of the way down a
frame they are meant to span — one more case of a rule that is correct for one
class being applied to all of them.

The anchor is now stated per chart rather than derived, and the first attempt is
worth recording because it is exactly the mistake the fix is meant to prevent:
`at(coolest,hPa)` *looks* like the left end of the isobar, and is on an emagram
and a Stuve — but **a Skew-T applies its skew inside `at`**, so there the cool
end climbs to the right with height, and the labels walked off diagonally across
the plot. Asking each chart what shape its isobars are cannot make that mistake;
deriving the answer from a transform can. Crowded labels are now thinned, and a
label that lands inside the frame is kept inside it.

**And the axis note was cut off by the bottom of the canvas on all four
charts.** Two rows are drawn below the frame — the tick numbers, then the note
saying what the axis is — and one row plus fourteen pixels was reserved for
them. The note's descenders were being clipped by the edge of the image; the
tephigram's, being the longest sentence, put the most ink on the last two rows,
which is how the screen found it.

### Found, not fixed — what is left is the painter, not the engines

With the quadratics gone, the whole catalogue was measured again, one engine
per process under a timeout so that a hang is a row in the table rather than a
missing tail. **434 engines, none timed out, none above ×114 for a hundredfold
of data, median ×6.** Nothing in the catalogue grows faster than its input any
more.

Thirteen engines still take more than two seconds at 24,000 points, and they
are all linear: the cost is the painter drawing 120,000 points, not the engine
deciding what to draw. The worst of them is worth recording precisely, because
the number is alarming and the cause is not:

**Drawdown Curve, 28.7 seconds.** Isolating the five columns showed 418 ms of
the 450 ms small-case cost coming from *one* of them — the synthetic `label`
column, which holds 0 and 1 alternating. A drawdown from the running peak turns
that into a square wave between 0% and −100%, which is a zigzag across the full
height of the plot at *every point*, and the engine draws as an Area, so that
zigzag is **filled**. Twenty-four thousand full-height spikes, antialiased. The
engine is a single linear pass; the figure is pathological. The same column is
behind the Weight and Balance Envelope's old figure too.

The remaining lever is a real one and is **not** taken here: a polyline with
more vertices than the plot has pixel columns is drawing over itself, and
reducing each column to its minimum and maximum is visually equivalent for a
line or an area while turning 24,000 points into about a thousand segments. It
would help all thirteen. It is left out of this batch deliberately, because it
changes antialiased coverage on every dense figure in the catalogue and that is
a decision to take with the gallery in front of you, not one to slip into a
performance batch.

### A note on the timings quoted below

Every figure is wall-clock in a shared container, so the absolute milliseconds
are worth only what the machine was doing at the time — two stale probe
processes from an earlier run were found competing for the CPU partway through
this round, which is exactly the kind of thing that makes an absolute number
lie. **The number that carries the argument is the ratio**: each engine is
drawn at 240 points and at 24,000, seconds apart in the same process, and a
hundredfold in the data should cost about a hundredfold in the time. Ten
thousandfold means something is quadratic. Those ratios, before and after:

| engine | was | now |
| --- | ---: | ---: |
| Composite Curves (Pinch) | ×1,660 | ×13 |
| Weight and Balance Envelope | ×1,601 | ×60 |
| LOWESS Trend | ×1,503 | ×29 |
| Seasonal Subseries Plot | ×966 | ×79 |
| Growth Rate (OD) | ×649 | ×64 |
| Seasonal Decomposition | ×527 | ×124 |

A hundredfold of data for well under a hundredfold of time is the renderer's
fixed cost showing through, not a miracle; ×124 for the decomposition is four
panels of 24,000 points being *drawn*, which is the painter's work and not the
engine's.

### Fixed — four more engines that could not be given real data

All four came back **byte-identical**, so these are changes of method only.

- **A pinch analysis of 24,000 streams took 4.1 seconds.** A composite curve
  is enthalpy accumulated upward through the temperature intervals the stream
  endpoints cut the range into, and the capacity of each interval was found by
  asking *every stream* whether it covered that interval. But every endpoint is
  itself a level, so a stream covers a contiguous run of intervals and nothing
  outside it: adding its capacity where the run begins and taking it away after
  the run ends leaves a prefix sum that is the capacity of each interval in
  turn. The interpolation onto the other curve was the second half of the cost
  — a linear scan for the bracketing breakpoint, asked once per breakpoint
  inside an eighty-step bisection. **4,145 ms to 173 ms.**

- **A series of 24,000 readings spent two seconds deciding its own period.**
  With no period column mapped, the period is the lag of the strongest
  autocorrelation, and every lag up to a third of the record was scored by its
  own pass over the record. The autocovariance at *every* lag is one inverse
  transform of the power spectrum — and the overlap count, which is what keeps
  missing readings honestly accounted for rather than assumed absent, is the
  same correlation of the validity mask with itself. The centred moving average
  that follows had the same shape in miniature, re-adding every reading of the
  window at every position, and the window is one period wide, so it grew with
  the record too; it now moves. **Subseries 1,822 ms to 165 ms, decomposition
  2,079 ms to 620 ms.** A periodic series correlates exactly as well at twice
  its period as at its period, so — as in the growth window above — the tie is
  now broken deliberately toward the shorter lag rather than by the last bits
  of the transform, which is the difference between reporting a season of
  twelve months and one of twenty-four.

- **Four fleets of 24,000 loadings against a 24,000-vertex envelope: 12.1
  seconds.** Whether a loading is within limits is a point-in-polygon test, and
  it walked the whole envelope for every loading — two and a half billion
  straddle tests. The arithmetic was right; it was done far too many times. An
  edge can only matter to a loading whose weight lies between that edge's ends,
  so the edges are now filed by the bands of weight they cross and a loading
  consults only its own band. **12,051 ms to 506 ms** — and *identical* is the
  right word rather than "close", because crossings are counted by parity, so
  visiting the same straddling edges in a different order gives the same
  verdict and each one gets the same arithmetic as before.

### Fixed — LOWESS fitted more points than the figure has pixels

LOWESS fits over a fixed *fraction* of the sample — a quarter here — so unlike
the Savitzky-Golay window beside it, which is capped at fifty either side, its
window grows with the record and the whole smoother is quadratic: **3.3 seconds
at 24,000 readings**, with nothing to show for it. A local regression is smooth
by construction, so between two fits a thousandth of the record apart there is
nothing for the curve to do but go straight.

The fit is now evaluated at a bounded number of anchors and the readings
between them are carried on the straight line joining their neighbours — which
is Cleveland's own `delta`, expressed as a count rather than as a distance.
**3,259 ms to 736 ms, byte-identical.** Below the cap every reading is still an
anchor, so for anything of ordinary size the old behaviour is not merely
preserved, it is literally unchanged.

### Fixed — the growth window search, and the one figure that moved

A growth curve quotes its rate from the **straightest window**, because a
culture spends its beginning in lag and its end in stationary phase and a rate
fitted across all three describes none of them. The search copied the window
out at every start position and walked it twice more — once for the mean, once
for the residuals — which is quadratic in the reading count. The same six
running numbers used everywhere else give the fit, the residual sum and the
total sum of squares at once.

**This is the one rewrite of the six that did not come back byte-identical, and
the reason is worth recording.** A clean exponential phase is straight along its
whole length, so every window that falls inside it scores the same R-squared
*and* the same slope. In the demonstration culture fifteen start positions tied
exactly, and which one won was decided by the last bits of the accumulation —
which changing the order of summation flipped. The rate in the legend never
moved (mu 0.5, doubling time 1.386, both before and after); the drawn segment
slid along the curve.

An accidental tie-break is not a result. The tie is now broken deliberately:
a window must show a *real* improvement before it displaces the incumbent, so
the answer is the **earliest** window of the exponential phase. That is stable
under any order of summation, and it is the answer worth quoting — a growth
rate is measured from where exponential growth begins, not from wherever inside
the phase the arithmetic happened to land. The fitted segment now starts at the
first reading of the exponential phase rather than two thirds of the way along
it.

### Fixed — two guards that ran after the work they existed to prevent

**The mosaic plot gives up when either axis has more than forty levels, and
the check sat after the loop that builds the contingency table.** A continuous
column mapped by mistake built a 24,000 by 24,000 table and then threw it away.
Every new column appends a cell to every existing row and every new row
allocates a vector as long as the column list, so both the work and the memory
are quadratic: **18.9 seconds**, the slowest thing in the catalogue.

The confusion matrix had the same fault in a different shape. Its cap of twenty
classes was applied after collecting the class list, and the collection asked
`QVector::contains` once per value — a linear scan, so 576 million comparisons
to reach a verdict of "too many".

Counting first is linear, stops as soon as it has seen one too many, and
reaches the same verdict. **Mosaic 18,906 ms to 1.8 ms; confusion matrix 719 ms
to 1.9 ms**, and every figure in the gallery byte-identical.

### Fixed — a well test of 24,000 readings took 37 seconds

Two quadratic passes in the Pressure Derivative Plot, both the same mistake.
Bourdet's derivative is taken over a fixed span in log time and the data is
sorted, so the window's ends only ever move forward — but each point copied its
window into two vectors and fitted it fresh. Then the radial-flow plateau
search did it again: a fixed-length window over the derivative, copied out and
fitted at every start position, eighteen thousand vectors of six thousand
doubles.

A least-squares slope needs five running numbers — the count and the sums of x,
y, x² and xy. Adding a point at the leading edge and dropping one at the
trailing edge keeps all five current. **36,872 ms to 355 ms, byte-identical.**

A scaling run over all 434 engines at 240 and 24,000 points per series found
this and a dozen other super-linear engines; the rest are recorded in the
project notes. Nothing in the plot backend starts a timer off the GUI thread,
so the backend is not the source of the `startTimer` warning.

### Added — a cross-thread warning now says which thread

`QObject::startTimer: Timers cannot be started from another thread` has been
sitting in `startup.log` across several builds with nothing to go on: the
message names neither the object nor the thread. The message handler now
records the thread for that warning and its relatives — its address, its name,
and whether it is the GUI thread. Qt names its own threads, so the name alone
usually identifies the culprit, and the GUI thread, the Qt Quick scene-graph
render thread and a QtConcurrent pool thread are three very different bugs (the
first of them is not a bug at all).

Rendering all 434 engines on a `QtConcurrent` pool thread produces no such
warning, so the plot backend is not the source.

### Fixed — a fixture that was silently never used

**A duplicate key in a `QHash` brace-initialiser is not an error, not a
warning, and not visible in the figure.** The last entry wins and the other one
simply never runs. A Duane Plot fixture added near the top of the demonstration
table was shadowed by one four hundred lines below it, and the only way it
showed was that the figure's series carried the *other* entry's label. Both the
fixture table and the axis-name table beside it are now checked for duplicates,
because the compiler cannot: a duplicate key is well-formed C++ that means
something other than what it looks like.

Six more engines that read a specific pair were given the shared signals and
are now given pairs: Volcano Plot (whose p column was a smooth exponential, so
no gene was significant and the figure said so), Bland-Altman, L'Abbé Plot,
Prediction Error Plot, Learning Curve and Validation Curve. The volcano fixture
was itself wrong on the first try in a way worth recording: the null fold
change and the null p-value were both derived from the same normal deviate,
which made them perfectly correlated and drew the null cloud as a narrow V
pinched at the origin — a picture of the generator, not of an experiment.

### Fixed — figures that demonstrated the one answer their engine is never run to find

- **An isoconversional plot of a constant activation energy** is a perfectly
  horizontal trace. That is the correct answer for a one-step reaction and it
  is the one result the plot is never run to find — the whole reason to compute
  Ea at each conversion separately is to see whether it *changes*, because a
  change means the mechanism changes partway through and a single Arrhenius fit
  to the whole run is wrong. It now runs on two overlapping steps, 135 kJ/mol
  rising through a transition near 45% conversion to 185.
- **A confusion matrix with no diagonal and an accuracy of 0.250** — chance on
  four classes. It shared the mosaic plot's two category columns, which are two
  independent counters. What the figure is read for is the *off*-diagonal, and
  that needs a diagonal to be off.
- **A spy matrix of dense data is a solid block.** The one engine whose entire
  content is where the zeros are was being shown columns with none.
- **An event plot of 240 evenly spaced samples is five parallel rules**, and a
  **spectrogram of a stationary signal** is a heatmap of horizontal stripes: a
  correct picture that shows nothing the power spectral density above it does
  not already show more clearly. Three Poisson spike trains and a linear chirp
  with a fixed-frequency burst laid across it.
- **Seven engines that plot a formula were drawn over a measurement's range.**
  Their domain follows the mapped data so a formula can be overlaid on a
  measurement — which is right, and which meant `sin(x)*cos(y)` was drawn over
  a window three units wide, where its zero set is one vertical line and one
  horizontal one. The Implicit Function entry was a cross: mathematically
  correct and unrecognisable as what the engine does.

### Fixed — four attribute control charts that could not be told apart

A p-chart and a u-chart have **stepped** control limits — they widen on a small
lot and tighten on a large one — while an np-chart and a c-chart, which assume
a constant lot, have straight ones. That is the main reason there are four
entries rather than two, and on a constant demonstration lot size the
difference does not exist to be drawn: the four figures came out proportional
to each other, 1.06 grey levels between the p-chart and the u-chart.

The second half is that **a defective is not a defect**. A p-chart counts the
items that failed; a c- or u-chart counts the faults found, and one item can
carry several. Given the same column both pairs drew the same shape with a
different axis label.

### Fixed — a catalogue ROC curve that reported a perfect classifier

**A perfect classifier is the one result that means the data is wrong**, and
the catalogue was showing it twice: the ROC curve and the precision-recall
curve both ran on the shared demonstration columns, where the label column is a
deterministic function of the row index and every score separates it
completely. Both reported an area of exactly 1.000. They now run on two
overlapping score populations with a 22% positive rate — AUC 0.914, average
precision 0.800 — which is what a real model looks like and what makes the two
figures worth comparing with each other.

The same pass gave demonstration data to fifteen more engines that were
answering a question about a signal column: the five that read a digital
elevation model (slope, aspect, hillshade, terrain profile, hypsometric curve),
the flow duration curve, the MA and Manhattan plots, the Lorenz and
concentration curves, the rank-abundance, scree and elbow plots. The
concentration curve turned out to need **two** columns — the outcome and the
variable the population is ranked by, which is the entire difference between it
and a Lorenz curve of the outcome alone — and given one it correctly drew
nothing at all, which the sweep reported as an engine that draws nothing. That
was the sweep doing its job on a fixture that was half a fixture.

### Fixed — four engines whose annotations reached nothing

**`applyLimits` discarded every annotation a rewrite produced.** It refreshes
the painting decisions on a cached prepared spec from the caller's — colour
scale, custom colours, the 3-D view, and annotations — because none of them is
in the fingerprint. That was written when only a caller could make an
annotation. Four engines now do: the ternary diagram's corner names, the VFA
profile's peak, the availability timeline's per-machine uptime percentages and
the animated line's step numbers. All four were overwritten by the caller's
list, which is normally empty.

The symptom is the worst kind. The code that makes the note is there, it runs,
it reviews as correct, and nothing appears on the figure. It was found by
adding a fifth — corner names on the ternary diagram — and watching three
annotations turn into none. `PlotAnnotation` now records whether the engine or
the caller made it, and the refresh keeps the engine's.

- **A ternary diagram did not name its corners.** The composition names were
  put in the triangle's legend label, on a figure whose legend is switched off
  two lines above. Without them the reader can see that a point is near one
  vertex and has no way to learn which of the three components it is. The
  labels also needed room made for them: the axis limits hugged the triangle
  tightly enough that a label placed outside fell past the annotation clip and
  was not drawn at all.

### Fixed — six figures whose content is a small number of things compared

A dendrogram of two hundred leaves, a slope graph of sixty crossing lines, a
population pyramid of forty rows of a sine, a ternary scatter squeezed into a
thumbprint in the middle of its own triangle. The caps inside those engines
kept them from being worse and could not make them readable. The waterfall is
the clearest: every step was positive, so the chart was a staircase that only
climbed — which is a cumulative sum, and the one thing a waterfall is chosen
over a cumulative sum to show is where the ground was given back.

### Fixed — the beam, the R chart, and twelve process figures

- **A moment diagram that was never zero at a support.** Shear and Moment
  integrated the load from the left end with nothing at that end, which is a
  *cantilever* — built in at x = 0, free at the far end. It was drawn under the
  heading "shear and moment" for a beam nobody said was a cantilever: a uniform
  load came out with the moment growing to its largest value at the free end
  and neither diagram returning to zero. Every engineer's first check on a
  moment diagram is that it is zero at a simple support. A single span on two
  supports is statically determinate, so the reactions follow from the load
  alone and no boundary condition has to be asked for; 12 kN/m over 8 m now
  gives shear ±48 kN crossing zero at mid-span and a peak moment of 96 kN·m —
  wL/2 and wL²/8 exactly. The y axis says which support condition it assumed.
- **The R chart was drawn on the mean chart's ordinate.** A bore turned to 48
  mm has a subgroup range near 0.9, so on one axis scaled 0 to 50 the means
  were a flat line across the top and the ranges a flat line along the bottom,
  and neither chart could be read. An X-bar and R chart is conventionally two
  stacked panels for exactly this reason; the range series and its control
  limits now go on the right-hand ordinate, because a limit belongs on the axis
  of the thing it limits.
- **Twelve process and survey figures were drawn from a rising sine.** A
  control chart of one reports every point out of control; a CUSUM of one
  reports "210 points beyond h"; a capability study of one computes a Cpk from
  a bimodal histogram. The charts were right — a sine *is* out of control — and
  saying so 240 times demonstrates nothing. Control Chart, CUSUM, EWMA, X-bar
  and R, Process Capability, the four attribute charts, Funnel Plot, CTD
  Profile, T-S Diagram, Drawdown Curve, Mass Haul Diagram and Shear and Moment
  now run on a process with a real step in it, a cast through a real
  thermocline, an alignment whose cut and fill actually balance.

  The deviate behind those fixtures was wrong on the first attempt in a way
  worth recording: it summed twelve terms of a golden-ratio sequence rather
  than a generator's output, on the reasoning that a low-discrepancy sequence
  is reproducible where a PRNG might not be. It is reproducible and it is not
  noise — consecutive draws differ by twelve steps of the same irrational
  rotation, so the sum is very nearly periodic. The capability histogram came
  out **bimodal**, two clean humps with a gap between them, which is what a
  reader would have taken as the finding of the figure.

### Fixed — catalogue figures drawn from a shape the engine does not read

Twelve more engines read a record rather than a column of measurements: a date
and a count; a row and the two ends of a bar; four price columns; an origin and
a destination. Handed the shared demonstration columns each of them drew
*something* — a Gantt schedule of 240 overlapping bars in a fan, an OHLC chart
of 240 candles a pixel wide, a borehole log of 240 beds, an availability
timeline reporting an uptime percentage computed from a sine. Calendar Heatmap,
Rainflow Matrix, Gantt Schedule, Availability Timeline, Borehole Log, OHLC
Candlestick, Origin-Destination Flow, Cumulative Flow, Inventory Sawtooth,
Fundamental Diagram, Eye Diagram and Psychrometric Chart now each run on the
shape they read.

Three of those fixtures were wrong on the first attempt and the figures said so
— which is the argument for checking a demonstration against the engine's own
arithmetic rather than against whether the picture looks busy. The traffic
counts supplied *speed* where the engine reads *flow*, so it fitted a parabola
to a straight line, reported a free-flow speed of 8.4 against 104, and drew the
flow curve peaking at 215 on an axis of speeds that never exceed 104. The
psychrometric states supplied the humidity *ratio* where the engine reads
relative *humidity* — the unit on an axis is not always the unit of the column
that produced it. And the borehole had one track, which is a correct log and a
three-pixel-wide figure.

### Fixed — legends that quoted fabricated measurements

Eighteen catalogue engines fit a named physical law and put the constants they
recover into the legend, which is the most quotable thing on a figure. On the
shared demonstration columns those constants were invented and printed to four
significant figures: *"Ea 0.0 kJ/mol"*, *"EC50 1.386, Hill 1.26"* on data with
no dose in it, *"Vmax 4.598, Km 0.104"* fitted to a sine, *"max crosswind 0.0"*
on a wind field three thousandths of a knot across, a weight and balance
envelope reporting 240 loadings out of limits, and a top of climb at *"5"*.

Each now runs on data drawn **from the law the engine fits**, so the number in
the legend can be checked against the number the data was made with. That check
is the point, and it caught three faults in the engines themselves that the
figures alone did not show:

- **Young's modulus was fitted over the plastic range.** The elastic region was
  taken as "everything below a tenth of the strain range", which is not the
  elastic region of anything ductile — a tensile test that necks at 20% strain
  yields at about 0.4%. The fit swept in every point up to 2% strain and
  returned 17 GPa for a material with a real modulus of 70, printed as
  `E 1.686e+04`. It is now found below forty per cent of the ultimate stress,
  the usual laboratory rule, and recovers 70 GPa exactly.
- **The 0.2% offset construction line set the axis of the figure it explains.**
  Carried across the whole strain range, a 70 GPa offset line reaches 14,000
  MPa on a specimen that breaks at 410, so the curve was a flat trace along the
  bottom of the frame. It now stops a little past the intersection it exists to
  make.
- **The S-N Basquin exponent was fitted through the endurance limit**, and the
  **rating curve was fitted without the datum offset** the demonstration data
  had been given. Both showed up only because there was a known answer to
  compare against.

Constants now recovered, against the values the data was generated from:
Arrhenius Ea 72.5 kJ/mol (72.5), pre-exponential 4.18e9 (4.1e9); Michaelis
Vmax 8.403 (8.4); dose-response EC50 34.99 and Hill 1.41 (35, 1.4); calibration
slope 0.1872 (0.1873), R² 1.0000; rating Q = 12.4 h^2.098 (12.4 h^2.1);
Crow-AMSAA beta 0.620 (0.62); Weibull eta 1448 h (1450); lift-curve slope
0.0959 /deg (0.098); drag polar CD0 0.0208 and k 0.0479 (0.021, 0.047); great
circle 9,569 km (published 9,560).

The sweep's table of adjudicated look-alikes is now empty. It had one entry,
excusing Geo Line and Ground Track on the grounds that "these longitudes span
about three degrees, so nothing crosses the antimeridian" — a true observation
and the wrong conclusion, since the reason they spanned three degrees was that
the engines were being demonstrated on a signal column. An adjudication that
explains why a figure cannot show what its engine does is a note that the
demonstration is broken, not a reason to stop counting the pair.

### Added — a real scatterplot matrix

- **Plot Matrix drew a single scatter of the first pair** and had done since it
  was added, on the reasoning that "an n × n grid of panels is n² figures and
  there is one frame". That was true of the frame machinery and not of the
  canvas — the marginal-scatter painter was already subdividing the plot area to
  put a distribution along each edge. It now draws scatters below the diagonal,
  each variable's distribution on it, and the correlation for each pair above,
  each panel on its own two ranges, for up to six mapped columns. The
  correlation it used to compute and show nowhere is the number in the upper
  triangle, sized by strength so a strong pair is visible from across the figure.

### Added — a second ordinate

- **Five engines drew two quantities of different scale on one axis**, and said
  so in the axis label: "voltage / power", "sCOD / removal %", "|Z| / phase".
  P = IV, so a cell at a volt and tens of amps has a power in the tens and a
  voltage under one — the voltage curve, which is what a polarisation plot is
  *for*, was a flat line along the bottom. A scree plot worked around it by
  multiplying the cumulative share by the largest eigenvalue so that it fitted,
  and put the apology in the axis title; the number a scree plot is read for
  could not be read off the figure at all. A Pareto chart did the same.

  A series can now ask for a right-hand ordinate with its own range, drawn up
  the right-hand side with no gridlines of its own (a second set of horizontal
  lines through one plot area cannot be told from the first). The legend marks
  which series belongs to it. Applied to the polarisation power, the sCOD
  removal percentage, the Bode phase, and the cumulative shares of the scree
  and Pareto charts — all of which now read off a real axis.
- **A Bode plot could not carry its phase at all**: it was limited to two
  mapped columns, so the "|Z| / phase" axis only ever had one of them on it. It
  now takes frequency, magnitude and phase.

- **Process Capability drew a time series.** It set its engine to "Histogram"
  and returned, but a rewrite's output does not go back through the rewriter, so
  the binning never ran: 240 measurements came out as 240 bars at their own
  values, with the specification limits somewhere inside the block. The limits
  were also drawn *as bars*, reaching from 0 to 1 on an axis of counts running
  to fifty. A bar chart can now carry a reference rule, the same fix the scatter
  needed for the thirteen rewrites that append reference lines to it.
- **Learning Curve and Validation Curve were one figure with two axis labels.**
  They are not the same question: a learning curve asks whether more data would
  help and is read where the validation score stops climbing; a validation curve
  asks which hyperparameter is best and is read at its peak. Each now marks its
  own point and states the training/validation gap there — the number that says
  whether a model is overfitting.

- **A rug plot's rows sat at whatever three per cent of each column's own range
  came to**, accumulated — so an axis labelled "series" carried the numbers 0.09
  and 0.27. A rug's vertical position is a row; rows are now named.

### Fixed — figures that were hard to read

- **A five-column histogram was a barcode.** Bars from different series divide
  the slot so grouped bars stand side by side; histograms bin independently and
  are compared by overlay. The width was also measured from every series'
  positions pooled together, so the most finely binned column decided how every
  other column was drawn. Multi-column histograms are now overlaid, translucent,
  each at its own bin width, and have a legend — they had none.
- **Legends repeated themselves, covered the figure, and hid rows silently.**
  A flight profile listed "top of climb 5" twice; a lift curve's fitted
  parameters made a box over half the plot; a legend too tall for its box simply
  stopped drawing. Now: one row per name, elided to a third of the plot width,
  ending in "+N more".
- **Engines that group by column numbered the groups.** A box plot turns its
  legend off, so its boxes had no names anywhere; a correlation matrix joined
  every name into one line of axis-label text; a radar chart labelled its spokes
  0°, 30°, 60° — the position of each variable expressed as an angle. All three
  now carry the names on the axis.
- **Global Sensitivity ranked five inputs and numbered them 1 to 5.**
- **A chart with one category filled its whole plot area with one bar.**
- **Series left at the default colour all drew in the same blue** while the
  legend named them separately — the pie's fault, in every other painter.
- **Strip Plot was 4D/5D Scatter with a different marker size.** It now draws
  what the name means: every observation of a group at that group's position,
  jittered deterministically across a band.
- **Error Bar read two columns and let any number be mapped**, so five mapped
  columns drew two under a legend naming five.

- **A bubble cloud's labels lay across their own bubbles.** The circle is sized
  by value and the text size was chosen from the value independently, so a small
  share with a long name was clipped mid-word with the ends outside the bubble.
- **A heatmap carried a legend naming the columns it was built from** —
  "time / subject / value" — beside its own colour bar.
- **Parallel coordinates, slope graphs and radar charts labelled their axes with
  numbers where no axis exists.** All three now carry the variable names.
- **A treemap of one column is a rectangle**, and the sweep was drawing one.
- **Plot Matrix computed the correlation that justifies it standing in for a
  matrix, and then showed it nowhere** — it was in a one-row legend, which is not
  drawn. It is now stated under the figure, along with what the panel is.

- **A Pareto chart of hollow rectangles.** Both it and the abatement cost curve
  build their bars as closed outlines on a Line Chart (because the bar painter
  would read a cumulative line as a second row of bars), and neither was filled —
  the length of a bar, and the *area* of an abatement block, are read from the
  ink in them.
- **A star glyph grid and a waffle chart were drawn inside an axis** numbering
  their own layout positions.

### Fixed — build

- **`Quick-Build.ps1` and `Build-Release.ps1` failed unless run from the repo
  root.** Both resolve the project directory and then called `cmake --preset`,
  which reads `CMakePresets.json` from the *current* directory — so the scripts
  reported a correctly prepared build environment and then died on the next line
  with a message naming a directory nobody asked to build.

### Added

- `LICENSE` (GPL-3.0-or-later) and `tools/Generate-Notices.ps1`, which builds
  `THIRD-PARTY-NOTICES.md` from the ports whose binaries are actually in the
  staged release.
- `tools/check_definitions.py` — every member function declared in a header must
  be defined somewhere. Stricter than the linker, which only reports a missing
  definition once something still references it.
- `.github/workflows/science-tests.yml` — the Python science suites had never run
  in CI.
- Test suites: `test_numbers.py` (analysis results against closed forms),
  `test_scan_quality.py` (recommendation quality), `test_cxx_numerics.py` (the
  C++ quantile, mean and standard deviation compiled and checked against NumPy),
  `test_reachable.py` (nothing may be written and left unreachable).

### Known gaps

- The visual pass is incomplete: engines may be correct but mislabelled or hard
  to read at publication size.
- Freedman-Diaconis binning and Silverman bandwidth were verified by reading them
  against the standard forms, not by execution — they are inline in painting
  functions that need a live `QPainter`.
