# SPDX-License-Identifier: GPL-2.0+
#
# (C) Copyright 2015 Mateusz Kulikowski <mateusz.kulikowski@gmail.com>

ifndef CONFIG_XPL_BUILD
obj-y += board.o dram.o
obj-$(CONFIG_EFI_HAVE_CAPSULE_SUPPORT) += capsule_update.o
obj-$(CONFIG_OF_LIVE) += of_fixup.o
endif
obj-$(CONFIG_SPL_BUILD) += spl.o
