.. SPDX-License-Identifier: GPL-2.0-only

EFI Secure Boot
================

About this
-----------
This document describes how to enable and use UEFI Secure Boot in U-Boot,
and how to generate and enroll the keys required to authenticate EFI images
(such as GRUB, the Linux EFI stub kernel, or a Unified Kernel Image) before
they are executed.

This guide uses the variable preseeding method, where the PK/KEK/db keys
are generated on a build host and embedded into the U-Boot image at build
time via ``CONFIG_EFI_VARIABLES_PRESEED``, so that they are already
enrolled the first time the board boots. This is different from
interactively enrolling keys at runtime.

Secure Boot trust model
------------------------
UEFI Secure Boot relies on a hierarchy of keys and signature databases:

- **PK (Platform Key)** - the root authority. The PK owner controls updates
  to the KEK database.
- **KEK (Key Exchange Key)** - authorizes updates to the ``db``/``dbx``
  signature databases.
- **db (Signature Database)** - contains the certificates/hashes that are
  allowed to sign EFI images which U-Boot will execute.
- **dbx (Forbidden Signature Database)** - contains revoked certificates or
  hashes.

At boot, U-Boot validates the signature of an EFI image against ``db`` and
rejects images that are untrusted or that have been revoked via ``dbx``.

Preseeded variable store
^^^^^^^^^^^^^^^^^^^^^^^^^
The authenticated variables (``PK``, ``KEK``, ``db``, ``dbx``) are normally
enrolled at runtime through the platform firmware. For an embedded boot
flow, U-Boot instead supports *preseeding* these variables at build time.

A file named ``ubootefi.var`` is generated on the build host and embedded
into the U-Boot image when ``CONFIG_EFI_VARIABLES_PRESEED`` is enabled, so
that the PK/KEK/db are already enrolled the first time the board boots.

Required build configuration
-----------------------------
To enable EFI Secure Boot support, the following options must be enabled in
the U-Boot configuration. A ready-made config fragment is provided at
``configs/efi_secureboot.config``::

    CONFIG_EFI_VARIABLES_PRESEED=y
    CONFIG_EFI_SECURE_BOOT=y
    CONFIG_FIT_SIGNATURE=y
    CONFIG_SHA512=y
    CONFIG_SHA384=y

To enable Secure Boot for a given board, configure U-Boot for that board
and then merge the ``configs/efi_secureboot.config`` fragment into the
resulting ``.config`` using ``scripts/kconfig/merge_config.sh``::

    make CROSS_COMPILE=<toolchain-prefix> <your_board>_defconfig
    scripts/kconfig/merge_config.sh .config configs/efi_secureboot.config

Generating keys and enrolling them
-----------------------------------
The following steps are performed once, on a controlled Linux signing host,
to generate the PK/KEK/db key pairs and certificates, and to produce the
preseeded variable store that gets embedded into U-Boot. The examples use
RSA-2048 and SHA-256.

Step 1: Generate PK, KEK, and db credentials
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Generate a random GUID to identify the owner, then generate the three
self-signed certificates::

    uuidgen --random > GUID.txt

    # PK | Platform Key
    openssl req -new -x509 -newkey rsa:2048 -subj "/CN=Custom PK/" \
      -keyout PK.key -out PK.crt -days 3650 -nodes -sha256

    # KEK | Key Exchange Key
    openssl req -new -x509 -newkey rsa:2048 -subj "/CN=Custom KEK/" \
      -keyout KEK.key -out KEK.crt -days 3650 -nodes -sha256

    # db | Image signing key
    openssl req -new -x509 -newkey rsa:2048 -subj "/CN=Custom DB Signing Key 1/" \
      -keyout db.key -out db.crt -days 3650 -nodes -sha256

This produces the following outputs: ``PK.key``/``PK.crt``,
``KEK.key``/``KEK.crt`` and ``db.key``/``db.crt``.

Step 2: Create EFI Signature Lists
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Using the ``efitools`` package, convert each certificate into an EFI
Signature List (``.esl``)::

    cert-to-efi-sig-list -g "$(< GUID.txt)" PK.crt  PK.esl
    cert-to-efi-sig-list -g "$(< GUID.txt)" KEK.crt KEK.esl
    cert-to-efi-sig-list -g "$(< GUID.txt)" db.crt  db.esl

Step 3: Preseed ubootefi.var
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
From the U-Boot source tree, use ``tools/efivar.py`` (matching the branch
being built) to populate the preseeded variable store with the ``.esl``
files generated above.

The global (platform) GUID ``8be4df61-93ca-11d2-aa0d-00e098032b8c`` is used
for ``PK`` and ``KEK``, while the image security GUID
``d719b2cb-3d3a-4596-a3bc-dad00e67656f`` is used for ``db`` and ``dbx``::

    tools/efivar.py set --infile ubootefi.var --name PK \
      --attrs nv,bs,rt,at --guid 8be4df61-93ca-11d2-aa0d-00e098032b8c \
      --type file --data PK.esl

    tools/efivar.py set --infile ubootefi.var --name KEK \
      --attrs nv,bs,rt,at --guid 8be4df61-93ca-11d2-aa0d-00e098032b8c \
      --type file --data KEK.esl

    tools/efivar.py set --infile ubootefi.var --name db \
      --attrs nv,bs,rt,at --guid d719b2cb-3d3a-4596-a3bc-dad00e67656f \
      --type file --data db.esl

The resulting ``ubootefi.var`` file is picked up by the build when
``CONFIG_EFI_VARIABLES_PRESEED`` is enabled, and gets embedded into the
U-Boot image so the keys are enrolled from first boot.

Step 4: Sign the EFI images that will be booted
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Every EFI PE/COFF executable that U-Boot will load directly (bootloader
stub, GRUB, EFI stub kernel or UKI, etc.) must be signed with the ``db`` key
so that it validates against the enrolled signature database. Using
``sbsign`` (from ``sbsigntool``), mount the EFI System Partition image,
sign the executables, and replace the originals::

    mkdir -p efimountedbin
    sudo mount -o loop efi.bin efimountedbin

    cp efimountedbin/EFI/BOOT/bootaa64.efi ./bootaa64.efi
    # Copy the EFI-stub kernel or UKI only if it is directly loaded as an EFI executable
    cp efimountedbin/EFI/Linux/<linux-image>.efi ./<linux-image>.efi

    sbsign --key db.key --cert db.crt --output bootaa64.efi.signed bootaa64.efi
    sbsign --key db.key --cert db.crt --output <linux-image>.efi.signed <linux-image>.efi

    sudo cp bootaa64.efi.signed efimountedbin/EFI/BOOT/bootaa64.efi
    sudo cp <linux-image>.efi.signed efimountedbin/EFI/Linux/<linux-image>.efi

    sync && sudo umount efimountedbin

.. note::
   ``<linux-image>.efi`` refers to the EFI-stub kernel image name used on
   the target. Replace it with the actual kernel/UKI file name being
   signed.

Step 5: Build U-Boot with Secure Boot enabled
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
With the configuration above merged into ``.config``, build U-Boot as
usual::

    make CROSS_COMPILE=<toolchain-prefix> -j$(nproc)

Using Secure Boot
------------------
Once U-Boot boots with the preseeded PK/KEK/db enrolled:

- Only EFI images signed with a key present in ``db`` (and not present in
  ``dbx``) will be allowed to execute via the ``bootefi``/``bootmgr``
  commands.
- Unsigned images, or images signed by a key that is not enrolled, will be
  rejected, and U-Boot will print ``Image not authenticated`` on the
  console.
- ``dbx`` can be updated (via a properly signed authenticated variable
  update, following the same process as Steps 1-3 above) to revoke a
  previously trusted signing key, for example if a signing key is
  compromised.
