# Third-party notices

Omarchy Replay's own code is licensed under the [MIT License](LICENSE). Dependencies and protocol definitions retain their original licenses.

## Distribution boundary

The source tree does not vendor library implementations, OCR models or protocol XML. The build uses system development packages. `wayland-scanner` generates four protocol clients in the ignored build directory; that generated code is compiled into Replay. The notices below accompany those clients, toml++ headers and the Hyprland lock-notification protocol used by the handwritten wire declarations.

The supported Arch build links shared system libraries. It does not copy their shared objects or Tesseract language data into the source tree. `tesseract-data-eng` supplies the English model. Experimental Python OCR dependencies and downloaded models under `runs/` are outside the installed recorder and are not release assets.

Keep this file and `LICENSE` with source and binary distributions. A future bundle that includes libraries, platform plugins, command-line tools or models needs an inventory and license materials for the exact artifacts included. This file is not a complete inventory of every transitive system dependency.

## System dependencies

| Component | Use | Upstream licensing reference |
| --- | --- | --- |
| Qt 6 Core, Gui, Widgets, Concurrent, Network, DBus; Wayland platform plugin | Application and desktop UI; Qt Test is used by tests | [Qt licensing](https://doc.qt.io/qt-6/licensing.html): these open-source modules are available under LGPLv3 or GPLv3; Qt also offers commercial licensing. Individual Qt third-party components retain their own terms. |
| toml++ | Configuration parsing; system shared library and C++ headers | [MIT](https://github.com/marzer/tomlplusplus/blob/master/LICENSE); full notice below. |
| Tesseract and English trained data | Local OCR | [Tesseract Apache-2.0](https://github.com/tesseract-ocr/tesseract/blob/main/LICENSE), [tessdata Apache-2.0](https://github.com/tesseract-ocr/tessdata/blob/main/LICENSE). |
| Leptonica | OCR image processing | [BSD-2-Clause](https://github.com/DanBloomberg/leptonica/blob/master/leptonica-license.txt). |
| SQLite | Local history and text index | [Public-domain dedication](https://sqlite.org/copyright.html). |
| libwebp | Image encoding and decoding | [BSD-3-Clause](https://github.com/webmproject/libwebp/blob/main/COPYING). |
| Wayland client and wayland-protocols | Capture and idle notifications | [Wayland MIT](https://gitlab.freedesktop.org/wayland/wayland/-/blob/main/COPYING); generated protocol notices below. |
| GCC libstdc++ and libgomp | C++ and OpenMP runtime in the Arch build | [GPLv3 with GCC Runtime Library Exception 3.1](https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html). |
| libarchive, libcurl, libglvnd and glibc | System dependencies included in the current linker inputs | [libarchive](https://github.com/libarchive/libarchive/blob/master/COPYING), [curl](https://curl.se/docs/copyright.html), [libglvnd](https://github.com/NVIDIA/libglvnd/blob/master/README.md#license), [glibc](https://sourceware.org/glibc/). Their installed packages carry component-specific notices. |

FFmpeg is invoked as a separate system program for legacy video history. Its [license depends on its build configuration](https://ffmpeg.org/legal.html); Replay does not ship FFmpeg. Hyprland, systemd, Python and optional `grim` are also external system programs. Synthetic desktop checks additionally use installed `mpv`. Installing or calling these programs does not change their licenses.

## Notices for generated and included interfaces

The following Wayland notices were checked against wayland-protocols 1.49. Generated files preserve their corresponding notice. Recheck these texts when changing protocol versions.

### ext-image-copy-capture-v1

[Protocol definition](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/1.49/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml).

```text
Copyright © 2021-2023 Andri Yngvason
Copyright © 2024 Simon Ser

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

### ext-image-capture-source-v1

[Protocol definition](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/1.49/staging/ext-image-capture-source/ext-image-capture-source-v1.xml).

```text
Copyright © 2022 Andri Yngvason
Copyright © 2024 Simon Ser

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

### ext-foreign-toplevel-list-v1

[Protocol definition](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/1.49/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml).

```text
Copyright © 2018 Ilia Bozhinov
Copyright © 2020 Isaac Freund
Copyright © 2022 wb9688
Copyright © 2023 i509VCB

Permission to use, copy, modify, distribute, and sell this
software and its documentation for any purpose is hereby granted
without fee, provided that the above copyright notice appear in
all copies and that both that copyright notice and this permission
notice appear in supporting documentation, and that the name of
the copyright holders not be used in advertising or publicity
pertaining to distribution of the software without specific,
written prior permission.  The copyright holders make no
representations about the suitability of this software for any
purpose.  It is provided "as is" without express or implied
warranty.

THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.
```

### ext-idle-notify-v1

[Protocol definition](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/1.49/staging/ext-idle-notify/ext-idle-notify-v1.xml).

```text
Copyright © 2015 Martin Gräßlin
Copyright © 2022 Simon Ser

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

### hyprland-lock-notify-v1

`src/recording_environment.cpp` declares the wire interfaces directly; it does not vendor the XML or generated headers. The [upstream protocol](https://github.com/hyprwm/hyprland-protocols/blob/main/protocols/hyprland-lock-notify-v1.xml) carries this notice.

```text
Copyright © 2025 Maximilian Seidler
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### toml++

The Arch build uses the system shared library; included template and inline code retains the same [MIT license](https://github.com/marzer/tomlplusplus/blob/v3.4.0/LICENSE).

```text
MIT License

Copyright (c) Mark Gillard <mark.gillard@outlook.com.au>

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```
