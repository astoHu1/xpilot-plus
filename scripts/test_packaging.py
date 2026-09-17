#!/usr/bin/env python3
"""Offline packaging regressions. Compiled fixtures are inspected, NEVER executed."""
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import ci_build_policy as policy
import macos_bundle as bundle


class PolicyTests(unittest.TestCase):
    def test_development_without_secrets(self):
        with tempfile.TemporaryDirectory() as root, patch.dict(os.environ, {
                'RELEASE_BUILD': 'false', 'GITHUB_OUTPUT': str(Path(root) / 'output')}, clear=True):
            policy.preflight('macos')
            output = (Path(root) / 'output').read_text()
            self.assertIn('allow_auth_stub=ON', output)
            self.assertIn('development-unverified', output)

    def test_releases_require_credentials_and_pinned_auth(self):
        with tempfile.TemporaryDirectory() as root:
            env = {'RELEASE_BUILD': 'true', 'GITHUB_OUTPUT': str(Path(root) / 'output')}
            with patch.dict(os.environ, env, clear=True):
                with self.assertRaisesRegex(ValueError, 'prerequisites missing'):
                    policy.preflight('macos')
            env.update({key: 'true' for key in policy.REQUIRED + policy.MAC_REQUIRED})
            with patch.dict(os.environ, env, clear=True):
                with self.assertRaisesRegex(ValueError, 'approved real auth commit'):
                    policy.preflight('macos')
            env['VATSIM_AUTH_REF'] = 'a' * 40
            with patch.dict(os.environ, env, clear=True):
                policy.preflight('macos')
            self.assertIn('allow_auth_stub=OFF', (Path(root) / 'output').read_text())

    def test_paths_with_spaces_and_dylib_id(self):
        text = '''fixture (architecture arm64):
Load command 0
          cmd LC_RPATH
      cmdsize 72
         path /path with spaces/Qt libs (offset 12)
Load command 1
          cmd LC_ID_DYLIB
      cmdsize 48
         name @rpath/own identity.dylib (offset 24)
Load command 2
          cmd LC_LOAD_DYLIB
      cmdsize 48
         name @rpath/another library.dylib (offset 24)
'''
        self.assertEqual(bundle.parse_commands(text), (['/path with spaces/Qt libs'], ['@rpath/another library.dylib']))
        with self.assertRaises(ValueError):
            bundle.parse_commands('otool failed without load commands')
        with self.assertRaises(ValueError):
            bundle.parse_commands('Load command 0\n cmd LC_RPATH\n invalid')

    def test_inspection_failure_is_not_swallowed(self):
        with patch.object(bundle.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'otool')):
            with self.assertRaises(subprocess.CalledProcessError):
                bundle.commands(Path('fixture'), 'arm64')


@unittest.skipUnless(sys.platform == 'darwin', 'requires macOS static binary tools')
class MacBundleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='xpilot static fixtures ')
        cls.root = Path(cls.temporary.name).resolve()
        cls.fixture = cls.root / 'Fixture App.app'
        contents = cls.fixture / 'Contents'
        for directory in ('MacOS', 'Frameworks', 'PlugIns/platforms', 'Resources/qml/QtQuick'):
            (contents / directory).mkdir(parents=True)
        cls.main = contents / 'MacOS/Fixture App'
        cls.library = contents / 'Frameworks/library with spaces.dylib'
        cls.entitlements = cls.root / 'entitlements.plist'
        cls.entitlements.write_bytes(plistlib.dumps({}))
        (contents / 'Info.plist').write_bytes(plistlib.dumps({
            'CFBundleExecutable': 'Fixture App', 'CFBundleIdentifier': 'org.xpilot.static-fixture',
            'CFBundlePackageType': 'APPL', 'CFBundleVersion': '1'}))
        (cls.root / 'lib.c').write_text('int fixture(void) { return 0; }\n')
        (cls.root / 'main.c').write_text('extern int fixture(void); int main(void) { return fixture(); }\n')
        bundle.run('clang', '-arch', 'arm64', '-arch', 'x86_64', '-dynamiclib', cls.root / 'lib.c',
                   '-Wl,-headerpad_max_install_names', '-Wl,-install_name,@rpath/library with spaces.dylib',
                   '-Wl,-rpath,/obsolete Qt path/with spaces', '-o', cls.library)
        bundle.run('clang', '-arch', 'arm64', '-arch', 'x86_64', cls.root / 'main.c', cls.library,
                   '-Wl,-headerpad_max_install_names', '-Wl,-rpath,/old build path/Qt libs', '-o', cls.main)
        shutil.copy2(cls.library, contents / 'PlugIns/platforms/libqcocoa.dylib')
        bundle.clean_rpaths(cls.fixture)
        bundle.sign(cls.fixture, cls.entitlements, '')

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.copy = tempfile.TemporaryDirectory(prefix='xpilot bundle test ')
        self.app = Path(self.copy.name).resolve() / 'Another App.app'
        shutil.copytree(self.fixture, self.app, symlinks=True)
        self.main = self.app / 'Contents/MacOS/Fixture App'
        self.library = self.app / 'Contents/Frameworks/library with spaces.dylib'

    def tearDown(self):
        self.copy.cleanup()

    def verify(self, signature='development'):
        bundle.verify(self.app, ['x86_64', 'arm64'], signature, 'ABCDEFGHIJ')

    def test_universal_with_spaces_and_all_rpaths_cleaned(self):
        self.verify()
        for path in bundle.macho_files(self.app):
            for arch in bundle.architectures(path):
                self.assertTrue(all(not r.startswith('/') for r in bundle.commands(path, arch)[0]))

    def test_slice_specific_rpath_cleanup(self):
        with tempfile.TemporaryDirectory() as temporary:
            slices = []
            for arch in ('arm64', 'x86_64'):
                thin = Path(temporary) / arch
                bundle.run('lipo', self.library, '-thin', arch, '-output', thin)
                bundle.run('install_name_tool', '-add_rpath', '/old path ' + arch, thin)
                slices.append(thin)
            bundle.run('lipo', '-create', *slices, '-output', self.library)
        bundle.clean_rpaths(self.app)
        bundle.sign(self.app, self.entitlements, '')
        self.verify()

    def test_missing_architecture_fails(self):
        thin = self.library.with_suffix('.thin')
        bundle.run('lipo', self.library, '-thin', 'arm64', '-output', thin)
        thin.replace(self.library)
        with self.assertRaisesRegex(ValueError, 'requires'):
            self.verify()

    def test_absolute_rpath_fails(self):
        bundle.run('install_name_tool', '-add_rpath', '/another Qt directory/with spaces', self.library)
        with self.assertRaisesRegex(ValueError, 'non-portable'):
            self.verify()

    def test_relative_build_fallbacks_are_removed(self):
        for path in (self.main, self.library):
            bundle.run('install_name_tool', '-add_rpath', '@loader_path/../../../../lib', path)
            bundle.run('install_name_tool', '-add_rpath', '@loader_path/missing Qt libs', path)
        bundle.clean_rpaths(self.app)
        bundle.sign(self.app, self.entitlements, '')
        self.verify()

    def test_missing_dependency_fails(self):
        self.library.unlink()
        with self.assertRaisesRegex(ValueError, 'unresolved dependency'):
            self.verify()

    def test_adhoc_cannot_pass_release(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.verify('developer-id')

    def test_broken_signature_fails(self):
        bundle.run('codesign', '--remove-signature', self.library)
        with self.assertRaises(subprocess.CalledProcessError):
            self.verify()

    def test_escaping_symlink_fails(self):
        (self.app / 'Contents/Resources/escape').symlink_to(self.root)
        with self.assertRaisesRegex(ValueError, 'escapes bundle'):
            self.verify()


if __name__ == '__main__':
    unittest.main()
