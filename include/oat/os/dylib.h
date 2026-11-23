// oat/os/dylib.h - loading shared libraries.
#pragma once

#include "../base.h"

struct SDL_SharedObject;

namespace oat::os {

struct Library {
  SDL_SharedObject* handle = nullptr;

  [[nodiscard]] static Result<Library> load(Str path);

  // The address of an exported function, or nullptr if there is none.
  void* symbol(const char* name);

  // symbol() cast to a function pointer type:
  //   auto update = lib.symbol_as<void (*)(f32)>("game_update");
  template <class Fn>
  Fn symbol_as(const char* name) {
    return reinterpret_cast<Fn>(symbol(name));
  }

  void unload();
};

} // namespace oat::os
