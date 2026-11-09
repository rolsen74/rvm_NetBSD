# RVM GCC lowers recognized zeroing loops to bzero, including bzero's own
# implementation. Keep this primitive from recursively calling itself.
COPTS.bzero.c+= -fno-builtin
