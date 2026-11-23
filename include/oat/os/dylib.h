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
//     if (changed.poll(path)) {             // new build, and the linker is done
//       if (auto fresh = os::Library::load_copy(path)) {
//         game.unload();
//         game = *fresh;                    // then look the symbols up again
//       }                                   // on failure keep running the old one
//     }
//
// Two things make the obvious version (load the file, reload when its time
// changes) fail, and the recipe above handles both:
//   - Windows locks a loaded DLL, so the linker cannot write the next build.
//     load_copy loads a private copy and leaves the original free.
//   - The modified time changes when the linker starts writing, not when it
//     finishes. FileChange waits until the file has stopped changing.
struct Library {
  SDL_SharedObject* handle = nullptr;
  char* copy_path = nullptr; // set by load_copy; the file is deleted by unload
  u32 copy_path_size = 0;

  // On Windows, loading a file that is not a valid library can open a system
  // dialog. A game that hot-reloads should call
  // SetErrorMode(SEM_FAILCRITICALERRORS) once at start-up.
  [[nodiscard]] static Result<Library> load(Str path);

  // Copies the library next to itself under a unique name and loads the copy.
  // unload() deletes the copy.
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
