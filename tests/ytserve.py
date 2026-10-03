#!/usr/bin/env python3
"""A stand-in for YouTube, for tests/youtube.py: a results page whose own
data (ytInitialData) lists videos as YouTube's does, and a watch page with
a #movie_player that answers the calls the YouTube app makes and plays a
clip through a media source as YouTube's player does -- an init segment
and then two-second fragments of VP9 and of Opus, and after a seek only
the fragments from the one holding the time asked for -- so the web module's
tap, the pipes, the media player and the app are all exercised without
the network. Prints "port N" once it is listening.
"""
import json, os, sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

ROOT = sys.argv[1]
VIDEOS = [("vid1", "A test pattern", "Pattern Channel", "0:12"),
          ("vid2", "Another clip", "Second Channel", "0:12"),
          ("vid3", "Third result", "Pattern Channel", "0:12")]

WATCH = """<!doctype html><html><head><title>%(title)s - YouTube</title></head>
<body><div id="movie_player"></div><video id="v" muted></video><script>
var V = document.getElementById("v"), P = document.getElementById("movie_player");
var ms = new MediaSource(), sbs = {}, segs = {}, segdur = 2;
V.src = URL.createObjectURL(ms);
function queue(k, from) {
  var sb = sbs[k], list = segs[k], i = from;
  function next() {
    if (i >= list.length || sb.updating) return;
    var b = list[i++]; sb.appendBuffer(b);
  }
  sb.onupdateend = next; next();
}
ms.addEventListener("sourceopen", function () {
  sbs.v = ms.addSourceBuffer('video/webm; codecs="vp9"');
  sbs.a = ms.addSourceBuffer('audio/webm; codecs="opus"');
  fetch("/media/list").then(function (r) { return r.json(); }).then(function (l) {
    segdur = l.segdur;
    return Promise.all(["v", "a"].map(function (k) {
      return Promise.all(l[k].map(function (f) {
        return fetch("/media/" + f).then(function (r) { return r.arrayBuffer(); });
      })).then(function (bs) { segs[k] = bs.map(function (b) { return new Uint8Array(b); }); });
    }));
  }).then(function () { queue("v", 0); queue("a", 0); });
});
var state = -1, quality = "auto";
P.getPlayerState = function () { return state; };
P.getDuration = function () { return 12; };
P.getVideoData = function () { return {title: "%(title)s", author: "%(author)s"}; };
P.setPlaybackQualityRange = function (q) { quality = q; window.__quality = q; };
P.mute = function () { V.muted = true; };
P.playVideo = function () { state = 1; V.play().catch(function () {}); };
P.pauseVideo = function () { state = 2; V.pause(); window.__paused = 1; };
P.seekTo = function (t) {
  window.__seeked = t; V.currentTime = t;
  var k = 1 + Math.floor(t / segdur);
  ["v", "a"].forEach(function (s) { sbs[s].abort(); queue(s, Math.min(k, segs[s].length - 1)); });
};
</script></body></html>"""


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, body, ctype="text/html"):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urlsplit(self.path)
        q = parse_qs(u.query)
        if u.path == "/results":
            word = q.get("search_query", [""])[0]
            items = [{"videoRenderer": {
                "videoId": i, "title": {"runs": [{"text": "%s (%s)" % (t, word)}]},
                "ownerText": {"runs": [{"text": c}]}, "lengthText": {"simpleText": l},
                "shortViewCountText": {"simpleText": "1K views"},
                "publishedTimeText": {"simpleText": "1 day ago"}}} for i, t, c, l in VIDEOS]
            data = {"contents": {"twoColumnSearchResultsRenderer": {"primaryContents": {
                "sectionListRenderer": {"contents": [{"itemSectionRenderer": {
                    "contents": items}}]}}}}}
            page = ("<html><head><title>%s - YouTube</title></head><body><script>"
                    "setTimeout(function(){window.ytInitialData=%s}, 300)</script>"
                    "</body></html>" % (word, json.dumps(data)))
            return self.send(200, page)
        if u.path == "/watch":
            vid = q.get("v", [""])[0]
            t = dict((i, (t, c)) for i, t, c, l in VIDEOS).get(vid, ("Unknown", "Nobody"))
            return self.send(200, WATCH % {"title": t[0], "author": t[1]})
        if u.path == "/media/list":
            fs = sorted(os.listdir(ROOT))
            def seq(p):
                segs = sorted((f for f in fs if f.startswith(p + "seg-")),
                              key=lambda f: int(f.split("-")[1].split(".")[0]))
                return [p + "init.webm"] + segs
            return self.send(200, json.dumps({"v": seq("v"), "a": seq("a"), "segdur": 2}),
                             "application/json")
        if u.path.startswith("/media/"):
            f = os.path.join(ROOT, os.path.basename(u.path))
            if os.path.exists(f):
                return self.send(200, open(f, "rb").read(), "application/octet-stream")
        self.send(404, "not found", "text/plain")


srv = ThreadingHTTPServer(("127.0.0.1", 0), H)
srv.daemon_threads = True
print("port %d" % srv.server_address[1], flush=True)
srv.serve_forever()
