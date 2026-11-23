#include "test.h"

#include <oat/oat.h>
#include <oat/os.h>

using namespace oat;

TEST(path_parts) {
  CHECK(os::filename("a/b/c.txt") == "c.txt");
  CHECK(os::filename("c.txt") == "c.txt");
  CHECK(os::filename("a/b/") == "");
  CHECK(os::filename("") == "");

  CHECK(os::extension("a/b/c.txt") == ".txt");
  CHECK(os::extension("a/b.tar.gz") == ".gz");
  CHECK(os::extension("a.dir/file") == "");
  CHECK(os::extension(".gitignore") == "");
  CHECK(os::extension("a/.config.json") == ".json");
  CHECK(os::extension("file.") == ".");
  CHECK(os::extension("..") == "");
  CHECK(os::extension(".") == "");
  CHECK(os::extension("") == "");

  CHECK(os::stem("a/b.tar.gz") == "b.tar");
  CHECK(os::stem("a/b/c") == "c");
  CHECK(os::stem(".gitignore") == ".gitignore");
  CHECK(os::stem("..") == "..");

  CHECK(os::parent("a/b/c") == "a/b");
  CHECK(os::parent("a/b/") == "a/b");
  CHECK(os::parent("/a") == "/");
  CHECK(os::parent("a") == "");
  CHECK(os::parent("") == "");

  CHECK(os::is_absolute("/usr/bin"));
  CHECK(os::is_absolute("C:/Windows"));
  CHECK(!os::is_absolute("usr/bin"));
  CHECK(!os::is_absolute("C:relative"));
  CHECK(!os::is_absolute(""));
}

TEST(path_join_normalize) {
  Allocator h = heap();
  Arena arena = Arena::create(h);
  Allocator a = arena.allocator();

  auto join = [&](std::string_view l, std::string_view r) { return os::join(l, r, a); };
  CHECK(join("a", "b") == "a/b");
  CHECK(join("a/", "b") == "a/b");
  CHECK(join("", "b") == "b");
  CHECK(join("a", "") == "a");
  CHECK(join("a", "/abs") == "/abs");
  CHECK(join("/", "x") == "/x");
  std::string_view joined = join("assets", "tex.png");
  CHECK(joined.data()[joined.size()] == '\0');

  auto norm = [&](std::string_view p) { return os::normalize(p, a); };
  CHECK(norm("a/./b/../c") == "a/c");
  CHECK(norm("a//b///c/") == "a/b/c");
  CHECK(norm("a\\b\\c") == "a/b/c");
  CHECK(norm("./a") == "a");
  CHECK(norm("a/..") == ".");
  CHECK(norm("") == ".");
  CHECK(norm(".") == ".");
  CHECK(norm("../a") == "../a");
  CHECK(norm("../../a/../b") == "../../b");
  CHECK(norm("a/../../b") == "../b");
  CHECK(norm("/a/../..") == "/");
  CHECK(norm("/../a") == "/a");
  CHECK(norm("/") == "/");
  CHECK(norm("//a//b") == "/a/b");
  CHECK(norm("C:\\Games\\..\\Saves\\") == "C:/Saves");
  CHECK(norm("C:/..") == "C:/");
  CHECK(norm("C:") == "C:");
  CHECK(norm("a/b/c/../../d") == "a/d");
  std::string_view n = norm("x/./y");
  CHECK(n.data()[n.size()] == '\0');
  arena.destroy();
}
