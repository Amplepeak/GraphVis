from pathlib import Path
import sys
import zipfile
import tarfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = (ROOT / 'VERSION').read_text().strip()
errors = []

required_source = [
    'install.bat',
    'install.sh',
    'run.bat',
    'run.sh',
    'build.bat',
    'build.sh',
    'START_HERE.txt',
    'docs/INSTALL_AND_BUILD.md',
    'installer/windows/GraphVis.nsi',
    'installer/windows/Prerequisites.nsh',
    'packaging/end_user/Install.bat',
    'packaging/end_user/Repair.bat',
    'packaging/end_user/Uninstall.bat',
    'packaging/linux/install.sh',
    'packaging/linux/run.sh',
    'packaging/linux/uninstall.sh',
    'packaging/windows/README-WINDOWS.md',
    'packaging/linux/README-LINUX.md',
    'tools/Build-LinuxRelease.sh',
    'tools/Bootstrap-LinuxBuild.sh',
    'tools/Install-State.ps1',
    'packaging/internal/Runtime-Maintenance.ps1',
]
for rel in required_source:
    if not (ROOT / rel).exists():
        errors.append(f'missing source release component: {rel}')

for old in ['Start_GraphVis.bat', 'Developer_First_Time_Setup.bat', 'installer/windows/GraphVis.iss', 'tools/Stage-Graphviz.ps1']:
    if (ROOT / old).exists():
        errors.append(f'legacy/redundant distribution component still present: {old}')

for empty in ['build-support', 'app/qml/theme']:
    if (ROOT / empty).exists():
        errors.append(f'known empty folder should be removed: {empty}')

win_zip = ROOT / 'dist' / f'GraphVis-{VERSION}-Windows.zip'
if win_zip.exists():
    prefix = f'GraphVis-{VERSION}-Windows/'
    expected = {
        prefix + 'Install.exe',
        prefix + 'Install.bat',
        prefix + 'Uninstall.bat',
        prefix + 'README.md',
        prefix + 'Support/Repair.bat',
        prefix + 'Support/Uninstall.bat',
    }
    with zipfile.ZipFile(win_zip) as zf:
        actual = {n for n in zf.namelist() if not n.endswith('/')}
    if actual != expected:
        errors.append(f'Windows ZIP layout mismatch; expected {sorted(expected)}, got {sorted(actual)}')

linux_tar = ROOT / 'dist' / f'GraphVis-{VERSION}-Linux.tar.gz'
if linux_tar.exists():
    prefix = f'GraphVis-{VERSION}-Linux/'
    required = {
        prefix + 'install.sh',
        prefix + 'run.sh',
        prefix + 'uninstall.sh',
        prefix + 'README.md',
        prefix + 'app/graphvis',
        prefix + 'app/libgraphvis_ffi.so',
        prefix + 'app/internal/runtime-manifest.sha256',
    }
    with tarfile.open(linux_tar, 'r:gz') as tf:
        actual = {m.name for m in tf.getmembers() if m.isfile()}
        modes = {m.name: m.mode for m in tf.getmembers() if m.isfile()}
    missing = required - actual
    if missing:
        errors.append(f'Linux archive missing: {sorted(missing)}')
    for rel in [prefix + 'install.sh', prefix + 'run.sh', prefix + 'uninstall.sh', prefix + 'app/graphvis']:
        if rel in modes and not (modes[rel] & 0o111):
            errors.append(f'Linux executable permission missing: {rel}')

if errors:
    print('GraphVis public release verification: FAIL', file=sys.stderr)
    for e in errors:
        print(' - ' + e, file=sys.stderr)
    raise SystemExit(1)

if not win_zip.exists() and not linux_tar.exists():
    print('NOTE: public binary archives do not exist yet; source packaging layout was verified.')
print('GraphVis public release verification: PASS')
