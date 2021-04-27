ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
dtbo-$(CONFIG_ARCH_LAHAINA) += \
	lahaina-mtp-overlay.dtbo \
	lahaina-mtp-v2-overlay.dtbo \
	lahaina-mtp-v2.1-overlay.dtbo

dtbo-$(CONFIG_MACH_ASUS_PICASSO) += \
	PICASSO-ER1-overlay.dtbo \
	PICASSO-ER2-overlay.dtbo \
	PICASSO-EVB-overlay.dtbo \
	PICASSO-EVB2-overlay.dtbo \
	PICASSO-MP-overlay.dtbo \
	PICASSO-PR1-overlay.dtbo \
	PICASSO-PR2-overlay.dtbo \
	PICASSO-SR1-overlay.dtbo

dtbo-$(CONFIG_MACH_ASUS_SAKE) += \
	SAKE-ER1-overlay.dtbo \
	SAKE-ER2-overlay.dtbo \
	SAKE-EVB-overlay.dtbo \
	SAKE-EVB2-overlay.dtbo \
	SAKE-MP-overlay.dtbo \
	SAKE-PR-overlay.dtbo \
	SAKE-SR1-overlay.dtbo \
	SAKE-SR2-overlay.dtbo

dtbo-$(CONFIG_MACH_ASUS_VODKA) += \
	VODKA-ER1-overlay.dtbo \
	VODKA-ER2-overlay.dtbo \
	VODKA-EVB-overlay.dtbo \
	VODKA-EVB2-overlay.dtbo \
	VODKA-MP-overlay.dtbo \
	VODKA-PR-overlay.dtbo \
	VODKA-SR1-overlay.dtbo \
	VODKA-SR2-overlay.dtbo

dtbo-$(CONFIG_MACH_ASUS_ZS673KS) += \
	ZS673KS-ER1-overlay.dtbo \
	ZS673KS-ER2-overlay.dtbo \
	ZS673KS-EVB-overlay.dtbo \
	ZS673KS-EVB2-overlay.dtbo \
	ZS673KS-MP-overlay.dtbo \
	ZS673KS-PR1-overlay.dtbo \
	ZS673KS-SR1-overlay.dtbo

lahaina-mtp-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
lahaina-mtp-v2-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
lahaina-mtp-v2.1-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)

PICASSO-ER1-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
PICASSO-ER2-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
PICASSO-EVB-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
PICASSO-EVB2-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
PICASSO-MP-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
PICASSO-PR1-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
PICASSO-PR2-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
PICASSO-SR1-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)

SAKE-ER1-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
SAKE-ER2-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
SAKE-EVB-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
SAKE-EVB2-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
SAKE-MP-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
SAKE-PR-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
SAKE-SR1-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
SAKE-SR2-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)

VODKA-ER1-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
VODKA-ER2-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
VODKA-EVB-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
VODKA-EVB2-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
VODKA-MP-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
VODKA-PR-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
VODKA-SR1-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
VODKA-SR2-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)

ZS673KS-ER1-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
ZS673KS-ER2-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
ZS673KS-EVB-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
ZS673KS-EVB2-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
ZS673KS-MP-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
ZS673KS-PR1-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)
ZS673KS-SR1-overlay.dtbo-base := $(lahaina-mtp-overlay.dtbo-base)

else

dtb-$(CONFIG_ARCH_LAHAINA) += \
	lahaina-mtp.dtb \
	lahaina-mtp-v2.dtb \
	lahaina-mtp-v2.1.dtb
endif

always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)
clean-files	:= *.dtb *.dtbo
