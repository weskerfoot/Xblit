#! /usr/bin/env bash

gcc $(pkg-config --cflags --libs x11 x11-xcb xcb gl xcb-glx) blit3.c

