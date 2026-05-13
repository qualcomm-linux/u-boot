.. SPDX-License-Identifier: GPL-2.0+
.. Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

FvUpdate.xml to Capsule Converter
==================================

This Python script provides complete end-to-end conversion from Qualcomm FvUpdate.xml files to ready-to-deploy EFI capsule files for U-Boot capsule updates.

Overview
--------

The script offers two modes of operation:

1. **FIT-only mode**: Converts FvUpdate.xml to FIT images
2. **Complete mode**: Full workflow from XML to final capsule file

The complete workflow includes:

- FvUpdate.xml parsing and validation
- FIT image generation
- Final capsule creation with provided GUID
- Auto-installation of missing tools

Features
--------

Core Functionality
~~~~~~~~~~~~~~~~~~

- **XML Parsing**: Extracts UPDATE operations from FvUpdate.xml
- **Binary Validation**: Ensures all referenced binary files exist
- **ITS Generation**: Creates Image Tree Source files with proper node naming
- **FIT Compilation**: Uses mkimage to generate FIT images
- **Capsule Creation**: Creates final capsule files using mkeficapsule with provided GUID
- **Tool Management**: Auto-installs missing tools (mkimage, mkeficapsule)

Compatibility
~~~~~~~~~~~~~

- Qualcomm U-Boot capsule update system
- Multi-partition firmware updates
- FIT-based capsule payloads
- EFI Firmware Management Protocol (FMP)
- DFU backend integration

Usage
-----

FIT Image Only
~~~~~~~~~~~~~~

Creates only system.fit::

    ./fvupdate_to_fit.py FvUpdate.xml

Complete Capsule Workflow
~~~~~~~~~~~~~~~~~~~~~~~~~~

Creates system.fit and firmware.capsule::

    ./fvupdate_to_fit.py FvUpdate.xml \
        --mkeficapsule <path_to_mkeficapsule_tool> \
        --guid <guid_of_the_board> \
        --fw-version <version_of_the_capsule_payload_in_0.0.A.B_format>

Additional Examples
~~~~~~~~~~~~~~~~~~~

With custom output names::

    ./fvupdate_to_fit.py FvUpdate.xml \
        --mkeficapsule /path/to/mkeficapsule \
        --guid 12345678-1234-5678-9abc-123456789abc \
        --fw-version 0.0.1.0 \
        --output custom.fit \
        --capsule-output custom.capsule

With verbose output::

    ./fvupdate_to_fit.py FvUpdate.xml \
        --mkeficapsule /path/to/mkeficapsule \
        --guid 12345678-1234-5678-9abc-123456789abc \
        --fw-version 0.0.1.0 \
        --verbose

With Capsule Signing
~~~~~~~~~~~~~~~~~~~~

Creates a signed capsule::

    ./fvupdate_to_fit.py FvUpdate.xml \
        --mkeficapsule /path/to/mkeficapsule \
        --guid 12345678-1234-5678-9abc-123456789abc \
        --fw-version 0.0.1.0 \
        --monotonic-count 1 \
        --private-key keys/CRT.key \
        --certificate keys/CRT.crt

Command Line Options
--------------------

.. list-table::
   :header-rows: 1
   :widths: 20 50 30

   * - Option
     - Description
     - Default
   * - ``xml_file``
     - Path to FvUpdate.xml file
     - Required
   * - ``--guid``
     - GUID value of the board (enables complete mode). Board-specific GUIDs: qcs615: ``9FD379D2-670E-4BB3-86A1-40497E6E17B0``, qcs6490-rb3gen2: ``6f25bfd2-a165-468b-980f-ac51a0a45c52``, lemans-evk: ``78462415-6133-431c-9fae-48f2bafd5c71``
     - Required
   * - ``--mkeficapsule``
     - Path to mkeficapsule binary
     - Required
   * - ``--fw-version``
     - Firmware version in "0.0.A.B" format (e.g., "0.0.1.0")
     - Optional
   * - ``--monotonic-count``
     - Monotonic count for capsule signing
     - Optional
   * - ``--private-key``
     - Path to the private key for signing
     - Optional
   * - ``--certificate``
     - Path to the certificate for signing
     - Optional
   * - ``-o, --output``
     - Output FIT image name
     - system.fit
   * - ``-c, --capsule-output``
     - Output capsule file name
     - firmware.capsule
   * - ``-v, --verbose``
     - Enable verbose output
     - Disabled
   * - ``-h, --help``
     - Show help message
     - \-

Requirements
------------

System Requirements
~~~~~~~~~~~~~~~~~~~

- Python 3.6+
- Linux system with package manager (apt, yum, dnf, or pacman)
- sudo access for tool installation

Required Tools (Auto-installed)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- U-Boot tools (mkimage, mkeficapsule)

Required Files
~~~~~~~~~~~~~~

- FvUpdate.xml file
- Binary files in Images/ directory
- GUID value for the target board (for complete mode)

Installation
------------

1. Make the script executable::

       chmod +x fvupdate_to_fit.py

2. The script will auto-install missing tools on first run

File Structure
--------------

The script expects the following directory structure::

    project/
    ├── FvUpdate.xml
    ├── Images/
    │   ├── xbl.elf
    │   ├── uefi.bin
    │   └── boot.img
    └── fvupdate_to_fit.py

Example FvUpdate.xml
~~~~~~~~~~~~~~~~~~~~

An example FvUpdate.xml file is provided for reference:

.. literalinclude:: fvupdate_to_fit/example_FvUpdate.xml
   :language: xml

This example demonstrates the XML structure with UPDATE operations for
multiple firmware partitions.

XML Structure Requirements
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The tool only processes the following XML elements:

**Required Elements:**

- ``<FwEntry>`` - Container for each firmware entry
- ``<InputBinary>`` - Binary filename (must exist in Images/ directory)
- ``<Operation>`` - Must be "UPDATE" to be processed
- ``<Dest>/<PartitionName>`` - Target partition name (case-sensitive)

**XML Structure:**

.. code-block:: xml

   <?xml version="1.0" encoding="utf-8"?>
   <FVItems>
     <FwEntry>
       <InputBinary>firmware.bin</InputBinary>
       <Operation>UPDATE</Operation>
       <Dest>
         <PartitionName>partition_name</PartitionName>
       </Dest>
     </FwEntry>
   </FVItems>

**Notes:**

- The tool hardcodes the Images/ directory for binary files
- Only entries with ``<Operation>UPDATE</Operation>`` are processed
- Partition names are case-sensitive and must match U-Boot partition discovery

Output Files
------------

Complete Mode
~~~~~~~~~~~~~

- ``system.its`` - Image Tree Source file (kept)
- ``system.fit`` - FIT image (kept)
- ``firmware.capsule`` - Final capsule file (ready for deployment)

FIT-only Mode
~~~~~~~~~~~~~

- ``system.its`` - Image Tree Source file
- ``system.fit`` - FIT image

Deployment Workflow
-------------------

After generating the capsule:

1. **Copy capsule to boot partition**::

       cp firmware.capsule /boot/

2. **Deploy in U-Boot**::

       => fatload mmc 0:1 $loadaddr firmware.capsule
       => efidebug capsule update $loadaddr

Technical Details
-----------------

FIT Image Structure
~~~~~~~~~~~~~~~~~~~

- **Node Names**: Match partition names (e.g., ``xbl_a``, ``uefi_a``)
- **Image Type**: All images marked as "firmware"
- **Hash Algorithm**: SHA256 for integrity verification
- **Configuration**: Single config referencing all firmware images

Capsule Structure
~~~~~~~~~~~~~~~~~

- **GUID**: Provided via --guid option
- **Index**: Always 1 (required for FIT capsules)
- **Payload**: FIT image containing all firmware
- **Version**: Optional firmware version (in 0.0.A.B format) via --fw-version, encoded as (A << 16 | B)
- **Signing**: Optional signing with monotonic count, private key, and certificate

FMP Driver Compatibility
~~~~~~~~~~~~~~~~~~~~~~~~~

The provided GUID must match what the FMP (Firmware Management Protocol) driver expects:

- Uses actual partition names from FvUpdate.xml
- Compatible with Qualcomm capsule update flow
- Works with ``fit_update()`` processing

Error Handling
--------------

Comprehensive error handling with fail-fast approach:

- Missing XML files or invalid format
- Missing binary files in Images/ directory
- Tool installation failures
- Capsule creation failures
- Invalid GUID format

Example Output
--------------

Complete Mode
~~~~~~~~~~~~~

::

    Converting FvUpdate.xml to capsule...
    ============================================================
    Installing missing tools...
      Installed u-boot-tools ✓
    Parsing XML file: FvUpdate.xml
    Found 3 FwEntry elements
      Added: xbl_a -> xbl.elf
      Added: uefi_a -> uefi.bin
      Added: boot_a -> boot.img
    Successfully parsed 3 UPDATE entries
    Validating binary files...
      xbl.elf: 524288 bytes
      uefi.bin: 1048576 bytes
      boot.img: 2097152 bytes
    All binary files validated successfully
    Generating ITS file: system.its
    ITS file generated successfully
    Compiling FIT image: system.fit
    Running: mkimage -f system.its system.fit
    FIT image compiled successfully: 3670016 bytes
    Using GUID: 12345678-1234-5678-9abc-123456789abc
    Creating capsule: firmware.capsule
      Encoded Firmware version: 65536 (from 0.0.1.0)
      Command: mkeficapsule -g 12345678-1234-5678-9abc-123456789abc -i 1 -v 65536 system.fit firmware.capsule
    Capsule created successfully: 3670144 bytes ✓
    ============================================================
    SUCCESS: Complete capsule workflow completed!

    Files created:
      ITS file: system.its
      FIT file: system.fit
      Capsule file: firmware.capsule (3.5 MB)

    Capsule GUID: 12345678-1234-5678-9abc-123456789abc

    Ready for deployment:
      1. Copy firmware.capsule to boot partition
      2. In U-Boot: fatload mmc 0:1 $loadaddr firmware.capsule
      3. In U-Boot: efidebug capsule update $loadaddr

FIT-only Mode
~~~~~~~~~~~~~

::

    Converting FvUpdate.xml to FIT image...
    ============================================================
    Parsing XML file: FvUpdate.xml
    Found 3 FwEntry elements
      Added: xbl_a -> xbl.elf
      Added: uefi_a -> uefi.bin
      Added: boot_a -> boot.img
    Successfully parsed 3 UPDATE entries
    Validating binary files...
      xbl.elf: 524288 bytes
      uefi.bin: 1048576 bytes
      boot.img: 2097152 bytes
    All binary files validated successfully
    Generating ITS file: system.its
    ITS file generated successfully
    Compiling FIT image: system.fit
    FIT image compiled successfully: 3670016 bytes
    ============================================================
    CONVERSION SUMMARY:
      Input XML: FvUpdate.xml
      Generated ITS: system.its
      Output FIT: system.fit
      Partitions: 3
        - xbl_a (xbl.elf)
        - uefi_a (uefi.bin)
        - boot_a (boot.img)

    ============================================================
    SUCCESS: FIT image created successfully!
    Output: system.fit

    To create capsule, run again with:
       ./fvupdate_to_fit.py FvUpdate.xml --mkeficapsule <path_to_tool> --guid <board_guid> --fw-version <version>

Viewing Generated File Contents
--------------------------------

FIT Image Contents
~~~~~~~~~~~~~~~~~~

You can inspect the generated ``system.fit`` file using U-Boot tools::

    # View FIT image structure and metadata
    mkimage -l system.fit

    # List all images in the FIT
    dumpimage -l system.fit

Capsule Contents
~~~~~~~~~~~~~~~~

You can inspect the generated ``firmware.capsule`` file using mkeficapsule::

    # View capsule header and metadata
    mkeficapsule --dump-capsule firmware.capsule

    # Or using local mkeficapsule binary
    /path/to/local/mkeficapsule --dump-capsule firmware.capsule

Troubleshooting
---------------

mkimage not found
~~~~~~~~~~~~~~~~~

Install U-Boot tools::

    # Ubuntu/Debian
    sudo apt-get install u-boot-tools

    # CentOS/RHEL
    sudo yum install uboot-tools

mkeficapsule not found
~~~~~~~~~~~~~~~~~~~~~~

If you get an error about mkeficapsule not being found, use a locally compiled version::

    # Use local mkeficapsule binary
    ./fvupdate_to_fit.py FvUpdate.xml \
        --mkeficapsule /path/to/local/u-boot/tools/mkeficapsule \
        --guid 12345678-1234-5678-9abc-123456789abc \
        --fw-version 0.0.1.0

    # Example with U-Boot build directory
    ./fvupdate_to_fit.py FvUpdate.xml \
        --mkeficapsule /local/mnt/workspace/bselvana/k2c_le/u-boot_upstream/u-boot_v2025_upstream/tools/mkeficapsule \
        --guid 12345678-1234-5678-9abc-123456789abc \
        --fw-version 0.0.1.0

Invalid GUID format
~~~~~~~~~~~~~~~~~~~

Ensure the GUID follows the format: ``xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx``

Supported Boards and GUIDs
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following Qualcomm boards are supported with their respective GUIDs:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Board
     - GUID
   * - **qcs615**
     - ``9FD379D2-670E-4BB3-86A1-40497E6E17B0``
   * - **qcs6490-rb3gen2**
     - ``6f25bfd2-a165-468b-980f-ac51a0a45c52``
   * - **lemans-evk**
     - ``78462415-6133-431c-9fae-48f2bafd5c71``


Example valid GUID format: ``12345678-1234-5678-9abc-123456789abc``

Missing binary files
~~~~~~~~~~~~~~~~~~~~

Ensure all files referenced in FvUpdate.xml exist in the Images/ directory with correct names.

XML parsing errors
~~~~~~~~~~~~~~~~~~

Verify FvUpdate.xml is well-formed XML with proper FwEntry structure.

mkeficapsule binary not found
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you specify a custom mkeficapsule path, ensure:

- The file exists and is executable
- The path is correct (absolute or relative to current directory)
- The binary supports capsule creation
