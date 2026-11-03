# ----------------------------
# LINKTEST - USB calc-to-calc link test for tiboyce-link
# Build with the CE C toolchain: make
# ----------------------------

NAME = LINKTEST
ICON = icon.png
DESCRIPTION = "tiboyce-link USB test"
COMPRESSED = NO

CFLAGS = -Wall -Wextra -Oz -DLINKLIB_KEY="\"tiboyce-link LINKTEST\""
CXXFLAGS = -Wall -Wextra -Oz -DLINKLIB_KEY="\"tiboyce-link LINKTEST\""

# ----------------------------

include $(shell cedev-config --makefile)
