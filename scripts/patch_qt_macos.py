#!/usr/bin/env python3
"""Remove Qt 6.5.2's obsolete AGL link flag, asserting the known input."""
from pathlib import Path
import re
import sys

library_dir = Path(sys.argv[1])
finder = library_dir / 'cmake/Qt6/FindWrapOpenGL.cmake'
source = finder.read_text()
marker = '# xPilot: obsolete AGL link removed for current macOS SDKs.'
pattern = r'^\s*target_link_libraries\(WrapOpenGL::WrapOpenGL INTERFACE \$\{__opengl_agl_fw_path\}\)\s*$'
patched, count = re.subn(pattern, '\n' + marker, source, flags=re.M)
if count != 1 and not (count == 0 and marker in source):
    raise SystemExit('Unexpected Qt FindWrapOpenGL.cmake: expected exactly one AGL link directive')
finder.write_text(patched)
for prl in library_dir.glob('*.prl'):
    prl.write_text(prl.read_text().replace('-framework AGL', ''))
