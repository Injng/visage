#!/bin/sh

gcc -fsanitize=address -g src/main.c -o visage.out \
    -lavcodec -lavformat -lavutil -lswscale -lSDL3 \
    -Wall -Wextra
