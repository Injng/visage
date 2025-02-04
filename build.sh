#!/bin/sh

gcc src/main.c -o visage.out \
    -lavcodec -lavformat -lavutil -lswscale \
    -Wall -Wextra
