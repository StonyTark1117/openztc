#include <doctest.h>

#include <filesystem>
#include <fstream>

#include "../src/engine/ModManager.hpp"

// A scratch mods directory with fake ztd files, removed on destruction
class ModsDirectory {
public:
  ModsDirectory(std::vector<std::string> file_names) {
    this->path = (std::filesystem::temp_directory_path() / "openztc-mod-test").string();
    std::filesystem::remove_all(this->path);
    std::filesystem::create_directory(this->path);
    for (const std::string &file_name : file_names) {
      std::ofstream(this->path + "/" + file_name) << "fake";
    }
  }
  ~ModsDirectory() {
    std::filesystem::remove_all(this->path);
  }
  std::string path;
};

TEST_CASE("a missing mods directory yields no mods") {
  ModManager manager("/nonexistent/openztc-mods");
  manager.load();
  CHECK(manager.getMods().empty());
  CHECK(manager.getEnabledArchives().empty());
}

TEST_CASE("new mods appear enabled in name order") {
  ModsDirectory dir({"zebra.ztd", "aardvark.ztd", "notamod.txt"});
  ModManager manager(dir.path);
  manager.load();
  REQUIRE(manager.getMods().size() == 2);
  CHECK(manager.getMods()[0].file_name == "aardvark.ztd");
  CHECK(manager.getMods()[1].file_name == "zebra.ztd");
  CHECK(manager.getMods()[0].enabled);
  CHECK(manager.getMods()[1].enabled);
  CHECK(manager.getEnabledArchives().size() == 2);
}

TEST_CASE("toggling and moving persist across a reload") {
  ModsDirectory dir({"a.ztd", "b.ztd", "c.ztd"});
  {
    ModManager manager(dir.path);
    manager.load();
    manager.toggle(1);         // disable b
    manager.move(2, -1);       // c above b
  }
  ModManager reloaded(dir.path);
  reloaded.load();
  REQUIRE(reloaded.getMods().size() == 3);
  CHECK(reloaded.getMods()[0].file_name == "a.ztd");
  CHECK(reloaded.getMods()[1].file_name == "c.ztd");
  CHECK(reloaded.getMods()[2].file_name == "b.ztd");
  CHECK(reloaded.getMods()[1].enabled);
  CHECK(!reloaded.getMods()[2].enabled);
  std::vector<std::string> archives = reloaded.getEnabledArchives();
  REQUIRE(archives.size() == 2);
  CHECK(archives[0] == dir.path + "/a.ztd");
  CHECK(archives[1] == dir.path + "/c.ztd");
}

TEST_CASE("mods removed from the directory drop from the state") {
  ModsDirectory dir({"a.ztd", "b.ztd"});
  {
    ModManager manager(dir.path);
    manager.load();
    manager.toggle(0);
  }
  std::filesystem::remove(dir.path + "/a.ztd");
  ModManager reloaded(dir.path);
  reloaded.load();
  REQUIRE(reloaded.getMods().size() == 1);
  CHECK(reloaded.getMods()[0].file_name == "b.ztd");
}

TEST_CASE("moving past the ends does nothing") {
  ModsDirectory dir({"a.ztd", "b.ztd"});
  ModManager manager(dir.path);
  manager.load();
  CHECK(!manager.move(0, -1));
  CHECK(!manager.move(1, 1));
  CHECK(manager.move(1, -1));
  CHECK(manager.getMods()[0].file_name == "b.ztd");
}

TEST_CASE("conflicts list other enabled mods sharing resource names") {
  ModsDirectory dir({"a.ztd", "b.ztd", "c.ztd"});
  ModManager manager(dir.path);
  manager.setArchiveLister([&dir](const std::string &archive) -> std::vector<std::string> {
    if (archive == dir.path + "/a.ztd") {
      return {"shared/file.tga", "a-only.txt"};
    }
    if (archive == dir.path + "/b.ztd") {
      return {"shared/file.tga", "b-only.txt"};
    }
    return {"c-only.txt", "some-directory/"};
  });
  manager.load();
  std::vector<std::string> conflicts = manager.getConflicts(0);
  REQUIRE(conflicts.size() == 1);
  CHECK(conflicts[0] == "b.ztd");
  CHECK(manager.getConflicts(2).empty());
  // Disabled mods do not conflict
  manager.toggle(1);
  CHECK(manager.getConflicts(0).empty());
}
