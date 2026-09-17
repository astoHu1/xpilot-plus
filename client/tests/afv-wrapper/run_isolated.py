"""Run only the offline test executable with disposable settings/log directories."""
import os
import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="afv-wrapper-test-") as root:
    env = dict(os.environ, HOME=root, USERPROFILE=root,
               XDG_DATA_HOME=root, APPDATA=root, LOCALAPPDATA=root,
               AFV_TEST_HOME=root, QT_QPA_PLATFORM="offscreen", QSG_RHI_BACKEND="software")
    result = subprocess.run([str(pathlib.Path(sys.argv[1]).resolve()), *sys.argv[2:]],
                            cwd=root, env=env)
    sys.exit(result.returncode)
