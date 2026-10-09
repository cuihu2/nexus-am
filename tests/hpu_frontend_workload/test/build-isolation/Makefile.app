# Probe variable propagation from the real Makefile.case without compiling HPU code.
DST_DIR ?= app-default-objects
BINARY ?= app-default-binary

.PHONY: image
image:
	@printf 'parent_dst=%s\nparent_binary=%s\n' '$(DST_DIR)' '$(BINARY)'
	@$(MAKE) -s -f '$(AM_HOME)/Makefile.child' show-layout
