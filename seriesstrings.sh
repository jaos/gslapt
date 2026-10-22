#!/bin/bash
set -efu

INPUT=$1
OUTPUT=$2

grep '^_[^=]*=' "${INPUT}" | cut -d= -f2- | sort -u |
    awk '
        BEGIN {
            print "/* generated from series_map.rc.in by series-strings.sh: do not edit */";
            print "#ifndef N_";
            print "#define N_(String) (String)";
            print "#endif";
            print "const char *const gslapt_series_map_strings[] = {";
        }
        {
            gsub(/\\/, "\\\\");
            gsub(/"/, "\\\"");
            printf "\tN_(\"%s\"),\n", $0;
        }
        END { print "};"; }
    ' > "${OUTPUT}"
