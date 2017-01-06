# architecture-dependent additional targets and manual dependencies

# Program the device.
program: bin hex eep
	$(AVRDUDE) $(AVRDUDE_FLAGS) $(AVRDUDE_WRITE_FLASH)  $(AVRDUDE_WRITE_EEPROM)

# bootloader firmware update for petSD-duo
ifeq ($(CONFIG_HARDWARE_VARIANT),11)
load: hex
	update-petSD-duo -d $(BOOTLOADER_DEVICE) -P mast -p $(TARGET).hex -b 230400

autoload: hex
	clear ; \
	printf "\n\tPress [Ctrl-a] [d] to detach" ; \
	sleep 3 ; \
	while true ; \
	do \
   	   screen $(BOOTLOADER_DEVICE) 115200; \
	   kill `lsof | grep /dev/cu.usbserial-00303424 | awk '{print $$2}'` ; \
	   clear ; \
	   printf "Press\n\n\tCtrl-C to abort and return to shell prompt\n\n\tENTER to upload firmware" ; \
	   read ; \
	   update-petSD-duo -d $(BOOTLOADER_DEVICE) -P mast -p $(TARGET).hex -b 230400 ; \
	   clear ; \
	   printf "Press\n\n\tCtrl-C to abort and return to shell prompt\n\n\tENTER to show debug messages" ; \
	   read ; \
	done
endif

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

