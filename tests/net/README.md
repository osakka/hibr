# tests/net -- what NetworkManager and BlueZ really say

The `.txt` files are **captured**, verbatim, from the owner's own machine on
2026-10-20: NetworkManager with nmcli 1.52.1 over wpa_supplicant, and BlueZ
with bluetoothctl 5.82. They are the reference the stand-in is held to,
because a stand-in written from memory agrees with the client written from
the same memory and proves nothing (CLAUDE.md, on `bwserve.py`).

| file | command |
|---|---|
| `systemctl.txt` | `systemctl is-active NetworkManager iwd wpa_supplicant systemd-networkd bluetooth` |
| `nmcli-version.txt` | `nmcli --version` |
| `dev-status.txt` | `nmcli -t -f DEVICE,TYPE,STATE,CONNECTION dev status` |
| `wifi-list.txt` | `nmcli -t -f IN-USE,SSID,BSSID,MODE,CHAN,SIGNAL,BARS,SECURITY dev wifi list` |
| `con-show.txt` | `nmcli -t -f NAME,UUID,TYPE,DEVICE con show` |
| `dev-show.txt` | `nmcli -t -f GENERAL.STATE,IP4.ADDRESS,IP4.GATEWAY,IP4.DNS dev show` |
| `con-show-ts.txt` | `nmcli -t -f NAME,UUID,TYPE,TIMESTAMP,AUTOCONNECT con show` |
| `permissions.txt` | `nmcli general permissions` -- the owner's own session: every action auth or no |
| `polkit-prompt.txt`, `polkit-failed.txt` | `pkttyagent`'s prompt for a check-only action, and its answer to a wrong password -- captured on this machine with `pkcheck`, which performs nothing |
| `bt-show.txt`, `bt-devices.txt`, `bt-info.txt` | `bluetoothctl show`, `devices`, `info <a keyboard>` -- for Gitea #312 |

The owner's paste turned bluetoothctl's indentation into spaces; bluetoothctl
indents with tabs, and the files keep tabs.

`nmcli` is the stand-in `tests/net.py` puts first on the PATH. Its four
reads above are checked byte for byte against these files (an empty first
field, a BSSID's `\:`, a name with a `/` and a space, `dev show`'s blocks).
**What is not captured**, and so is the stand-in's own reading of nmcli(1):

- the three networks `net.py` adds to the captured one (an open one, a WPA2
  one never saved, and `gibnet`, which a saved connection joins);
- what an action says on success -- nothing in the desktop parses it; only
  the exit status and an `Error:` line are read;
- `-g 802-11-wireless.ssid connection show uuid U`, a saved connection's
  network name;
- `connection edit` reading `set`, `save` and `quit` on its standard input,
  the path a new network's password takes -- confirmed on the owner's
  machine as far as its save, which polkit refused there ("Insufficient
  privileges"), the error text kept here.

`pkttyagent` is the stand-in for polkit's text agent: when the stand-in
nmcli meets an action whose answer is `auth` it writes `auth.req` and waits
for `auth.res`; the agent prints the captured prompt, reads a line on its
pty, and prints the captured failure or, for the right password (state's
`agentpw`), the green COMPLETE banner -- which was not captured, there
being no password to give here, and is polkit's own text from its source.

`bluetoothctl` is `tests/bt.py`'s stand-in for BlueZ: `show`, `devices`
and `info` are the captured text with their values put in, checked byte
for byte against `bt-*.txt`. Not captured, and bluez's own wording from its
source: what an action replies (`Connection successful`, `Failed to
connect: org.bluez.Error...`), a connected device's `Battery Percentage:
0x55 (85)`, and an interactive session's agent questions (`Confirm passkey
123456 (yes/no):`, `Enter PIN code:`) -- the owner's machine has not yet
been asked to pair anything for a capture. Its state is
`$HIBR_BTSTATE/state.json`.

State lives in `$HIBR_NMSTATE/state.json`; every call's argv is appended to
`argv.log` there, which is how the suite shows a password never reaches a
command line. `monitor` prints a line whenever the state file changes.
