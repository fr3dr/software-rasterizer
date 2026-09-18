TARGET = software-rasterizer
CC = gcc
CFLAGS = -O3 -Wall -Wextra
LIBS = -lm -lwwl
SRCS = $(wildcard src/*.c)
DEPS = $(wildcard src/*.h)
BUILDDIR = build

.PHONY: all clean run

all: $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET): $(SRCS) $(DEPS) $(BUILDDIR)
	$(CC) $(CFLAGS) $(LIBS) $(SRCS) -o $(BUILDDIR)/$@

run: $(TARGET)
	./$(BUILDDIR)/$<

clean:
	-rm $(BUILDDIR)/$(TARGET)
	-rm -r $(BUILDDIR)
