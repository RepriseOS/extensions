# Cover Grid

Shows albums as a grid of covers, from a row in Music after Cover Flow and on the main menu. It uses Cover Flow's library query: the same music-only albums sorted by artist, and it does not open without music. Like Cover Flow, it reopens where it was left.

The grid shows four columns, two full rows and part of a third. The selected cover is lifted, with an edge in the selection colour and a shadow. Its album and artist scroll in the bar at the bottom. A scrollbar shows while the grid scrolls.

| Action | Result |
|---|---|
| Wheel | moves the selection, scrolling by whole rows once it leaves the two full rows |
| Select | opens the album's songs, at the top of the list |
| Menu | leaves the grid |

Covers load in the background through Cover Flow's loader, with its placeholder until they arrive. Each is scaled once and kept while the grid is on screen. Colours follow the stock roles, so themes apply.

Leaving the grid frees the covers. Opening an album keeps them for one second, then frees them.

| | |
|---|---|
| Default | off |
| Memory | the extension image while closed; about 160 KB of scaled covers and the artwork session while the grid is on screen |
| Settings | the two menu visibility flags |
