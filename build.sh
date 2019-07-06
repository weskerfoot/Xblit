#! /usr/bin/env bash
$CC $(pkg-config --cflags --libs x11 x11-xcb xcb gl glu xcb-glx) $1

