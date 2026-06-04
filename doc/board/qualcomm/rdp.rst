.. SPDX-License-Identifier: GPL-2.0
.. sectionauthor:: Varadarajan Narayanan <quic_varada@quicinc.com>

Qualcomm Reference Design Platform (RDP)
========================================

Qualcomm RDPs are development boards based on the Qualcomm IPQ series of
SoCs. These SoCs are used as the application processors in WiFi router
platforms. RDPs come in multiple variants with differences in storage
medium (NOR, NAND, MMC), no. of USB and PCIe ports, n/w ports etc.

.. _Qualcomm's product page: https://www.qualcomm.com/products/internet-of-things/networking/wi-fi-networks/networking-pro-series/qualcomm-networking-pro-820-platform

Installation
------------
First, setup ``CROSS_COMPILE`` for aarch64. Then, build U-Boot for ``IPQ9574``::

  $ export CROSS_COMPILE=<aarch64 toolchain prefix>
  $ make qcom_ipq9574_mmc_defconfig
  $ make -j8

This will build ``u-boot.elf`` in the configured output directory.

The firmware expects the ELF images to be in MBN format. The `elftombn.py` tool
can be used to convert the ELF images to MBN format.

	IPQ9574: (MBN version 6)

		$ python elftombn.py -f u-boot.elf -o u-boot.mbn -v6

	IPQ5424: (MBN version 7)

		$ python elftombn.py -f u-boot.elf -o u-boot.mbn -v7

Then install the resulting ``u-boot.mbn`` to the ``0:APPSBL`` partition
on your device with::

  IPQ9574# tftpboot path/to/u-boot.mbn
  IPQ9574# mmc part (note down the start & end block no.s of '0:APPSBL' partition)
  IPQ9574# mmc erase <start blk no> <count>
  IPQ9574# mmc write $fileaddr <blk no> <count>

U-Boot should be running after a reboot (``reset``).

Build steps for IPQ5210 based Qualcomm Dragonwing F8 & N8 Platforms:
--------------------------------------------------------------------

Please refer to the following URLs for more details about the platforms.

	F8: https://www.qualcomm.com/networking-infrastructure/products/f-series/f8-platform

	N8: https://www.qualcomm.com/networking-infrastructure/products/n-series/n8-platform

1. Since U-Boot SPL is enabled on these platforms, the build command generates
   both the U-Boot SPL and U-Boot proper images. Assuming ${uboot_dir} is the
   top of the U-Boot sources and ${out_dir} as the output directory,

	$ cd ${uboot_dir}
	$ export CROSS_COMPILE=<aarch64 toolchain prefix>
	$ make -j8 O=${out_dir} qcom_ipq5210_mmc_defconfig
	$ make -j8 O=${out_dir}

   U-Boot SPL image:	${out_dir}/spl/u-boot-spl.wrap-elf
   U-Boot image:	${out_dir}/u-boot.elf

2. Convert the SPL image to multi ELF

	$ cd ${out_dir}/spl
	$ python elftombn.py -f u-boot-spl.wrap-elf -o u-boot-spl.mbn -v7
	$ python `create_multielf.py` -f u-boot-spl.mbn,tmel-ipq52xx-patch.elf \
					-o u-boot-spl.melf

   This u-boot-spl.melf should be flashed into 0:SPL partition.
   Please see below for the location of `tmel-ipq52xx-patch.elf`

3. Convert the U-Boot image to bootloader image

	$ cd ${out_dir}
	$ python elftombn.py -f u-boot.elf -o u-boot.mbn -v7

   The u-boot.mbn has to be combined with `qc_config.elf`, `QCLib.elf`, `TFA`
   and `OPTEE`. Please see below for the location for these ELFs. TFA and OPTEE
   can be built from the sources using the following commands

   TFA:
	$ make PLAT=ipq52xx QTISECLIB_PATH=path/to/`libqtisec_dbg.a` SPD=opteed

   OPTEE:
	$ make PLATFORM=qcom-ipq52xx -j16

   These binaries can be combined into a flashable image using binman.

   Assuming all the required binaries are available in ${uboot_dir}/binman

	$ export bm=${uboot_dir}/binman
	$ cd ${bm}
	$ ${uboot_dir}/tools/binman/binman			\
		--toolpath ${uboot_dir}/tools build		\
		-u						\
		-d ${out_dir}/u-boot.dtb			\
		-O ${bm}.out					\
		-I ${uboot_dir}					\
		-I ${bm}					\
		-I ${uboot_dir}/board				\
		-I ${out_dir}/dts/upstream/src/arm64		\
		-a of-list="qcom/ipq5210-rdp504"		\
		-a atf-bl31-path=${bm}/bl31.mbn			\
		-a tee-os-path=${bm}/tee-pager_v2.mbn		\
		-a qcom-config-path=${bm}/qc_config.elf		\
		-a qcom-lib-path=${bm}/QCLib.elf		\
		-a qcom-appsbl-path=${out_dir}/u-boot.mbn	\
		-a default-dt="qcom/ipq5210-rdp504"		\
		-a spl-bss-pad=1				\
		-a spl-dtb=y					\
		-a of-spl-remove-props="interrupt-parent interrupts"

   This should be flashed into 0:BOOTLDR partition.

.. WARNING
	Boards with newer software versions would automatically go the emergency
	download (EDL) mode if U-Boot is not functioning as expected. If its a
	runtime failure at Uboot, the system will get reset (due to watchdog)
	and XBL will try to boot from next bank and if Bank B also doesn't have
	a functional image and is not booting fine, then the system will enter
	EDL.  A tool like bkerler's `edl` can be used for flashing with the
	firehose loader binary appropriate for the board.

	Note that the support added is very basic. Restoring the original U-Boot
	on boards with older version of the software requires a debugger.

.. _create_multielf.py: https://raw.githubusercontent.com/coreboot/coreboot/refs/heads/main/util/qualcomm/create_multielf.py
.. _elftombn.py: https://git.codelinaro.org/clo/qsdk/oss/system/tools/meta/-/tree/NHSS.QSDK.13.0.5.r2/scripts?ref_type=heads
.. _edl: https://github.com/bkerler/edl
.. _libqtisec_dbg.a: https://softwarecenter.qualcomm.com/nexus/generic/product/chip/software-product/IPQ5210.NLQ.14.0/ipq5210.nlq.14.0-qca-oem-qartifact/r00036.1/WIN.TFA.1.0.R4/apss_proc/out/proprietary/qtiseclib/output/ipq52xx/release/libqtisec_dbg.a
.. _OPTEE: https://git.codelinaro.org/clo/trusted-firmware/optee_os/optee_os/-/tree/win.optee.1.0?ref_type=heads
.. _qc_config.elf: https://softwarecenter.qualcomm.com/nexus/generic/product/chip/software-product/IPQ5210.NLQ.14.0/ipq5210.nlq.14.0-qca-oem-qartifact/r00036.1/BOOT.MXF.2.3.1.1/boot_images/boot/QcomPkg/SocPkg/Hermosa/Bin/LC/RELEASE/qc_config.elf
.. _QCLib.elf: https://softwarecenter.qualcomm.com/nexus/generic/product/chip/software-product/IPQ5210.NLQ.14.0/ipq5210.nlq.14.0-qca-oem-qartifact/r00036.1/BOOT.MXF.2.3.1.1/boot_images/boot/QcomPkg/SocPkg/Hermosa/Bin/LC/RELEASE/QCLib.elf
.. _TFA: https://git.codelinaro.org/clo/trusted-firmware/tf-a/trusted-firmware-a/-/tree/win.tfa.1.0.r4?ref_type=heads
.. _tmel-ipq52xx-patch.elf: https://softwarecenter.qualcomm.com/nexus/generic/product/chip/software-product/IPQ5210.NLQ.14.0/ipq5210.nlq.14.0-qca-oem-qartifact/r00036.1/TMEL.WNS.2.4/tmel-ipq52xx-patch.elf
