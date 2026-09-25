#!/bin/sh
# ZAPM, X11 frontend (curses shim, port/): map top left, Status and
# Inventory on the right, Messages below. Text only. Saves in user/.
# Override positions with ZAPM_MAP/_STATUS/_MSG/_INV="x,y".
cd "$(dirname "$0")" || exit 1
mkdir -p user
exec ./zapm-x11 "$@"
