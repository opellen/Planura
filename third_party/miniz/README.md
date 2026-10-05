# Vendored miniz

Vendored for v1.M11 (`docs/DESIGN.md` decision 2's ZIP
container work: `.plr`'s reserved `PK` sniff branch, `io::assembleContainer`/
`io::openContainer`, `src/app/io/plr_reader.cpp`'s `Container::Zip` case).

## Provenance

- **Upstream**: <https://github.com/richgel999/miniz.git>
- **Commit**: `77d0dce8627735138c51770d1799a1ef48f2117d`
- **Version**: 3.1.2 (per `miniz.h`'s own header comment / the commit's own
  "Increment version and update changelog" message)
- **License**: public domain -- each vendored `.c`/`.h` file carries its own
  Unlicense statement verbatim at its end (`<http://unlicense.org/>`); the
  upstream repository's own `LICENSE` file is MIT. Neither statement was
  edited when copying these files in.
- **Same commit openskp's package pins via CMake `FetchContent`**: this repo's
  sibling C++ project, `packages/cpp/CMakeLists.txt` in
  `C:\Users\sante\Documents\03_Resources\Code\graphics\openskp`, declares
  `FetchContent_Declare(miniz GIT_REPOSITORY https://github.com/richgel999/miniz.git
  GIT_TAG 77d0dce8627735138c51770d1799a1ef48f2117d ...)` -- the exact same
  upstream commit, fetched over the network at CMake configure time rather
  than vendored as files in that repo. `planura` vendors the files
  physically instead (this directory), consistent with `DESIGN.md` decision
  2's "vendored miniz ... under `third_party/miniz/`" and this repo's own
  convention of not depending on network access at configure time for a
  production (non-test) build dependency -- `tests/CMakeLists.txt`'s
  `FetchContent`-based `googletest` is a test-only dependency, not a
  precedent for production code.

## Files

Copied verbatim from the commit above, license statements intact:

- `miniz.h` / `miniz.c` -- top-level umbrella header/source (zlib-subset API,
  PNG writing helpers)
- `miniz_common.h` -- shared macros/typedefs
- `miniz_tdef.h` / `miniz_tdef.c` -- the "tdefl" compressor
- `miniz_tinfl.h` / `miniz_tinfl.c` -- the "tinfl" decompressor
- `miniz_zip.h` / `miniz_zip.c` -- the ZIP archive reader/writer API this repo
  actually uses (`mz_zip_reader_*`/`mz_zip_writer_*`)

`miniz_export.h` is NOT part of the upstream project -- it is a tiny local
shim (mirroring openskp's own generated `miniz_export.h.in`) that `#define`s
the `MINIZ_EXPORT` macro every miniz header uses to empty, since `plnr_miniz`
is always linked as a static library (never built as a shared library that
would need real dllexport/dllimport visibility control). `miniz.pc.in`
(upstream's pkg-config template) was not copied -- nothing here uses
pkg-config.

## Build

`CMakeLists.txt` in this directory builds a static library target `plnr_miniz`
from `miniz.c`/`miniz_tdef.c`/`miniz_tinfl.c`/`miniz_zip.c` (mirrors which
four `.c` files openskp's own `OPENSKP_MINIZ_SOURCES` list builds), consumed
by `src/app/io/CMakeLists.txt` (`plnr_io` links it privately -- the ZIP
container is an `plnr_io`-internal implementation detail, not exposed in any
`plnr_io` public header).
