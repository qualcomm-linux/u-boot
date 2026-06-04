# SPDX-License-Identifier: GPL-2.0
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# Entry-type module for U-Boot MBN image
#

from binman.etype.blob_named_by_arg import Entry_blob_named_by_arg

class Entry_qcom_appsbl(Entry_blob_named_by_arg):
    """U-Boot mbn image

    Properties / Entry arguments:
        - filename: Filename of u-boot MBN (default 'u-boot.mbn')

    This is the U-Boot MBN image.
    """
    def __init__(self, section, etype, node):
        super().__init__(section, etype, node, 'qcom-appsbl')
