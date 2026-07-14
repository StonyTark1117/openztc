#ifndef MOD_MANAGER_HPP
#define MOD_MANAGER_HPP

#include <string>
#include <vector>

// A mod is a ztd archive in the mods directory next to the game data. The
// list order is the load order: earlier mods win when two mods carry the
// same resource, and every mod wins over the game archives. The enabled
// flags and the order persist in a plain text state file in the same
// directory, so the directory stays self contained.
typedef struct {
  std::string file_name;
  bool enabled;
} ModInfo;

class ModManager {
public:
  ModManager(const std::string &mods_directory);

  // Reads the state file and scans the directory. Mods that appeared since
  // the last run are appended enabled, like dropping a ztd into the game
  // directory of the original.
  void load();
  void save();

  const std::vector<ModInfo> &getMods() { return this->mods; }
  void toggle(int index);
  // Moves the mod up (-1) or down (1) in the load order
  bool move(int index, int direction);

  // Full paths of the enabled archives in load order
  std::vector<std::string> getEnabledArchives();

private:
  std::string mods_directory;
  std::string state_file;
  std::vector<ModInfo> mods;
};

#endif // MOD_MANAGER_HPP
