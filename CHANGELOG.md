# Changelog

## 2026-11-27

- Same as the other 3DS ports (cross-port pass): the START menu has a LEFT HAND row before EXIT GAME (the same switch as the manual's "Left hand"; it flips at once and the menu stays open).
- The exit question names the game ("Exit Ultima IV?" or "Exit Ultima VI?"), and its buttons read YES / NO (was "YES, EXIT"). Second line: "It is saved first (its autosave slot)." or "Not saved here: progress since your last save is lost."

## 2026-11-22

- Fixed: opening a ScummVM window (START > SAVE / LOAD / OPTIONS, or L) while the game was on the top screen stopped ScummVM with both screens frozen (seen on the Ultima IV title screen). The screens now switch to the normal layout before the window is sized.
- START menu: SAVE GAME and LOAD GAME are their own entries and go straight to the slot list; OPTIONS opens ScummVM's menu. Entries that cannot be used are greyed with the reason.
- B closes ScummVM's own windows (it was Y only).
- While a ScummVM window is open in a game, the top screen says what it is and how to get back.
- Title screens: Ultima IV shows "Continue to the menu" during the title and the menu's own five entries at the menu; Ultima VI shows its five menu entries by name (no more "A" and "B") and "Skip the intro film".
- Ultima VI: while the game's own menu is open the touch screen shows arrows, Enter and Cancel.
- Ultima IV title: Journey Onward says "(no saved game yet)" on its button when there is nothing to continue.
- ScummVM's windows are laid out for the touch screen (the save list was cut off at the right).
- Left-hand mode now also applies in the START menu and on the Choose a Game screen, and the hints name the right buttons.
- START menu footer has the same wording as the other ports; the exit question says its explanation once.
- The top-screen hint strip (game on the touch screen) names the play buttons.
- Manual: the section being read has a 2 px gold ring.

## 2026-11-21

- Ultima IV: Journey Onward after EXIT GAME no longer stops ScummVM (the exit save is in slot 0, which the game refused to load).
- The Choose a Game screen pages when you have more than five games (NEXT PAGE button, or L / R); the page keys no longer go into the wrong menu.
- The Start menu works out once per opening whether EXIT GAME can save (before, it did the check on every redraw, which made the menu slow in a game).
- The manual's page-number buffer is safe, and an unhandled-case warning is gone.
- Quieter touch click.

## 2026-11-20

- Journey Onward now finds the exit save, and EXIT GAME asks "Exit anyway?" when the save failed ("Not saved!").
- HOME > Close saves like EXIT GAME; the game keeps 4 MB of memory free.
- The build uses the pinned ScummVM commit, not a moving master.
- Install path: the program is /3ds/scummvm.3dsx. The START menu shows the build version.

## 2026-11-19

- Manual: drag the stylus to scroll the text, with a little momentum.
