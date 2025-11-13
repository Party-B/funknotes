# funknotes
FunkNotes is a tiny command-line note-taking tool written in C. It stores data as plain text files under
`$HOME/.funknotes/projects/` and keeps a small `config.txt` for the primary project and a project counter. FunkNotes (From the idea of function based notes - originally I made this in vba as class oriented notes so you could do funk.new(proj) etc).

Funknotes is project-centric - that is, the application is developed with the idea that you'll mainly be working in one project, not switching between a heap. This means a lot of the efficiency I was aiming for is to make dealing with your one project quick. All parts of the project are generic "Objects" so you can name them however you like - some examples, BUGS, TODOS, PLANS etc.

This README summarizes current features, recent changes, and common commands.

Notes
- Initial prototype was produced with AI-assisted iterative development.
- Intended to run on Unix-like systems (macOS, Linux). Build requires only gcc (no external dependencies).
- You may want to symlink the compiled binary into `/usr/local/bin` for convenience.

Latest changes (2025/11/08)
Shell & Interactive Modes
- `funknotes shell` enters a REPL where you can run funknotes commands interactively. Exit with `q`, `quit`, `exit`, `drop`, or Ctrl+C.
- Object shell mode: `funknotes add <object>`, `funknotes new <object>`, or `funknotes open <object>` (with no text argument) enters an interactive shell for that object, allowing you to add items line by line. Exit with `q`, `quit`, `exit`, or `drop`.
	- In object shell mode:
		- Type `delete` to enter delete shell for that object (delete items by number/range interactively).
		- Type `show` to refresh and display the object's items.
		- Type `clear` to clear the terminal.
		- All other input adds a new item to the object.
	- In delete shell, enter a number or range (e.g. `3`, `2-5`) to delete items, or exit as above.


Latest changes (2025/11/07)
- Added safe deletion features:
	- `funknotes delete object <name>` now prompts before deleting an object from the primary project.
	- `funknotes delete <object> <index>` deletes a single 1-based item from an object (interactive confirmation).
	- `funknotes delete <object> <indexes>` accepts comma-separated indexes and ranges (e.g. `2,4,6-8`) to delete multiple items in one operation; prompts once before applying changes and records history entries for each deleted item.
	- `funknotes delete project <name|index>` and `funknotes delete projects <a,b,c>` allow deleting projects (by name or index); primary is unset if deleted.
- Auto-create objects on add: `funknotes add <object> <text>` will create the object if it doesn't exist; in interactive shells it prompts "The object '<name>' does not exist, create it? Y/n" (default: yes).
- `show` improvements: `funknotes show` lists objects in the primary project; `funknotes show <project>` accepts project name or index and lists that project's objects; `funknotes show <project> <object>` shows items in a specific object in any project.
- Projects listing: `projects` command (renamed from `list`) lists all projects with their index and primary marker.
- Merge utilities:
	- `funknotes merge projects <proj1,proj2,...,target>` — merge multiple projects into the target (last) project; prompts and optionally deletes sources after merge.
	- `funknotes merge <project> <obj1,obj2,target>` — merge objects within a single project.
- Search: added a case-insensitive substring search that accepts multiple keywords (AND semantics) and can be scoped to an object: `funknotes search [<object>] <keywords...>`.

Quick build
You need gcc installed. Example on macOS:

```bash
gcc -o funknotes funknotes.c
```


Commands / Usage (high level)

- Create a new project
	- funknotes new project <name>
- Set primary project (by name or index)
	- funknotes primary <name|index>
- Create an object in the primary project
	- funknotes object <name>
- Add an item to an object
	- funknotes add <object> <text>
	- or: echo "text" | funknotes add <object>
	- If the object doesn't exist you'll be prompted to create it (interactive shells).
	- If you run `funknotes add <object>` (with no text), you enter object shell mode for that object.
- Object shell mode
	- funknotes add <object>
	- funknotes new <object>
	- funknotes open <object>
	- In shell: type lines to add items, `delete` to enter delete shell, `show` to refresh, `clear` to clear, exit with `q`, `quit`, `exit`, or `drop`.
- List projects
	- funknotes projects
- Show objects / items
	- funknotes show                # list objects in primary
	- funknotes show <project>      # list objects in named/indexed project
	- funknotes show <project> <object>  # show items in that object
- Search notes
	- funknotes search [<object>] <keywords...>
		- Case-insensitive, all keywords must be present (AND)
- Merge projects
	- funknotes merge projects <proj1,proj2,...,target>
	- Prompted; combines objects/items/history into target
- Merge objects within a project
	- funknotes merge <project> <obj1,obj2,target>
- Delete
	- funknotes delete project <name|index>
	- funknotes delete projects <proj1,proj2,...>
	- funknotes delete object <name>             # deletes whole object (prompts)
	- funknotes delete <object> <index>          # deletes one 1-based item from object
	- funknotes delete <object> <indexes>        # deletes multiple items (e.g. 1,3,5-7)

Notes on behavior and safety
- All interactive delete operations prompt for confirmation. In non-interactive contexts (scripts, piped stdin) the tool refuses to delete by default to avoid accidental data loss. If you want a non-interactive forced delete behavior, I can add a `-y/--yes` flag later.
- Each object maintains an `items` array and a `history` array; deletions append `DELETE_ITEM` entries to history with timestamps and text for auditability.

Development ideas / TODO
- Add optional `-y/--yes` for scripted deletes.
- Add richer matching delete (delete by substring or regex).
- Add unit/smoke tests that create temporary projects and verify behavior automatically.
- Implement the "nit" file linking idea for locating moved project folders.

Examples

```bash
# create and set project
funknotes new project Cafe_GUI
funknotes primary Cafe_GUI

# add and auto-create object
funknotes add TODO "Implement button functionality"

# show objects
funknotes show

# delete the 4th item in TODO
funknotes delete TODO 4

# delete multiple items
funknotes delete TODO 2,4,6-8

# merge projects into a target (last is target)
funknotes merge projects alpha,beta,gamma
```

License: see LICENSE
