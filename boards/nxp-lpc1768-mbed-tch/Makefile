-include ../../config.mk

export TARGET := freertos.armv7m

include $(OPENMRNPATH)/etc/mbed.mk
DEPS += HAVE_MBED

SYSLIBRARIESEXTRA = -lfreertos_drivers_mbed_lpc1768

OBJEXTRA = $(OPENMRNPATH)/targets/$(TARGET)/freertos_drivers/drivers_lpc1768/mbed_can.o \
           $(OPENMRNPATH)/targets/$(TARGET)/freertos_drivers/drivers_lpc1768/mbed_async_usbserial.o \
           $(OPENMRNPATH)/targets/$(TARGET)/freertos_drivers/drivers_lpc1768/stack_malloc.o \

INCLUDES += \
	-I"$(MBEDPATH)/mbed/vendor/NXP/capi" \
	-I"$(MBEDPATH)/mbed/vendor/NXP/capi/LPC1768" \
	-I"$(MBEDPATH)/mbed/vendor/NXP/cmsis/LPC1768"

include $(OPENMRNPATH)/etc/prog.mk

CORECFLAGS += -DTARGET_LPC1768 -DSNIFF_ON_USB -DINTERRUPT_ATTRIBUTE=  

ifeq ($(MISSING_DEPS),)

all: $(EXECUTABLE).bin

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	$(CHECKSUM) -p LPC1768 -d $@

endif  #MISSING_DEPS
