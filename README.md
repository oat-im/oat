# oat

A fast-compiling, game-ready replacement for the parts of the C++ standard library a game uses.

- **`oat`** (core, no dependencies): containers, memory, strings, algorithms, errors.
- **`oat_os`** (over SDL3): threads, jobs, files, paths, time, library loading.
- **`oat_net`** (over SDL3_net): TCP and UDP sockets, address resolution.

Each is a static library made of one umbrella header and one unity `.cpp`. Templates are thin typed wrappers; growth, rehashing and formatting live in non-template code that compiles once and is optimized even in your debug build. No SDL header is ever included from an oat header.

C++23, exceptions and RTTI off, desktop only (Linux, macOS, Windows).

## Using it

```cmake
add_subdirectory(third_party/oat)
target_link_libraries(game PRIVATE oat oat_os)
target_precompile_headers(game PRIVATE <oat/oat.h> <oat/os.h>)
oat_enable_lint(game ${CMAKE_SOURCE_DIR}/src)
```

With the umbrella headers in the precompiled header, game sources include nothing. `examples/consumer` is a complete project set up this way.

```cpp
using namespace oat;

Allocator h = heap();
Arena frame = Arena::create(h);
Allocator temp = frame.allocator();

Array<Enemy> enemies;                 // 16 bytes; the allocator is passed to calls that can grow
enemies.push(Enemy{.hp = 10}, h);

Map<Sym, u32> score;                  // flat, open addressing
score.get_or_insert(names.intern("eric"), h) += 100;

Result<Span<u8>> file = os::read_entire_file("levels/1.txt", temp);
if (!file) return err(file.error());  // os::last_error_text() has SDL's message

frame.reset();                        // everything from `temp` is gone
enemies.free(h);
```

### Options

| Option | Default | |
| --- | --- | --- |
| `OAT_BUILD_OS` | `ON` | Build `oat_os`. Needs SDL3 (`find_package(SDL3 CONFIG)`). |
| `OAT_BUILD_NET` | `OFF` | Build `oat_net`. Needs SDL3_net. |
| `OAT_BUILD_TESTS` | top-level only | Tests, compile-fail tests, lint tests. |
| `OAT_BUILD_EXAMPLES` | top-level only | The sample consumer. |
| `OAT_BUILD_BENCH` | `OFF` | Benchmarks; downloads EASTL unless `OAT_BENCH_EASTL=OFF`. |
| `OAT_OPTIMIZE_IN_DEBUG` | `ON` | Build the oat libraries with `-O2` in Debug (GCC, Clang). |
| `OAT_SANITIZE` | empty | `address,undefined` or `thread`, for a top-level build. |
| `OAT_WERROR` | `OFF` | Warnings as errors for oat and its tests. |

The exception and RTTI flags are `PUBLIC`, so everything that links oat builds the same way. On MSVC, remove `/EHsc` from `CMAKE_CXX_FLAGS` in your top-level project (oat does this itself when it is the top-level project).

## The standard library, as oat spells it

oat is built on a handful of standard types. They are aliased in `oat/base.h`, and project code uses the oat names; the lint rejects any `std::`.

| oat | is | notes |
| --- | --- | --- |
| `Result<T>` | `std::expected<T, Error>` | `if (r)`, `*r`, `r.error()`. Return errors with `err(Error::NotFound)`. |
| `Expected<T, E>` | `std::expected<T, E>` | when the error is not an `oat::Error` |
| `Option<T>`, `none` | `std::optional<T>`, `std::nullopt` | |
| `Str` | `std::string_view` | not NUL-terminated |
| `Span<T>` | `std::span<T>` | |
| `Atomic<T>` | `std::atomic<T>` | |
| `oat::move`, `oat::forward` | `std::move`, `std::forward` | using-declarations, so compilers still treat them as built-ins |

`OAT_TRY(name, expr)` and `OAT_TRY_VOID(expr)` unwrap a `Result` or return its error.

## What is in it

**`base.h`** `u8`..`u64`, `i8`..`i64`, `f32`, `f64`, `usize`, `isize`; `OAT_ASSERT`, `OAT_ASSERT_MSG`, `OAT_UNREACHABLE`, `set_assert_handler`; `is_relocatable<T>`; `Error`, `Result`; `min`, `max`, `clamp`, `swap`, `array_count`.

**`memory.h`** `Allocator` (a function pointer and a context), `heap()` with `heap_stats()`, `Arena` (chained blocks, marks, in-place growth of the last allocation, ASan poisoning), `ArenaScope`.

**`array.h`** `Array<T>`, `FixedArray<T, N>`, `RingBuffer<T>`, `BitSet`, `FixedBitSet<N>`.

**`map.h`** `Map<K, V>`, `Set<K>`: flat, linear probing, 75% load, backward-shift deletion. `KeyTraits<K>` is the customization point; the default hashes raw bytes, and `Str` keys hash their characters.

**`str.h`** `StrBuf` (always NUL-terminated, `appendf` is compiler-checked), `trim`, `split_once`, `parse_int`, `parse_float`, `to_cstr`, `utf8_decode`, `utf8_encode`, `Sym` and `StrTable` (interning).

**`pool.h`** `Pool<T>` with generational `Handle<T>` (chunked, so addresses are stable), `IntrusiveList<T>` (elements derive from `ListNode<>`).

**`algo.h`** `sort` (introsort), `sort_stable`, `radix_sort`, `lower_bound`, `upper_bound`, `hash_bytes`, `hash_u64`, `hash_combine`, `Rng` (xoshiro256**).

**`os.h`** `os::init`, `os::last_error_text`; `Thread`, `Mutex`, `MutexLock`, `CondVar`, `Semaphore`; `JobSystem`, `JobGroup`, `parallel_for`, `scratch_arena`; `read_entire_file`, `write_entire_file` (synced to disk, then renamed into place), `File`; `list_dir`, `path_info`, `create_dirs`, `remove`, `rename`, `exe_dir`, `user_data_dir`; `join`, `extension`, `stem`, `parent`, `normalize`; `now_ns`, `sleep_ns`, `wall_clock`, `to_calendar`; `Library` with `load_copy`, and `FileChange` to poll for a finished rebuild (the hot-reload recipe is at the top of `os/dylib.h`).

**`net.h`** `net::resolve` (with a timeout), `TcpListener` (all addresses or one), `TcpStream` (`drain` before `close`, or queued data is dropped), `UdpSocket`, `wait_for_input`. Writes queue without limit, so backpressure is the caller's job (`pending_writes`).

The rules every type and call site must follow are in [PRECONDITIONS.md](PRECONDITIONS.md). The short version: elements are moved with `memcpy`, containers never run destructors, and nothing holds a pointer across growth.

## Compile time

Measured with `tools/compile_time.py` (Clang `-ftime-trace`, fastest of several runs, `-O0`) on an Apple Silicon MacBook Pro with Apple clang 21.

| Translation unit | Time |
| --- | ---: |
| `#include <oat/oat.h>` only | 84 ms |
| `<vector>`, `<unordered_map>`, `<string>`, `<algorithm>` only | 174 ms |
| The same four from EASTL only | 88 ms |
| oat: every container instantiated for 20 element types | 324 ms |
| std: the equivalent code (`bench/compile_time/std_instantiate.cpp`) | 3,190 ms |

Including the headers is cheap for all three; the difference is in what each instantiation costs, which is where a game's build time goes. With oat in a precompiled header the include cost disappears as well. There is no EASTL version of the instantiation probe, so that comparison is not made here.

Read the 10x as the upper end, not as a typical figure. The probe is written to stress instantiation, and its std side uses some of the heaviest templates in the library for each of the 20 types: `std::deque`, `std::stable_sort`, `unique_ptr` and three unordered containers. A game that mostly uses `std::vector` would see a smaller gap. On CI the same probe measured 6x on Linux with both GCC and Clang, and 11x on macOS.

CI fails if the oat probe gets more than 20% slower than its recorded baseline. Because runners differ in speed from run to run, what is recorded and compared is the oat probe's time divided by the std probe's time in the same run.

## Benchmarks

`bench/bench.cpp`, nanoseconds per element, best of 7 runs after a warm-up, one million `u64`. Same machine and compiler as above; EASTL 3.27.01. Run them yourself:

```
cmake -S . -B build/bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DOAT_BUILD_BENCH=ON
cmake --build build/bench --target oat_bench_O0 oat_bench_O2
./build/bench/bench/oat_bench_O0 && ./build/bench/bench/oat_bench_O2
```

Benchmark code built with `-O0` (a debug build of the game):

| benchmark | oat | std | EASTL |
| --- | ---: | ---: | ---: |
| push 1M u64 | 2.80 | 21.47 | 13.46 |
| iterate 1M u64 | 0.60 | 2.53 | 0.59 |
| map insert 1M u64 -> u64 | 47.23 | 251.71 | 188.20 |
| map find 1M (all hits) | 43.82 | 96.97 | 58.26 |
| sort 1M u64 | 90.03 | 100.35 | 105.96 |

Benchmark code built with `-O2`:

| benchmark | oat | std | EASTL |
| --- | ---: | ---: | ---: |
| push 1M u64 | 0.60 | 0.32 | 0.35 |
| iterate 1M u64 | 0.07 | 0.07 | 0.07 |
| map insert 1M u64 -> u64 | 24.50 | 103.81 | 68.25 |
| map find 1M (all hits) | 6.64 | 10.65 | 9.32 |
| sort 1M u64 | 39.38 | 40.15 | 40.35 |

Reading these:

- In a debug build oat is several times faster than `std` at pushing and at map work, because the growth and rehash code is in the always-optimized library.
- The map is faster in both builds: one flat allocation instead of a node per entry.
- **`push` without `reserve` is slower than `std` and EASTL at `-O2`**, by well under a nanosecond per element. Run to run it varies between about 0.6 and 1.2 ns. Calling `reserve` first removes the difference.
- Sorting and iterating are a wash at `-O2`.

## Tests

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DOAT_BUILD_NET=ON
cmake --build build && ctest --test-dir build
```

- `oat_tests`, `oat_os_tests`, `oat_net_tests`: one `test_*.cpp` per module on a 50-line runner (`tests/test.h`). Containers are checked against a brute-force model under random operations. `oat_os` tests run in a temporary directory next to the executable; `oat_net` tests use loopback ports 47631-47639.
- **Compile-fail tests** (`tests/compile_fail`): every static-assert and compiler-flag rule has a file that must not compile, checked at configure time with `try_compile`.
- **Lint tests**: the lint must accept one directory and reject six.
- Sanitizers: `-DOAT_SANITIZE=address,undefined` or `-DOAT_SANITIZE=thread`.

## Debugger visualizers

In `debug/`, covering `Array`, `FixedArray`, `RingBuffer`, `Map`, `Set`, `StrBuf`, `Pool` and `Handle`. `oat_visualizer_demo` has one of each to try them on.

| Debugger | File | Load with |
| --- | --- | --- |
| LLDB | `oat_lldb.py` | `command script import path/to/oat_lldb.py` |
| GDB | `oat_gdb.py` | `source path/to/oat_gdb.py` |
| Visual Studio | `oat.natvis` | add the file to the project |

## Status

What has been run, and where. Linux and Windows results are from CI (`.github/workflows/ci.yml`); macOS was also run locally.

| | macOS (Apple Clang) | Linux (GCC 14, Clang 19) | Windows (MSVC) |
| --- | --- | --- | --- |
| Build warning-free (`-Werror` / `/WX`), all three libraries | yes | yes | yes |
| All tests, Debug; core tests also in Release | yes | yes | yes |
| ASan and UBSan | yes (local) | yes | n/a |
| TSan (threads, jobs) | yes (local) | yes | n/a |
| Lint on oat and the sample consumer | yes | yes | yes |
| Benchmarks against std and EASTL | yes | yes | yes |
| Compile-time gate, baseline recorded | | yes (Clang 19) | |
| LLDB visualizers | yes (local) | | |
| GDB visualizers, natvis | | written, not yet run | written, not yet run |

The benchmark tables above are from one Apple Silicon machine. The benchmarks CI job prints the same tables for each platform in its run summary; on hosted runners the map and debug-build results hold on all four compilers, and plain `push` at `-O2` is level with or ahead of `std` on Linux and MSVC.

Things verified against the plan's open questions:

- Every SDL3 and SDL3_net function oat calls was checked against the installed headers (SDL 3.4, SDL_net 3.2).
- `SDL_RenamePath` is documented to replace an existing file, and the Windows tests that overwrite one pass. `write_entire_file` calls `SDL_FlushIO` before the rename, which SDL implements as `fdatasync`, `F_FULLFSYNC` on Apple systems and `FlushFileBuffers` on Windows (read from SDL's source; `SDL_CloseIO` alone does not sync). So a power cut leaves the old file or the new one, never an empty one. Whether the rename itself is durable at that instant is up to the file system.
- SDL's file, time, thread and library functions work without `SDL_Init`; the tests never call it. `os::init()` only installs the allocator.
- On Windows, paths over 260 characters fail unless long paths are enabled (found by CI), and a load of a file that is not a library can open a system dialog unless the application calls `SetErrorMode` (see `os/dylib.h`).
- SDL3_net rejects empty datagrams; `UdpSocket::send` returns `InvalidArgument` for one, and for anything over the 65,507-byte UDP payload limit.
- Error codes from `oat_os` are a best guess, because SDL only reports text: see the note at the top of `os/fs.h`.
- SDL3_net has no call to read back the port chosen for port 0.
- `hash_bytes` is wyhash (final version 4.3, public domain under The Unlicense), transcribed without changes and checked against the reference code on 120,000 random inputs plus fixed vectors in the tests. `hash_u64` is the splitmix64 finalizer. Nothing was ported from EASTL.
- Minimum compilers: GCC 14, Visual Studio 2022 17.8, and Clang 19 when Clang is used with libstdc++ (Clang 18 does not enable libstdc++'s `<expected>`). On GCC, `__has_trivial_destructor` is used where Clang and MSVC have `__is_trivially_destructible`.

## License

oat is under the BSD 3-Clause license; see [LICENSE](LICENSE).

SDL3 and SDL3_net are zlib-licensed and linked `PRIVATE`. EASTL (BSD 3-Clause) is fetched only for the benchmarks and is not part of oat.
