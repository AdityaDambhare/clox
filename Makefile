BINDIR=bin
SRCDIR=clox/src
OBJDIR=obj
INCDIR=clox/include
HEADERS=$(wildcard $(INCDIR)/*.h)
CFLAGS=-Iclox/include  -O3
CC=gcc
SOURCES=$(wildcard $(SRCDIR)/*.c)
OBJECTS=$(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
TARGET=$(BINDIR)/clox

all: $(TARGET)

$(TARGET): $(OBJECTS)
	if	not	exist	"$(BINDIR)"	mkdir  $(BINDIR)
	$(CC) $^ -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(HEADERS)
	if	not	exist	"$(OBJDIR)"	mkdir  $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR) 
	rm -rf $(BINDIR)