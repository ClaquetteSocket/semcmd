.PHONY : clean all install install-completions uninstall

CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -D_REENTRANT
PREFIX ?= /usr/local
# zsh does not expand '~' in 'PREFIX=~/.local', so expand it here
override PREFIX := $(patsubst ~/%,$(HOME)/%,$(PREFIX))
BINDIR = $(PREFIX)/bin

BASH_COMPLETION_DIR = $(PREFIX)/share/bash-completion/completions
ZSH_COMPLETION_DIR = $(PREFIX)/share/zsh/site-functions
# Directories in the default fpath of zsh: no configuration needed
ZSH_DEFAULT_FPATH = /usr/share/zsh/site-functions /usr/local/share/zsh/site-functions

all : semcmd

semcmd : Makefile src/semcmd.c
	$(CC) $(CFLAGS) src/semcmd.c -o semcmd

# The failed command has already displayed the cause of the error: only report
# the failed step, and suggest a fix for the most common cause.
define install_error
	echo "Error: $1 failed (see the message above)." >&2; \
	echo "If you cannot write to $(PREFIX), use 'sudo make $@' or 'make $@ PREFIX=\$$HOME/.local'." >&2; \
	exit 1
endef

install : semcmd
	@install -d "$(BINDIR)" || { $(call install_error,creating $(BINDIR)); }
	@install -m 755 semcmd "$(BINDIR)" || { $(call install_error,installing semcmd to $(BINDIR)); }

install-completions : install
	@install -d "$(BASH_COMPLETION_DIR)" || { $(call install_error,creating $(BASH_COMPLETION_DIR)); }
	@install -d "$(ZSH_COMPLETION_DIR)" || { $(call install_error,creating $(ZSH_COMPLETION_DIR)); }
	@install -m 644 completions/_semcmd_completion.bash "$(BASH_COMPLETION_DIR)/semcmd" || { $(call install_error,installing bash completion); }
	@install -m 644 completions/_semcmd "$(ZSH_COMPLETION_DIR)/_semcmd" || { $(call install_error,installing zsh completion); }
ifeq ($(filter $(ZSH_DEFAULT_FPATH),$(ZSH_COMPLETION_DIR)),)
	@echo "Note for zsh users: add this line to ~/.zshrc, before 'compinit', then restart your shell:"
	@echo "  fpath=($(ZSH_COMPLETION_DIR) \$$fpath)"
endif

uninstall :
	@rm -f "$(BINDIR)/semcmd" || { $(call install_error,removing semcmd from $(BINDIR)); }
	@rm -f "$(BASH_COMPLETION_DIR)/semcmd" || { $(call install_error,removing bash completion from $(BASH_COMPLETION_DIR)); }
	@rm -f "$(ZSH_COMPLETION_DIR)/_semcmd" || { $(call install_error,removing zsh completion from $(ZSH_COMPLETION_DIR)); }

clean :
	rm -f semcmd
