# Browse tree self-test
`run.sh` compiles the plugin's real `browseDidl()` and `soapActionName()` (extracted from `dlna_server.cpp` when it runs)
against a stub server, then crawls the whole tree like a renderer: root -> Artists / Albums / Genres / Folders / All Tracks.
It checks well-formed DIDL-Lite, `childCount` == `TotalMatches`, `parentID` links, that paging equals the full listing,
that every view reaches every track, that invalid ids and `RequestedCount = 0xFFFFFFFF` are handled, and SOAP action recognition.
