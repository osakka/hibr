#!/usr/bin/env python3
"""tests/desktop.py's sections on moving, resizing, snapping, workspaces, icons, the strip, the saver, the lock and the login screen, run on their own.

    python3 tests/desktop_windows.py [path-to-hibr]

The checks are written in tests/desktop.py; tests/appslice.py says how a part
is cut from it.
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import appslice

appslice.run("windows", "desktop")
