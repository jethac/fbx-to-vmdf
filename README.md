# fbx-to-vmdf

An FBX → `.vmdf` model converter for the [Usagi engine](https://github.com/vitei/Usagi),
and a drop-in replacement for Ayataka, the engine's original converter.

Ayataka read `.fbx` files through the Autodesk FBX SDK (and, on Linux, an old
libxml2), which can't be redistributed and isn't available everywhere.
fbx-to-vmdf reads FBX with [ufbx](https://github.com/ufbx/ufbx) instead. It
has no dependencies outside this repository and builds with any C++17
compiler on Linux, Windows and macOS.

- **Same command line, same outputs.** It writes the `.vmdf` model, the
  `_build/skel/<name>.vmdf.xml` bone hierarchy, and the `.d` dependency file
  exactly where Ayataka does, from the same flags. It also turns effect
  `.yml` files into `.cfx` files. Switching the engine's rake rules over is a
  one-line tool-path change.
- **Same bytes where it matters.** The `.vmdf` writer is Ayataka's, adapted
  (`CmdlBinaryStore`, the exchange model, material definitions and
  overrides). The FBX-facing layer is new: it reproduces the FBX SDK
  behaviour Ayataka depended on, including triangulation, splitting meshes
  per material and tangent generation. See [Fidelity](#fidelity).
- **Deterministic everywhere.** The output is byte-identical on every host.
  - It uses no host transcendental maths.
  - It has no FMA contraction or fast-math.
  - It never iterates an unordered container.
  - Its number formatting doesn't depend on the locale.
  - Every byte Ayataka left uninitialised is written as zero.

## Usage

```
fbx-to-vmdf -a16 -o<out.vmdf> -sk<anim dir/> -lh -d<out.vmdf.d> -h<skel.xml> <in.fbx>
fbx-to-vmdf -o<out.cfx> -d<out.cfx.d> <effect.yml>
```

These are Ayataka's flags:

| Flag | Meaning |
| --- | --- |
| `-a<n>` | alignment (16) |
| `-o` | output |
| `-sk` | animation output directory |
| `-lh` | mirror X into the engine's left-handed space |
| `-d` | dependency file |
| `-h` | bone hierarchy XML |
| `--flip-uv` | flip V |
| `--skeleton-only` | write only the bone hierarchy |
| `--instance` | accepted; it does nothing, as in Ayataka |

The effect definitions are found the way Ayataka finds them: under
`$USAGI_DIR/Data/GLSL/effects/<ShaderPak>.yml`. Use `--effects-dir <dir>` to
point somewhere else. The `<model>.yml` next to the `.fbx` supplies the
`MaterialOverrides`.

fbx-to-vmdf also adds its own options:

| Option | Meaning |
| --- | --- |
| `--compare <ref.vmdf>` | after converting, compare the result with a reference, field by field |
| `--compare-files <a> <b>` | compare two existing `.vmdf` files |
| `--dump <file.vmdf>` | print a model's structure (`--verbose` adds the first vertices) |
| `--ayataka-compat=bounds,uvindex` | reproduce two Ayataka bugs that were fixed after the Windows reference cook was made, to show that everything else matches it |
| `--probe <in.fbx>` | print the per-corner FBX data in the format of `tools/sdkprobe` |

A comparison ends with one of these verdicts:

- `IDENTICAL`
- `EQUIVALENT (only never-initialised bytes differ)`
- `EQUIVALENT (same triangles, different vertex/index order)`
- `DIFFERENT`

The never-initialised bytes are the ones Ayataka never wrote: string tails
after the NUL, unused array slots, the adjacency stream, constant data
outside the constant sets, `refCamera` and `constantIndexes`.

## Building

You need CMake 3.14+ and a C++17 compiler. Everything else is vendored in
`third_party/`.

### Linux (gcc or clang)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release        # add -G Ninja if you like
cmake --build build -j
(cd build && ctest)                                   # self-test
```

### Windows (MSVC, Visual Studio 2019 or 2022)

From a "x64 Native Tools Command Prompt for VS":

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cd build && ctest -C Release
```

The binary is `build\Release\fbx-to-vmdf.exe`. Only 64-bit targets are
supported. `/fp:precise` is set; never build with `/fp:fast`.

### macOS (AppleClang, Xcode command line tools)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
(cd build && ctest)
```

This builds for the host architecture, arm64 or x86_64. The build passes
`-ffp-contract=off`, which matters most here: clang would otherwise fuse
multiply-adds on arm64 and change the output.

### Checking a new host

`scripts/ci.sh` builds, runs the self-test, converts every model in a data
tree, and compares the SHA-256 of each output with the golden Linux hashes
in `tests/golden/`. Run it on Windows from Git Bash.

```sh
scripts/ci.sh <path to game Data/Models> <path to Usagi engine>
```

## Using it from Usagi

The engine branch `jethac/fbx-to-vmdf` makes the switch.
`BuildConfig#cmdl_converter` names `data_tool('fbx-to-vmdf')` instead of
`data_tool('Ayataka')`. The rake rules (`cmdl` and `custom_effect`) are
otherwise unchanged. Put the binary in one of these places:

- **Linux, macOS:** `<engine>/_build/tools/linux/bin/fbx-to-vmdf`
- **Windows:** `<engine>/Tools/bin/fbx-to-vmdf.exe`

Alternatively, set `USAGI_FBX_TO_VMDF` to its path.

## Fidelity

The reference is Ayataka's output. Where fbx-to-vmdf differs, it's for one of
the reasons below. The compare mode tells them apart.

1. **Uninitialised memory.** Ayataka left garbage in about a third of each
   material, in the adjacency stream and after every string's terminator.
   The Windows build left heap and stack contents; the Linux CRT shim left
   0xFE. fbx-to-vmdf writes zeros. Compare treats these bytes as don't-care.
2. **Ayataka bugs fixed in the engine since the Windows reference cook.**
   fbx-to-vmdf has the fixed behaviour, and `--ayataka-compat` restores the
   old one:
   - **`bounds`, fixed by 513d3feb.** The shape bounds were flipped in Z
     instead of being mirrored in X.
   - **`uvindex`, fixed by 1d43dca8.** Every UV set after the first was read
     through the first set's index array.
3. **Effect pak name.** The Linux Ayataka build before 572419bb stored the
   whole effect `.yml` path in `renderPasses[].effectName`.
   fbx-to-vmdf always stores `<file name>.<effect>[.<pass>]`, for example
   `Model.FBXDefault.deferred`.
4. **Triangulation order.** Quads are split along the same diagonal as the
   FBX SDK splits them, giving the same triangles. The SDK breaks ties
   between equally good ears with rounding noise, so the order and rotation
   in which it emits the two triangles can differ, and so can the vertex
   order that meshoptimizer derives from them.
5. **Tangent-generation edge cases.** fbx-to-vmdf's `GenerateTangentsData`
   replica matches the SDK bit for bit on almost every vertex:
   - the per-triangle UV gradient is orthogonalised against each corner's
     normal;
   - it's averaged over the corners that share the control point, the UV
     index, the normal and the triangle handedness;
   - the binormal is `±normalise(T × N)`;
   - after a material split, tangents are regenerated per corner.

   It still differs in two cases:
   - triangles with zero UV area, where the SDK invents a frame by an
     unknown rule;
   - components that cancel to about 1e-17, where the last bits depend on
     the SDK's exact operation order.

## Not supported (yet)

None of these are used by any current UsagiGP model:

- skeletal and material animation export (`.vskla` / `.vmata`, the `-sk`
  directory);
- blend shapes;
- collision models (`--collision`);
- big-endian output (`-be`);
- FBX files that aren't Y-up right-handed and in metres, which Ayataka
  converted with the SDK's `ConvertScene`. fbx-to-vmdf warns and converts
  them as they are.

Skinning, lights and cameras are ported from Ayataka, but no current model
exercises them.

## Layout

| Path | Contents |
| --- | --- |
| `src/VmdfFormat.h` | the `.vmdf` / `.cfx` structs, with static_asserts pinning the engine layout |
| `src/FbxScene.*` | ufbx import, raw layer elements from the FBX DOM, and the SDK operations (triangulate, split per material, tangents) |
| `src/Loader.*` | Ayataka's FbxLoad / FbxConverter, ported onto `FbxScene` |
| `src/Exchange.*`, `src/Writer.*`, `src/Material*` | Ayataka's exchange model, binary store, material definitions and overrides |
| `src/FbxMath.h`, `src/UsgMath.h`, `src/DetMath.*` | FbxAMatrix-compatible double maths, usg float maths, and deterministic sin/cos/atan2/asin |
| `src/Compare.*` | `--dump` and `--compare` |
| `tools/sdkprobe/` | a research harness that links the real FBX SDK, used to reverse-engineer its behaviour; not built by CMake |

## Licence

MIT. See [LICENSE](LICENSE): large parts are adapted from Ayataka
(Copyright (c) 2013-2018 Vitei Inc., MIT), and the vendored libraries keep
their own licences.
