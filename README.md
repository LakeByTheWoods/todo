# todo

Simple todo list command line utility.

## Licence
This is free and unencumbered software released into the public domain.  
See licence file for more details.

## Compiling
To compile first install [zig master](https://github.com/ziglang/zig).  
`cd` to todo git directory and run `zig build`  
The executable can be found in `zig-cache/bin`

## Usage
To add new entries to the todo list (default location ~/todolist) run `todo "Descpription of entry 1" "Description of entry 2" "Description of entry 3"`
To view and update entries in curses interface just run `todo`

### Interface Controls
Up/Down arrows change highlighted entry  
Right arrow cycle through progress of selected entry (Not Started, Doing, Review, Done)
Left arrow reset entry to Not Started
Press ! to mark entry as High Priority
Press d to Delete entry (entries are never truly deleted, just moved to ARCHIVE section)

