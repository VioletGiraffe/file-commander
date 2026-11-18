# Styling: deferred work

The Qt app is getting selectable themes built on the `qtutils/theme` module. This lists the work deferred past the
first version (v1). Current state only; done items get removed.

## Terms

- **Theme**: colours (a `CBasePalette` plus app tokens), a structure, the file list's paint parameters (cursor
  form, selection shape, column lines, row bands and rules), whether controls use a monospace font, and stylesheet rules of its own.
- **Structure**: the geometry and stylesheet template that several themes share: corner radius, row height,
  paddings, gaps.
  - *Rounded*: panels are cards on the window background; rows, fields, buttons and tabs have rounded corners.
  - *Flat*: square corners, no gaps, 1 px line frames.
- `CThemeController` stores one theme name per polarity, so the light and the dark theme are chosen independently.

## v1 scope

- The app always runs a theme, on the Fusion style. There is no unthemed mode.
- The rounded structure with one theme, Cobalt (neutral greys, blue accent), in both polarities.
- A theme picker per polarity replaces the style combo box and the raw stylesheet box.
- The file list paints its own cursor, selection and row geometry. Selection is a filled row.
- The file list font stays a user setting, separate from the theme. Other controls use the system font.

## v2

### Flat structure: what is left

Blueprint is the flat theme, in both polarities.

- To try: selected rows and folder names in bold; the function keys as bare numbers.

### Further themes

Meadow, Dusk, Greenbar, Departures and Notebook come in one polarity each; their counterparts in the other polarity come later.

- Meadow, done: light with a sage tint. Its controls have an 11 px radius: the stylesheet engine drops a radius
  larger than half the control's height, so a true pill would need a radius per control.
- Dusk, done: dark with a plum tint, panels edge to edge, the active panel has a lighter surface, amber accent.
- Ledger, done: typographic, no boxes, hairline-ruled rows, the folder name as a large title.
  Light is white with a blue accent; dark is a warm grey with an amber accent.
- Greenbar, done: light, flat, the file list banded like green bar printer paper.
- Departures, done: dark, flat, every row a strip of a split-flap board, yellow names.
- Notebook, done: light, ruled rows with a red margin line between the icons and the names.
- Not scheduled: light, black and white, 2 px black rules, uppercase labels, one highlighter colour for the selection.

Rejected: a dense grey look with square segmented controls. It resembles stock Linux styles.

Whether the cursor or the selection is the more prominent of the two may differ between themes.

### Theme icons

Every theme draws a folder glyph in place of the OS's default folder icon, and a file glyph in place of its icon for a
type no program claims. A drive button has a glyph for its kind of drive, and none for an internal drive. Every other
icon is the OS's, unchanged.

A drive glyph takes the button's text colour in every state: the stylesheet builder derives an icon rule from each
rule that colours a drive button.

### Smaller items

- To try: folder names in a heavier weight.

## v3

### Type colouring

File names coloured by extension group.

- Colour then marks the type, so text colour alone must never mark anything else, selection included.
- The theme owns the colour set. Each colour must stay readable on the plain surface and on the selection fill;
  `ColorUtils` can derive a colour that reaches a target contrast.
- A solid cursor bar has one text colour, so the cursor row loses its type colour. A tinted or frame cursor keeps it.
  Both are acceptable.
- Open: the extension groups, and whether the user can edit them.

### Smaller items

- Potential, low priority: the window title bar in the theme's colours. On Windows 11 `DwmSetWindowAttribute` sets the
  caption, text and border colours of one window; every top-level window needs the call. Windows 10 offers dark or light only.
- The text viewer plugin picks its syntax theme by polarity only; it should follow the active theme.
- Compare with the theme integration of the other apps built on `qtutils`, and move the shared parts into `qtutils`.
  Deliberately after v1, so that their existing choices do not shape the v1 design.
