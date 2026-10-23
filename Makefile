CC ?= cc
CFLAGS ?= -O2 -g
SANE_INCLUDE ?= /usr/include
TARGET = libsane-canon_chmp.so.1.0.0

all: $(TARGET)

$(TARGET): backend.c
	$(CC) $(CFLAGS) -Wall -Wextra -Werror -fPIC -shared \
		-I$(SANE_INCLUDE) -o $@ $< -ljpeg

clean:
	rm -f $(TARGET)

.PHONY: all clean
