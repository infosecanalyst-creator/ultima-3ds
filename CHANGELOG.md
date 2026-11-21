# Changelog

## 2026-11-21

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
