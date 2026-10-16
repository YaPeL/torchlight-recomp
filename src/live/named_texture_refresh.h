// Textures the guest loaded by name whose file changed while the game runs (the pad prompts'
// textures when the connected pad changes style, mods/pad_prompts.h): the host draws the new file
// in the same guest texture from the next frame on, without the guest loading anything again.
//
// The guest loads a named texture once and keeps its own copy; in the native mode the host's copy
// is the one drawn, read from the file the texture resolver gives (mods::CurrentTextureResolver,
// LiveContentSource::NamedTexture). A refresh makes the backend thread create those textures again
// before its next frame, which re-resolves each name: the caller changes the resolver's locations
// first (mods::SetTextureLocations), then asks for the refresh. F9 captures resolve the files when
// they are taken, so they keep the new bytes. A session recording (--live_record) keeps the bytes
// of the texture's first description: a replayed session shows the files the session started
// with. With Xenos the guest's own copy is drawn and nothing changes.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace torchlight::live {

// Any thread. Names as the guest gives them or as files of the pak ("media/ui/icons2/widgets1.dds"),
// compared after mods::TextureNameKey and without our device prefix ("TLPROMPT:/..."), so a texture
// the guest named with or without one matches.
void RefreshNamedTextures(std::vector<std::string> names);

// The backend thread: the keys asked for since the last call, and the queue emptied.
std::vector<std::string> TakeTextureRefreshes();

// The key a texture name is compared by: mods::TextureNameKey, then without a device prefix.
std::string RefreshKey(std::string_view name);

}  // namespace torchlight::live
