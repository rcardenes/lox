SUFFIXES += .d
CC := gcc-12
DEPDIR := .deps
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d
TARGETS := vlox
TARGETSRC := $(patsubst %,%.c,$(TARGETS))
TARGETDIR := ../bin
BUILDDIR := ../build
LOCALDEPS := main.c chunk.c memory.c debug.c value.c vm.c compiler.c scanner.c \
	     object.c table.c native.c list.c
OBJFILES := $(patsubst %.c,%.o,$(patsubst %,$(BUILDDIR)/%,$(LOCALDEPS)))
SOURCES := $(TARGETSRC) $(LOCALDEPS)
DEPFILES := $(SOURCES:%.c=$(DEPDIR)/%.d)
OBJTARGET := $(patsubst %,$(TARGETDIR)/%,$(TARGETS))

NODEPS := clean

CFLAGS := -std=c11 -Wall -Wpedantic -g -I ../common

all: $(TARGETDIR) $(BUILDDIR) $(OBJTARGET)

# Don't create dependencies when we're cleaning, for instance
ifeq (0, $(words $(findstring $(MAKECMDGOALS), $(NODEPS))))
    #Chances are, these files don't exist.  GMake will create them and
    #clean up automatically afterwards
    -include $(DEPFILES)
endif

.SECONDEXPANSION:
$(OBJTARGET): $(OBJFILES)
	$(CC) $(OUTPUT_OPTION) $^

$(TARGETDIR) $(BUILDDIR): ; mkdir -p $@

$(BUILDDIR)/%.o: %.c
$(BUILDDIR)/%.o: %.c $(DEPDIR)/%.d | $(DEPDIR)
	$(CC) $(DEPFLAGS) $(CFLAGS) -o $@ -c $<

$(DEPDIR): ; @mkdir -p $@

$(DEPFILES):

include $(wildcard $(DEPFILES))

clean:
	rm -f $(OBJFILES)
	rm -f $(OBJTARGET)
	rm -fr $(DEPDIR)
