#include "inventory.h"

using namespace oat;

static bool run(Allocator& a) {
  StrTable names = StrTable::create(a);
  Inventory inv;

  Result<u32> lines = load_inventory("sword = 1\npotion = 3\n\npotion = 2\n", names, &inv, a);
  bool ok = lines && *lines == 3;
  ok = ok && inv.items.len == 2;
  ok = ok && inv.count_of(names.intern("potion")) == 5;
  ok = ok && inv.count_of(names.intern("shield")) == 0;

  // Most valuable stack first.
  sort(inv.items.span(), [](const Item& x, const Item& y) { return x.count > y.count; });
  ok = ok && names.str(inv.items[0].name) == "potion";

  Result<u32> bad = load_inventory("sword = many\n", names, &inv, a);
  ok = ok && !bad && bad.error() == Error::InvalidArgument;

#if CONSUMER_HAS_OS
  // Save the inventory and read it back.
  StrBuf text;
  for (const Item& item : inv.items) {
    text.append(names.str(item.name), a);
    text.appendf(a, "=%u\n", item.count);
  }
  ArenaScope scratch(os::scratch_arena());
  Allocator temp = os::scratch_arena().allocator();
  Str path = os::join(os::exe_dir(), "consumer_inventory.txt", temp);
  ok = ok && os::write_entire_file(path, text.view()).has_value();

  Result<Span<u8>> saved = os::read_entire_file(path, temp);
  ok = ok && saved.has_value();
  if (saved) {
    Inventory loaded;
    Str saved_text(reinterpret_cast<const char*>(saved->data()), saved->size());
    Result<u32> again = load_inventory(saved_text, names, &loaded, a);
    ok = ok && again && *again == 2 && loaded.count_of(names.intern("sword")) == 1;
    loaded.free(a);
  }
  ok = ok && os::remove(path).has_value();
  text.free(a);
#endif

  inv.free(a);
  names.destroy();
  return ok;
}

int main() {
#if CONSUMER_HAS_OS
  if (!os::init()) return 2;
#endif
  Allocator h = heap();
  bool ok = run(h);
#if CONSUMER_HAS_OS
  os::free_scratch_arena();
#else
  // Everything was freed explicitly; in a debug build the heap can prove it.
  // (With oat_os, SDL's own long-lived allocations are counted too.)
  if (OAT_DEBUG && heap_stats().live_count != 0) return 3;
#endif
  return ok ? 0 : 1;
}
