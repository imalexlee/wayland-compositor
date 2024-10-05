#!/bin/sh
gcc -o ../out/test-server ../test-server/entry.c  -lwayland-server ; ./../out/test-server
