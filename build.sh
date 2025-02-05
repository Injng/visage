#!/bin/sh

gcc -g src/main.c -o visage.out \
    -lavcodec -lavformat -lavutil -lswscale -lswresample -lSDL3 \
    -Wall -Wextra
