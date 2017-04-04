# architecture-dependent additional targets and manual dependencies

# Program the device.
program: bin hex eep
	$(AVRDUDE) $(AVRDUDE_FLAGS) $(AVRDUDE_WRITE_FLASH)  $(AVRDUDE_WRITE_EEPROM)

# Set fuses of the device
fuses: $(CONFIG)
	$(AVRDUDE) $(AVRDUDE_FLAGS) $(AVRDUDE_WRITE_FUSES)

# Reset EEPROM memory to check defaults
#
# AVRDUDE's "chip erase" won't erase the EEPROM if the EESAVE fuse bit is set
# Starts with reading back the EEPROM contents to determine the size of
# the EEPROM memory, then generates a size filled with 0xFF with the same
# size and writes this generated temporary file.
delete-eeprom:
	$(AVRDUDE) $(AVRDUDE_FLAGS) -U eeprom:r:rb.bin:r
	scripts/avr/make-ff.sh `wc -c < rb.bin`
	$(AVRDUDE) $(AVRDUDE_FLAGS) -U eeprom:w:ff.bin:r
	rm -f ff.bin rb.bin

# Manual dependency for the assembler module
$(OBJDIR)/src/avr/fastloader-ll.o: src/config.h src/fastloader.h $(OBJDIR)/autoconf.h

