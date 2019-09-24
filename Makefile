ifeq ($(BUILD),debug)   
# "Debug" build - no optimization, and debugging symbols
CFLAGS += -O0 -g
else
# "Release" build - optimization, and no debug symbols
CFLAGS += -O2 -s -DNDEBUG
endif

# install dir
PREFIX = /usr/local

# the compiler: gcc for C program, define as g++ for C++
CC = gcc

# basic compiler flags:
CFLAGS  += -Wall -std=gnu99

#files
OBJFILES = triacd.o optoboard.o hat.o fader.o

# the build target executable:
TARGET = triacd
LIBS += -lrt -pthread

all: $(TARGET)

debug:
	make "BUILD=debug"

$(TARGET): $(OBJFILES)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJFILES) $(LIBS)

clean:
	rm -f $(OBJFILES) $(TARGET) *~
	
install: $(TARGET)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	cp $(TARGET).service $(DESTDIR)/etc/systemd/system

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	rm -f $(DESTDIR)/etc/systemd/system/$(TARGET).service
