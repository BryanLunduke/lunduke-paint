// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_APP_LIVE_EDIT_HPP
#define LUNDUKEPAINT_APP_LIVE_EDIT_HPP

namespace lundukepaint {

// What to do with an open text box or an in-progress polygon / polyline / curve.
enum class LiveEditAction { Leave, Stamp, Cancel };

enum class CloseAnswer { Cancel, Discard, Save };

// Quit and Close ask first. Cancel must not stamp. Save stamps, then writes.
// Discard drops the overlay with the document.
inline LiveEditAction live_edit_for_close(CloseAnswer answer) {
  switch (answer) {
    case CloseAnswer::Cancel:
      return LiveEditAction::Leave;
    case CloseAnswer::Save:
      return LiveEditAction::Stamp;
    case CloseAnswer::Discard:
      return LiveEditAction::Cancel;
  }
  return LiveEditAction::Leave;
}

// Switching tabs leaves the edit on the document it was typed into.
// An empty text box is closed. A word, or a polygon that is already on
// screen, is stamped as one undo step.
inline LiveEditAction live_edit_for_tab_switch(bool text_editing, bool text_nonempty,
                                              bool persistent_preview) {
  if (text_editing) {
    return text_nonempty ? LiveEditAction::Stamp : LiveEditAction::Cancel;
  }
  if (persistent_preview) {
    return LiveEditAction::Stamp;
  }
  return LiveEditAction::Leave;
}

}  // namespace lundukepaint

#endif
