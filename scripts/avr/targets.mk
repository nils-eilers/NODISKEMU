# architecture-dependent additional targets and manual dependencies

# Program the device.
program: bin hex eep
	$(AVRDUDE) $(AVRDUDE_FLAGS) $(AVRDUDE_WRITE_FLASH)  $(AVRDUDE_WRITE_EEPROM)

# Set fuses of the device
fuses: $(CONFIG)
	$(AVRDUDE) $(AVRDUDE_FLAGS) $(AVRDUDE_WRITE_FUSES)


# Manual dependency for the assembler module
$(OBJDIR)/src/avr/fastloader-ll.o: src/config.h src/fastloader.h $(OBJDIR)/autoconf.h

