# Release Notes v0.0.23

## Major Features

### SID Subsongs Support
- Navigation between subsongs in a SID file (Previous/Next buttons + direct selection)
- Display "Subsong: X / Y" in the player
- Respects the file's default subsong

### Rating Filter by Stars
- New multi-criteria filter (author, year, **rating**)
- >= or = operator to filter by star count
- Works in combination with other filters

## Major Fixes

### CPU Bug with Active Filters
- Fixed CPU spike when launching a track from history/search with active filters
- Automatic filter deactivation if the file doesn't match

### Playlist Navigation with Filters
- Automatic "next song" navigation now respects active filters
- Fixed focus loss in playlist tree

## Minor Improvements

- Integrated labels in filters ("All Author", "All Year", "All Stars") - space saving
- Clickable file name in player to navigate to the track
- Microchip + music icons for current file
- Hand cursor on hover for rating stars
- Rainbow color offset slider in config (0-255)
- Loop state restoration at startup
- Cleanup: removed temporary files and workspace from git

---
**6 commits** | **~220 lines added** | **Since v0.0.22**
