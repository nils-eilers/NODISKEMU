# Hey Emacs, this is a -*- makefile -*-

ifndef CONFIG
  CONFIG = config
endif

CONFDATA     := $(shell perl ../scripts/configparser.pl --confdata $(CONFIG))
CONFIGSUFFIX := $(word 1,$(CONFDATA))
OBJDIR       := obj-$(CONFIGSUFFIX)
CONFFILES    := $(wordlist 2,99,$(CONFDATA))

export CONFIGSUFFIX CONFIG OBJDIR

# Enable verbose compilation with "make V=1"
ifdef V
 Q :=
 E := @:
else
 Q := @
 E := @echo
endif

all: $(OBJDIR) $(OBJDIR)/make.inc
	$(Q)$(MAKE) --no-print-directory -f Makefile.main

$(OBJDIR)/make.inc: $(CONFFILES) | $(OBJDIR)
	$(E) "  CONFIG $(CONFFILES)"
	$(Q)perl ../scripts/configparser.pl --genfiles --makeinc $(OBJDIR)/make.inc --header $(OBJDIR)/autoconf.h $(CONFIG)

$(OBJDIR):
	$(E) "  MKDIR  $(OBJDIR)"
	-$(Q)mkdir $(OBJDIR)

copy clean fuses program delete-eeprom lst: FORCE | $(OBJDIR) $(OBJDIR)/make.inc
	$(Q)$(MAKE) --no-print-directory -f Makefile.main $@

FORCE: ;
