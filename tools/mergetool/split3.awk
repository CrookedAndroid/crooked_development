#!/usr/bin/awk -f

# Supports only "simple" diff3 style conflicts. Criss-cross conflicts not supported.
# Assumes conflict marker length to be 7 (default length).

BEGIN {
  NO_CONFLICT = 0
  LOCAL = 1
  BASE = 2
  REMOTE = 3

  if (TARGET == "LOCAL") {
    TARGET = LOCAL
  }
  if (TARGET == "BASE") {
    TARGET = BASE
  }
  if (TARGET == "REMOTE") {
    TARGET = REMOTE
  }
  if (TARGET !~ /^(1|2|3)$/) {
    print "Usage: ./split3.awk <file_with_diff3_conflict_markers -v TARGET={LOCAL,BASE,REMOTE}"
    exit 1
  }

  STATE = NO_CONFLICT
}

/^<{7}( .+)?$/ {
  STATE = LOCAL
  next
}

/^[|]{7}( .+)?$/ {
  STATE = BASE
  next
}

/^={7}( .+)?$/ {
  STATE = REMOTE
  next
}

/^>{7}( .+)?$/ {
  STATE = NO_CONFLICT
  next
}

STATE == NO_CONFLICT || STATE == TARGET {
  print
}
