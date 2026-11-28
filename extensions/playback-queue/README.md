# Playback Queue

Partly adapted from the RepriseOS core's [Play Next](https://github.com/RepriseOS/repriseos/blob/c997a51/payload/play_next.c) and [queue selection](https://github.com/RepriseOS/repriseos/blob/c997a51/payload/queue_selection.c), by lgreitis and brilliant-alien. The Up Next screen follows the one Alex Ball added in [e501359](https://github.com/RepriseOS/repriseos/commit/e501359).

Adds Play Next and Play Last to the context menu of a song, album, artist or audio podcast episode, and an Up Next screen. Play Next inserts at the front of the queue, Play Last at the end; albums and artists keep their order. Up Next lists the upcoming items' titles and artists or publishers, without the current item, or "No Upcoming Items".

| Action | Result |
|---|---|
| Select an item | plays it in Now Playing; it and the items before it leave the queue but stay in the history for Previous |
| Hold Select on an item | move it up or down, send it to the top or bottom, remove it, browse its album and artist, or open File Info; moves that would change nothing are hidden |
| Clear Up Next, from the Up Next or Now Playing context menu | removes every upcoming item after a confirmation; the current item and repeat are kept |

Browse Album and Browse Artist go through Music as the stock song menus do; a compilation track gets Browse Compilation in their place when the stock menus offer it. File Info shows while the File Info extension runs.

After a removal the highlight moves to the item above, or stays at the top. Returning to Up Next highlights the first item. The list follows playback.

Adding items while nothing plays builds the queue without starting playback; Play/Pause starts the first item. Adding items while paused keeps playback paused. Playing a selection from the library replaces the queue.

Podcast episodes keep Apple's saved position and played status. Video podcasts and whole shows cannot be added. Only a player-owned list of media entries without nested groups can be edited; other lists are left alone.

Up Next shows in Music and not on the Main Menu by default; Settings > Music Menu and Settings > Main Menu change that. On first start a saved Main Menu checkbox of the Up Next row that earlier RepriseOS releases built in carries over, and Up Next then stays out of Music as it did there. Its code stays loaded while the extension is on.

| | |
|---|---|
| Default | on |
| Memory | image only |
| Settings | the two menu visibility flags |
