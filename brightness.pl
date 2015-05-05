#!/usr/bin/env perl

use strict;
use warnings;
use 5.21.10;
use experimental 'smartmatch';

use Term::ReadKey;



my $num_args = $#ARGV +1;
my $start_brightness;
my $current_brightness;
my $next_brightness;
my $max_brightness = 4095;
my @history;
my $filename;
my $fh;

sub instructions {
    say "     n for next brightness level";
    say "     q to quit";
    say "     s to save current and next level";
    say "     m set next_brightness to max level";
    say "     b bisect current and next_brightness";
    say "     u undo previous step";
    say "     c biscept previous next_brightness and next_brightness";
    say "     h show help message";
}

sub usage {
    say "Usage: brightness.pl [start brightness]";
    instructions;
}

sub set_brightness {
    my ($brightness) = @_;
    #say "Setting brightness to: $brightness";
    my @args = (qw(/home/jason/clock/clock -d 1 -v 8 -g));
    push @args, $brightness;
    system(@args) == 0
        or die "system @args failed: $?";
}

sub print_brightness {
    printf "alternating between %s and %s.",$current_brightness, $next_brightness;
    if ((scalar @history) > 1) {
        printf " n-1 brightness: %s\n", $history [$#history - 1 ];
    } else {
        printf "\n";
    }
};

sub alternate_brightness{
    state $tick = 0;
    state $initialised = 0;
    state $lcurrent_brightness = $current_brightness;
    state $lnext_brightness = $next_brightness;

    if (($lcurrent_brightness != $current_brightness) || ($lnext_brightness != $next_brightness) || !$initialised) {
        $initialised = 1;
        $lcurrent_brightness = $current_brightness;
        $lnext_brightness = $next_brightness;
    };
    if ($tick) {
        set_brightness($next_brightness);
    } else {
        set_brightness($current_brightness);
    }
    $tick ^= 1;
}

sub save_step{
    my ($step) = @_;
    printf $fh "%d\n", $step;
    $current_brightness = $step;
}

sub adjust_next_brightness {
    my ($val) = @_;
    push @history, $val;
    print_brightness;
}

sub open_file {
    $filename = 'brightness_levels.txt';
    open ($fh, '>>', $filename) or die "Could not open file '$filename' $!";
}
sub close_file {
    close $fh;
}


$SIG{INT} = sub {
    ReadMode 0;
    die "Caught a sigint $!";
};

if ($num_args != 1) {
    usage;
    exit 1;
} else {
    $start_brightness = $ARGV[0];
    $current_brightness = $start_brightness;
    $next_brightness = $current_brightness+1;
    push @history,$current_brightness;
    open_file;
    save_step($current_brightness);
};



ReadMode 4;
instructions;
while(1) {
    my $key = ReadKey 0.5; #Sleep for 50
    for ($key) { # for is prefered over given, less experimental
        when (undef) { alternate_brightness(); }
        when (/^q/) {ReadMode 0; close_file; exit 0;}
        when (/^n/) {adjust_next_brightness(++$next_brightness);}
        when (/^p/) {adjust_next_brightness(--$next_brightness);}
        when (/^s/) {save_step($next_brightness);}
        when (/^m/) {adjust_next_brightness($next_brightness = $max_brightness);}
        when (/^b/) {adjust_next_brightness($next_brightness = ($current_brightness + int (($next_brightness - $current_brightness)/2)));}
        when (/^h/) {instructions;}
        default {}
    }
}


ReadMode 0;
