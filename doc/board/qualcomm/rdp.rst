.. SPDX-License-Identifier: GPL-2.0
.. sectionauthor:: Varadarajan Narayanan <varadarajan.narayanan@oss.qualcomm.com>

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

This will build the signed ``u-boot.mbn`` in the configured output directory. More information
about image signing can be found in :doc:`signing`.

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

Since U-Boot SPL is enabled on these platforms, the build command generates both
the U-Boot SPL and U-Boot proper images.

Download `tmel-ipq52xx-patch.elf` and update CONFIG_QCOM_TMEL_ELF in the config
file (i.e. configs/qcom_ipq5210_mmc_defconfig) as appropriate.

Assuming ${uboot_dir} is the top of the U-Boot sources and ${out_dir} as the
output directory,

.. code-block:: bash

    cd ${uboot_dir}
    export CROSS_COMPILE=<aarch64 toolchain prefix>
    make -j8 O=${out_dir} qcom_ipq5210_mmc_defconfig
    make -j8 O=${out_dir}

U-Boot SPL image:      ${out_dir}/spl/u-boot-spl.melf
U-Boot image:  ${out_dir}/u-boot.mbn

TFA:

.. code-block:: bash

    make PLAT=ipq52xx QTISECLIB_PATH=path/to/`libqtisec_dbg.a` SPD=opteed

OPTEE:

.. code-block:: bash

    make PLATFORM=qcom-ipq52xx -j16

These binaries can be combined into a flashable image using `gen_its.py`.

.. code-block:: bash

	python gen_its.py --arch ipq5210                \
		--qclib_path `QCLib.elf`                \
		--qcconfig_path `qc_config.elf`         \
		--tfa_bl31_path bl31.mbn                \
		--uboot_path u-boot.mbn                 \
		--optee_path tee-raw.mbn                \
		-p qcconfig qclib                       \
		-P tfa_bl31 uboot optee                 \
		-o output/hm_503_test_uboot.img         \
		--template `template.its`

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

.. _edl: https://github.com/bkerler/edl
.. _gen_its.py: https://git.codelinaro.org/clo/qsdk/oss/system/tools/meta/-/tree/win.platform_tools.1.0.r34/scripts?ref_type=heads
.. _libqtisec_dbg.a: https://softwarecenter.qualcomm.com/nexus/generic/product/chip/software-product/IPQ5210.NLQ.14.0/ipq5210.nlq.14.0-qca-oem-qartifact/r00036.1/WIN.TFA.1.0.R4/apss_proc/out/proprietary/qtiseclib/output/ipq52xx/release/libqtisec_dbg.a
.. _OPTEE: https://git.codelinaro.org/clo/trusted-firmware/optee_os/optee_os/-/tree/win.optee.1.0?ref_type=heads
.. _qc_config.elf: https://softwarecenter.qualcomm.com/nexus/generic/product/chip/software-product/IPQ5210.NLQ.14.0/ipq5210.nlq.14.0-qca-oem-qartifact/r00036.1/BOOT.MXF.2.3.1.1/boot_images/boot/QcomPkg/SocPkg/Hermosa/Bin/LC/RELEASE/qc_config.elf
.. _QCLib.elf: https://softwarecenter.qualcomm.com/nexus/generic/product/chip/software-product/IPQ5210.NLQ.14.0/ipq5210.nlq.14.0-qca-oem-qartifact/r00036.1/BOOT.MXF.2.3.1.1/boot_images/boot/QcomPkg/SocPkg/Hermosa/Bin/LC/RELEASE/QCLib.elf
.. _template.its: https://git.codelinaro.org/clo/qsdk/oss/system/tools/meta/-/tree/win.platform_tools.1.0.r34/scripts?ref_type=heads
.. _TFA: https://git.codelinaro.org/clo/trusted-firmware/tf-a/trusted-firmware-a/-/tree/win.tfa.1.0.r4?ref_type=heads
.. _tmel-ipq52xx-patch.elf: https://softwarecenter.qualcomm.com/nexus/generic/product/chip/software-product/IPQ5210.NLQ.14.0/ipq5210.nlq.14.0-qca-oem-qartifact/r00036.1/TMEL.WNS.2.4/tmel-ipq52xx-patch.elf
