// gow3 (iOS): AvPlayer needs FFmpeg, which has no iOS build here yet. No libSceAvPlayer functions
// are registered, so the loader reports the import as unresolved if the game ever calls one.
#include "core/libraries/libs.h"

namespace Libraries::AvPlayer {
void RegisterLib(Core::Loader::SymbolsResolver*) {}
} // namespace Libraries::AvPlayer
