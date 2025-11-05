# funknotes
It's your function style note taking application that you can call from your command line.

# Notes
1. Roughed this out with AI assistance
2. Intended to run on Linux - as yet untested
3. Probably will be best with a symlink to /usr/local/bin
4. Switched from python to C - I'd like to say it's for speed, but then I wouldn't be using json libraries if that were true

# For development
1. Create a nifty search function
2. I'd like to somehow add a "tic" to directories to indicate a funknote - for example, if I'm in my main dev folder for XYZ Project, when I create the funknote for it, I get an option to "drop nit" - the nit is a tiny file that sits in the dev folder, if for whatever reason, I move that folder around and my main funknotes tracker loses where it is, I can do a "comb.nits" - funknotes will search for nit files and check for the match and re-link the directory
3. At some point, I'll check how this interacts with nvim/vim - I definitely want this to work from the vim : mode

# Example of potential use
// Create a new project

funknotes new Cafe_GUI

// Set it as primary

funknotes primary Cafe_GUI

// Create a TODO object

funknotes object TODO

// Add items

funknotes add TODO "Implement JSON for buttons"
funknotes add TODO "Design main menu layout"

// Show all objects in current project

funknotes show

// Show items in an object

funknotes show TODO

// View history

funknotes history TODO

// List all projects

funknotes list

// Switch projects

funknotes primary 2
