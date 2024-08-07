#!/usr/bin/awk -f

# Supports only "simple" diff3 style conflicts. Criss-cross conflicts not supported.
# Assumes conflict marker length to be 7 (default length).

BEGIN {
  NO_CONFLICT = 0
  HEAD = 1
  BASE = 2
  OTHER = 3

  STATE = NO_CONFLICT

  if (TARGET == "HEAD") {
    TARGET = HEAD
  }
  if (TARGET == "BASE") {
    TARGET = BASE
  }
  if (TARGET == "OTHER") {
    TARGET = OTHER
  }
  if (TARGET !~ /^(1|2|3)$/) {
    print "Usage: ./split3.awk <file_with_diff3_conflict_markers -v TARGET={HEAD,BASE,OTHER}"
    exit 1
  }
}

/^<<<<<<<( .+)?$/ {
  STATE = HEAD
  next
}

/^[|]{7}( .+)?$/ {
  STATE = BASE
  next
}

/^=======( .+)?$/ {
  STATE = OTHER
  next
}

/^>>>>>>>( .+)?$/ {
  STATE = NO_CONFLICT
  next
}

STATE == NO_CONFLICT || STATE == TARGET {
  print
}
