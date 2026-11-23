#pragma once
// No includes: <oat/oat.h> comes from the precompiled header.

struct Item {
  oat::Sym name;
  oat::u32 count;
};

struct Inventory {
  oat::Array<Item> items;
  oat::Map<oat::Sym, oat::u32> index_by_name;

  void add(oat::Sym name, oat::u32 count, oat::Allocator& a);
  oat::u32 count_of(oat::Sym name) const;
  void free(oat::Allocator& a);
};

// Parses lines of "name=count". The first bad line stops it with an error.
oat::Result<oat::u32> load_inventory(oat::Str text, oat::StrTable& names, Inventory* out,
                                     oat::Allocator& a);
