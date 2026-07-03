#!/usr/bin/env perl

use strict;
use warnings;
use File::Copy qw(copy);

my $release_file = "configure/RELEASE";
my $default_release_file = "configure/RELEASE-DEFAULT";

die "Default release file not found: $default_release_file\n"
    unless -e $default_release_file;

if (!-e $release_file) {
    print "Creating RELEASE file: $release_file\n";

    copy($default_release_file, $release_file)
        or die "Failed to copy $default_release_file to $release_file: $!\n";

    exit 0;
}

print "RELEASE file found at $release_file. Replace? [y/N]: ";
chomp(my $answer = <STDIN> // "");

if (lc($answer) eq "y") {
    my $backup_file = "$release_file.orig";

    copy($release_file, $backup_file)
        or die "Failed to create backup $backup_file: $!\n";

    copy($default_release_file, $release_file)
        or die "Failed to replace $release_file with $default_release_file: $!\n";

    print "Replaced $release_file. Backup saved as $backup_file\n";
} else {
    print "Keeping existing $release_file\n";
}
