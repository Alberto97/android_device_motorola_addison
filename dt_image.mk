LOCAL_PATH := $(call my-dir)

## Build and run dtbtool
DTBTOOL := $(HOST_OUT_EXECUTABLES)/dtbToolCM$(HOST_EXECUTABLE_SUFFIX)
LZ4_DT_IMAGE := $(PRODUCT_OUT)/dt-lz4.img

ifndef TARGET_PREBUILT_DTB
$(INSTALLED_DTIMAGE_TARGET): $(DTBTOOL) $(INSTALLED_KERNEL_TARGET)
	$(call pretty,"Target dt image: $@")
	$(DTBTOOL) $(BOARD_DTBTOOL_ARGS) -o $@ -s $(BOARD_KERNEL_PAGESIZE) -p $(KERNEL_OUT)/scripts/dtc/ $(KERNEL_OUT)/arch/$(KERNEL_ARCH)/boot/
	$(hide) chmod a+r $@
	lz4 -9 < $@ > $(LZ4_DT_IMAGE) || lz4c -c1 -y $@ $(LZ4_DT_IMAGE)
	$(hide) $(ACP) $(LZ4_DT_IMAGE) $@
	@echo "Made DT image: $@"
else
$(INSTALLED_DTIMAGE_TARGET): $(TARGET_PREBUILT_DTB) | $(ACP)
	$(transform-prebuilt-to-target)
endif
