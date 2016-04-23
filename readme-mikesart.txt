; new-ui command - new command to create extra console/mi UI channels
https://sourceware.org/ml/gdb-patches/2016-05/msg00107.html
https://sourceware.org/ml/gdb-patches/2016-03/msg00388.html
https://sourceware.org/ml/gdb-patches/2016-02/msg00067.html
https://sourceware.org/ml/gdb-patches/2016-05/msg00098.html
https://sourceware.org/ml/gdb-patches/2016-03/msg00396.html
https://sourceware.org/ml/gdb-patches/2016-02/msg00078.html
http://patchwork.sourceware.org/patch/12537/

Add these notes:
 - The init_pair routine accepts negative values of foreground and background color to support the use_default_colors extension, but only if that routine has been first invoked.
 - As an extension, ncurses allows you to set color pair 0 via the assume_default_colors routine, or to specify the use of default colors (color number -1) if you first invoke the use_default_colors routine.
 - The value of the first argument must be between 1 and COLOR_PAIRS-1, except that if default colors are used (see use_default_colors) the upper limit is adjusted to allow for extra pairs which use a default color in foreground and/or background.

; Test with different color fg/bg w/ xterm
; https://wiki.archlinux.org/index.php/Xterm
xterm -fg PapayaWhip -bg "rgb:00/00/80"

; Using Modern GDB/MI Programs
https://sourceware.org/gdb/wiki/GDB%20Front%20Ends

; Add signed off to already commited commits
git filter-branch --msg-filter 'cat - && echo && echo "Signed-off-by: Michael Sartain <mikesart@gmail.com>"' 2e48801..HEAD

; 2004 gdb discussion about gdbmi w/ Bob Rossi
https://sourceware.org/ml/gdb/2004-10/threads.html

; The gdb/mi interface documentation
https://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI.html
http://marc.info/?l=vim-dev&m=107702407620107

; GDB C++ Conversion
https://sourceware.org/gdb/wiki/cxx-conversion

; Automake
http://socgsa.cs.clemson.edu/seminar/tools06/resources/08_autotools/automake.htm

; Autotools Mythbuster
https://autotools.io/index.html

; Writing programs with NCURSES
http://invisible-island.net/ncurses/ncurses-intro.html
http://invisible-island.net/ncurses/ncurses.faq.html

; True Color support
https://github.com/neovim/neovim/issues/59
https://lists.gnu.org/archive/html/bug-ncurses/2014-01/msg00008.html

Also neovim --embed?
; Or vis?
https://github.com/martanne/vis/issues/250

; use libvterm to draw output from app?
https://neovim.io/doc/user/nvim_terminal_emulator.html#nvim-terminal-emulator
https://github.com/neovim/libvterm.git
; curses libvterm
https://github.com/cantora/ncte

; dvtm Console Window Manager
https://github.com/martanne/dvtm

; gdb annotations
https://sourceware.org/gdb/onlinedocs/annotate/index.html

/*
 * Features
 */

- Prime directive would be to stay light and fast. Given this, we think
embedding a full blown editor like neovim or building cgdb into a
terminal multiplexer like tmux or dvtm isn't the best route.

- Disassembly window. I really want this. I don't care about them as
much, but other windows probably also make sense: registers, locals,
watch, breakpoints.

- Window manager with status bars, and focus indication w/ vi type
keybindings to switch panes: <C-W>j, etc.

- Save/restore window positions or maybe workspace. Not sure what this
is, but right now I find myself readjusting the window split just about
every time I start up.

- In our current cgdb branch, we added a "scroll mode" to the gdb pane.
When you hit page up you enter this mode where arrow keys and <C-N>,
<C-P> scroll single lines up/down. We could also add / and ? to search -
hopefully just reuse the vi navigation code directly. Tmux has this type
of mode and I really like it.

- Investigate adding libvterm for a builtin terminal emulator. Neovim is
using this, and I can actually run cgdb inside a neovim pane
surprisingly well. It's super cool - getting this in would allow cgdb to
debug ncurses apps and see the full output directly in a cgdb pane.

> The other approach, which I was working towards with the gdbwire
> integration, is supporting starting gdb in server mode. This would
> allow cgdb to start, and put out a command for the user to run in
> another terminal to get the cgdb interface while the debugged program
> runs in the terminal the user started in.

Valgrind does this with it's gdb integration and it works great.

- Lexerize the gdb output pane. Doing "info reg" (or whatever) and
having it colorized would be cool I think.

- search to the gdb output window

- save gdb output to file
