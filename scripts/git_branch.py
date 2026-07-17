"""
PlatformIO pre-build script: inject git branch/version info as POCKET_CRYPTID_VERSION,
e.g. "0.1.0-dev+claude/pocket-cryptid-firmware-a7td6q".
"""

import configparser
import os
import re
import subprocess
import sys


def warn(msg):
    print(f'WARNING [git_branch.py]: {msg}', file=sys.stderr)


def run_git_value(project_dir, args, label):
    try:
        value = subprocess.check_output(
            ['git', *args],
            text=True, stderr=subprocess.PIPE, cwd=project_dir
        ).strip()
        return ''.join(c for c in value if c not in '"\\')
    except FileNotFoundError:
        warn(f'git not found on PATH; {label} suffix will be "unknown"')
        return 'unknown'
    except subprocess.CalledProcessError as e:
        warn(f'git command failed (exit {e.returncode}): {e.stderr.strip()}; {label} suffix will be "unknown"')
        return 'unknown'
    except Exception as e:  # pylint: disable=broad-exception-caught
        warn(f'Unexpected error reading git {label}: {e}; {label} suffix will be "unknown"')
        return 'unknown'


def sanitize_version_component(value):
    value = value.strip()
    value = re.sub(r'[^A-Za-z0-9._-]+', '-', value)
    value = re.sub(r'-{2,}', '-', value)
    value = value.strip('-.')
    return value or 'unknown'


def get_git_branch(project_dir):
    branch = run_git_value(project_dir, ['rev-parse', '--abbrev-ref', 'HEAD'], 'branch')
    if branch == 'HEAD':
        return 'detached'
    return sanitize_version_component(branch)


def get_base_version(project_dir):
    ini_path = os.path.join(project_dir, 'platformio.ini')
    config = configparser.ConfigParser()
    if os.path.isfile(ini_path):
        config.read(ini_path)
    if not config.has_option('pocketcryptid', 'version'):
        warn('No [pocketcryptid] version in platformio.ini; base version will be "0.0.0"')
        return '0.0.0'
    return config.get('pocketcryptid', 'version')


def inject_version(env):
    project_dir = env['PROJECT_DIR']
    base_version = get_base_version(project_dir)
    branch = get_git_branch(project_dir)
    version_string = f'{base_version}-dev+{branch}'
    env.Append(CPPDEFINES=[('POCKET_CRYPTID_VERSION', f'\\"{version_string}\\"')])
    print(f'Pocket Cryptid build version: {version_string}')


try:
    Import('env')  # noqa: F821  # type: ignore[name-defined]
except NameError:
    class _Env(dict):
        def Append(self, **_): pass

    if '__file__' in globals():
        _project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    else:
        _project_dir = os.getcwd()
    inject_version(_Env({'PROJECT_DIR': _project_dir}))
else:
    inject_version(env)  # noqa: F821  # type: ignore[name-defined]
