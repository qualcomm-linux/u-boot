.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: reset (command)

reset command
=============

Synopsis
--------

::

    reset [-w]
    reset -<mode>
    reset -l

Description
-----------

Perform reset of the CPU. By default does COLD reset, which resets CPU,
DDR and peripherals, on some boards also resets external PMIC.

-w
    Do WARM reset: reset CPU but keep peripheral/DDR/PMIC active.

-<mode>
    Reset into a named mode registered with the reboot-mode framework, for
    example ``reset -edl`` to enter Qualcomm EDL/download mode. The modes are
    described in the device tree, not hardcoded per SoC (see below); an
    unknown mode prints the list of available modes.

-l
    List the reset modes registered with the reboot-mode framework.

Reset modes
-----------

Named reset modes are declared in the device tree rather than compiled into a
driver. For PSCI-based systems they live in a ``reboot-mode`` subnode of the
``psci`` node, one ``mode-<name>`` property per mode:

.. code-block:: dts

    psci {
        compatible = "arm,psci-1.0";
        method = "smc";

        reboot-mode {
            mode-edl = <0x80000000 0x00000001>;
        };
    };

Each ``mode-<name>`` property carries 1 to 3 cells describing a PSCI
``SYSTEM_RESET2`` vendor reset: ``<reset_type[, cookie_hi[, cookie_lo]]>``.
``reset_type`` must have bit 31 set (the vendor-reset bit). With two cells the
second is the cookie; with three cells the second is the high half and the
third the low half of a 64-bit cookie.

.. note::

   U-Boot's PSCI client passes only a 32-bit cookie to firmware, so a 3-cell
   mode whose ``cookie_hi`` is non-zero is rejected rather than silently
   truncated. Every current vendor reset (for example EDL, cookie = 1) fits in
   one or two cells.


Return value
------------

The return value $? is always set to 0 (true).
