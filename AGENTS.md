# Project conventions

Use English for code comments, user-visible strings, documentation, and commits.
Keep this project limited to the Spotify notification image compatibility shim.
Do not distribute Spotify binaries, decoded client resources, or generated libraries.
Do not pin support to release versions, executable hashes, Build IDs, or fixed RVAs.
Limit image decoding to the main executable's notification instruction pattern,
and image clearing to libnotify text updates originating from that executable.
Re-analyze changed notification behavior or instruction patterns and add regression tests.
Run `make test` for code changes; document decoder-dependent test prerequisites.
Keep build outputs and Python bytecode out of tracked files and install targets.
Installation and tests must not stop or restart the user's running Spotify.
