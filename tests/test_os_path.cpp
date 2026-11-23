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
  CHECK(os::parent("C:/saves") == "C:/"); // the drive's root, not "C:"
  CHECK(os::parent("C:/saves/slot1") == "C:/saves");
  CHECK(os::parent("C:/") == "C:/");
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
  CHECK(join("a\\b", "c\\d") == "a/b/c/d"); // backslashes never survive
  CHECK(join("a\\", "c") == "a/c");
  CHECK(join("", "c\\d") == "c/d");
  CHECK(join("a\\b", "") == "a/b");
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
  CHECK(norm("/a//b") == "/a/b");
  CHECK(norm("///a//b") == "/a/b"); // three or more is not UNC
  // UNC paths keep their "//server/share" root, and ".." cannot climb out of it.
  CHECK(norm("//server/share") == "//server/share");
  CHECK(norm("\\\\server\\share\\dir\\file.txt") == "//server/share/dir/file.txt");
  CHECK(norm("//server/share/a/../b//c/") == "//server/share/b/c");
  CHECK(norm("//server/share/..") == "//server/share");
  CHECK(norm("//server/share/../../x") == "//server/share/x");
  CHECK(norm("//server") == "//server");
  CHECK(norm("C:\\Games\\..\\Saves\\") == "C:/Saves");
  CHECK(norm("C:/..") == "C:/");
  CHECK(norm("C:") == "C:");
  CHECK(norm("a/b/c/../../d") == "a/d");
  std::string_view n = norm("x/./y");
  CHECK(n.data()[n.size()] == '\0');
  arena.destroy();
}
