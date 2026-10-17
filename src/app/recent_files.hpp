// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace lundukepaint {

// Round 15 P-4: register an opened or saved image with the desktop's shared
// recent-files list (Gtk::RecentManager, ~/.local/share/recently-used.xbel),
// so the file chooser's "Recent" place and desktop menus show Paint files.
// Paint's own File > Open Recent list is kept in preferences as before.
// Returns false when the path is not absolute or GTK refused the item.
bool register_recent_file(const std::string& path);

// MIME type Paint registers for a path, by extension ("" lets GTK guess).
std::string recent_mime_for_path(const std::string& path);

}  // namespace lundukepaint
