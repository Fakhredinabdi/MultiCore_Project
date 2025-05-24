CC        := gcc
CFLAGS    := -O3 -std=c11 -Wall -Wextra -pedantic -pthread \
             -Wno-implicit-fallthrough
SRCDIR    := src
BINDIR    := bin
STUDENTID := MCC_030402_99106458
TARGET    := $(BINDIR)/$(STUDENTID)
SRC       := $(SRCDIR)/main.c
TARBALL   := $(STUDENTID).tar.gz

.PHONY: all clean run dist

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	@rm -rf $(BINDIR) results $(TARBALL)

run: all
	@mkdir -p results
	@echo "Running $(TARGET)"
	$(TARGET) \
	  --data_size $(data_size) \
	  --threads    $(threads)   \
	  --tsize      $(tsize)     \
	  --input      $(input)

dist: all
	@rm -rf $(STUDENTID)
	@mkdir $(STUDENTID)
	@cp -r bin src results $(STUDENTID)/
	@tar -czf $(TARBALL) $(STUDENTID)
	@rm -rf $(STUDENTID)
	@echo "=> Created $(TARBALL)"
