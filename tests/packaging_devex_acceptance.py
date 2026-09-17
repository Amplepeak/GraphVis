from pathlib import Path
R = Path(__file__).resolve().parents[1]

def req(c, m):
    if not c:
        raise AssertionError(m)

installer = (R/'installer/windows/GraphVis.nsi').read_text()
prereq = (R/'installer/windows/Prerequisites.nsh').read_text()
install_state = (R/'tools/Install-State.ps1').read_text()
build = (R/'tools/Build-Release.ps1').read_text()
linux_build = (R/'tools/Build-LinuxRelease.sh').read_text()
linux_install = (R/'packaging/linux/install.sh').read_text()
bootstrap = (R/'tools/Bootstrap-WindowsBuild.ps1').read_text()
workflow = (R/'.github/workflows/windows-release.yml').read_text()
repair = (R/'packaging/end_user/Repair.bat').read_text()
uninstall = (R/'packaging/end_user/Uninstall.bat').read_text()
install_bat = (R/'packaging/end_user/Install.bat').read_text()
dev_state = (R/'tools/Dev-State.ps1').read_text()
cmake = (R/'CMakeLists.txt').read_text()
presets = (R/'CMakePresets.json').read_text()
app_controller = (R/'app/src/AppController.cpp').read_text()

# Clean public/developer split.
for old in ['Start_GraphVis.bat','Start_GraphVis.ps1','Developer_First_Time_Setup.bat','Quick_Build_GraphVis_18.bat','Build_GraphVis_18.bat','Build_Full_Release_18.bat','Diagnose_GraphVis_Start.bat','tools/Stage-Graphviz.ps1']:
    req(not (R/old).exists(), f'legacy/redundant workflow remains: {old}')
req(not (R/'build-support').exists() and not (R/'app/qml/theme').exists(), 'known empty source folders remain')
# THE DEVELOPER ENTRY POINTS ARE AT THE ROOT, and this asked for them under
# dev/. dev/dev_bootstrap.bat and dev/dev_run.bat do not exist, are not tracked,
# and are not on the machine this project is built on either - so the very first
# line of this file raised FileNotFoundError and none of the ninety checks below
# it ever ran. Not a check that failed: a check that was never once executed.
#
# It went unseen because of how it was invoked. The Linux workflow runs these
# scripts under bash -e, which stops at the first failure, and that is the run
# that finally reported it. The Windows workflow runs six of them in one pwsh
# block, where only the LAST command's exit code decides the step - so this
# script had been raising there for as long as it has existed while the step
# went green. Fixed in windows-release.yml in the same commit.
#
# install.bat, run.bat and build.bat at the repository root are the entry points
# the README documents and the ones that exist, so those are what is asserted.
for entry in ('install.bat', 'run.bat', 'build.bat'):
    req((R/entry).exists(), f'developer entry point missing: {entry}')

# Deterministic install state: Graphviz is deliberately absent because runtime code does not use it.
for marker in ['Test-GraphVisRuntime','VerifyManifest','graphvis.exe','graphvis_ffi.dll','runtime-manifest.sha256','Write-GraphVisInstallState']:
    req(marker in install_state, f'install state validation missing: {marker}')
req('graphviz' not in install_state.lower(), 'unused Graphviz remains a mandatory Windows runtime dependency')
for marker in ['dev-state.json','1.98.0','vcpkg_commit','native\\Cargo.lock','Test-GraphVisDevState']:
    req(marker in dev_state, f'developer state validation missing: {marker}')

# Windows public package.
req('OutFile "${GraphVisOutputDir}\\Install.exe"' in installer, 'NSIS Install.exe output missing')
req('CheckPrerequisites' in installer and 'Prerequisites.nsh' in installer, 'NSIS prerequisite hook missing')
req('GRAPHVIS_REQUIRE_GRAPHVIZ' in prereq and 'where.exe dot.exe' in prereq and 'https://graphviz.org/download/' in prereq, 'friendly optional Graphviz check missing')
for marker in ['Install.exe','Install.bat','README.md','Support/Repair.bat','Support/Uninstall.bat','Compress-Archive']:
    req(marker in build, f'Windows public packaging missing: {marker}')
req('GraphVis.nsi' in build and 'makensis' in build.lower(), 'NSIS build path missing')
req('NSIS.NSIS' in bootstrap and 'Inno' not in bootstrap, 'bootstrap still depends on Inno or does not install NSIS')
req('Install.exe' in install_bat and 'start "GraphVis Setup"' in install_bat, 'Install.bat does not launch Install.exe')
req('..\\Install.exe' in repair, 'Support/Repair.bat cannot find root Install.exe')
req('HKCU\\Software\\GraphVis\\18.4' in uninstall and 'Uninstall.exe' in uninstall, 'Uninstall does not use registered GraphVis path')

# Linux public package.
for marker in ['linux-release','x64-linux','GRAPHVIS_BUILD_FLIGHT']:
    req(marker in presets, f'Linux/public CMake preset missing: {marker}')
for marker in ['libgraphvis_ffi.so','ldd','runtime-manifest.sha256','tar -C', 'GraphVis-$VERSION-Linux']:
    req(marker in linux_build, f'Linux release build missing: {marker}')
for marker in ['app/graphvis','libgraphvis_ffi.so','apt-get','dnf','zypper','pacman','graphviz.required']:
    req(marker in linux_install, f'Linux installer prerequisite/runtime logic missing: {marker}')
req('#ifndef Q_OS_WIN' in app_controller and 'VTK / PBR' in app_controller, 'non-Windows renderer default does not avoid Win32-only WGPU surface')

# THE SCIENCE ADD-ON'S SOURCE IS SHIPPED ON PURPOSE. This used to assert the
# opposite - that `install(DIRECTORY services/` must NOT appear - and that was
# reversed deliberately, with the reasoning recorded above the install() rule in
# CMakeLists.txt: the add-on is installed by pip FROM services/python, and that
# folder only ever existed in the source tree, so an end user with Install.exe
# could not install it. Behind it were 149 dataset formats, all nine analysis
# operations, the seventeen surface estimators, literature extraction, .gvfig
# save and open, folder batching and the MATLAB engine path - every one of them
# unreachable from a release.
#
# So the question is no longer whether the source ships, but whether only the
# SOURCE ships. A few hundred kilobytes of .py is the point; a vendored
# virtualenv, wheels or site-packages would be the release quietly growing by
# hundreds of megabytes, which is the fault this check now guards.
req('install(DIRECTORY services/python/graphvis_science' in cmake,
    'the science add-on source is no longer installed, so a released copy cannot install the add-on')
req('PATTERN "__pycache__" EXCLUDE' in cmake and 'PATTERN "*.pyc" EXCLUDE' in cmake,
    'compiled Python is being shipped with the add-on source')
for heavy in ('site-packages', '.venv', 'install(DIRECTORY services/python/build'):
    req(heavy not in cmake, f'a built Python environment is being installed, not just source: {heavy}')
req((R/'services/python/pyproject.toml').exists(), 'optional Python science add-on source was incorrectly deleted')
req('if(GRAPHVIS_BUILD_RUST)' in cmake and 'WIN32 AND GRAPHVIS_BUILD_RUST' not in cmake, 'compiled Rust runtime is not installed cross-platform')

# CI should target the public Windows ZIP naming.
req('GraphVis-18.4.0-Windows.zip' in workflow, 'Windows CI does not verify/upload the public ZIP')
req('graphviz/bin/dot.exe' not in workflow.lower(), 'Windows CI still requires unused Graphviz')

print('GraphVis 18.4 packaging/DevEx acceptance: PASS')
