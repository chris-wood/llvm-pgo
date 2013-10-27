#!/usr/bin/perl -w

use Time::HiRes qw/ time sleep /;

my $start = time;
system("./$ARGV[0] $ARGV[1] > $ARGV[0].out");
my $end = time;
my $run_time = $end - $start;
print "TIME: $ARGV[1],$run_time\n";
