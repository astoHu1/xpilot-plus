# CI builds and packaging

Run **Build xPilot** with Actions' branch selector, or push `codex/**`.
Pull requests and master pushes also run it. The reusable `offline-tests`
and `package-checks` jobs must pass before the three platform builds start.
The platform workflows may also be dispatched individually for development.

Ordinary/manual builds require no repository secrets. They explicitly enable
`XPILOT_ALLOW_AUTH_STUB=ON`, use non-secret development configuration, and upload
`*.zip-development-unverified` artifacts containing `BUILD-STATUS.txt`.
These clients cannot authenticate to VATSIM. They do not produce installers.
For local development using the public library, pass the same CMake flag.

Only a pushed `v*` tag enables release packaging. Missing prerequisites fail
before building; there is no fallback to the stub or unsigned release packages.
Configure repository variable `VATSIM_AUTH_REF` with the approved, full 40-digit
commit of `xpilot-project/vatsim-auth`. Set repository secret
`PERSONAL_ACCESS_TOKEN` to a token with read access to that private repository.
The token is optional for development and only used by release checkout, with
credential persistence disabled. No local credential file is used.

Release prerequisites on every platform are `INSTALLBUILDER_LICENSE`,
`VATSIM_CLIENT_ID`, `VATSIM_CLIENT_KEY`, `VATSIM_TOWERVIEW_CLIENT_ID`, and
`CONFIG_ENCRYPTION_KEY`. The config encryption key is an unsigned integer, not
a string. macOS also requires `APPLE_SIGNING_CERTIFICATE` (base64 PKCS#12),
`APPLE_SIGNING_CERTIFICATE_PASSWORD`, `APPLE_SIGNING_CERTIFICATE_IDENTITY`,
`APPLE_TEAM_ID`, `NOTARIZATION_USERNAME`, and `NOTARIZATION_PASSWORD`.
Secrets are exposed only to the relevant steps. Certificates are imported after
compilation, and the temporary certificate, license, and keychain are cleaned up.

Windows deploys shared Qt/QML and MSVC DLLs into the same directory consumed
by the installer and build artifact. Linux builds an AppImage with shared Qt
and QML dependencies using linuxdeploy. Tool release URLs and SHA-256 hashes
are pinned in `appimage.sh`; the linuxdeploy hash is also recorded by GitHub's
release asset digest. The Qt plugin digest was computed from its release asset.
The old vendored AppRun/appimagetool binaries are no longer used.

macOS removes absolute RPATHs from every Mach-O slice before signing nested
code and the outer bundle. Verification is **static only**: it checks required
architectures, portable/resolvable dependencies and RPATHs, symlinks, and code
signatures. Development permits ad-hoc signatures; releases require hardened
Developer ID signatures from the configured Apple team. Both the deployed
bundle and extracted archive are checked. Nothing launches the client, simulator,
or a VATSIM/network test. Static success is not evidence of runtime or network
compatibility.

Run offline packaging regressions with:

```sh
python3 -m unittest discover -s scripts -p 'test_*.py' -v
```

macOS tests compile tiny synthetic universal Mach-O fixtures and inspect/sign
them; they never execute the fixtures. They exercise paths with spaces, mixed
architectures, per-slice RPATH cleanup, missing dependencies, and invalid
signatures. Other platforms run the policy/parser regressions and skip tests
requiring Apple's tools.
