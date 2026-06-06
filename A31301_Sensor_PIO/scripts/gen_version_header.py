import os, re, datetime, subprocess
from pathlib import Path

try:
    from SCons.Script import DefaultEnvironment
except Exception:
    # Graceful import for static analysis
    class Dummy: pass
    def DefaultEnvironment(): return {}

env = DefaultEnvironment()
# Resolve project_dir without relying on __file__ (can be undefined in some SCons exec contexts)
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
include_dir = project_dir / 'include'
include_dir.mkdir(parents=True, exist_ok=True)
out_path = include_dir / 'Version.h'

def sh(cmd: str, default: str = '') -> str:
    try:
        return subprocess.check_output(cmd, shell=True, text=True).strip()
    except Exception:
        return default

def parse_semver(tag: str):
    # Accept v1.2.3 or v1.2.3-beta(.N). Be tolerant of surrounding quotes/backslashes from build_flags.
    if tag is None:
        return None
    t = str(tag).strip()
    # Strip surrounding quotes and common escape patterns
    if t.startswith('"') and t.endswith('"'):
        t = t[1:-1]
    t = t.replace('\\"', '').replace('"', '')
    # Now match SemVer (optionally prefixed with 'v')
    m = re.fullmatch(r"v?(\d+)\.(\d+)\.(\d+)(-[0-9A-Za-z.-]+)?", t)
    if not m:
        return None
    maj, minor, patch, pre = m.groups()
    sv = f"{maj}.{minor}.{patch}{pre or ''}"
    return sv, int(maj), int(minor), int(patch)

is_ci = any(k in os.environ for k in [
    'GITHUB_ACTIONS', 'GITLAB_CI', 'JENKINS_HOME', 'BUILD_NUMBER'
])

def get_pio_option(name: str):
    try:
        # Only valid under real SCons environment
        return env.GetProjectOption(name)
    except Exception:
        return None

def _find_define_in_build_flags(key: str):
    """Extract value for -D KEY=VALUE from PlatformIO build_flags option."""
    try:
        bf = get_pio_option('build_flags')
        if not bf:
            return None
        # build_flags may be a string with newlines or a list
        if isinstance(bf, str):
            items = bf.split()
        elif isinstance(bf, (list, tuple)):
            # Flatten nested lists that PlatformIO may pass
            items = []
            for it in bf:
                if isinstance(it, (list, tuple)):
                    items.extend(list(it))
                else:
                    items.append(str(it))
        else:
            return None
        # Accept formats: -DKEY=VAL, -D KEY=VAL, -D KEY, -DKEY
        i = 0
        while i < len(items):
            tok = items[i]
            if tok == '-D' and i + 1 < len(items):
                define = items[i+1]
                i += 2
            elif tok.startswith('-D'):
                define = tok[2:]
                i += 1
            else:
                i += 1
                continue
            if not define:
                continue
            if '=' in define:
                k, v = define.split('=', 1)
            else:
                k, v = define, ''
            if k.strip() == key:
                v = v.strip().strip('"')
                return v
        return None
    except Exception:
        return None

"""
Version resolution precedence:
  CI builds:
    1) SEMVER_OVERRIDE/LOCAL_SEMVER
    2) RELEASE_TAG/git tag
    3) ballu_semver from platformio.ini
    4) FW_SEMVER/APP_VERSION from build flags
  Local (non‑CI) builds:
    1) SEMVER_OVERRIDE/LOCAL_SEMVER
    2) ballu_semver from platformio.ini
    3) FW_SEMVER/APP_VERSION from build flags
    4) RELEASE_TAG/git tag
"""

# 1) Highest priority override via environment
ovr = os.getenv('SEMVER_OVERRIDE') or os.getenv('LOCAL_SEMVER')

# 2) PlatformIO project option and tag
pio_semver = get_pio_option('ballu_semver')
raw_tag = os.getenv('RELEASE_TAG') or sh('git describe --tags --abbrev=0', '')

sv = None; maj = minor = patch = 0

def _try_set_from_tag(tag_str):
    global sv, maj, minor, patch, raw_tag
    p = parse_semver(tag_str)
    if p:
        sv, maj, minor, patch = p
        raw_tag = f"v{sv}"
        return True
    return False

if ovr and _try_set_from_tag(ovr):
    pass
else:
    if not is_ci:
        # Local: prefer platformio.ini and build flags before git tags
        if pio_semver and _try_set_from_tag(pio_semver):
            pass
        else:
            # Check build flags via project option (robust) then CPPDEFINES
            dsem_opt = _find_define_in_build_flags('FW_SEMVER') or _find_define_in_build_flags('APP_VERSION')
            if dsem_opt:
                _try_set_from_tag(dsem_opt)
            
            try:
                cppdefs = env.get('CPPDEFINES') or []
                def find_define(key):
                    for d in cppdefs:
                        if isinstance(d, (list, tuple)) and len(d) == 2 and d[0] == key:
                            val = d[1]
                            if isinstance(val, bytes):
                                val = val.decode('utf-8', errors='ignore')
                            return str(val).strip('"')
                    return None
                dsem = find_define('FW_SEMVER') or find_define('APP_VERSION')
                if dsem:
                    _try_set_from_tag(dsem)
            except Exception:
                pass
            if not sv and raw_tag:
                _try_set_from_tag(raw_tag)
    else:
        # CI: prefer release tag
        if raw_tag and _try_set_from_tag(raw_tag):
            pass
        elif pio_semver:
            _try_set_from_tag(pio_semver)
        else:
            # As last resort, build flags
            dsem_opt = _find_define_in_build_flags('FW_SEMVER') or _find_define_in_build_flags('APP_VERSION')
            if dsem_opt:
                _try_set_from_tag(dsem_opt)
            else:
                try:
                    cppdefs = env.get('CPPDEFINES') or []
                    def find_define(key):
                        for d in cppdefs:
                            if isinstance(d, (list, tuple)) and len(d) == 2 and d[0] == key:
                                val = d[1]
                                if isinstance(val, bytes):
                                    val = val.decode('utf-8', errors='ignore')
                                return str(val).strip('"')
                        return None
                    dsem = find_define('FW_SEMVER') or find_define('APP_VERSION')
                    if dsem:
                        _try_set_from_tag(dsem)
                except Exception:
                    pass

sha = os.getenv('GIT_SHA') or sh('git rev-parse --short HEAD', 'nogit')
dirty = 0 if sh('git diff --quiet || echo changed', '') == '' else 1

# Build number: monotonic in CI, else 0 (use sha for display)
if is_ci:
    run = os.getenv('GITHUB_RUN_NUMBER') or os.getenv('CI_PIPELINE_IID') or os.getenv('BUILD_NUMBER')
    try:
        run_i = int(run) if run and str(run).isdigit() else 1
    except Exception:
        run_i = 1
    build_int = int(datetime.datetime.utcnow().strftime('%Y%m%d')) * 100 + (run_i % 100)
else:
    build_int = 0


# Channel detection
channel = os.getenv('RELEASE_CHANNEL')
if not channel:
    if is_ci:
        prerelease = os.getenv('GITHUB_PRERELEASE')
        if prerelease is None:
            channel = 'beta' if (raw_tag and '-' in raw_tag) else 'stable'
        else:
            channel = 'beta' if str(prerelease).lower() == 'true' else 'stable'
    else:
        # Local default to dev; but if semver contains a pre-release suffix, mark as beta
        channel = 'beta' if (sv and '-' in sv) else 'dev'

track = {'stable': 0, 'beta': 1}.get(channel, 2)

# HW from environment name
pioenv = (env.get('PIOENV') or '').upper()
if 'GEN1' in pioenv:
    hw = 'GEN1'
elif 'HUB' in pioenv:
    hw = 'HUB'
else:
    hw = 'GEN2'

proto = int(os.getenv('PROTO_VER', '2'))
min_proto = int(os.getenv('MIN_PROTO', str(proto)))

if not sv:
    sv = '0.0.0-dev'
    raw_tag = 'v0.0.0-dev'

content = f"""
#pragma once
#define FW_SEMVER        "{sv}"
#define FW_VER_MAJOR     {maj}
#define FW_VER_MINOR     {minor}
#define FW_VER_PATCH     {patch}
#define FW_BUILD         {build_int}
#define GIT_SHA          "{sha}"
#define GIT_DIRTY        {dirty}
#define PROTO_VER        {proto}
#define MIN_PROTO        {min_proto}
#define HW_REV           "{hw}"
#define RELEASE_CHANNEL  "{channel}"
#define RELEASE_TRACK    {track}
#define RELEASE_TAG      "{raw_tag}"
""".strip() + "\n"

out_path.write_text(content)
print(f"[gen_version_header] Wrote {out_path} :: {sv} {channel} build={build_int or sha}")
