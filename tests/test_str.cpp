#include "test.h"

#include <oat/oat.h>

#include <cmath>
#include <string>

using namespace oat;
using namespace std::string_view_literals;

TEST(strbuf_append) {
  Allocator h = heap();
  StrBuf s;
  CHECK(s.view().empty() && s.c_str()[0] == '\0' && s.is_empty());
  s.append("hello", h);
  s.append_char(',', h);
  s.append(" world"sv, h);
  s.append("", h);
  CHECK(s.view() == "hello, world");
  CHECK(std::strlen(s.c_str()) == s.len);

  std::string model = "hello, world";
  for (u32 i = 0; i < 2000; ++i) {
    s.append_char(static_cast<char>('a' + i % 26), h);
    model.push_back(static_cast<char>('a' + i % 26));
    if (i % 7 == 0) {
      s.append("xyz", h);
      model += "xyz";
    }
  }
  CHECK(s.view() == model);
  CHECK(s.c_str()[s.len] == '\0');

  s.clear();
  CHECK(s.len == 0 && s.c_str()[0] == '\0');
  s.reserve(100, h);
  CHECK(s.cap >= 101 && s.len == 0);
#if OAT_DEBUG
  s.append("abc", h);
  CHECK_ASSERTS(s.append(s.view(), h));
#endif
  s.free(h);
  CHECK(s.data == nullptr);

  StrBuf r;
  r.reserve(0, h);
  CHECK(r.c_str()[0] == '\0');
  r.free(h);
}

TEST(strbuf_numbers) {
  Allocator h = heap();
  StrBuf s;
  s.append_int(0, h);
  s.append_char(' ', h);
  s.append_int(-42, h);
  s.append_char(' ', h);
  s.append_int(INT64_MAX, h);
  s.append_char(' ', h);
  s.append_int(INT64_MIN, h);
  s.append_char(' ', h);
  s.append_uint(UINT64_MAX, h);
  CHECK(s.view() == "0 -42 9223372036854775807 -9223372036854775808 18446744073709551615");

  s.clear();
  s.append_float(0.5, h);
  s.append_char(' ', h);
  s.append_float(-3.0, h);
  s.append_char(' ', h);
  s.append_float(0.1, h);
  CHECK(s.view() == "0.5 -3 0.1");

  // Shortest form round-trips exactly.
  Rng rng = Rng::seeded(20);
  bool ok = true;
  for (u32 i = 0; i < 2000; ++i) {
    f64 v = std::ldexp(static_cast<f64>(rng.next_u64() >> 11), static_cast<int>(rng.range(0, 200)) - 100);
    if (i % 2) v = -v;
    s.clear();
    s.append_float(v, h);
    Result<f64> back = parse_float(s.view());
    ok = ok && back && *back == v;
    i64 n = static_cast<i64>(rng.next_u64());
    s.clear();
    s.append_int(n, h);
    Result<i64> nback = parse_int(s.view());
    ok = ok && nback && *nback == n;
  }
  CHECK(ok);
  s.free(h);
}

TEST(strbuf_appendf) {
  Allocator h = heap();
  StrBuf s;
  s.appendf(h, "%d-%s-%.2f", 7, "seven", 7.0);
  CHECK(s.view() == "7-seven-7.00");
  s.appendf(h, "%s", "");
  s.appendf(h, "|%5u|", 12u);
  CHECK(s.view() == "7-seven-7.00|   12|");
  // Longer than any small internal buffer.
  std::string big(5000, 'q');
  s.clear();
  s.appendf(h, "<%s>", big.c_str());
  CHECK(s.len == 5002 && s.view().front() == '<' && s.view().back() == '>');
  CHECK(s.c_str()[s.len] == '\0');
  s.free(h);
}

TEST(str_trim_split) {
  CHECK(trim("  a b \t\r\n") == "a b");
  CHECK(trim("abc") == "abc");
  CHECK(trim("   ").empty());
  CHECK(trim("").empty());

  std::string_view l, r;
  CHECK(split_once("key=value=more", '=', &l, &r));
  CHECK(l == "key" && r == "value=more");
  CHECK(split_once("=x", '=', &l, &r) && l.empty() && r == "x");
  CHECK(split_once("x=", '=', &l, &r) && l == "x" && r.empty());
  CHECK(!split_once("none", '=', &l, &r));
  CHECK(l == "none" && r.empty());

  // Walk a line of fields.
  std::string_view rest = "a,b,,c";
  u32 fields = 0;
  for (bool more = true; more;) {
    std::string_view field;
    more = split_once(rest, ',', &field, &rest);
    ++fields;
  }
  CHECK(fields == 4);
}

TEST(str_parse_int) {
  CHECK(*parse_int("0") == 0);
  CHECK(*parse_int("-0") == 0);
  CHECK(*parse_int("+17") == 17);
  CHECK(*parse_int("-123456789") == -123456789);
  CHECK(*parse_int("9223372036854775807") == INT64_MAX);
  CHECK(*parse_int("-9223372036854775808") == INT64_MIN);
  CHECK(parse_int("9223372036854775808").error() == Error::OutOfRange);
  CHECK(parse_int("-9223372036854775809").error() == Error::OutOfRange);
  CHECK(parse_int("99999999999999999999999").error() == Error::OutOfRange);
  for (std::string_view bad : {""sv, "-"sv, "+"sv, " 1"sv, "1 "sv, "1x"sv, "x"sv, "1.0"sv, "--1"sv, "0x10"sv})
    CHECK(parse_int(bad).error() == Error::InvalidArgument);
}

TEST(str_parse_float) {
  CHECK(*parse_float("0") == 0.0);
  CHECK(*parse_float("1.5") == 1.5);
  CHECK(*parse_float("-2.25e2") == -225.0);
  CHECK(*parse_float("+4") == 4.0);
  CHECK(*parse_float("1e-3") == 0.001);
  CHECK(*parse_float(".5") == 0.5);
  for (std::string_view bad : {""sv, " 1"sv, "1 "sv, "abc"sv, "1.5x"sv, "-"sv, "+"sv, "++1"sv, "1,5"sv})
    CHECK(parse_float(bad).error() == Error::InvalidArgument);
  CHECK(parse_float("1e999").error() == Error::OutOfRange);
}

TEST(str_to_cstr) {
  Allocator h = heap();
  Arena arena = Arena::create(h);
  Allocator a = arena.allocator();
  std::string_view whole = "hello world";
  const char* c = to_cstr(whole.substr(0, 5), a);
  CHECK(std::strcmp(c, "hello") == 0);
  CHECK(std::strcmp(to_cstr("", a), "") == 0);
  arena.destroy();
}

TEST(utf8_round_trip) {
  bool ok = true;
  for (u32 cp = 0; cp <= 0x10ffff; ++cp) {
    char buf[4];
    u32 n = utf8_encode(cp, buf);
    if (cp >= 0xd800 && cp <= 0xdfff) {
      ok = ok && n == 0;
      continue;
    }
    u32 expected_len = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    u32 index = 0;
    ok = ok && n == expected_len && utf8_decode(std::string_view(buf, n), &index) == cp && index == n;
  }
  CHECK(ok);
  char buf[4];
  CHECK(utf8_encode(0x110000, buf) == 0);

  // A mixed string decodes code point by code point.
  std::string_view text = "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80z"; // a, e-acute, euro, emoji, z
  u32 expected[] = {'a', 0xe9, 0x20ac, 0x1f600, 'z'};
  u32 index = 0, count = 0;
  while (index < text.size()) {
    u32 cp = utf8_decode(text, &index);
    CHECK(count < 5 && cp == expected[count]);
    ++count;
  }
  CHECK(count == 5);
}

TEST(utf8_malformed) {
  auto first = [](std::string_view s, u32* advance) {
    u32 index = 0;
    u32 cp = utf8_decode(s, &index);
    *advance = index;
    return cp;
  };
  u32 adv = 0;
  CHECK(first("\x80", &adv) == 0xfffd && adv == 1);             // stray continuation
  CHECK(first("\xc3", &adv) == 0xfffd && adv == 1);             // truncated
  CHECK(first("\xe2\x82", &adv) == 0xfffd && adv == 1);         // truncated
  CHECK(first("\xc3\x41", &adv) == 0xfffd && adv == 1);         // bad continuation
  CHECK(first("\xc0\x80", &adv) == 0xfffd && adv == 1);         // overlong
  CHECK(first("\xe0\x80\x80", &adv) == 0xfffd && adv == 1);     // overlong
  CHECK(first("\xed\xa0\x80", &adv) == 0xfffd && adv == 1);     // surrogate
  CHECK(first("\xf4\x90\x80\x80", &adv) == 0xfffd && adv == 1); // past U+10FFFF
  CHECK(first("\xff", &adv) == 0xfffd && adv == 1);
  // Decoding always makes progress, whatever the bytes.
  Rng rng = Rng::seeded(21);
  char junk[256];
  for (char& c : junk) c = static_cast<char>(rng.next_u64());
  u32 index = 0, steps = 0;
  while (index < sizeof(junk)) {
    utf8_decode(std::string_view(junk, sizeof(junk)), &index);
    ++steps;
  }
  CHECK(steps <= sizeof(junk));
}

TEST(str_table) {
  Allocator h = heap();
  HeapStats before = heap_stats();
  StrTable table = StrTable::create(h);

  Sym empty = table.intern("");
  CHECK(empty.id == 0 && empty == Sym{} && table.str(empty).empty());
  Sym a = table.intern("alpha");
  Sym b = table.intern("beta");
  std::string copy = "alpha";
  Sym a2 = table.intern(copy);
  CHECK(a == a2 && !(a == b) && table.len() == 2);
  CHECK(table.str(a) == "alpha" && table.str(b) == "beta");
  CHECK(table.str(a).data() != copy.data()); // the table owns its own copy
  CHECK(table.str(a).data()[5] == '\0');
  CHECK(table.find("beta") && *table.find("beta") == b);
  CHECK(!table.find("gamma"));
  CHECK(table.find("") && table.find("")->id == 0);

  // Views stay valid while thousands more strings are interned.
  std::string_view alpha_view = table.str(a);
  const char* alpha_ptr = alpha_view.data();
  StrBuf name;
  Array<Sym> syms;
  for (u32 i = 0; i < 5000; ++i) {
    name.clear();
    name.appendf(h, "string-%u", i);
    syms.push(table.intern(name.view()), h);
  }
  bool ok = true;
  for (u32 i = 0; i < 5000; ++i) {
    name.clear();
    name.appendf(h, "string-%u", i);
    ok = ok && table.str(syms[i]) == name.view() && table.intern(name.view()) == syms[i];
  }
  CHECK(ok);
  CHECK(table.len() == 5002);
  CHECK(table.str(a).data() == alpha_ptr && alpha_view == "alpha");

  // Sym is a valid raw-byte map key.
  Map<Sym, u32> by_sym;
  by_sym.insert(a, 1, h);
  by_sym.insert(b, 2, h);
  CHECK(*by_sym.find(table.intern("beta")) == 2);
  by_sym.free(h);

  syms.free(h);
  name.free(h);
  table.destroy();
  CHECK(heap_stats().live_bytes == before.live_bytes);
}
