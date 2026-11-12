# Notice

SMAS Mobile is an unofficial Android app for playing your own copy of
Super Mario All-Stars (USA). This repository holds its source code.

## Where the code comes from

SMAS Mobile is built on **SuperMarioAllStars** by **Darhkfox**
(<https://github.com/Darhkfox/SuperMarioAllStars>), version 1.1.1
(commit `0c49e5eec9ee6b33f72a30b904bf17bb19d21ccf`), which is published under
the MIT License, Copyright (c) 2026 Darhkfox.

The upstream files were brought in unchanged in one commit
("import: upstream SuperMarioAllStars v1.1.1, the Android subset"), so that
everything after it in the history is what SMAS Mobile changed. The details
are in [docs/UPSTREAM.md](docs/UPSTREAM.md).

The changes and additions made for SMAS Mobile are Copyright (c) 2026 LIN4CRE
and are published under the same MIT License. The full text is in
[LICENSE](LICENSE).

SMAS Mobile is an independent adaptation. It is not the upstream project, and
its author is not responsible for it and does not support it. Please do not
report problems with SMAS Mobile to the upstream project.

## Third-party components

The other components used, and their licences, are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Nintendo

SMAS Mobile is not a Nintendo product. It is not affiliated with, endorsed,
sponsored or approved by Nintendo. "Super Mario All-Stars", "Super Mario
Bros." and "Mario" are trademarks of Nintendo; they are named only to say what
the app is compatible with.

This repository contains no game: no ROM image, and no code, graphics, music
or other data taken from one. The app runs a game file that the player
supplies. None may be added here: `tools/repo_guard.py` checks every change
for game files, save files, signing keys and credentials.
