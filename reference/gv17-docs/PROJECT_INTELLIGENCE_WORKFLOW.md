# GraphVis Project Intelligence Workflow

## Goal

Project Intelligence lets GraphVis reason about a research project as a collection of related **literature + datasets + scripts**, instead of treating each file as an isolated import.

## Blank startup

GraphVis opens with one blank `Workspace` graph tab. No scientific figure is rendered automatically and old graph tabs are not restored automatically. The graph controls remain available. Project datasets are discovered in the background so they can be selected later without browsing for the files again.

Use **File → Restore previous graph tabs** only when the prior session's graphs are deliberately wanted.

## Read a thesis or paper once

Use **Read Literature / Thesis…**.

GraphVis:

1. copies the source into `documents/literature`;
2. extracts text/tables/captions where possible;
3. derives semantic context such as likely graph type, variables and experimental conditions;
4. saves numeric series recovered from the document under `datasets/literature_extracted`;
5. caches a compact literature index in `context/literature_index.json`;
6. links the document to the active Project Group.

The cached context is loaded on later starts; the original paper/thesis does not need to be reselected.

## Named Project Groups

Open **Project Groups…** to create logical research groups. A group can contain any combination of:

- literature/thesis documents;
- imported project datasets;
- analysis/parameter scripts;
- notes.

Example:

```text
Thesis main study
  Literature: Thesis.pdf, methods-paper.pdf
  Datasets: experiment_main.xlsx, EIS.csv
  Scripts: analysis.m

Validation cohort
  Literature: validation-paper.pdf
  Datasets: validation.csv
```

The active group defines the context used by Smart Map Suite.

## Linked Literature Bundle

**Add Linked Literature Bundle…** is different from a Project Group. It means one primary paper/thesis plus directly associated supplementary files. The first PDF is treated as the primary document when possible; all chosen files are retained as linked sources and assigned to a Project Group.

## Script / parameter intelligence

Use **Import Script / Parameters…** to paste or load MATLAB (`.m`), Python (`.py`), R (`.R/.r`), Julia (`.jl`) or plain-text analysis code.

GraphVis does not execute imported scripts. It statically extracts:

- variable assignments;
- common plot calls;
- `xlabel`, `ylabel`, `zlabel` and title text;
- comments/notes.

Recognized plot calls are mapped onto concrete GraphVis render engines. This gives Smart Map Suite useful intent even if the source workflow originated in MATLAB or another analysis package.

## Smart Map Suite

**Smart Map Suite from Active Group** combines, in priority order:

1. saved optional AI Project Advisor suggestions;
2. imported script plot hints;
3. offline literature/project keyword-context hints;
4. the existing dataset/literature visualization advisor;
5. GraphVis dataset-compatibility checks and automatic axis mapping.

Only compatible plots are opened. The suite opens a focused set of recommended graphs rather than generating every possible chart.

## Optional AI Project Advisor

The advisor is opt-in. It accepts a user-supplied OpenAI-compatible chat-completions endpoint, model identifier and API key. Before any request, GraphVis asks for confirmation that project context will leave the machine.

The advisor sends a compact project summary and dataset schema/column information rather than raw dataset rows. The API key exists only in the dialog/task memory and is not written to the project.

Offline Smart Map recommendations continue to work without the API feature.

## Progress and cancellation

File copying uses chunked progress with byte/percentage status. A partial project copy is written as `.graphvis.part`, then atomically committed when complete. Cancel removes the partial file.

PDF extraction/OCR reports page progress and checks for cancellation between processing stages/pages. Some third-party PDF/OCR calls cannot be interrupted in the middle of one library call, but the task will stop at the next cancellation checkpoint.

## Project folders

```text
Projects/<project>/
  datasets/
    literature_extracted/
  documents/
    literature/
    scripts/
  context/
    project_context.json
    literature_index.json
    script_index.json
```

The dataset scanner recursively discovers files below `datasets`, which means manually imported and literature-extracted datasets automatically reappear in future sessions without browsing to them again.
