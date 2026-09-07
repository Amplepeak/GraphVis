# Runtime installer / launch hotfix

This release addresses two Windows failures observed in the shared-runtime build.

1. `Repair_GraphVis.bat` previously reinstalled the entire recommended stack. A failure building the optional `netCDF4` package therefore caused repair to fail even when the core GraphVis runtime was usable. Repair now targets the core runtime only; optional groups cannot block startup.
2. The application imported the full MVC/UI/rendering graph before constructing QApplication and the splash screen. On first launch the console could appear idle for a long time with no otter logo. MVC imports are now deferred until after the splash is visible, and startup stages are printed to the launcher.

NetCDF uses xarray+h5netcdf/SciPy by default. The optional `netCDF4` C extension is isolated to the full profile.
