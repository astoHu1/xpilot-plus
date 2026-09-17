# Offline AFV wrapper tests

These compile the production `afv.cpp`, `appconfig.cpp`, and load the production
`SettingsAudio.qml`. Native AFV, libevent dispatch, simulator and network managers
are observable doubles: no real client, socket, audio device or VATSIM session is
started. The test launcher uses a disposable home/data directory; a test-only
QStandardPaths shim routes settings there even on macOS, where native standard
paths may ignore HOME. The executable refuses non-isolated configuration paths.
The QML component is invisible and uses Qt's offscreen platform.

Dependencies: CMake 3.21+, C++17, Python 3, Qt 6.5+ Core/Gui/Network/Qml/Quick/Test,
and the corresponding QML modules (Quick Controls, Layouts, Shapes, Models).
No Qt Multimedia or native audio/network libraries are needed.

```sh
cmake -S client/tests/afv-wrapper -B /tmp/afv-wrapper-tests -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build /tmp/afv-wrapper-tests --parallel
ctest --test-dir /tmp/afv-wrapper-tests --output-on-failure
```

Native races/device teardown still require the separate native tests/sanitizers;
these doubles check wrapper ordering, ownership, settings and QML behavior.
