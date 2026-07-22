.. SPDX-License-Identifier: GPL-2.0
.. sectionauthor:: Varadarajan Narayanan <varadarajan.narayanan@oss.qualcomm.com>

QCLib Interface Table
=====================

Overview
--------

QCLib (Qualcomm Library) is a Qualcomm proprietary firmware binary that
performs DDR initialization and other pre-DDR hardware bring-up on Snapdragon
SoCs. U-Boot SPL communicates with QCLib through a shared in-memory structure
called the **interface table** (``struct interface_table``). The table is
populated by SPL before jumping to QCLib, and QCLib updates selected entries
before returning.

Interface Table Header
----------------------

The table header occupies the first 0x20 bytes of the structure:

.. code-block:: none

    Offset  Size  Field               Description
    ------  ----  ------------------  ----------------------------------------
    0x00     8    magic_key           ASCII magic: "QCLIB_CB" (no NUL)
    0x08     4    version             Interface version; currently 0x00000001
    0x0C     4    num_entries         Number of valid entries in the table
    0x10     4    max_entries         Maximum entries the table can hold (16)
    0x14     4    global_attributes   Bitmask of global control flags
    0x18     4    reserved1           Reserved; must be zero
    0x1C     4    reserved2           Reserved; must be zero
    0x20     -    entries[]           Array of up to 16 table entries

The magic key ``"QCLIB_CB"`` is validated by QCLib on entry. If the magic does
not match, QCLib will not proceed.

Table Entry Format
------------------

Each entry is 0x28 bytes:

.. code-block:: none

    Offset  Size  Field               Description
    ------  ----  ------------------  ----------------------------------------
    0x00    24    entry_name          NUL-padded ASCII name (see entries below)
    0x18     8    address             Physical address of the blob in SRAM
    0x20     4    size                Size of the blob in bytes
    0x24     4    attributes          Bitmask of per-entry flags

The ``global_attributes`` and per-entry ``attributes`` fields are bitmasks
defined by the QCLib interface specification. SPL initializes both to zero
unless a SoC-specific override sets them.

Interface Table Entries
-----------------------

.. list-table::
   :header-rows: 1
   :widths: 22 12 66

   * - Entry name
     - Direction
     - Description
   * - ``qc_config``
     - Input
     - Entry point / load address of the pre-DDR configuration image
       (``qcom-config-1`` in the FIT). Contains platform configuration
       data consumed by QCLib.
   * - ``qcsdi``
     - Bidirectional
     - QCSDI (Qualcomm Crash Dump Interface) entry. SPL initializes the
       address to zero; QCLib writes the QCSDI physical address here before
       returning. SPL reads it back and passes it to downstream firmware
       (e.g. TF-A BL31 via ``arg0``) for crash-dump support.
