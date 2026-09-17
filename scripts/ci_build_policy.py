#!/usr/bin/env python3
"""Fail closed for releases; explicitly label public stub build artifacts."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
REQUIRED = ('HAS_AUTH_TOKEN', 'HAS_INSTALLER_LICENSE', 'HAS_CLIENT_ID',
            'HAS_CLIENT_KEY', 'HAS_TOWERVIEW_ID', 'HAS_CONFIG_KEY')
MAC_REQUIRED = ('HAS_CERTIFICATE', 'HAS_CERTIFICATE_PASSWORD', 'HAS_SIGNING_IDENTITY',
                'HAS_NOTARIZATION_USERNAME', 'HAS_NOTARIZATION_PASSWORD', 'HAS_TEAM_ID')


def preflight(platform):
    release = os.environ.get('RELEASE_BUILD') == 'true'
    if release:
        missing = [key for key in REQUIRED + (MAC_REQUIRED if platform == 'macos' else ())
                   if os.environ.get(key) != 'true']
        if missing:
            raise ValueError('Release prerequisites missing: ' + ', '.join(missing))
        if not re.fullmatch(r'[0-9a-fA-F]{40}', os.environ.get('VATSIM_AUTH_REF', '')):
            raise ValueError('Set repository variable VATSIM_AUTH_REF to the approved real auth commit (40 hex characters)')
    with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as output:
        output.write('artifact_suffix=' + ('' if release else '-development-unverified') + '\n')
        output.write('allow_auth_stub=' + ('OFF' if release else 'ON') + '\n')
    message = ('Release prerequisites present; real auth checkout must still be verified.' if release else
               'UNVERIFIED DEVELOPMENT BUILD: public auth stub; cannot authenticate to VATSIM. No client/network test is performed.')
    print(message)
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as summary:
            summary.write(message + '\n')


def manifest(output):
    auth = ROOT / 'dependencies/vatsim-auth'
    release = os.environ.get('RELEASE_BUILD') == 'true'
    if release:
        revision = subprocess.check_output(['git', '-C', str(auth), 'rev-parse', 'HEAD'], text=True).strip()
        if revision.lower() != os.environ['VATSIM_AUTH_REF'].lower():
            raise ValueError('Real auth checkout does not match the approved commit')
        source = (auth / 'src/vatsimauth.cpp').read_bytes()
        stub = subprocess.check_output(['git', '-C', str(ROOT), 'show', 'HEAD:dependencies/vatsim-auth/src/vatsimauth.cpp'])
        if hashlib.sha256(source).digest() == hashlib.sha256(stub).digest():
            raise ValueError('Release still contains the public auth stub')
        message = 'RELEASE BUILD: approved private auth source at ' + revision
    else:
        message = 'UNVERIFIED DEVELOPMENT BUILD: public auth stub; VATSIM authentication is unavailable.'
    Path(output).write_text(message + '\nStatic/build checks only; no client launch or VATSIM connection was tested.\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('preflight').add_argument('platform', choices=('windows', 'linux', 'macos'))
    sub.add_parser('manifest').add_argument('output')
    args = parser.parse_args()
    try:
        if args.command == 'preflight':
            preflight(args.platform)
        else:
            manifest(args.output)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{error}\n')
