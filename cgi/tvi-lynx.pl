#!/usr/bin/perl -w
use strict;
use CGI;
use Env;

my $q = CGI->new;
my $url = $q->param('url');

print "Content-type: text/plain\n\n";

# Set terminal type = TVI-950
$ENV{TERM} = "tvi950";

# Call lynx -dump $URL
my @cmd;
@cmd = ("/usr/bin/lynx", "-dump", $url);
system(@cmd)
