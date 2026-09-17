# SPDX-License-Identifier: GPL-2.0+
#
# (C) Copyright Linaro Ltd.
#
# Qualcomm specific make target for MBN signed ELF files.
#

# Create Qualcomm signed elf images
CMD_MKMBN = $(srctree)/tools/qcom/mkmbn/mkmbn.py
quiet_cmd_mkmbn = MBN     $@
      cmd_mkmbn = $(CMD_MKMBN) -l $(CONFIG_TEXT_BASE) $<

u-boot.mbn: u-boot.bin FORCE
	$(call if_changed,mkmbn)

ifeq ($(CONFIG_QCOM_GENERATE_MBN),y)

quiet_cmd_mksplmbn = SPLMBN     $@
      cmd_mksplmbn = $(CMD_MKMBN) -o spl/u-boot-spl.mbn -l $(CONFIG_SPL_TEXT_BASE) $<

INPUTS-$(CONFIG_SPL) += spl/u-boot-spl.mbn

spl/u-boot-spl.mbn: spl/u-boot-spl.bin FORCE
	$(call if_changed,mksplmbn)

ifneq ($(wildcard $(CONFIG_QCOM_TMEL_ELF)),)

quiet_cmd_mksplmelf = SPLMELF     $@
      cmd_mksplmelf = $(CMD_MKMBN) -o spl/u-boot-spl.melf spl/u-boot-spl.mbn $(CONFIG_QCOM_TMEL_ELF)

INPUTS-$(CONFIG_SPL) += spl/u-boot-spl.melf

spl/u-boot-spl.melf: spl/u-boot-spl.mbn FORCE
	$(call if_changed,mksplmelf)
endif	# CONFIG_QCOM_TMEL_ELF

endif	# CONFIG_QCOM_GENERATE_MBN
