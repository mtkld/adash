# ADash - Activity Dashboard

_ADash_ is a very simple project manager.

_ADash_ keeps track of all registered activities, and allow for checking in and out of a specific activity. Only one activity can be checked in at a time. This limitation enforces focus on only that single activity while checked in. Because time is tracked and measured for each checked in activity, it is more compelling to remain focusing only on respective activity while checked in.

## Motivation

This system was developed to help managing demanding long term development of computer system, where context switching is frequent while simultaneously demanding high quality focus in order to create the solutoins within the different contexts. An exmaple is writing a websocket server in node.js which has the context of the server which must be installed correctly, and the context of the JS code that has to be written. Adding to this example, the necessity of the node.js solution may be questioned, whereas another context of investigating other solutions may be required. _ADash_ strategically opens a new context for each acitivty and is able to track and collect notes related to the activity. The mere activity of checking in and out helps aligning with a more structured approach, but also the grouping of related information under each activity helpos to quickly resume the activity.

## Installation

To install _ADash_, clone the repository and compile the source code:

```bash
sudo make install
```

## Note

This has been remade in bash... using fzf to select among projects

## Usage

To use _ADash_, run the following command:

```bash
adash path/to/your/project-dir
```

`adash` creates its database in that directory, as ordinary text files.

## Guide

The interactive prompt shows: `[i/o/f/x c/d l n p q z]`.

| Key | Action                       |
| --- | ---------------------------- |
| `i` | Check in                     |
| `o` | Check out                    |
| `f` | Finish current project       |
| `x` | Cancel current project       |
| `c` | Add comment                  |
| `d` | Delete comment               |
| `l` | List all comments            |
| `n` | Create new project           |
| `p` | Switch to project list       |
| `q` | Quit the application         |
| `z` | Switch to checked-in project |
