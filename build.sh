#!/bin/sh

gcc src/main.c -o visage.out \
    -lavcodec -lavformat \
    -Wall -Wextra
