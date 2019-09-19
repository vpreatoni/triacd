# the compiler: gcc for C program, define as g++ for C++
CC = gcc

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
CFLAGS  = -g -Wall -std=gnu99

#files
OBJFILES = triacd.o optoboard.o hat.o statemachine.o

# the build target executable:
TARGET = triacd
LIBS = -lrt

all: $(TARGET)

$(TARGET): $(OBJFILES)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJFILES) $(LIBS)

clean:
	rm -f $(OBJFILES) $(TARGET) *~
