# SPDX-License-Identifier: GPL-2.0
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# Entry-type module for QC Config ELF
#

from binman.entry import Entry
from binman.etype.blob_named_by_arg import Entry_blob_named_by_arg

class Entry_qcom_config(Entry_blob_named_by_arg):
    """QC Config ELF

    Properties / Entry arguments:
        - qcom-config-path: Filename of QC Config ELF (typically 'qcconfig.elf')

    This will be part of the Qualcomm SPL based bootloader image

    """
    def __init__(self, section, etype, node):
        super().__init__(section, etype, node, 'qcom-config')
        self.external = True
