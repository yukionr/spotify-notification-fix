CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror
PIXBUF_CFLAGS := $(shell pkg-config --cflags gdk-pixbuf-2.0)

.PHONY: all test
all: build/spotify-notification-fix.so

build/spotify-notification-fix.so: src/spotify-notification-fix.c
	mkdir -p build
	$(CC) -std=c11 $(CFLAGS) -fPIC -fvisibility=hidden -shared $< $(PIXBUF_CFLAGS) -ldl -pthread -Wl,-z,relro,-z,now -o $@

test:
	python3 tests/test_launcher.py
