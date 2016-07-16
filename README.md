### CGDB fork which adds features / fixes.

Please let us know if you try any of the below and run into any issues or have any comments. Thanks!

#### Features
- vertical split mode (Ctrl+W in source window)
- add ansi escape color parsing
- "set ansiescapecode" and "set color" cgdbrc options
- alt+f, alt+b now work in readline
- added arrowstyle for source window with long, short, highlight options
- add utf8 support (linking with ncursesw)
- add G, gg, ##G support
- add "scroll mode" to GDB window with location status line
    Has home / end, C-P, C-N, arrow up/down support
- optimized source file loading and token parsing: loading sqlite3.c went from 2:48 minutes to 2 seconds
- :help now works from source tree, has color for highlighting groups and color names
- implement ^L for clearing gdb window
- use wnoutrefresh/doupdate instead of wrefresh when drawing ncurses windows. Eliminates a lot of flashing.
- use strcasecmp for default file type (fixes fltk .H parsing)
- add support for local and global marks (including '., '')
- add C-U, C-D, g, gg, G support to filedlg
- Add support for <digits>j, <digits>k source movement
- Color logos. Woot! :)
- Pull Armin Widegreen's rust syntax highlighting patch
- Show disassembly when no source is available
- Add ":set [no]dis" command to toggle source / disassembly
- Add regex search to gdb output window
- ESC+s (or s in source window) sets focus to gdb and enables scroller mode
- Several keys now enabled in gdb window in scroller mode:  
      m[A-Z]: set global mark  
      m[a-z]: set local mark  
      '[a-zA-Z]: jump to mark  
      q, i: exit scroller mode  
      '': jump to last jump location  
      '.: jump to bottom of buffer  
      Ctrl+U, Ctrl+D: page up, page down  
      gg: jump to top of bufer  
      G: jump to bottom of buffer  
      k: up line  
      j: down line  

### Minor tweaks / bug fixes
- update configure version number
- fix several shadow warnings
- cgdb builds cleanly with g++ and Wall, Wextra, and Wshadow
- fix several memory leaks
- add -w option which waits for debugger to attach on startup
- ibuf copies blocks of strings instead of individual characters
- don't execute gdb tui commands: they hang cgdb
- fix .gdbinit breakpoints executing in cgdbrc file but not being shown
- replace log10 calls with log10_uint, stop linking with libm
- Kill tgdb client interface pointers - compile time build option now
- don't exit on hl_groups_get_attr() failures
- parse cgdbrc before if_init() is called
    fixes bugs with default colors being used and shown before rc file is parsed.
- add terminal color test app
- add build checks for ttyname_r, ptsname_r, pty.h, and openpty check
- allow setting CGDG_VERSION for autogen.sh. Ie: CGDB_VERSION=$(git describe --tags) ./autogen.sh
- add attribute printf support
- fix incorrect printf format specifiers
- fix several spelling, grammar mistakes
- switch several malloc calls to cgdb_malloc
- check file existence before adding to file dialog
- fix several flashing issues
- display exit code in status bar when inferior exits
- clean up log file output
- move log files to ~/.cgdb/logs
- check for kill command and hide executing marker

CGDB
====

CGDB is a very lightweight console frontend to the GNU debugger.  It provides
a split screen interface showing the GDB session below and the program's
source code above.  The interface is modelled after vim's, so vim users should
feel right at home using it.

Screenshot, downloads, and documentation are available from the home page:
http://cgdb.github.com/

Official source releases are available here:
http://cgdb.me/files
