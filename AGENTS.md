# Project conventions

Use English for code comments, user-visible strings, documentation, and commits.
Keep this project limited to the Spotify notification image compatibility shim.
Do not distribute Spotify binaries, decoded client resources, or generated libraries.
Preserve the executable SHA-256, GNU Build ID, and exact call-site guards.
Changes to a supported version require fresh binary analysis and regression tests.
Run `make test` for code changes; document decoder-dependent test prerequisites.
Keep build outputs and Python bytecode out of tracked files and install targets.
Installation and tests must not stop or restart the user's running Spotify.
