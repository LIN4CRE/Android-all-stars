# Third-party notices

What SMAS Mobile is built from, apart from its own code, and under which
licences. "In the app" says whether the component ends up in the Android app.

| Component | Version | Licence | In this repository | In the app |
| --- | --- | --- | --- | --- |
| SuperMarioAllStars (Darhkfox) | 1.1.1 | MIT | `src/`, `android/` (the base of this project) | yes |
| SDL | 2.32.10 | zlib | no: the Android build downloads the official source release | yes |
| Luckiest Guy (font) | 1.001 | Apache-2.0 | `third_party/fonts/` | yes |
| stb_image | 2.30 | public domain or MIT | `third_party/stb/stb_image.h` | yes |
| stb_truetype | 1.28 | public domain or MIT | `third_party/stb/stb_truetype.h` | no: only the host preview tool |

## SuperMarioAllStars

Copyright (c) 2026 Darhkfox. MIT License: the full text is in
[LICENSE](LICENSE). See [NOTICE.md](NOTICE.md) and
[docs/UPSTREAM.md](docs/UPSTREAM.md) for what was taken from it and what was
changed.

## SDL (Simple DirectMedia Layer)

<https://www.libsdl.org/>. Window, input, audio and the Android glue. Its
source is not kept in this repository; the Android build fetches release
2.32.10 and compiles it.

```
Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

## Luckiest Guy

The font of the app's titles, `third_party/fonts/LuckiestGuy-Regular.ttf`,
unmodified.

Copyright (c) 2010 by Brian J. Bonislawsky DBA Astigmatic (AOETI). Licensed
under the Apache License, Version 2.0: the full text is in
[third_party/fonts/LICENSE.txt](third_party/fonts/LICENSE.txt). "Luckiest Guy"
is a trademark of Astigmatic (AOETI).

## stb_image and stb_truetype

<https://github.com/nothings/stb>, by Sean Barrett and contributors. Each is
one header file, under either the MIT License or the public domain (the
Unlicense) at the user's choice; the full text is at the end of each file.

* `stb_image.h` decodes pictures, and its decompressor reads the app's backup
  files.
* `stb_truetype.h` is used only by the preview tool in `tests/`, which draws
  the app's screens on a desktop. It is not compiled into the app.
