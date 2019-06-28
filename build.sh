#! /usr/bin/env bash


args=$(pkg-config --cflags --libs xcb x11)

#gcc $args blit.c
gcc $args blit2.c
