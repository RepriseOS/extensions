# File Info

Based on the RepriseOS core's [File Info](https://github.com/RepriseOS/repriseos/blob/acbb928/payload/file_info.c), by lgreitis and paviro.

Adds File Info to the context menu (hold Select) of a song, podcast episode, iTunes U episode, audiobook, movie, TV episode or video playlist entry, and to the Now Playing context menu. Other extensions can open it for a record through its service. The screen lists the item's tags and file properties.

| Media kind | Rows |
|---|---|
| Song | Title, Artist, Album Artist, Album, Year, Genre, Composer, Grouping, Comment, Duration, Track, Disc, Kind, Sample Rate, Bitrate, File Size, Volume Adjustment, Rating, Play Count, Last Played, Skip Count, Last Skipped, Date Added, Date Modified |
| Podcast episode | Title, Podcast, Artist, Category, Genre, Description, Release Date, Duration, Chapters, Kind, Sample Rate, Bitrate, File Size, Volume Adjustment, Play Count, Last Played, Date Added |
| iTunes U episode | as a podcast episode, with Album in place of Podcast |
| Audiobook | Title, Author, Book, Genre, Description, Duration, Chapters, Kind, Sample Rate, Bitrate, File Size, Volume Adjustment, Play Count, Last Played, Date Added |
| Music video | Title, Artist, Album Artist, Album, Year, Genre, Comment, Duration, Kind, Bitrate, File Size, Volume Adjustment, Rating, Play Count, Last Played, Date Added |
| Movie, TV episode, rental | Title, Show, Year, Genre, Description, Release Date, Duration, Kind, Bitrate, File Size, Volume Adjustment, Rating, Play Count, Last Played, Date Added |

Rows without a value are left out. Album Artist comes from the Album Artists extension while it runs, and only when it differs from the Artist.

| Row | Format |
|---|---|
| Kind | iTunes's file kind, such as "AAC audio file" |
| Track, Disc | "3 of 12" when the total is known |
| dates | local time, "2024-03-05 14:07" |
| Volume Adjustment | the Sound Check offset as a percentage |
| Rating | stars out of five, or Unrated |
| File Size | MiB with one decimal |

Values are read from Apple's library when the screen opens, and go blank if the library reloads.

| | |
|---|---|
| Default | on |
| Memory | image only, plus about 130 bytes for the row table and the item it shows |
| Settings | none |
