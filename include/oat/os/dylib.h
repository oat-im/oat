// oat/os/dylib.h - loading shared libraries, and reloading them while running.
#pragma once

#include "../base.h"

struct SDL_SharedObject;

namespace oat::os {

// Hot reload, the whole recipe:
//
//   os::FileChange changed;                 // see os/fs.h
//   os::Library game = *os::Library::load_copy(path);
//   each frame:
//     if (changed.poll(path)) {             // a new build that has stopped changing
//       if (auto fresh = os::Library::load_copy(path)) {
//         game.unload();
//         game = *fresh;                    // then look the symbols up again
//       } else {
//         changed.retry();                  // not loadable yet: ask again shortly
//       }                                   // meanwhile the old one keeps running
//     }
//
// The obvious version (load the file, reload when its time changes) fails in
// four ways. The recipe handles the first three; the fourth is in your build.
//   - Windows locks a loaded DLL, so the linker cannot write the next build.
//     load_copy loads a private copy and leaves the original free.
//   - The modified time changes when the linker starts writing, not when it
//     finishes. FileChange waits until the file has stopped changing.
//   - Even then the file can be incomplete (see FileChange), so a failed load
//     is retried instead of being treated as the end of that build.
//   - On Windows with a debugger attached, the debugger locks the library's
//     .pdb as well, and the next link fails on that instead. Give every build
//     its own PDB name, for example /PDB:game_%RANDOM%.pdb on the link line.
//     oat cannot do this for you.
struct Library {
  SDL_SharedObject* handle = nullptr;
  char* copy_path = nullptr; // set by load_copy; the file is deleted by unload
  u32 copy_path_size = 0;

  // On Windows, loading a file that is not a valid library can open a system
  // dialog. A game that hot-reloads should call
  // SetErrorMode(SEM_FAILCRITICALERRORS) once at start-up.
  [[nodiscard]] static Result<Library> load(Str path);

  // Copies the library next to itself under a unique name
  // ("game.live-<id>-<n>.dll") and loads the copy. unload() deletes the copy.
  // Copies left behind by an earlier run that crashed are deleted here too.
  // That sweep removes every "game.live-*" copy this process did not make, so
  // do not hot-reload the same library file from two running games.
  [[nodiscard]] static Result<Library> load_copy(Str path);

  // The address of an exported function, or nullptr if there is none.
  void* symbol(const char* name);

  // symbol() cast to a function pointer type:
  //   auto update = lib.symbol_as<void (*)(f32)>("game_update");
  template <class Fn>
  Fn symbol_as(const char* name) {
    return reinterpret_cast<Fn>(symbol(name));
  }

  // Pointers from symbol() are dead after this.
  void unload();
};

} // namespace oat::os
