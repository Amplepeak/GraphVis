# Security

## Reporting a vulnerability

Please do not open a public issue for a security problem. Use the private
reporting channel on whichever forge you are reading this on — "Report a
vulnerability" under the Security tab on GitHub, or a private issue on
Codeberg — and give it a few days for a first reply.

Include what you did, what happened, and which version (`VERSION`, or Help ▸
About in the application).

## What is in scope

GraphVis is a desktop application that reads data files you point it at. The
interesting surface is therefore the readers:

- **Data import.** CSV, Excel, MATLAB, HDF5, NetCDF, Arrow and the rest of the
  optional Science add-on formats. A crafted file that causes a crash, a read
  outside a buffer, or code execution is in scope.
- **Boundary and literature import**, which parse files from outside sources.
- **Project files.** A saved figure or project that executes something, or
  writes outside the folder it was opened from, is in scope.
- **The Python science service**, which runs locally and is spoken to over a
  local channel.

## What is not

- The program renders whatever numbers it is given. A figure that is misleading
  because the data is misleading is not a vulnerability — though if GraphVis
  itself draws something that misrepresents the data it was given, please report
  it as a bug, because that is the fault this project cares most about.
- Denial of service by opening a very large file on a machine without the memory
  for it.
- Findings in Qt, VTK, Arrow or other third-party components should go to those
  projects. Tell us anyway if GraphVis ships an affected version — see
  `THIRD-PARTY-NOTICES.md` for what is shipped.
