.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

.. index::
   single: efidebug (command)

efidebug command
================

Synopsis
--------

::

    efidebug capsule images

Description
-----------

The *efidebug* command provides debugging access to the UEFI sub-system.
Only the *capsule images* sub-command is described here.

efidebug capsule images
~~~~~~~~~~~~~~~~~~~~~~~

List the firmware images that can be updated by a capsule on this board.
The command walks all instances of the Firmware Management Protocol (FMP)
and prints the image descriptors each of them reports: the image index, the
firmware name and the image type GUID.

The image type GUID is what the GUID in a capsule has to match for the
capsule to be applied to the image. It can be used to check the GUIDs in a
capsule against what the board expects before attempting an update.

Example
-------

::

    => efidebug capsule images
    Image Index    Firmware Name         Image Type GUID
    ===========    ===================== ====================================
              1    SANDBOX-UBOOT         985f2937-7c2e-5e9a-8a5e-8e063312964b
              2    SANDBOX-UBOOT-ENV     9e339473-c2eb-530a-a69b-0cd6bbbed40e

Configuration
-------------

The *efidebug* command is enabled by CONFIG_CMD_EFIDEBUG=y. The list is only
available if a capsule firmware update method is enabled, for example with
CONFIG_EFI_CAPSULE_FIRMWARE_RAW=y.

Return value
------------

The return value $? is 0 (true) on success, 1 (false) on failure. If no
firmware image is found, a message is printed and the return value is 0.
