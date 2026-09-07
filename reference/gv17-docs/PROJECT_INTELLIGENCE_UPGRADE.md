# Project Intelligence / Blank Startup Upgrade

- Startup now opens a lightweight blank `Workspace` tab with no automatic figure rendering.
- Previous graph tabs restore only through an explicit File-menu action.
- Project datasets are scanned in the background without generating preview plots.
- Added persistent named Project Groups linking literature, datasets, scripts and notes.
- Replaced the confusing primary literature controls with `Read Literature / Thesis`, `Add Dataset`, `Project Groups`, `Import Script / Parameters`, `Add Linked Literature Bundle`, and `Smart Map Suite from Active Group`.
- Literature/theses are copied to the project and cached so they do not need to be reselected each session.
- Literature-extracted numeric data is stored in `datasets/literature_extracted` and participates in normal project scanning.
- Added static MATLAB/Python/R/Julia script/parameter intent extraction without executing user code.
- Added optional provider-neutral OpenAI-compatible API advisor using a user-supplied endpoint/model/API key. Raw data rows are not included in advisor requests.
- Smart Map Suite now combines AI advice, script plot calls, offline project/literature context and dataset-based recommendations.
- Long copies and literature tasks are cancellable; copy progress is byte based and incomplete `.graphvis.part` files are cleaned up.
- Replaced generic busy animation with the GraphVis otter plus rotating water-arrow arc and swimming motion.
- Object Manager can display named project-context groups and their linked sources.
