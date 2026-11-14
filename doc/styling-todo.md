# Styling: deferred work

The Qt app is getting selectable themes built on the `qtutils/theme` module. This lists the work deferred past the
first version (v1). Current state only; done items get removed.

## Terms

- **Theme**: colours (a `CBasePalette` plus app tokens), a structure, and the file list's paint parameters (cursor
  form, selection shape, column lines).
- **Structure**: the geometry and stylesheet template that several themes share: corner radius, row height,
  paddings, gaps.
  - *Rounded*: panels are cards on the window background; rows, fields, buttons and tabs have rounded corners.
  - *Flat*: square corners, no gaps, 1 px line frames, a monospace font in every control.
- `CThemeController` stores one theme name per polarity, so the light and the dark theme are chosen independently.

## v1 scope

- The app always runs a theme, on the Fusion style. There is no unthemed mode.
- The rounded structure with one theme, Cobalt (neutral greys, blue accent), in both polarities.
- A theme picker per polarity replaces the style combo box and the raw stylesheet box.
- The file list paints its own cursor, selection and row geometry. Selection is a filled row.
- The file list font stays a user setting, separate from the theme. Other controls use the system font.
- The theme definition is designed for both structures; only the rounded one is implemented.

## v2

### Flat structure and its first theme

- First theme: dark navy surfaces, cyan accent.
- The panel's path is drawn into the top edge of the panel frame. The active panel's path is highlighted and its
  frame takes the accent colour.
- Column separator lines run the full height of the list, below the last row too.
- F-key bar: the key number as plain text, the label on an accent-filled cell.
- Requires a per-theme font for the controls other than the file list.
- A light flat theme follows the dark one.

### Further themes

Mocked and liked, not scheduled:
- Light with a sage tint: pill-shaped controls, a solid cursor bar, teal accent.
- Dark with a plum tint: panels edge to edge, the active panel has a lighter surface, amber accent.
- Light, typographic: no boxes, hairline-ruled rows, the folder name as a large title.
- Light, black and white: 2 px black rules, uppercase labels, one highlighter colour for the selection.

Rejected: a dense grey look with square segmented controls. It resembles stock Linux styles.

Whether the cursor or the selection is the more prominent of the two may differ between themes.

### Type colouring

File names coloured by extension group.

- Colour then marks the type, so text colour alone must never mark anything else, selection included.
- The theme owns the colour set. Each colour must stay readable on the plain surface and on the selection fill;
  `ColorUtils` can derive a colour that reaches a target contrast.
- A solid cursor bar has one text colour, so the cursor row loses its type colour. A tinted or frame cursor keeps it.
  Both are acceptable.
- Open: the extension groups, and whether the user can edit them.

### Theme icons

Colourful OS icons clash with the more stylized themes.

- Theme-drawn glyphs for at least the folder and the plain file, through `CTintedSvgIconEngine`.
- Processing of system icons (desaturation, tint) where the icon provider caches them.
- Open: one of the two or both, and which files keep their own OS icon.

### Breadcrumb path bar

Replaces the panel's path combo box.

- Each path component is a button that navigates to it.
- Must keep what the combo box provides: typing a path, and quick access to the global history drop-down.
- Each component has its own drop-down: the history entries under that component's path, a separator, then that
  folder's subfolders as listed at the moment of clicking.
- Listing on click is considered low-risk: every component is an ancestor of a path that was just listed.
- Open: whether that listing may run on the UI thread (see [threading.md](threading.md)). The doubtful case is an
  ancestor on a network path.

### Smaller items

- A disk usage bar beside the free-space label.
- The text viewer plugin picks its syntax theme by polarity only; it should follow the active theme.
- Compare with the theme integration of the other apps built on `qtutils`, and move the shared parts into `qtutils`.
  Deliberately after v1, so that their existing choices do not shape the v1 design.
