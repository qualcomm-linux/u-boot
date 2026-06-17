.. SPDX-License-Identifier: GPL-2.0+

Snagboot Mode for Qualcomm Platforms
=====================================

Overview
--------

Snagboot is an open-source, scriptable flashing framework that provides
an alternative to the proprietary Firehose protocol for device
provisioning. Like the traditional EDL+Firehose flow, Snagboot uses the
Qualcomm boot ROM recovery mode (EDL) and Sahara protocol to download
and execute bootloaders. However, instead of using the proprietary
Firehose programmer, Snagboot loads U-Boot into DDR and uses the
standard fastboot protocol for device flashing.

The framework consists of three modular components:

* **snagrecover**: Downloads XBL/QCLib to initialize DDR and load U-Boot into RAM
* **snagflash**: Communicates with U-Boot over USB using fastboot to flash system images
* **snagfactory**: Orchestrates parallel factory flashing tasks

Why Snagboot?
-------------

* EDL + Firehose remains the default and supported factory provisioning flow
* Customers are requesting support for Snagboot, an open, scriptable flashing framework,
  as an alternative option
* Snagboot reuses Qualcomm recovery primitives and standard flashing protocols,
  minimizing platform disruption
* Provides an open-source alternative to proprietary flashing tools

Architecture
------------

Snagboot Execution Flow on Qualcomm SoCs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The snagboot execution flow consists of two main phases:

**Phase 1 - snagrecover**:

1. ROM code enters recovery mode (EDL)
2. Snagrecover uses Sahara protocol to download XBL/QCLib to internal memory
3. XBL executes and initializes DDR
4. Snagrecover downloads U-Boot into DDR
5. Control transfers to U-Boot

**Phase 2 - snagflash**:

1. U-Boot runs in DDR and enters fastboot mode
2. Snagflash communicates with U-Boot over USB using fastboot protocol
3. System images are flashed to non-volatile memory (UFS, eMMC)

Boot Environment Considerations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In the snagboot execution flow, ROM code in recovery mode loads
snagrecover, which downloads XBL to internal memory. XBL executes
and initializes DDR, then snagrecover downloads U-Boot into DDR and
transfers control to it. At this point, XBL has left the MMU enabled
with its own page tables and has not initialized PSCI firmware or
populated the Command DB. U-Boot must handle this non-standard boot
environment to enter fastboot mode for device flashing.

This is why several standard U-Boot features must be disabled in snagboot mode.

Comparison with Traditional EDL+Firehose
-----------------------------------------

**Standard Device Provisioning Path Using EDL and Firehose**:

* **Emergency Download Mode (EDL)**:

  * EDL is a special boot mode built into Qualcomm SoCs Primary Bootloader (PBL) in ROM
  * Triggered via hardware strap, software command, or recovery from boot failure

* **Sahara Protocol (Stage 1)**:

  * Host PC uses Sahara protocol to communicate with the SoC's PBL
  * Transfers Device programmer (Firehose) into RAM

* **Firehose Protocol (Stage 2)**:

  * Once the Firehose Programmer is running on the device, it establishes a Firehose
    protocol session with the host
  * Host sends XML-based commands to flash firmware images to specific partitions,
    erase or read flash memory and manage partition tables
  * Integrated into tools like QFIL and PCAT

**Snagboot Alternative**:

Instead of loading a proprietary Firehose programmer, Snagboot loads U-Boot into DDR
and uses the standard, open-source fastboot protocol for flashing operations.

Supported Platforms
-------------------

Currently supported platforms:

* **Lemans-EVK **: Initial platform with snagboot support

Additional platforms can be enabled by following the board enablement guide below.

Board Enablement
----------------

To enable snagboot mode on a new platform, follow these steps:

Required Kconfig Options
^^^^^^^^^^^^^^^^^^^^^^^^

1. Enable snagboot mode::

    CONFIG_QCOM_SNAGBOOT_MODE=y

   This will automatically select:

   * ``CONFIG_QCOM_BOOT0_SNAGBOOT_MODE``: Early boot initialization
   * ``CONFIG_ENABLE_ARM_SOC_BOOT0_HOOK``: Enable boot0 hooks
   * ``CONFIG_SKIP_RELOCATE``: Skip relocation (U-Boot runs from load address)

2. Platform-specific settings that must be configured:

   * ``CONFIG_COUNTER_FREQUENCY``: Set to your platform's timer frequency (e.g., 19200000 for Lemans)
   * ``CONFIG_TEXT_BASE``: Set to the address where XBL will load U-Boot (e.g., 0x1c100000 for Lemans)
   * ``CONFIG_FASTBOOT_BUF_ADDR``: Set fastboot buffer address for your platform

Disabled Configs
^^^^^^^^^^^^^^^^

The following configs must be disabled in snagboot mode (automatically handled by the
config fragment):

* ``CONFIG_PSCI_RESET``: Not available in Snagboot mode (XBL doesn't initialize PSCI firmware)
* ``CONFIG_IOMMU``: Not initialized by XBL in Snagboot mode
* ``CONFIG_SAVE_PREV_BL_INITRAMFS_START_ADDR``: Previous bootloader context is not preserved
  in snagboot mode
* ``CONFIG_REBOOT_MODE_ENV_UPDATE``: Not applicable in device/factory provisioning mode

Environment Configuration
^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``CONFIG_ENV_IS_NOWHERE=y`` to ensure recovery works even with corrupted environment
storage. This is critical for a recovery mechanism - it should not load a potentially
broken or untrusted environment from the board.

Using the Config Fragment
^^^^^^^^^^^^^^^^^^^^^^^^^^

A reusable config fragment is provided at ``configs/qcom-snagboot.config`` that contains
all common snagboot settings. Include it in your platform-specific defconfig::

    # Configuration for building U-Boot for snagboot/recovery mode
    # on <Your Platform> boards.
    #
    # For normal production boot, use <platform>_defconfig instead.

    #include "qcom_defconfig"
    #include "qcom-snagboot.config"

    # Platform-specific settings for <Your Platform>

    # Address where U-Boot will be loaded
    CONFIG_TEXT_BASE=0x1c100000
    CONFIG_REMAKE_ELF=y
    CONFIG_FASTBOOT_BUF_ADDR=0xdb300000
    CONFIG_DEFAULT_DEVICE_TREE="qcom/<your-board>"

    # Timer frequency for your platform
    CONFIG_COUNTER_FREQUENCY=19200000

Example: Lemans-EVK
^^^^^^^^^^^^^^^^^^^

See ``configs/qcom_lemans_snagboot_defconfig`` for a complete example of a
platform-specific snagboot defconfig.

References
----------

* Snagboot project: https://github.com/bootlin/snagboot
* Snagboot documentation: https://github.com/bootlin/snagboot/tree/main/docs
* Qualcomm EDL mode: See your platform's technical documentation
