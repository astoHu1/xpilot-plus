#!/usr/bin/env python3
"""Deploy-time Mach-O cleanup and strictly static bundle verification. Never launches code."""
import argparse
import os
from pathlib import Path
import plistlib
import re
import subprocess
import tempfile

MACHO_MAGIC = {bytes.fromhex(value) for value in (
    'feedface', 'cefaedfe', 'feedfacf', 'cffaedfe',
    'cafebabe', 'bebafeca', 'cafebabf', 'bfbafeca')}
LOAD_DYLIB = {'LC_LOAD_DYLIB', 'LC_LOAD_WEAK_DYLIB', 'LC_REEXPORT_DYLIB',
              'LC_LOAD_UPWARD_DYLIB', 'LC_LAZY_LOAD_DYLIB'}


def run(*args):
    result = subprocess.run([str(arg) for arg in args], check=True, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return result.stdout + result.stderr


def inside(path, bundle):
    path = path.resolve(strict=True)
    if not path.is_relative_to(bundle):
        raise ValueError(f'Path escapes bundle: {path}')
    return path


def bundle_files(bundle):
    def fail(error):
        raise error
    for directory, dirs, files in os.walk(bundle, onerror=fail):
        for name in dirs + files:
            path = Path(directory) / name
            if path.is_symlink():
                inside(path, bundle)
        for name in files:
            path = Path(directory) / name
            if not path.is_symlink():
                yield path


def macho_files(bundle):
    # Read magic bytes ourselves: unreadable files must fail, not become non-Mach-O.
    result = []
    for path in bundle_files(bundle):
        with path.open('rb') as file:
            if file.read(4) in MACHO_MAGIC:
                result.append(path)
    if not result:
        raise ValueError('No Mach-O files found')
    return result


def executable(bundle):
    with (bundle / 'Contents/Info.plist').open('rb') as file:
        name = plistlib.load(file)['CFBundleExecutable']
    if not isinstance(name, str) or not name or Path(name).name != name:
        raise ValueError('Invalid CFBundleExecutable')
    path = inside(bundle / 'Contents/MacOS' / name, bundle)
    if not path.is_file() or not os.access(path, os.X_OK):
        raise ValueError(f'Bundle executable is missing or not executable: {path}')
    return path


def architectures(path):
    result = run('lipo', '-archs', path).strip().split()
    if not result or any(arch not in ('x86_64', 'arm64', 'arm64e', 'i386') for arch in result):
        raise ValueError(f'Invalid architecture list for {path}: {result}')
    return result


def parse_commands(text):
    """Preserve paths with spaces; LC_ID_DYLIB is an identity, not a dependency."""
    rpaths, dependencies = [], []
    for block in re.split(r'^Load command \d+\s*$', text, flags=re.M)[1:]:
        command = re.search(r'^\s*cmd (LC_\w+)\s*$', block, re.M)
        if not command:
            raise ValueError('Malformed otool load command')
        command = command[1]
        if command != 'LC_RPATH' and command not in LOAD_DYLIB:
            continue
        field = 'path' if command == 'LC_RPATH' else 'name'
        match = re.search(r'^\s*' + field + r' (.+) \(offset \d+\)\s*$', block, re.M)
        if not match:
            raise ValueError(f'Malformed {command} path')
        (rpaths if command == 'LC_RPATH' else dependencies).append(match[1])
    if not re.search(r'^Load command \d+', text, re.M):
        raise ValueError('otool returned no load commands')
    return rpaths, dependencies


def commands(path, arch):
    return parse_commands(run('otool', '-arch', arch, '-l', path))


def clean_rpaths(bundle):
    main = executable(bundle)
    for path in macho_files(bundle):
        archs = architectures(path)
        edits = {}
        for arch in archs:
            rpaths, _ = commands(path, arch)
            options = []
            for rpath in dict.fromkeys(rpaths):
                # Qt builds can also leave @loader_path/../../lib fallbacks
                # that escape the deployed bundle, or nonexistent directories.
                # They are not useful deployment paths and can load host code.
                portable = False
                if not rpath.startswith('/'):
                    try:
                        resolved = expand_path(rpath, path, main).resolve(strict=True)
                        portable = resolved.is_dir() and resolved.is_relative_to(bundle)
                    except (ValueError, OSError):
                        pass
                if not portable:
                    options.extend(('-delete_rpath', rpath))
            if path == main and '@executable_path/../Frameworks' not in rpaths:
                options.extend(('-add_rpath', '@executable_path/../Frameworks'))
            edits[arch] = options
        if not any(edits.values()):
            continue
        if all(options == edits[archs[0]] for options in edits.values()):
            run('install_name_tool', *edits[archs[0]], path)
        else:
            # Different slices can carry different RPATHs. Editing the fat file
            # directly fails when a requested RPATH is absent from one slice.
            with tempfile.TemporaryDirectory(prefix='xpilot-rpaths-') as temporary:
                slices = []
                for arch, options in edits.items():
                    thin = Path(temporary) / arch
                    run('lipo', path, '-thin', arch, '-output', thin)
                    if options:
                        run('install_name_tool', *options, thin)
                    slices.append(thin)
                run('lipo', '-create', *slices, '-output', path)


def sign(bundle, entitlements, identity):
    options = ['--force', '--sign', identity or '-']
    if identity:
        options += ['--options', 'runtime', '--timestamp']
    # Every changed Mach-O is re-signed. Seal nested bundles inside out; never
    # use codesign --deep for signing (it can omit code in nonstandard places).
    main = executable(bundle)
    for path in sorted(macho_files(bundle), key=lambda p: len(p.parts), reverse=True):
        if path == main:
            continue  # Signing this path implicitly seals the outer bundle.
        run('codesign', *options, path)
    nested = [path for path in bundle.rglob('*')
              if path.is_dir() and not path.is_symlink()
              and path.suffix in ('.framework', '.app', '.xpc', '.bundle')]
    for path in sorted(nested, key=lambda p: len(p.parts), reverse=True):
        run('codesign', *options, path)
    run('codesign', *options, '--entitlements', entitlements, bundle)


def expand_path(value, loader, main):
    for token, base in (('@loader_path', loader.parent), ('@executable_path', main.parent)):
        if value == token or value.startswith(token + '/'):
            return base / value[len(token):].lstrip('/')
    raise ValueError(f'Unsupported or non-portable runtime path: {value}')


def verify(bundle, expected_archs, signature, team_id):
    main = executable(bundle)
    for relative in ('Contents/Frameworks', 'Contents/PlugIns/platforms/libqcocoa.dylib',
                     'Contents/Resources/qml/QtQuick'):
        inside(bundle / relative, bundle)
    files = macho_files(bundle)
    if main not in files:
        raise ValueError('Bundle executable is not Mach-O')
    arch_map = {path: architectures(path) for path in files}
    for path, archs in arch_map.items():
        if not set(expected_archs).issubset(archs):
            raise ValueError(f'{path} has {archs}; requires {expected_archs}')
    metadata = {(path, arch): commands(path, arch) for path in files for arch in expected_archs}
    for (path, arch), (rpaths, dependencies) in metadata.items():
        own_paths = [inside(expand_path(value, path, main), bundle) for value in rpaths]
        main_paths = [inside(expand_path(value, main, main), bundle)
                      for value in metadata[main, arch][0]]
        for dependency in dependencies:
            if dependency.startswith(('/System/Library/', '/usr/lib/')):
                continue  # Apple libraries can live exclusively in the dyld cache.
            if dependency.startswith('@rpath/'):
                choices = [root / dependency[len('@rpath/'):] for root in own_paths + main_paths]
            else:
                choices = [expand_path(dependency, path, main)]
            targets = [inside(candidate, bundle) for candidate in choices if candidate.is_file()]
            if not targets:
                raise ValueError(f'{path} ({arch}): unresolved dependency {dependency}')
            # dyld uses the first existing candidate, not any later compatible one.
            if arch not in arch_map.get(targets[0], []):
                raise ValueError(f'{path} ({arch}): dependency lacks architecture: {targets[0]}')
    if signature == 'developer-id' and not re.fullmatch(r'[A-Z0-9]{10}', team_id):
        raise ValueError('Developer ID verification requires --team-id (10 uppercase letters/digits)')
    requirement = ('anchor apple generic and certificate leaf[subject.OU] = "' + team_id +
                   '" and certificate leaf[field.1.2.840.113635.100.6.1.13] exists')
    for path in files:
        args = ['codesign', '--verify', '--strict', '--all-architectures']
        if signature == 'developer-id':
            args += ['-R', requirement]
        run(*args, path)
        if signature == 'developer-id':
            for arch in expected_archs:
                details = run('codesign', '--display', '--verbose=4', '--arch', arch, path)
                if ('Signature=adhoc' in details or 'Authority=Developer ID Application:' not in details
                        or f'TeamIdentifier={team_id}\n' not in details
                        or not re.search(r'^CodeDirectory .*flags=.*\bruntime\b', details, re.M)):
                    raise ValueError(f'{path} ({arch}): expected hardened Developer ID signature for {team_id}')
    run('codesign', '--verify', '--deep', '--strict', '--all-architectures', bundle)
    label = 'Developer ID' if signature == 'developer-id' else 'development (ad-hoc signatures allowed)'
    print(f'Statically verified {len(files)} Mach-O files; architectures: {", ".join(expected_archs)}; {label}. No application was launched.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    package = sub.add_parser('clean-sign')
    package.add_argument('bundle', type=Path)
    package.add_argument('entitlements', type=Path)
    package.add_argument('identity', nargs='?', default='')
    check = sub.add_parser('verify')
    check.add_argument('bundle', type=Path)
    check.add_argument('--architectures', nargs='+', default=['x86_64', 'arm64'])
    check.add_argument('--signature', choices=['development', 'developer-id'], default='development')
    check.add_argument('--team-id', default='')
    args = parser.parse_args()
    try:
        bundle = args.bundle.resolve(strict=True)
        if args.command == 'clean-sign':
            clean_rpaths(bundle)
            sign(bundle, args.entitlements.resolve(strict=True), args.identity)
        else:
            verify(bundle, args.architectures, args.signature, args.team_id)
    except (OSError, ValueError, KeyError, plistlib.InvalidFileException, subprocess.CalledProcessError) as error:
        detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else str(error)
        parser.exit(1, f'macOS bundle {args.command} failed: {detail}\n')


if __name__ == '__main__':
    main()
