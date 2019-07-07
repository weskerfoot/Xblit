#! /usr/bin/env bash
$CC -Wall --pedantic --std=gnu11 $(pkg-config --cflags --libs cairo x11 x11-xcb xcb gl glu xcb-glx) $1

