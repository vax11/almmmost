#!/bin/bash
# Script to take a URL and convert it to the data format needed for IMAGEGET.ZASM
curl -s "$1" | convert - -colorspace Gray  -ordered-dither o4x4 -scale 640x480 -scale 100%x50% -negate pbm:- | ./pbm2bin
