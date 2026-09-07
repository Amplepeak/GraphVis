from pathlib import Path
import tomllib, json
R=Path(__file__).resolve().parents[1]
def req(c,m):
    if not c: raise AssertionError(m)

def main():
    cargo=(R/'native/Cargo.toml').read_text()
    cmake=(R/'CMakeLists.txt').read_text()
    workflow=(R/'.github/workflows/windows-release.yml').read_text()
    docker=(R/'infra/Dockerfile.rust-dev').read_text()
    audit=(R/'scripts/audit_cull.sh').read_text()
    env=(R/'tools/Build-Environment.ps1').read_text()
    fallback=(R/'scripts/Build-WithCacheFallback.ps1').read_text()
    vcpkg=json.loads((R/'vcpkg.json').read_text())
    req(vcpkg.get('builtin-baseline')=='9e593bb18ea69cc5095e012465dcd675a822ed0d','vcpkg baseline changed/unpinned')
    req('[profile.fast]' in cargo and 'codegen-units = 256' in cargo,'fast Cargo profile missing')
    req('--locked' in cmake and 'GRAPHVIS_RUST_PROFILE_ARGS' in cmake,'CMake Rust build not locked/profile-aware')
    req('cargo chef prepare' in docker and 'cargo chef cook' in docker,'cargo-chef Docker layers missing')
    req('sha256sum -c' in docker and 'sccache' in docker,'prebaked sccache checksum verification missing')
    req('x-gha' not in workflow,'removed vcpkg x-gha cache provider still present')
    req('actions/cache@v4' in workflow and 'vcpkg-binaries' in workflow,'CI file-cache replacement missing')
    req('VCPKG_BINARY_SOURCES' in env and 'RUSTC_WRAPPER' in env,'central cache environment missing')
    req('GRAPHVIS_DISABLE_BUILD_CACHE' in fallback and 'Remove-Item' in fallback,'cache-poison fallback missing')
    for marker in ['cargo fmt','cargo clippy','cargo test','cargo machete','cargo +nightly udeps','cargo build --workspace --release --locked']:
        req(marker in audit, f'audit gate missing: {marker}')
    # direct deps culled in source audit
    core=(R/'native/crates/graphvis-core/Cargo.toml').read_text(); render=(R/'native/crates/graphvis-render/Cargo.toml').read_text()
    req('arrow.workspace' not in core and 'thiserror.workspace' not in core,'core dead deps remain')
    req('windows.workspace' not in render and 'parking_lot.workspace' not in render,'render dead deps remain')
    req(not (R/'docs/STARTUP_RECOVERY_18.2.1.md').exists(),'obsolete startup patch doc remains')
    req(not (R/'assets/branding/graphvis_logo.png').exists() and not (R/'assets/branding/graphvis_wordmark.png').exists(),'unreferenced branding remains')
    print('GraphVis 18.4 DevOps acceleration acceptance: PASS')
    if not (R/'native/Cargo.lock').exists():
        print('  RELEASE BLOCKED AS DESIGNED: native/Cargo.lock must be generated/committed on Rust-enabled machine')

if __name__=='__main__': main()
