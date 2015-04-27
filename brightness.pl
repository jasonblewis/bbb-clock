#!/usr/bin/env perl

use strict;
use warnings;
use 5.21.10;
use experimental 'smartmatch';

use Term::ReadKey;

$SIG{INT} = sub {
    ReadMode 0;
    die "Caught a sigint $!";
};


my $num_args = $#ARGV +1;
my $start_brightness;
my $current_brightness;
my $next_brightness;
my $max_brightness = 4095;
my @history;

sub instructions {
    say "     n for next brightness level";
    say "     q to quit";
    say "     s to save current and next level";
    say "     m set next_brightness to max level";
    say "     b bisect current and next_brightness";
    say "     u undo previous step";
    say "     c biscept previous next_brightness and next_brightness";
}

sub usage {
    say "Usage: brightness.pl [start brightness]";
    instructions;
}

sub set_brightness {
    my ($brightness) = @_;
    say "Setting brightness to: $brightness";
    my @args = (qw(/home/jason/clock/clock -d 1 -v 8 -g));
    push @args, $brightness;
    system(@args) == 0
        or die "system @args failed: $?";
}

if ($num_args != 1) {
    usage;
    exit 1;
} else {
    $start_brightness = $ARGV[0];
    $current_brightness = $start_brightness;
    $next_brightness = $current_brightness+1;
    push @history,$current_brightness;
};

sub alternate_brightness{
    state $tick = 0;
    printf "alternating between %s and %s. previous next_brightness: %s\n",$current_brightness, $next_brightness, $history[$#history];
    if ($tick) {
#        say "setting next: $next_brightness";
        set_brightness($next_brightness);
    } else {
 #       say "setting current: $current_brightness";
        set_brightness($current_brightness);
    }
    $tick ^= 1;
}

sub save_step{
    say "Current: $current_brightness Next: $next_brightness";
    $current_brightness = $next_brightness;
}

ReadMode 4;
instructions;
while(1) {
    my $key = ReadKey 0.5; #Sleep for 50
    for ($key) { # for is prefered over given, less experimental
        when (undef) { alternate_brightness(); }
        when (/^q/) {ReadMode 0; exit 0;}
        when (/^n/) {$next_brightness++; push @history, $next_brightness ;}
        when (/^p/) {$next_brightness--; push @history, $next_brightness ;}
        when (/^s/) {save_step();}
        when (/^m/) {$next_brightness = $max_brightness; ; push @history, $next_brightness;}
        when (/^b/) {$next_brightness = $current_brightness +  int(($next_brightness - $current_brightness)/2); ; push @history, $next_brightness;}
        default {}
    }
}


ReadMode 0;
