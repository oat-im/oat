# oat preconditions

Every rule here is a precondition of the library. Each has a mechanical check, and a test that trips the check.

| # | Rule | Checked by | What breaks it | Test |
| --- | --- | --- | --- | --- |
| 1 | Container elements are movable with `memcpy` | `static_assert(is_relocatable<T>)` | `struct Node { Node* self = this; };`<br>`Array<Node> a; a.push(n, h);` | `compile_fail/array_not_relocatable.cpp` and three more |
| 2 | Containers never run element destructors | `static_assert` that `T` is trivially destructible | `struct Tex { ~Tex() { release(); } };`<br>`Pool<Tex> p; p.add(t, h);` | `compile_fail/array_nontrivial_destructor.cpp`, `pool_nontrivial_destructor.cpp` |
| 3 | Raw-byte hash keys have no padding and no floats | `static_assert(__has_unique_object_representations(K))` | `struct K { u8 kind; u32 id; };`<br>`Map<K, int> m; m.find(k);` | `compile_fail/map_key_padding.cpp`, `map_key_float.cpp`, `set_key_float_member.cpp` |
| 4 | No exceptions, no RTTI | Compiler flags, `PUBLIC` on the `oat` target | `throw Error::Io;`<br>`dynamic_cast<Enemy*>(entity)` | `compile_fail/flags/uses_exceptions.cpp`, `uses_rtti.cpp` |
| 5 | Fallible functions return `Result<T>` and the result is used | `[[nodiscard]]` on every such function | `os::create_dirs("saves");`<br>(the error is dropped) | `compile_fail/flags/discards_result.cpp` |
| 6 | `.value()` is never called on a `Result` or `Option` | Lint | `i64 n = parse_int(s).value();`<br>(aborts on error, with exceptions off) | `lint_cases/bad_value` |
| 7 | No `std::` in project code | Lint | `std::vector<Enemy> enemies;`<br>`std::move(x)` (write `oat::move`) | `lint_cases/bad_std`, `bad_std_move` |
| 8 | No system includes outside `oat/` and the PCH | Lint | `#include <vector>`<br>in any game source | `lint_cases/bad_include` |
| 9 | Length and capacity are 32-bit | Runtime check, always on | `Array<u8> a;` grown past<br>4,294,967,295 elements | `array_asserts` |
| 10 | No inserting an element of a container into itself | `OAT_ASSERT` on the address | `a.push(a[0], h);`<br>(growth frees `a[0]` first) | `array_asserts`, `map_self_insert_asserts`, `strbuf_append` |
| 11 | No pointers held across growth | ASan poisoning of abandoned and reset arena memory | `Enemy* e = &a[0]; a.push(x, temp);`<br>`e->hp = 0;` | `arena_allocator_resize`, `arena_mark_reset` (ASan builds) |
| 12 | `Arena` is single-threaded | Debug assert on the owning thread | An arena created on the main thread<br>and used from a job | `arena_wrong_thread_asserts` |
| 13 | Paths are UTF-8 with `/` | Converted only inside SDL; `os::normalize` | `os::join(dir, "saves\\slot1")`<br>(normalize first) | `fs_long_and_unicode_paths`, `path_join_normalize` |

Rules 9 to 12 are runtime checks. Rule 9 is always on; rules 10 and 12 fire when `OAT_DEBUG` is set (the default when `NDEBUG` is not); rule 11 needs an AddressSanitizer build.

## Notes on each rule

**1. Relocation.** `is_relocatable<T>` defaults to "trivially copyable". A type that owns a pointer but is still safe to `memcpy` opts in:

```cpp
template <> constexpr bool oat::is_relocatable<Mesh> = true;
```

oat's own containers are plain structs with no destructor, so they qualify already and can be nested (`Array<Array<u32>>`, `Map<u64, StrBuf>`).

**2. Destructors.** Give a type that owns something an explicit `destroy()` or `free(Allocator&)` and call it before removing the element. The asserts are inside member functions, not at class scope, so self-referential types compile: `struct Node { Array<Node> children; };`.

**3. Hash keys.** Padding bytes are not initialized, so two equal keys can hash differently. Floats have `-0.0 == 0.0` with different bytes, and `NaN != NaN` with equal bytes. For such a key, specialize `oat::KeyTraits<K>` with `hash()` and `eq()`. `Str` keys hash their characters; the map stores the view, not the characters, so use interned or arena strings.

**5. Results.** Write `(void)` in front of a call whose failure really does not matter.

**6 and 7.** Use `if (r)` and `*r`, or `OAT_TRY`. The oat names for the standard types are `Result`, `Expected`, `Option`, `none`, `Str`, `Span`, `Atomic`, `oat::move` and `oat::forward`. The lint does not skip comments, so a comment that mentions a banned name is flagged too.

**9. 32-bit sizes.** `Array`, `StrBuf`, `BitSet` and `Pool` stop at 2^32 - 1 elements, `Map` at 2^31 slots.

**10. Self-insertion.** Copy the element out first: `Enemy copy = a[0]; a.push(copy, h);`. `FixedArray` is exempt because it never reallocates.

**11. Growth.** Any call that takes an `Allocator&` can move the container's storage. So can any `Map` insert or remove. Keep an index or a `Handle`, not a pointer. `Pool` is the exception: its elements never move.

**12. Threads.** `Heap` is thread-safe. `Arena` is not; hand one to another thread with `adopt_thread()`, or use `os::scratch_arena()`, which is per thread. Containers are never thread-safe.

**Out of memory is fatal.** A failed allocation calls the assert handler and aborts, which is why `push` and friends do not return a `Result`.
