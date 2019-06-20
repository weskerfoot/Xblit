#! /usr/bin/env bash


xlib=$(pkg-config --cflags --libs x11)

gcc $xlib blit.c
