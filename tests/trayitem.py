#!/usr/bin/env python3
"""A stand-in for a program's tray item (StatusNotifierItem) and its menu
(com.canonical.dbusmenu), for tests/traymod.py and the desktop's tray.

    trayitem.py LOG [--path] [--late MS] [--pixmap-only] [--title T]
    trayitem.py LOG --notify SUMMARY BODY

With --notify it is a program sending a notification instead: Notify with
actions (default, Open; later, Later), urgency 2 and markup in its body,
the id it was given logged, and every ActionInvoked and
NotificationClosed for it after; LOG.renotify (new summary) replaces it,
LOG.close closes it, LOG.quit ends.

Registers with the session bus's StatusNotifierWatcher: by its own bus name,
as Qt does, or with --path by its object path alone, as libappindicator
does (the watcher then takes the sender's name). --late answers GetAll
that many milliseconds late; --pixmap-only offers a 4x2 picture and no
icon name. Its menu: Open (1), a separator (2), Show Hidden as a checkmark
(3), More (4) with Quit (5) under it, and a row that is not visible (6).
Activate, SecondaryActivate, ContextMenu, Scroll, AboutToShow and every
menu Event are written to LOG as JSON lines. Files beside LOG drive it:
LOG.title (new text) retitles it and says NewTitle; LOG.menu adds a row
and says LayoutUpdated; LOG.quit ends it without unregistering, as a
program that crashes does; LOG.reregister registers again.
"""
import json, os, sys

import dbus
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib

LOG = sys.argv[1]
ARGS = sys.argv[2:]
BYPATH = "--path" in ARGS
PIXONLY = "--pixmap-only" in ARGS
LATE = int(ARGS[ARGS.index("--late") + 1]) if "--late" in ARGS else 0
TITLE = ARGS[ARGS.index("--title") + 1] if "--title" in ARGS else "Stand-in"

dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
ITEM = "/org/ayatana/NotificationItem/standin" if BYPATH else "/StatusNotifierItem"
MENU = "/MenuBar"
NAME = "org.kde.StatusNotifierItem-%d-1" % os.getpid()
IFACE = "org.kde.StatusNotifierItem"
DM = "com.canonical.dbusmenu"
state = {"title": TITLE, "extra": False, "rev": 1, "hidden": 0}


def note(**kw):
    with open(LOG, "a") as f:
        f.write(json.dumps(kw) + "\n")


def props():
    p = {
        "Category": "ApplicationStatus",
        "Id": "standin",
        "Title": state["title"],
        "Status": "Active",
        "ItemIsMenu": dbus.Boolean(False),
        "Menu": dbus.ObjectPath(MENU),
        "ToolTip": dbus.Struct(("", dbus.Array([], signature="(iiay)"),
                                "Stand-in tip", "more"), signature=None),
    }
    if PIXONLY:
        # 4x2, ARGB in network order: red then blue, opaque, then clear.
        px = bytes([255, 255, 0, 0] * 4 + [0, 0, 0, 255] * 4)
        p["IconPixmap"] = dbus.Array([dbus.Struct((dbus.Int32(4), dbus.Int32(2),
                                                   dbus.ByteArray(px)))],
                                     signature="(iiay)")
    else:
        p["IconName"] = "standin-icon"
        # Where a program keeps its own icons, as Steam does: the suite
        # writes standin-icon.png beside the log.
        p["IconThemePath"] = os.path.dirname(os.path.abspath(LOG))
    return p


class Item(dbus.service.Object):
    @dbus.service.method("org.freedesktop.DBus.Properties", in_signature="s",
                         out_signature="a{sv}", async_callbacks=("ok", "bad"))
    def GetAll(self, iface, ok, bad):
        if LATE:
            GLib.timeout_add(LATE, lambda: (ok(props()), False)[1])
        else:
            ok(props())

    @dbus.service.method("org.freedesktop.DBus.Properties", in_signature="ss",
                         out_signature="v")
    def Get(self, iface, name):
        return props()[name]

    @dbus.service.method(IFACE, in_signature="ii")
    def Activate(self, x, y):
        note(kind="Activate", x=x, y=y)

    @dbus.service.method(IFACE, in_signature="ii")
    def SecondaryActivate(self, x, y):
        note(kind="SecondaryActivate", x=x, y=y)

    @dbus.service.method(IFACE, in_signature="ii")
    def ContextMenu(self, x, y):
        note(kind="ContextMenu", x=x, y=y)

    @dbus.service.method(IFACE, in_signature="is")
    def Scroll(self, d, o):
        note(kind="Scroll", delta=d, orientation=o)

    @dbus.service.signal(IFACE, signature="")
    def NewTitle(self):
        pass


def node(i, label=None, kids=(), **extra):
    p = {}
    if label is not None:
        p["label"] = label
    p.update(extra)
    return dbus.Struct((dbus.Int32(i), dbus.Dictionary(p, signature="sv"),
                        dbus.Array(list(kids), signature="v")))


def layout():
    rows = [node(1, "_Open"),
            node(2, type="separator"),
            node(3, "Show _Hidden", **{"toggle-type": "checkmark",
                                       "toggle-state": dbus.Int32(state["hidden"])}),
            node(4, "_More", [node(5, "_Quit")], **{"children-display": "submenu"}),
            node(6, "Never seen", visible=dbus.Boolean(False))]
    if state["extra"]:
        rows.append(node(7, "Added Later"))
    return node(0, None, rows, **{"children-display": "submenu"})


class Menu(dbus.service.Object):
    @dbus.service.method(DM, in_signature="iias", out_signature="u(ia{sv}av)")
    def GetLayout(self, parent, depth, names):
        note(kind="GetLayout")
        return dbus.UInt32(state["rev"]), layout()

    @dbus.service.method(DM, in_signature="i", out_signature="b")
    def AboutToShow(self, i):
        note(kind="AboutToShow", id=i)
        return False

    @dbus.service.method(DM, in_signature="isvu")
    def Event(self, i, ev, data, ts):
        note(kind="Event", id=i, event=ev)

    @dbus.service.signal(DM, signature="ui")
    def LayoutUpdated(self, rev, parent):
        pass


if "--notify" in ARGS:
    i = ARGS.index("--notify")
    summary, body = ARGS[i + 1], ARGS[i + 2]
    nt = dbus.Interface(bus.get_object("org.freedesktop.Notifications",
                                       "/org/freedesktop/Notifications"),
                        "org.freedesktop.Notifications")
    mine = {}

    def notify(summary, rep=0):
        hints = {"urgency": dbus.Byte(2)}
        n = nt.Notify("Standin App", dbus.UInt32(rep), "standin-icon", summary, body,
                      ["default", "Open", "later", "Later"], hints, dbus.Int32(-1),
                      signature="susssasa{sv}i")
        mine["id"] = int(n)
        note(kind="notified", id=int(n), summary=summary)

    def acted(i, key):
        if int(i) == mine.get("id"):
            note(kind="ActionInvoked", id=int(i), key=str(key))

    def closed(i, why):
        if int(i) == mine.get("id"):
            note(kind="NotificationClosed", id=int(i), why=int(why))

    bus.add_signal_receiver(acted, "ActionInvoked", "org.freedesktop.Notifications")
    bus.add_signal_receiver(closed, "NotificationClosed", "org.freedesktop.Notifications")

    def ntick():
        if os.path.exists(LOG + ".renotify"):
            t = open(LOG + ".renotify").read().strip()
            os.unlink(LOG + ".renotify")
            notify(t, mine.get("id", 0))
        if os.path.exists(LOG + ".close"):
            os.unlink(LOG + ".close")
            nt.CloseNotification(dbus.UInt32(mine["id"]))
        if os.path.exists(LOG + ".quit"):
            os._exit(0)
        return True

    try:
        notify(summary)
    except dbus.DBusException as e:
        note(kind="nonotify", why=str(e))
    GLib.timeout_add(50, ntick)
    GLib.MainLoop().run()
    sys.exit(0)

item = Item(bus, ITEM)
menu = Menu(bus, MENU)
if not BYPATH:
    held = dbus.service.BusName(NAME, bus)


def register():
    w = bus.get_object("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher")
    w.RegisterStatusNotifierItem(ITEM if BYPATH else NAME,
                                 dbus_interface="org.kde.StatusNotifierWatcher")
    note(kind="registered", unique=bus.get_unique_name())


def tick():
    if os.path.exists(LOG + ".title"):
        state["title"] = open(LOG + ".title").read().strip()
        os.unlink(LOG + ".title")
        item.NewTitle()
    if os.path.exists(LOG + ".menu"):
        os.unlink(LOG + ".menu")
        state["extra"] = True
        state["rev"] += 1
        menu.LayoutUpdated(dbus.UInt32(state["rev"]), dbus.Int32(0))
    if os.path.exists(LOG + ".reregister"):
        os.unlink(LOG + ".reregister")
        register()
    if os.path.exists(LOG + ".quit"):
        os._exit(0)
    return True


try:
    register()
except dbus.DBusException as e:
    note(kind="noregister", why=str(e))
GLib.timeout_add(50, tick)
GLib.MainLoop().run()
