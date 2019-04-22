#!/usr/bin/perl -w
use strict;
use CGI;
use Env;

my $q = CGI->new;
my $url = $q->param('url');

print "Content-type: application/octet-stream\n\n";

# Set terminal type = TVI-950
$ENV{TERM} = "tvi950";

#do: curl "$1" | convert - -colorspace Gray  -ordered-dither o4x4 -scale 640x480 -scale 100%x50% -negate pbm:- | ./pbm2bin_v2
my @cmd;
@cmd = ("/usr/local/bin/tvimg_pipeline.sh", $url);
system(@cmd)
