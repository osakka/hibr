#!/usr/bin/env python3
"""tests/apps.py's sections on what nothing reached before 0.56, run on their own.

    python3 tests/apps_reach.py [path-to-hibr]

The checks are written in tests/apps.py; tests/appslice.py says how a part
is cut from it.
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import appslice

appslice.run("reach")
