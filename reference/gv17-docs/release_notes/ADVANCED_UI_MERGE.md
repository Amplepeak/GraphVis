# Advanced UI merge

This package uses the Professional Suite as the functional/backend base and the uploaded advanced GraphVis UI as the interaction/style baseline.

Key decisions:
- backend modules from the Professional Suite are retained (statistics, ML, connectors, workflows, digitizer, native figures, GPU preview, publishing, 121 render engines);
- plot selection is presented inline using searchable/collapsible categories instead of replacing the backend with the older graph registry;
- the existing bounded sidebar drag hotfix is retained, so adopting the older visual layout does not reintroduce the tiny/huge splitter bug;
- the full logo is alpha-transparent and the window icon uses `graphvis_icon.png`;
- `run_app.bat` and direct `main_app.py` both use the local virtual environment.
