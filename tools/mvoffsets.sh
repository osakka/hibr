#!/bin/sh
# Where each libav field the media module reads sits, per FFmpeg release:
# the numbers in mods/media/av.c's mv_tabs, taken with offsetof from each
# release's own headers rather than written by hand. Run it when FFmpeg
# makes a new major release, and add a line for it.
#
#   tools/mvoffsets.sh n8.0 [n9.0 ...]
set -e
work=${TMPDIR:-/tmp}/hibr-mvoff.$$
mkdir -p "$work"
trap 'rm -rf "$work"' EXIT
cat > "$work/off.c" <<'C'
#include <stdio.h>
#include <stddef.h>
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/frame.h"
#define O(t, f) printf(" %zu", offsetof(t, f))
int main(void)
{
	printf("{ %d, %d, %d, %d,", LIBAVFORMAT_VERSION_MAJOR,
	       LIBAVUTIL_VERSION_MAJOR, 0, 0);
	O(AVFormatContext, pb); O(AVFormatContext, nb_streams);
	O(AVFormatContext, streams); O(AVFormatContext, duration);
	O(AVStream, codecpar); O(AVStream, time_base); O(AVStream, duration);
	O(AVStream, start_time);
	O(AVCodecParameters, codec_type); O(AVCodecParameters, codec_id);
	O(AVCodecParameters, format); O(AVCodecParameters, width);
	O(AVCodecParameters, height); O(AVCodecParameters, sample_rate);
	O(AVCodecParameters, ch_layout);
	O(AVFrame, best_effort_timestamp); O(AVFrame, ch_layout);
	O(AVFrame, sample_rate); O(AVCodecContext, pkt_timebase);
	printf(" }  /* fixed: data 0, linesize %zu, width %zu, height %zu,"
	       " nb_samples %zu, format %zu, pts %zu; packet stream_index %zu,"
	       " pts %zu */\n", offsetof(AVFrame, linesize),
	       offsetof(AVFrame, width), offsetof(AVFrame, height),
	       offsetof(AVFrame, nb_samples), offsetof(AVFrame, format),
	       offsetof(AVFrame, pts), offsetof(AVPacket, stream_index),
	       offsetof(AVPacket, pts));
	return 0;
}
C
for tag in "$@"; do
	git clone -q --depth 1 --branch "$tag" https://github.com/FFmpeg/FFmpeg.git "$work/$tag"
	printf '#define AV_HAVE_BIGENDIAN 0\n#define AV_HAVE_FAST_UNALIGNED 1\n' > "$work/$tag/libavutil/avconfig.h"
	[ -f "$work/$tag/libavutil/ffversion.h" ] || echo '#define FFMPEG_VERSION "x"' > "$work/$tag/libavutil/ffversion.h"
	cc -w -I"$work/$tag" -o "$work/off-$tag" "$work/off.c"
	printf '%s: ' "$tag"
	"$work/off-$tag"
done
echo "(the two zeros are libswscale's and libswresample's majors: fill them in from"
echo " libswscale/version_major.h and libswresample/version_major.h)"
