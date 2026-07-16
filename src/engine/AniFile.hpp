#ifndef ANI_FILE_HPP
#define ANI_FILE_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>

#include "IniReader.hpp"
#include "CompassDirection.hpp"
#include "PalletManager.hpp"
#include "Pallet.hpp"
#include "AnimationData.hpp"
#include "Animation.hpp"

class AniFile {
public:
    // A replacement pallet substitutes the art's recolorable ramp: its
    // entries after the index 0 sentinel overwrite the loaded pallet from
    // replacement_start on, the way the original recolors buildings
    static Animation * getAnimation(PalletManager * pallet_manager, const std::string &ztd_file, const std::string &file_name,
                                    const Pallet * replacement_pallet = nullptr, int replacement_start = 0);
private:
    static std::string getAnimationDirectory(IniReader * ini_reader);
    static AnimationData * loadAnimationData(PalletManager * pallet_manager, const std::string &ztd_file, const std::string &directory,
                                             const Pallet * replacement_pallet = nullptr, int replacement_start = 0);
};

#endif // ANI_FILE_HPP