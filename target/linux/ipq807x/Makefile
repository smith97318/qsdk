include $(TOPDIR)/rules.mk

ARCH:=aarch64
BOARD:=ipq807x
BOARDNAME:=Qualcomm Atheros IPQ807x
FEATURES:=ubifs squashfs fpu ramdisk nand pcie usbgadget
CPU_TYPE:=cortex-a53
CPU_SUBTYPE:=neon-vfpv4
MAINTAINER:=John Crispin <blogic@openwrt.org>
SUBTARGETS:=generic ipq807x_32

KERNELNAME:=zImage Image dtbs

KERNEL_PATCHVER:=5.4

include $(INCLUDE_DIR)/target.mk
DEFAULT_PACKAGES += \
	uboot-envtools kmod-leds-gpio kmod-gpio-button-hotplug kmod-button-hotplug \
	kmod-ata-core kmod-ata-ahci kmod-ata-ahci-platform kmod-usb3 swconfig \
	kmod-usb-phy-ipq807x kmod-usb-dwc3-qcom-internal

$(eval $(call BuildTarget))
