# Loader integration fixture

These two minimal x64 programs provide a safe end-to-end test for `Loader.exe` without injecting the gameplay DLL into an unrelated application.

- `LoaderTestHost.exe` creates a per-process named event and waits for the DLL.
- `LoaderTestDll.dll` signals that event from its process-attach entry point.

The production loader is run with `--process LoaderTestHost.exe`, `--dll LoaderTestDll.dll`, and `--delay 0`. A passing test requires both the loader's remote-module verification and the host's DLL signal to succeed.
