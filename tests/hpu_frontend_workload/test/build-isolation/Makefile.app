# 只探测真实Makefile.case到递归make的变量传递；不编译或执行HPU。
DST_DIR ?= app-default-objects
BINARY ?= app-default-binary

.PHONY: image
image:
	@printf 'parent_dst=%s\nparent_binary=%s\n' '$(DST_DIR)' '$(BINARY)'
	@$(MAKE) -s -f '$(AM_HOME)/Makefile.child' show-layout
