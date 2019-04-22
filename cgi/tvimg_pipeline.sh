#!/bin/bash
curl -s "$1" | convert - -colorspace Gray  -ordered-dither o4x4 -scale 640x480 -scale 100%x50% -negate pbm:- | /usr/local/bin/pbm2bin_v2
