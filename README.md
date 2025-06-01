# ADash - Activity Dashboard

_ADash_ is a very simple project manager.

_ADash_ keeps track of all registered activities, and allow for checking in and out of a specific activity. Only one activity can be checked in at a time. This limitation enforces focus on only that single activity while checked in. Because time is tracked and measured for each checked in activity, it is more compelling to remain focusing only on respective activity while checked in.

## Motivation

This system was developed to help managing demanding long term development of computer system, where context switching is frequent while simultaneously demanding high quality focus in order to create the solutoins within the different contexts. An exmaple is writing a websocket server in node.js which has the context of the server which must be installed correctly, and the context of the JS code that has to be written. Adding to this example, the necessity of the node.js solution may be questioned, whereas another context of investigating other solutions may be required. _ADash_ strategically opens a new context for each acitivty and is able to track and collect notes related to the activity. The mere activity of checking in and out helps aligning with a more structured approach, but also the grouping of related information under each activity helpos to quickly resume the activity.

## Installation

To install _ADash_, clone the repository and compile the source code:

```bash
make
sudo make install
make clean
```

## Features

- 🧠 **Single-Project Focus**: Only one activity can be checked in at a time.
- 📅 **Timestamped Logs**: All state transitions and comments are saved with precise timestamps.
- 💬 **Comment System**: Add comments during a session; scroll through them easily.
- 📊 **Time Tracking**: Computes total tracked time for each activity.
- 🔄 **State Management**: Projects can be checked in, checked out, finished, or canceled.
- 🗂 **Project List View**: Navigate all projects with filters (active, finished, canceled).
- ✍️ **TUI Interface**: Navigate via `ncurses` interface using arrow keys or `j/k`.
- 🗑 **Archiving & Deletion**: Archive or delete projects directly from the interface.
- 📝 **Preview**: Shows the latest comment preview alongside each project in the list.
- 💾 **Persistence**: All logs are stored as plain text and can be versioned or synced easily.

## Usage

```bash
adash ~/path/to/storage
```

This will create and use the following directories under the provided path:

```
~/path/to/storage/
├── data/       # Stores activity logs (one file per project)
├── state/      # Stores active project state and lock
└── archived/   # Archived project logs
```

## Keybindings

### List View (Project Overview)

| Key         | Action                      |
| ----------- | --------------------------- |
| `↑/↓` `j/k` | Navigate project list       |
| `↵`         | Open selected project       |
| `n`         | Create new project          |
| `a`         | Show all projects           |
| `f`         | Show only finished projects |
| `c`         | Show only canceled projects |
| `s`         | Show only started projects  |
| `A`         | Archive selected project    |
| `x`         | Delete selected project     |
| `q`         | Quit                        |

### Data View (Per Project)

| Key   | Action                              |
| ----- | ----------------------------------- |
| `j/k` | Scroll through comments             |
| `c`   | Add comment (must be checked in)    |
| `d`   | Delete current comment (must be in) |
| `l`   | List all comments                   |
| `i`   | Check in to project                 |
| `o`   | Check out of project                |
| `f`   | Mark project as finished            |
| `x`   | Cancel project                      |
| `p`   | Return to project list view         |
| `q`   | Quit                                |

## Project States and Icons

> [!NOTE]
> TODO: All the icons don't have great support, so change... or remove icons entierly...

| Icon | Meaning          | State      |
| ---- | ---------------- | ---------- |
| 🟢   | Checked-in       | `checkin`  |
| 🔴   | Checked-out      | `checkout` |
| 🟡   | Started          | `created`  |
| ✘    | Canceled         | `cancel`   |
| ✔   | Finished         | `finish`   |
| 🗂   | Generic activity | fallback   |

## Internals

- Logs are stored per-project in `data/<id>.log`.

- Each log entry is in the format:

  ```
  YYYY-MM-DDTHH:MM:SSZ    action    comment
  ```

  Example:

  ```
  2025-06-01T14:00:00Z    checkin
  2025-06-01T14:05:12Z    comment    Investigating missing headers
  2025-06-01T15:22:34Z    checkout
  ```

- Lock file (`state/checkedin`) enforces exclusive check-ins.

- `state/running` stores the currently opened project in UI for continuity.

## Build

Requires `gcc` and `ncurses`.

```bash
gcc adash.c -o adash -lncurses
```
