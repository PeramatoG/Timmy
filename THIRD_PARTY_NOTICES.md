# Third-Party Notices

TIMMY uses the following third-party software. These notices are provided for
attribution and do not alter TIMMY's own license.

## miniaudio

- **Project:** miniaudio
- **Copyright:** Copyright 2025 David Reid
- **Source:** <https://github.com/mackron/miniaudio>
- **License:** Public Domain (Unlicense) or MIT No Attribution, at the user's
  option
- **Use in TIMMY:** Audio device enumeration and capture

The complete license text distributed with the vendored source is available at
[`third_party/miniaudio/LICENSE`](third_party/miniaudio/LICENSE).

## whisper.cpp and ggml

- **Project:** whisper.cpp, including its bundled ggml implementation
- **Copyright:** Copyright 2023-2026 The ggml authors
- **Source:** <https://github.com/ggml-org/whisper.cpp>
- **Version:** v1.9.4, commit `927cfce34f31707e17f2bff35c349632fb9e2c3a`
- **License:** MIT
- **Use in TIMMY:** Local CPU speech recognition and tensor computation

The dependency source is retrieved and hash-verified by CMake; model weights
are not included. The complete license text is available in the upstream
source archive at `LICENSE` and from the project source link above.

## wxWidgets

- **Project:** wxWidgets
- **Copyright:** wxWidgets contributors
- **Source:** <https://www.wxwidgets.org/>
- **License:** wxWindows Library Licence, Version 3.1
- **Use in TIMMY:** Optional desktop graphical frontend

wxWidgets is an external build dependency and is not vendored in this
repository. Its license permits distribution of applications in binary form
under the application's own terms, subject to the wxWindows Library Licence.
The full license text is available from the wxWidgets project at
<https://www.wxwidgets.org/about/licence/>.
