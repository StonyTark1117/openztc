#ifndef MOD_MANAGER_HPP
#define MOD_MANAGER_HPP

#include <functional>
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

  // How to list the resource names inside an archive, usually
  // ZtdFile::getFileList. Injected to keep the archive format out of here.
  void setArchiveLister(std::function<std::vector<std::string>(const std::string &)> lister) { this->archive_lister = lister; }

  // Names of other enabled mods sharing resource names with the mod at
  // index. Earlier mods win, so mods before it override it and mods after
  // it are overridden by it.
  std::vector<std::string> getConflicts(int index);

  // Loadouts are saved mod states: openztc-loadout-<name>.txt files in the
  // mods directory with the same format as the state file. Applying one
  // reorders and toggles the known mods to match and saves it as current.
  std::vector<std::string> listLoadouts();
  bool applyLoadout(const std::string &name);

private:
  std::string mods_directory;
  std::string state_file;
  std::vector<ModInfo> mods;
  std::function<std::vector<std::string>(const std::string &)> archive_lister;
};

#endif // MOD_MANAGER_HPP
