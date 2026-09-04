PROJECT = stm32_dronecan_pca9685

CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

BUILD_DIR = build

CFLAGS = \
	-mcpu=cortex-m3 \
	-mthumb \
	-Os \
	-ffunction-sections \
	-fdata-sections \
	-Wall \
	-Wextra \
	-Werror \
	-std=c11 \
	-D_DEFAULT_SOURCE \
	-Ilibcanard \
	-Ilibcanard/drivers/stm32 \
	-Isrc \
	-Idsdl_generated/include	

LDFLAGS = \
	-mcpu=cortex-m3 \
	-mthumb \
	-TSTM32F103C8T6.ld \
	-Wl,--gc-sections \
	-Wl,-Map=$(BUILD_DIR)/$(PROJECT).map \
	-nostartfiles \
	-nostdlib

SOURCES = \
	src/main.c \
	src/system.c \
	src/startup_stm32f103.s

OBJECTS = \
	$(BUILD_DIR)/main.o \
	$(BUILD_DIR)/system.o \
	$(BUILD_DIR)/can_hw.o \
	$(BUILD_DIR)/platform.o \
	$(BUILD_DIR)/minilibc.o \
	$(BUILD_DIR)/dronecan.o \
	$(BUILD_DIR)/canard.o \
	$(BUILD_DIR)/canard_stm32.o \
	$(BUILD_DIR)/node_status_msg.o \
	$(BUILD_DIR)/array_command_msg.o \
	$(BUILD_DIR)/get_node_info_res.o \
	$(BUILD_DIR)/startup_stm32f103.o \
	$(BUILD_DIR)/i2c.o \
	$(BUILD_DIR)/pca9685.o \
	$(BUILD_DIR)/status.o \
	$(BUILD_DIR)/oled.o \
	$(BUILD_DIR)/timebase.o	

all: $(BUILD_DIR)/$(PROJECT).elf \
     $(BUILD_DIR)/$(PROJECT).bin \
     size


$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)


$(BUILD_DIR)/main.o: src/main.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/system.o: src/system.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/status.o: src/status.c src/status.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/array_command_msg.o: dsdl_generated/src/uavcan.equipment.actuator.ArrayCommand.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/timebase.o: src/timebase.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/i2c.o: src/i2c.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pca9685.o: src/pca9685.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/oled.o: src/oled.c src/oled.h src/i2c.h src/board.h src/status.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
	
$(BUILD_DIR)/dronecan.o: src/dronecan.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/get_node_info_res.o: dsdl_generated/src/uavcan.protocol.GetNodeInfo_res.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/canard.o: libcanard/canard.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/node_status_msg.o: dsdl_generated/src/uavcan.protocol.NodeStatus.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/minilibc.o: src/minilibc.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/can_hw.o: src/can_hw.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/platform.o: src/platform.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/canard_stm32.o: libcanard/drivers/stm32/canard_stm32.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
	
$(BUILD_DIR)/startup_stm32f103.o: src/startup_stm32f103.s | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@


$(BUILD_DIR)/$(PROJECT).elf: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@ -lgcc


$(BUILD_DIR)/$(PROJECT).bin: $(BUILD_DIR)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@


size: $(BUILD_DIR)/$(PROJECT).elf
	$(SIZE) $<


clean:
	rm -rf $(BUILD_DIR)


.PHONY: all clean size
