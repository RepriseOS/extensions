# Album Artists

Partly adapted from the RepriseOS core's [Album Artists](https://github.com/RepriseOS/repriseos/blob/bfedd72/payload/album_artists.c) and its [index](https://github.com/RepriseOS/repriseos/blob/bfedd72/payload/album_artists_index.h), by lgreitis.

Adds Album Artists after Artists in Music, and optionally to the main menu, listing the library by the iTunes Album Artist tag with the Artist as the fallback. Opening a group shows its albums with their artists as subtitles and their artwork, on the stock Artists and Albums screens. Settings > Music Menu and Main Menu show or hide the two rows.

| | |
|---|---|
| Sort | the Sort Album Artist or Sort Artist tag when iTunes set one; otherwise the name, with Apple's rules for leading articles |
| Jump letters | from the Sort As text, "#" for digits and symbols |
| No artist | one "Unknown" group |

Apple's importer discards the Album Artist, Sort Artist and Sort Album Artist tags, so the extension keeps them while the library loads. A song whose Album Artist is its Artist and that carries no Sort As tag is not kept: it lands in the same group either way. File Info shows an Album Artist row for the songs whose Album Artist differs from their Artist.

On first start the two rows take over the Music Menu and Main Menu checkboxes of the Album Artists row that earlier RepriseOS releases built in.

| | |
|---|---|
| Default | on |
| Memory | 20 bytes per song whose Album Artist differs from its Artist or that carries a Sort As tag, plus those strings in Apple's artist cache; up to twice that as the load finishes; the list's rows while the list is open and until Apple's selection and artwork cache let it go |
| Settings | the two menu visibility flags |
