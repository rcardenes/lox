DIRDEPS := build bin
TARGETS := bin/vlox

.PHONY: $(TARGETS)

all: $(DIRDEPS) $(TARGETS)

$(DIRDEPS): ; @mkdir -p $@

bin/vlox:
	make -C src

clean:
	rm -f build/*
	rm -f bin/*

distclean:
	rm -rf $(DIRDEPS)

