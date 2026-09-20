# Build Validation — v0.8 Alpha 3 L

## View → SACD DLNA crash fix

Alpha 3 K contained a fatal `mainmenu_commands::get_command()` omission: the commands
`Refresh DSD Music Library`, `Stop Sharing Music Library`, and `Clear Persistent Cache`
were present in `get_command_count()` / `get_name()` / `execute()` but had no GUID return
cases. When foobar2000 enumerated the View → SACD DLNA menu, `uBugCheck()` was reached,
which could crash foobar2000 before any menu item was clicked.

Alpha 3 L adds dedicated stable GUIDs and explicit `get_command()` cases for all three
commands. The existing audio/server implementation is otherwise unchanged.

## User-confirmed baseline

Alpha 3 I was confirmed by the user as compiling without errors and working at runtime.
Alpha 3 L is based on the later menu/roadmap tree and requires a fresh Windows/MSVC
rebuild to validate this revision.
