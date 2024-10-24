#! /bin/bash

set -e

d0="$(cd "$(dirname "$0")" && pwd)"
outdir=.

while (($# > 0)); do
	case "$1" in
		-outdir)
			outdir="$2"
			shift 2;;
		*)
			echo "Error: unknown argument: $1" >&2
			exit 1;;
	esac
done

videogen="$d0/../tool/videogen.py"

mkdir -p "$outdir"

"$videogen" --vr 60         --ar 48000 'q=4,f=884,c=2' -o "$outdir/sync-pattern-6000.mp4"
"$videogen" --vr 60000/1001 --ar 48000 'q=4,f=884,c=2' -o "$outdir/sync-pattern-5994.mp4"
"$videogen" --vr 50         --ar 48000 'q=4,f=884,c=2' -o "$outdir/sync-pattern-5000.mp4"
"$videogen" --vr 24         --ar 48000 'q=2,f=884,c=2' -o "$outdir/sync-pattern-2400.mp4"
"$videogen" --vr 24000/1001 --ar 48000 'q=2,f=884,c=2' -o "$outdir/sync-pattern-2398.mp4"
