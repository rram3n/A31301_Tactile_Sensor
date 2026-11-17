import os, json, hashlib
from pathlib import Path

# This script registers an after-build action to generate a manifest once the .bin exists.
from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()

def _safe_env_get(key, default=None):
    try:
        return env.get(key, default)
    except Exception:
        return default

project_dir_str = (
    _safe_env_get('PROJECT_DIR')
    or os.getenv('PLATFORMIO_PROJECT_DIR')
    or os.getcwd()
)
project_dir = Path(project_dir_str).resolve()
build_dir = Path(_safe_env_get('BUILD_DIR', project_dir / '.pio' / 'build'))
pioenv = _safe_env_get('PIOENV', 'unknown')

ver_path = project_dir / 'include' / 'Version.h'

def _read_version_h():
    if not ver_path.exists():
        return ''
    return ver_path.read_text(encoding='utf-8', errors='ignore')

def _pick(ver_text: str, name: str, default=None):
    import re
    m = re.search(rf'#define\s+{name}\s+"?([^"\n]+)"?', ver_text)
    return (m.group(1) if m else default)

def _make_manifest(bin_path: Path):
    ver = _read_version_h()
    semver = _pick(ver, 'FW_SEMVER', '0.0.0-dev')
    hw = _pick(ver, 'HW_REV', 'GEN2')
    channel = _pick(ver, 'RELEASE_CHANNEL', 'dev')
    sha = _pick(ver, 'GIT_SHA', 'nogit')
    build = _pick(ver, 'FW_BUILD', '0')

    # sha256 of the produced binary
    h = hashlib.sha256()
    h.update(Path(bin_path).read_bytes())
    sha256 = h.hexdigest()

    base = os.getenv('MANIFEST_URL_PREFIX', 'file://')
    name = f"ballu_{(hw or 'GEN2').lower()}-{semver}+{(build if build != '0' else sha)}.bin"
    url = base.rstrip('/') + '/' + name

    try:
        fw_build = int(build)
        if fw_build == 0:
            fw_build = sha
    except Exception:
        fw_build = sha

    manifest = {
        "channel": channel,
        "hw": hw,
        "fw_semver": semver,
        "fw_build": fw_build,
        "sha256": sha256,
        "sig": None,
        "url": url,
    }

    env_dir = build_dir / pioenv
    outdir = env_dir / 'manifests'
    outdir.mkdir(parents=True, exist_ok=True)
    out = outdir / f"{pioenv}-{semver}.json"
    out.write_text(json.dumps(manifest, indent=2))
    print(f"[gen_manifest] Wrote {out}")

def _after_build_action(target, source, env):
    # SCons passes (target, source, env); for post-action, target[0] is the file being emitted
    try:
        bin_path = Path(str(target[0]))
        if bin_path.exists():
            _make_manifest(bin_path)
        else:
            # Fallback: try to find any .bin in BUILD_DIR/PIOENV
            env_dir = build_dir / pioenv
            bins = sorted(env_dir.glob('*.bin'), key=lambda p: p.stat().st_mtime)
            if bins:
                _make_manifest(bins[-1])
            else:
                print('[gen_manifest] No .bin found after build; skipping')
    except Exception as e:
        print(f"[gen_manifest] Error: {e}")

# Register actions to run after the firmware is linked/packed
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _after_build_action)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _after_build_action)
