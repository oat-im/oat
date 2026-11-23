#include "inventory.h"

using namespace oat;

void Inventory::add(Sym name, u32 count, Allocator& a) {
  if (u32* at = index_by_name.find(name)) {
    items[*at].count += count;
    return;
  }
  index_by_name.insert(name, items.len, a);
  items.push(Item{name, count}, a);
}

u32 Inventory::count_of(Sym name) const {
  const u32* at = index_by_name.find(name);
  return at ? items[*at].count : 0;
}

void Inventory::free(Allocator& a) {
  items.free(a);
  index_by_name.free(a);
}

Result<u32> load_inventory(Str text, StrTable& names, Inventory* out, Allocator& a) {
  u32 lines = 0;
  for (bool more = true; more;) {
    Str line;
    more = split_once(text, '\n', &line, &text);
    line = trim(line);
    if (line.empty()) continue;

    Str name, count_text;
    if (!split_once(line, '=', &name, &count_text)) return err(Error::InvalidArgument);
    OAT_TRY(count, parse_int(trim(count_text)));
    if (count < 0 || count > U32_MAX) return err(Error::OutOfRange);
    out->add(names.intern(trim(name)), static_cast<u32>(count), a);
    ++lines;
  }
  return lines;
}
