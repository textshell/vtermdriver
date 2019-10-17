vtermdriver is a simple wrapper around [libvterm](http://www.leonerd.org.uk/code/libvterm/)
to test rendering of terminal applications.


Usage
-----

vtermdriver wraps an terminal application while allowing the calling process to use
commands to introspect terminal state and interact with the tested application.

The test software uses a socket pair passed as standard in to communicate with the
test driver.

The protocol consists of zero terminated ascii commands and syncronous replies. Additionally
there are notifications send to the test software.

The replies and notifications are zero terminated byte strings. Notifications start with the
byte '*'. Replies start with '{' and form a valid json document.

Invocation: vtermdriver --control-via-fd0 /path/to/app/to/test [arguments]...

Commands
--------

capture:img

Sends the current screen content as json object.

capture:all

Sends the same data as capture:img plus additional terminal state

send-to-interior:

Sends bytes to the application that is tested. The bytes to send follow the ':' and are hex
encoded.

reset

Reset the terminal state.

quit

Kill the interior application (if still running) and exit.


Notifications
-------------

*exited

The application has exited.


Json Output
-----------

```json
{
    "version": 0,
    "cursor_column": 0,
    "cursor_row": 1,
    "cursor_blink": true,
    "cursor_shape": "block",
    "height": 24,
    "width": 80,
    "cells": [
        {
            "x": 0,
            "t": "a",
            "y": 0
        },
        {
            "x": 1,
            "t": "\u3042",
            "width": 2,
            "y": 0
        },
        {
            "x": 3,
            "t": " ",
            "y": 0
        },
        {
            "x": 4,
            "t": " ",
            "y": 0
        },
        // ...
    ]
}

```
