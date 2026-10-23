.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: reset (command)

reset command
=============

Synopsis
--------

::

    reset [-w]
    reset -m <mode>
    reset -l

Description
-----------

Perform reset of the CPU. By default does COLD reset, which resets CPU,
DDR and peripherals, on some boards also resets external PMIC.

-w
    Do WARM reset: reset CPU but keep peripheral/DDR/PMIC active.

-m <mode>
    Reset into a named mode registered with the reboot-mode framework, for
    example ``reset -m edl`` to enter Qualcomm EDL/download mode. The modes
    are described in the device tree, not hardcoded per SoC (see below); an
    unknown mode prints the list of available modes and fails.

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
``reset_type`` is the ``SYSTEM_RESET2`` reset type from the PSCI
specification. U-Boot always sets bit 31 (the vendor-reset bit) when making
the call, so the current vendor resets such as EDL use ``0x80000000``. With
two cells the second is the cookie; with three cells the second is the high
half and the third the low half of a 64-bit cookie.

.. note::

   U-Boot's PSCI client passes only a 32-bit cookie to firmware, so a 3-cell
   mode whose ``cookie_hi`` is non-zero is rejected rather than silently
   truncated. Every current vendor reset (for example EDL, cookie = 1) fits in
   one or two cells.


Return value
------------

The return value $? is 0 (true) unless a reset mode was requested. It is
non-zero (false) if the mode is unknown, if ``reset -m`` is given no mode name,
or if the reset into the requested mode could not be performed. A successful
reset does not return.
