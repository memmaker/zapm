#!/bin/sh
# Build ZAPM for the browser (Emscripten + Asyncify) into web/dist;
# port/be_web.cpp + web/zapm.js draw the panes. Deploy with web/deploy.sh.
set -e
cd "$(dirname "$0")/.."
OUT=web/dist
rm -rf "$OUT" && mkdir -p "$OUT"
emcc -O2 -Iport -c port/wcurses.c -o "$OUT/wcurses.o"
em++ -O2 -std=c++98 -w -I. -Iport -DZAPM_SHIM -Dusleep=wc_usleep \
	*.cpp port/be_web.cpp "$OUT/wcurses.o" -o "$OUT/zapm-core.js" \
	-sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sSTACK_SIZE=1048576 \
	-sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=32MB \
	-sEXPORTED_FUNCTIONS=_main \
	-sEXPORTED_RUNTIME_METHODS=FS,IDBFS,ENV,addRunDependency,removeRunDependency \
	-sFORCE_FILESYSTEM -lidbfs.js -sENVIRONMENT=web
rm "$OUT/wcurses.o"
cp web/index.html web/zapm.js "$OUT/"
# text fonts: the index page's fonts/ (served at ../fonts/ next to the games)
FONTS=${FONTS:-$HOME/Games/roguelikes-index/fonts}
if [ -d "$FONTS" ]; then (cd "$FONTS" && ls *.woff | sed 's/\.woff$//'); fi \
	| python3 -c 'import json,sys; print(json.dumps(sys.stdin.read().split()))' > "$OUT/fonts.json"
python3 web/make-help.py > "$OUT/help.html"
ls -la "$OUT"
