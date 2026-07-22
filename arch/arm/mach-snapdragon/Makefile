# SPDX-License-Identifier: GPL-2.0+
#
# (C) Copyright 2015 Mateusz Kulikowski <mateusz.kulikowski@gmail.com>

obj-y += dram.o

ifeq ($(CONFIG_SPL_BUILD),y)
obj-y += board_spl.o
obj-y += fit-handler.o
else
obj-y += board.o
obj-$(CONFIG_EFI_HAVE_CAPSULE_SUPPORT) += capsule_update.o
obj-$(CONFIG_QCOM_FIT_MULTIDTB) += qcom_fit_multidtb.o
obj-$(CONFIG_QCOM_HWDETECT) += qcom_hwdetect.o
obj-$(CONFIG_OF_LIVE) += of_fixup.o
endif
