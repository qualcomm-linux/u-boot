# SPDX-License-Identifier: MIT
#
# Copyright (c) Qualcomm Technologies, Inc.

import os
import sys
import getopt
import struct
import re
from pathlib import Path

# ----------------------------------------------------------------------------
#  A pre-calculated Table-Driven CRC table that uses following parameters,
#  same with the SWIV runtime library CRC table:
#  CRC result width         32 bits
#  Initial value            0xFFFFFFFF
#  Input data reflected     True
#  Result data reflected    True
#  XOR value                0xFFFFFFFF
#  Polynomial               0x04C11DB7
#  Check                    0xCBF43926
# ----------------------------------------------------------------------------
crc32_table = (
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
    0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
    0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
    0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
    0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
    0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
    0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
    0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
    0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
    0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
    0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,
    0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
    0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,
    0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
    0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,
    0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,
    0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
    0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
    0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
    0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,
    0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
    0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
    0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,
    0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
    0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
)

# Defintions
SIZE_4KB = 0x1000

# ELF Definitions
ELF_HDR_COMMON_SIZE = 24
ELF32_HDR_SIZE = 52
ELF32_PHDR_SIZE = 32
ELF32_SHDR_SIZE = 40
ELF64_HDR_SIZE = 64
ELF64_PHDR_SIZE = 56
ELF64_SHDR_SIZE = 64
ELFINFO_MAG0_INDEX = 0
ELFINFO_MAG1_INDEX = 1
ELFINFO_MAG2_INDEX = 2
ELFINFO_MAG3_INDEX = 3
ELFINFO_MAG0 = '\x7f'
ELFINFO_MAG1 = 'E'
ELFINFO_MAG2 = 'L'
ELFINFO_MAG3 = 'F'
ELFINFO_CLASS_INDEX = 4
ELFINFO_CLASS_32 = '\x01'
ELFINFO_CLASS_64 = '\x02'
ELFINFO_VERSION_INDEX = 6
ELFINFO_VERSION_CURRENT = '\x01'
ELFINFO_DATA2LSB = '\x01'
ELFINFO_EXEC_ETYPE = '\x02\x00'
ELFINFO_ARM_MACHINETYPE = '\x28\x00'
ELFINFO_VERSION_EV_CURRENT = '\x01\x00\x00\x00'
ELFINFO_SHOFF = 0x00
ELFINFO_PHNUM = '\x01\x00'
ELFINFO_RESERVED = 0x00
MAX_PHDR_COUNT = 100             # Maximum allowable program headers
SL_ET_EXEC = 2  # executable image
SL_ET_DYN = 3   # shared object image

# ELF Program Header Types
NULL_TYPE = 0x0
LOAD_TYPE = 0x1
DYNAMIC_TYPE = 0x2
INTERP_TYPE = 0x3
NOTE_TYPE = 0x4
SHLIB_TYPE = 0x5
PHDR_TYPE = 0x6
TLS_TYPE = 0x7

#ELF Section Header Types
SHT_NULL = 0
SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_RELA = 4
SHT_HASH = 5
SHT_DYNAMIC = 6
SHT_NOTE = 7
SHT_NOBITS = 8
SHT_REL = 9
SHT_SHLIB = 10
SHT_DYNSYM = 11

DT_PLTGOT = 0x3
DT_HASH = 0x4
DT_STRTAB = 0x5
DT_SYMTAB = 0x6
DT_RELA = 0x7
DT_INIT = 0xc
DT_FINI = 0xd
DT_DEBUG = 0x15

# Access Type
PF_X = 0x1
PF_W = 0x2
PF_R = 0x4

# SWIV Segment Magic Number
SWIV_MAGIC0 = b'S'
SWIV_MAGIC1 = b'W'
SWIV_MAGIC2 = b'I'
SWIV_MAGIC3 = b'V'

SWIV_SEG_SIZE_MIN = 16

# ----------------------------------------------------------------------------
# According to ELF standard manual
# section types between SHT_LOUSER(0x80000000) and SHT_HIUSER(0xFFFFFFFF) could be used by the application
# hence 0xD3574956 is picked as the value for SWIV section type. It's the result of 0x80000000 + 0x53574956 (ASCII value of "SWIV")
# ----------------------------------------------------------------------------
SWIV_SECTION_TYPE = 0xD3574956

global binary_output
binary_output = False
global padding_to
padding_to = SIZE_4KB

# ----------------------------------------------------------------------------
# ELF Common Header Class
# ----------------------------------------------------------------------------


class Elf_Ehdr_common:
    # Structure object to align and package the ELF Header
    s = struct.Struct('16sHHI')

    def __init__(self, data):
        unpacked_data = (Elf_Ehdr_common.s).unpack(data)
        self.unpacked_data = unpacked_data
        self.e_ident = unpacked_data[0]
        self.e_type = unpacked_data[1]
        self.e_machine = unpacked_data[2]
        self.e_version = unpacked_data[3]

    def printValues(self):
        print("ATTRIBUTE / VALUE")
        for attr, value in self.__dict__.iteritems():
            print(attr, value)

# ----------------------------------------------------------------------------
# ELF32 Header Class
# ----------------------------------------------------------------------------


class Elf32_Ehdr:
    # Structure object to align and package the ELF Header
    s = struct.Struct('16sHHIIIIIHHHHHH')

    def __init__(self, data):
        unpacked_data = (Elf32_Ehdr.s).unpack(data)
        self.unpacked_data = unpacked_data
        self.e_ident = unpacked_data[0]
        self.e_type = unpacked_data[1]
        self.e_machine = unpacked_data[2]
        self.e_version = unpacked_data[3]
        self.e_entry = unpacked_data[4]
        self.e_phoff = unpacked_data[5]
        self.e_shoff = unpacked_data[6]
        self.e_flags = unpacked_data[7]
        self.e_ehsize = unpacked_data[8]
        self.e_phentsize = unpacked_data[9]
        self.e_phnum = unpacked_data[10]
        self.e_shentsize = unpacked_data[11]
        self.e_shnum = unpacked_data[12]
        self.e_shstrndx = unpacked_data[13]

    def printValues(self):
        print("ATTRIBUTE / VALUE")
        for attr, value in self.__dict__.iteritems():
            print(attr, value)

    def getPackedData(self):
        values = [self.e_ident,
                  self.e_type,
                  self.e_machine,
                  self.e_version,
                  self.e_entry,
                  self.e_phoff,
                  self.e_shoff,
                  self.e_flags,
                  self.e_ehsize,
                  self.e_phentsize,
                  self.e_phnum,
                  self.e_shentsize,
                  self.e_shnum,
                  self.e_shstrndx
                  ]

        return (Elf32_Ehdr.s).pack(*values)

# ----------------------------------------------------------------------------
# ELF32 Program Header Class
# ----------------------------------------------------------------------------


class Elf32_Phdr:

    # Structure object to align and package the ELF Program Header
    s = struct.Struct('I' * 8)

    def __init__(self, data):
        unpacked_data = (Elf32_Phdr.s).unpack(data)
        self.unpacked_data = unpacked_data
        self.p_type = unpacked_data[0]
        self.p_offset = unpacked_data[1]
        self.p_vaddr = unpacked_data[2]
        self.p_paddr = unpacked_data[3]
        self.p_filesz = unpacked_data[4]
        self.p_memsz = unpacked_data[5]
        self.p_flags = unpacked_data[6]
        self.p_align = unpacked_data[7]

    def printValues(self):
        print("ATTRIBUTE / VALUE")
        for attr, value in self.__dict__.iteritems():
            print(attr, value)

    def getPackedData(self):
        values = [self.p_type,
                  self.p_offset,
                  self.p_vaddr,
                  self.p_paddr,
                  self.p_filesz,
                  self.p_memsz,
                  self.p_flags,
                  self.p_align
                  ]

        return (Elf32_Phdr.s).pack(*values)

# ----------------------------------------------------------------------------
# ELF32 Section Header Class
# ----------------------------------------------------------------------------


class Elf32_Shdr:

    # Structure object to align and package the ELF Program Header
    s = struct.Struct('I' * 10)

    def __init__(self, data):
        unpacked_data = (Elf32_Shdr.s).unpack(data)
        self.unpacked_data = unpacked_data
        self.sh_name = unpacked_data[0]
        self.sh_type = unpacked_data[1]
        self.sh_flags = unpacked_data[2]
        self.sh_addr = unpacked_data[3]
        self.sh_offset = unpacked_data[4]
        self.sh_size = unpacked_data[5]
        self.sh_link = unpacked_data[6]
        self.sh_info = unpacked_data[7]
        self.sh_addralign = unpacked_data[8]
        self.sh_entsize = unpacked_data[9]
        self.sh_name_str = ""

    def getPackedData(self):
        values = [self.sh_name,
                  self.sh_type,
                  self.sh_flags,
                  self.sh_addr,
                  self.sh_offset,
                  self.sh_size,
                  self.sh_link,
                  self.sh_info,
                  self.sh_addralign,
                  self.sh_entsize]

        return (Elf32_Shdr.s).pack(*values)

# ----------------------------------------------------------------------------
# ELF64 Header Class
# ----------------------------------------------------------------------------


class Elf64_Ehdr:
    # Structure object to align and package the ELF Header
    s = struct.Struct('16sHHIQQQIHHHHHH')

    def __init__(self, data):
        unpacked_data = (Elf64_Ehdr.s).unpack(data)
        self.unpacked_data = unpacked_data
        self.e_ident = unpacked_data[0]
        self.e_type = unpacked_data[1]
        self.e_machine = unpacked_data[2]
        self.e_version = unpacked_data[3]
        self.e_entry = unpacked_data[4]
        self.e_phoff = unpacked_data[5]
        self.e_shoff = unpacked_data[6]
        self.e_flags = unpacked_data[7]
        self.e_ehsize = unpacked_data[8]
        self.e_phentsize = unpacked_data[9]
        self.e_phnum = unpacked_data[10]
        self.e_shentsize = unpacked_data[11]
        self.e_shnum = unpacked_data[12]
        self.e_shstrndx = unpacked_data[13]

    def printValues(self):
        print("ATTRIBUTE / VALUE")
        for attr, value in self.__dict__.iteritems():
            print(attr, value)

    def getPackedData(self):
        values = [self.e_ident,
                  self.e_type,
                  self.e_machine,
                  self.e_version,
                  self.e_entry,
                  self.e_phoff,
                  self.e_shoff,
                  self.e_flags,
                  self.e_ehsize,
                  self.e_phentsize,
                  self.e_phnum,
                  self.e_shentsize,
                  self.e_shnum,
                  self.e_shstrndx
                  ]

        return (Elf64_Ehdr.s).pack(*values)

# ----------------------------------------------------------------------------
# ELF64 Program Header Class
# ----------------------------------------------------------------------------


class Elf64_Phdr:

    # Structure object to align and package the ELF Program Header
    s = struct.Struct('IIQQQQQQ')

    def __init__(self, data):
        unpacked_data = (Elf64_Phdr.s).unpack(data)
        self.unpacked_data = unpacked_data
        self.p_type = unpacked_data[0]
        self.p_flags = unpacked_data[1]
        self.p_offset = unpacked_data[2]
        self.p_vaddr = unpacked_data[3]
        self.p_paddr = unpacked_data[4]
        self.p_filesz = unpacked_data[5]
        self.p_memsz = unpacked_data[6]
        self.p_align = unpacked_data[7]

    def printValues(self):
        print("ATTRIBUTE / VALUE")
        for attr, value in self.__dict__.iteritems():
            print(attr, value)

    def getPackedData(self):
        values = [self.p_type,
                  self.p_flags,
                  self.p_offset,
                  self.p_vaddr,
                  self.p_paddr,
                  self.p_filesz,
                  self.p_memsz,
                  self.p_align
                  ]

        return (Elf64_Phdr.s).pack(*values)

# ----------------------------------------------------------------------------
# ELF64 Section Header Class
# ----------------------------------------------------------------------------

class Elf64_Shdr:

    # Structure object to align and package the ELF Program Header
    s = struct.Struct('IIQQQQIIQQ')

    def __init__(self, data):
        unpacked_data = (Elf64_Shdr.s).unpack(data)
        self.unpacked_data = unpacked_data
        self.sh_name = unpacked_data[0]
        self.sh_type = unpacked_data[1]
        self.sh_flags = unpacked_data[2]
        self.sh_addr = unpacked_data[3]
        self.sh_offset = unpacked_data[4]
        self.sh_size = unpacked_data[5]
        self.sh_link = unpacked_data[6]
        self.sh_info = unpacked_data[7]
        self.sh_addralign = unpacked_data[8]
        self.sh_entsize = unpacked_data[9]
        self.sh_name_str = ""

    def printValues(self):
        print("ATTRIBUTE / VALUE")
        for attr, value in self.__dict__.iteritems():
            print(attr, value)

    def getPackedData(self):
        values = [self.sh_name,
                  self.sh_type,
                  self.sh_flags,
                  self.sh_addr,
                  self.sh_offset,
                  self.sh_size,
                  self.sh_link,
                  self.sh_info,
                  self.sh_addralign,
                  self.sh_entsize]

        return (Elf64_Shdr.s).pack(*values)

# ----------------------------------------------------------------------------
# SWIV Program Header Class
# ----------------------------------------------------------------------------


class SWIV_Seg:

    s = struct.Struct('ccccIQ')

    def __init__(self):
        self.magic0 = SWIV_MAGIC0
        self.magic1 = SWIV_MAGIC1
        self.magic2 = SWIV_MAGIC2
        self.magic3 = SWIV_MAGIC3
        self.crc = 0
        self.padding = 0

    def getPackedData(self):
        values = [self.magic0,
                  self.magic1,
                  self.magic2,
                  self.magic3,
                  self.crc,
                  self.padding
                  ]

        return (SWIV_Seg.s).pack(*values)

# ----------------------------------------------------------------------------
# Ensure a int type value is returned
# Added for Python3 compatible
# ----------------------------------------------------------------------------


def ensure_int(item):
    if isinstance(item, int):
        return item
    assert isinstance(item, str)
    return ord(item)

# ----------------------------------------------------------------------------
# Ensure a byte type value is returned
# Added for Python3 compatible
# ----------------------------------------------------------------------------


def ensure_byte(item):
    if isinstance(item, int):
        return chr(item)
    assert isinstance(item, bytes)
    return item


# ----------------------------------------------------------------------------
# Verify ELF header contents from an input ELF file
# ----------------------------------------------------------------------------


def verify_elf_header(elf_header):
    if  (ensure_byte(elf_header.e_ident[ELFINFO_MAG0_INDEX]) != ELFINFO_MAG0) or \
        (ensure_byte(elf_header.e_ident[ELFINFO_MAG1_INDEX]) != ELFINFO_MAG1) or \
        (ensure_byte(elf_header.e_ident[ELFINFO_MAG2_INDEX]) != ELFINFO_MAG2) or \
        (ensure_byte(elf_header.e_ident[ELFINFO_MAG3_INDEX]) != ELFINFO_MAG3) or \
        ((ensure_byte(elf_header.e_ident[ELFINFO_CLASS_INDEX]) != ELFINFO_CLASS_64) and
         (ensure_byte(elf_header.e_ident[ELFINFO_CLASS_INDEX]) != ELFINFO_CLASS_32)) or \
        (ensure_byte(elf_header.e_ident[ELFINFO_VERSION_INDEX]) != ELFINFO_VERSION_CURRENT):
        return False
    else:
        return True

# ----------------------------------------------------------------------------
# Perform file copy given offsets and the number of bytes to copy
# ----------------------------------------------------------------------------


def file_copy_offset(in_fp, in_off, out_fp, out_off, num_bytes):
    in_fp.seek(in_off)
    read_in = in_fp.read(num_bytes)
    out_fp.seek(out_off)
    out_fp.write(read_in)

    return num_bytes

# ----------------------------------------------------------------------------
# Preprocess an ELF file and return the ELF Header Object and an
# array of ELF Program Header Objects
# ----------------------------------------------------------------------------


def preprocess_elf_file(elf_file_name):

    # Initialize
    elf_fp = open(elf_file_name, 'rb')
    elf_header = Elf_Ehdr_common(elf_fp.read(ELF_HDR_COMMON_SIZE))

    # Verify ELF header information
    if verify_elf_header(elf_header) is False:
        raise RuntimeError("Input ELF image verification failed : " + elf_file_name)

    elf_fp.seek(0)

    if ensure_byte(elf_header.e_ident[ELFINFO_CLASS_INDEX]) == ELFINFO_CLASS_64:
        elf_header = Elf64_Ehdr(elf_fp.read(ELF64_HDR_SIZE))
    else:
        elf_header = Elf32_Ehdr(elf_fp.read(ELF32_HDR_SIZE))

    phdr_table = []

    # Get program header size
    phdr_size = elf_header.e_phentsize

    # Find the program header offset
    file_offset = elf_header.e_phoff
    elf_fp.seek(file_offset)

    # Read in the program headers
    for i in range(elf_header.e_phnum):
        if ensure_byte(elf_header.e_ident[ELFINFO_CLASS_INDEX]) == ELFINFO_CLASS_64:
            phdr_table.append(Elf64_Phdr(elf_fp.read(phdr_size)))
        else:
            phdr_table.append(Elf32_Phdr(elf_fp.read(phdr_size)))

    elf_fp.close()
    return [elf_header, phdr_table]

# ----------------------------------------------------------------------------
# Process and format the section header
# ----------------------------------------------------------------------------


def process_section_header(elf_file_name):

    # Initialize
    elf_fp = open(elf_file_name, 'rb')
    elf_header = Elf_Ehdr_common(elf_fp.read(ELF_HDR_COMMON_SIZE))

    # Verify ELF header information
    if verify_elf_header(elf_header) is False:
        raise RuntimeError("Input ELF image verification failed : " + elf_file_name)

    elf_fp.seek(0)

    if ensure_byte(elf_header.e_ident[ELFINFO_CLASS_INDEX]) == ELFINFO_CLASS_64:
        elf_header = Elf64_Ehdr(elf_fp.read(ELF64_HDR_SIZE))
    else:
        elf_header = Elf32_Ehdr(elf_fp.read(ELF32_HDR_SIZE))

    shdr_table = []

    shdr_size = elf_header.e_shentsize
    elf_fp.seek(elf_header.e_shoff)

    for i in range(elf_header.e_shnum):
        if ensure_byte(elf_header.e_ident[ELFINFO_CLASS_INDEX]) == ELFINFO_CLASS_64:
            shdr_table.append(Elf64_Shdr(elf_fp.read(shdr_size)))
        else:
            shdr_table.append(Elf32_Shdr(elf_fp.read(shdr_size)))

    elf_fp.close()
    return shdr_table

# ----------------------------------------------------------------------------
# Perform CRC checksum calculation
# ----------------------------------------------------------------------------


def crc32_calculate(image_fp, phdr_table, num, loadable):
    crc = 0xffffffff

    for i in range(num):
        curr_phdr = phdr_table[i]

        # Calculate CRC only for loadable segments
        if curr_phdr.p_type != LOAD_TYPE:
            continue
        image_fp.seek(curr_phdr.p_offset)
        read_in = image_fp.read(curr_phdr.p_filesz)

        # Fill zero-initialized area if exists
        zero_size = curr_phdr.p_memsz - curr_phdr.p_filesz
        if zero_size > 0:
            read_in += zero_size * b'\x00'

        for ch in read_in:
            crc = crc32_table[(crc ^ ensure_int(ch)) & 0xff] ^ (crc >> 8)

    # If SWIV segment is loadable, it is also part of the payload needed to CRC
    # When calculating, the CRC is filled with zero
    if loadable is True:
        swiv_empty = SWIV_Seg()
        read_in = swiv_empty.getPackedData()
        read_in += (padding_to - SWIV_SEG_SIZE_MIN) * b'\x00'
        for ch in read_in:
            crc = crc32_table[(crc ^ ensure_int(ch)) & 0xff] ^ (crc >> 8)

    crc = crc ^ 0xffffffff
    print('Calculated CRC: %#x' % crc)

    return crc

# ----------------------------------------------------------------------------
# Verify the SWIV segment from the input image
# ----------------------------------------------------------------------------
def verify_swiv_seg(elf_in_fp, offset):

    elf_in_fp.seek(offset)
    swiv_seg = SWIV_Seg()
    unpacked_data = (SWIV_Seg.s).unpack(elf_in_fp.read(SWIV_SEG_SIZE_MIN))
    swiv_seg.magic0 = unpacked_data[0]
    swiv_seg.magic1 = unpacked_data[1]
    swiv_seg.magic2 = unpacked_data[2]
    swiv_seg.magic3 = unpacked_data[3]

    if (swiv_seg.magic0 != SWIV_MAGIC0) or \
       (swiv_seg.magic1 != SWIV_MAGIC1) or \
       (swiv_seg.magic2 != SWIV_MAGIC2) or \
       (swiv_seg.magic3 != SWIV_MAGIC3):

        return False
    else:
        return True

# ----------------------------------------------------------------------------
# Check the two given range overlaps or not
# ----------------------------------------------------------------------------


def is_overlap(start1, end1, start2, end2):
    return (end1 >= start2 and end2 >= start1)


# ----------------------------------------------------------------------------
# Check whether segment shift is needed
# ----------------------------------------------------------------------------


def check_shift(elf_hdr, phdr_table):
    phdr_end_off = elf_hdr.e_phoff + elf_hdr.e_phentsize * elf_hdr.e_phnum
    if phdr_end_off == phdr_table[0].p_filesz and phdr_table[0].p_offset == 0:
        # ELF and program headers are first segment
        if phdr_table[1].p_offset < (phdr_end_off + elf_hdr.e_phentsize):
            return True
        return False
    elif phdr_end_off < phdr_table[0].p_offset + phdr_table[0].p_filesz and phdr_table[0].p_offset == 0:
        # ELF and program headers are included in the first segment
        return True
    elif phdr_end_off <= phdr_table[0].p_offset:
        # ELF and program headers are not included in any segments
        if phdr_end_off + elf_hdr.e_phentsize < phdr_table[0].p_offset:
            return False
        return True
    else:
        return False

# ----------------------------------------------------------------------------
# Add SWIV segment into the target image
# ----------------------------------------------------------------------------

#Check if size of the segment 4K aligned
def is_segment_4K_aligned(size_of_segment):
    if (size_of_segment % 4096 == 0):
      return True
    else:
      return False

def swiv_generate(input_image, loadable, vaddr, paddr, align, output_image):
    # Open files
    elf_in_fp = open(input_image, "rb")
    elf_out_fp = open(output_image, "wb+")

    # Initialize
    [elf_header, phdr_table] = preprocess_elf_file(input_image)

    num_phdrs = elf_header.e_phnum
    num_shdrs = elf_header.e_shnum
    phdr_size = elf_header.e_phentsize

    # Assert limit on number of program headers in input image
    if num_phdrs > MAX_PHDR_COUNT:
        elf_in_fp.close()
        elf_out_fp.close()
        print("ERROR: Input image has exceeded maximum number of program headers.\nAbort processing.")
        sys.exit(2)

    if elf_header.e_shoff == 0:
        has_sections = 0
    else:
        has_sections = 1
        origin_sh_off = elf_header.e_shoff
        shdr_table_input = process_section_header(input_image)
        shdr_table_output = process_section_header(input_image)

    # Check whether the SWIV segment is already in the target image
    last_phdr = phdr_table[num_phdrs - 1]
    if (last_phdr.p_memsz == SWIV_SEG_SIZE_MIN):
        ret = verify_swiv_seg(elf_in_fp, last_phdr.p_offset)
        if ret == True:
            elf_in_fp.close()
            elf_out_fp.close()
            print("ERROR: A SWIV segment is detected in the input image.\nCannot add more SWIV segments. Abort")
            sys.exit(2)
        elf_in_fp.seek(0)
    if has_sections == 1:
        last_shdr = shdr_table_input[elf_header.e_shnum - 1]
        if (last_shdr.sh_size == SWIV_SEG_SIZE_MIN):
            ret = verify_swiv_seg(elf_in_fp, last_shdr.sh_offset)
            if ret == True:
                elf_in_fp.close()
                elf_out_fp.close()
                print("ERROR: A SWIV section is detected in the input image.\nnCannot add more SWIV segments. Abort")
                sys.exit(2)
        elf_in_fp.seek(0)

    # Check whether the given SWIV segment overlaps with existing loadable segments
    if loadable is True:
        for hdr in phdr_table:
            if hdr.p_type == LOAD_TYPE:
                if (is_overlap(paddr, paddr + SWIV_SEG_SIZE_MIN - 1, hdr.p_paddr, hdr.p_paddr + hdr.p_memsz - 1)):
                    elf_in_fp.close()
                    elf_out_fp.close()
                    print("ERROR: The input SWIV segment overlaps with an existing segment.\nAbort")
                    sys.exit(2)

    if ensure_byte(elf_header.e_ident[ELFINFO_CLASS_INDEX]) == ELFINFO_CLASS_64:
        swiv_shdr = Elf64_Shdr(b'\0' * ELF64_SHDR_SIZE)
        swiv_phdr = Elf64_Phdr(b'\0' * ELF64_PHDR_SIZE)
        elf_header_size = ELF64_HDR_SIZE
    else:
        swiv_shdr = Elf32_Shdr(b'\0' * ELF32_SHDR_SIZE)
        swiv_phdr = Elf32_Phdr(b'\0' * ELF32_PHDR_SIZE)
        elf_header_size = ELF32_HDR_SIZE

    # For the following cases, we have to add program header and its segment:
    # * The SWIV context is loadable
    # * There is no section header table
    # Otherwise we add a section header and its section
    if loadable is True or has_sections == 0:
        # If the program header is included in the first program header itself,
        if check_shift(elf_header, phdr_table) is True:
            elf_in_fp.close()
            elf_out_fp.close()
            print("ERROR: segment shift is needed. It is not supported yet. Abort")
            sys.exit(2)

        if loadable is True:
            swiv_phdr.p_type = LOAD_TYPE
        else:
            swiv_phdr.p_type = NULL_TYPE

        # Create a new program header for SWIV segment
        if has_sections == 0:
            swiv_phdr.p_offset = last_phdr.p_offset + last_phdr.p_filesz
        else:
            # TODO: current offset could only cover the section header not moving cases.
            # Need to add shift offset support
            swiv_phdr.p_offset = elf_header.e_shoff + elf_header.e_shnum * elf_header.e_shentsize

        # Make sure alignment
        # Alignement value 0 is valid according to ELF standard, same as alignment 1
        if align > 1:
            off = swiv_phdr.p_offset & (align - 1)
            if int(off) != 0:
                swiv_phdr.p_offset = (swiv_phdr.p_offset | (align - 1)) + 1

        swiv_phdr.p_align = align
        swiv_phdr.p_flags = PF_R
        swiv_phdr.p_vaddr = vaddr
        swiv_phdr.p_paddr = paddr
        elf_header.e_phnum += 1
    else:
        # Create a new section header for swiv section
        swiv_shdr.sh_name = 0
        swiv_shdr.sh_type = SWIV_SECTION_TYPE
        swiv_shdr.sh_flags = 0
        swiv_shdr.sh_addr = vaddr
        swiv_shdr.sh_size = SWIV_SEG_SIZE_MIN
        elf_header.e_shnum += 1
        swiv_shdr.sh_offset = elf_header.e_shoff + elf_header.e_shnum * elf_header.e_shentsize
        shdr_table_output.append(swiv_shdr)

    # If phdr exists, copy segments to output image,
    # in case there is section not included in any segment
    if elf_header.e_phoff != 0:
        # Output original ELF segments
        for i in range(num_phdrs):
            curr_phdr = phdr_table[i]
            src_offset = curr_phdr.p_offset

            # Copy the ELF segment
            file_copy_offset(elf_in_fp, src_offset, elf_out_fp, curr_phdr.p_offset, curr_phdr.p_filesz)

    # Copy the new ELF header to the output image
    elf_out_fp.seek(0)
    elf_out_fp.write(elf_header.getPackedData())

    # Place the program headers to the original offset
    phdr_start = elf_header.e_phoff

    # Copy origin program headers to the destination file
    for i in range(num_phdrs):
        curr_phdr = phdr_table[i]
        elf_out_fp.seek(phdr_start)
        elf_out_fp.write(curr_phdr.getPackedData())

        # Update phdr_start
        phdr_start += phdr_size

    # Output SWIV program header
    if loadable is True or has_sections == 0:
        swiv_phdr.p_filesz = padding_to
        swiv_phdr.p_memsz = padding_to
        elf_out_fp.seek(phdr_start)
        elf_out_fp.write(swiv_phdr.getPackedData())
    if has_sections == 1:
        # Copy all the original sections
        for i in range(num_shdrs):
            curr_shdr = shdr_table_input[i]
            if curr_shdr.sh_type is SHT_NOBITS or curr_shdr.sh_size == 0:
                continue
            src_offset = curr_shdr.sh_offset
            file_copy_offset(elf_in_fp, src_offset, elf_out_fp, shdr_table_input[i].sh_offset, curr_shdr.sh_size)

        # output all section headers
        shdr_start = elf_header.e_shoff
        for i in range(elf_header.e_shnum):
            curr_shdr = shdr_table_output[i]
            elf_out_fp.seek(shdr_start)
            elf_out_fp.write(curr_shdr.getPackedData())
            shdr_start += elf_header.e_shentsize

    elf_out_fp.flush()
    if loadable is True or has_sections == 0:
        # Generate and output SWIV segment
        swiv_seg = SWIV_Seg()
        swiv_seg.crc = crc32_calculate(elf_out_fp, phdr_table, num_phdrs, loadable)
        # Check if filesize of SWIV segment is 4K aligned when tz_with_swiv is enabled for the target
        if (not is_segment_4K_aligned(swiv_phdr.p_filesz)):
            raise Exception("SWIV Segment is not 4K aligned {0}".format(hex(swiv_phdr.p_filesz)))

        elf_out_fp.seek(swiv_phdr.p_offset)
        elf_out_fp.write(swiv_seg.getPackedData())
        elf_out_fp.write((padding_to-0x10) * b'\x00')
    else:
        # Generate and output SWIV section
        swiv_seg = SWIV_Seg()
        swiv_seg.crc = crc32_calculate(elf_out_fp, phdr_table, num_phdrs, loadable)
        elf_out_fp.seek(swiv_shdr.sh_offset)
        elf_out_fp.write(swiv_seg.getPackedData())

    if binary_output is True:
        # Generate separate SWIV binary
        elf_out_bfp = open(output_image + ".bin", "wb+")
        elf_out_bfp.write(swiv_seg.getPackedData())
        elf_out_bfp.close()

    # Close files
    elf_in_fp.close()
    elf_out_fp.close()
    print("The SWIV context is successfully generated.")

    return 0

# ----------------------------------------------------------------------------
# Convert string to hex value
# ----------------------------------------------------------------------------


def str2hex(s):
    odata = 0
    su = s.upper()
    for c in su:
        tmp = ord(c)
        if tmp <= ord('9'):
            odata = odata << 4
            odata += tmp - ord('0')
        elif ord('A') <= tmp <= ord('F'):
            odata = odata << 4
            odata += tmp - ord('A') + 10
    return odata

# ----------------------------------------------------------------------------
# SWIV segment address lookup
# ----------------------------------------------------------------------------


def get_swiv_segment_address(target):
    match target:
        case "lemans":
            DBG_POLICY_SIZE     = 0x00001000
            MON_PIMEM_CODE_SIZE = 0x00033000
            PIMEM_BASE_ADDR     = 0x1C000000
            PIMEM_DEVCFG_SIZE   = 0x0000C000
            XBL_SEC_PIMEM_SIZE  = 0x00002000

            return (DBG_POLICY_SIZE +
                    MON_PIMEM_CODE_SIZE +
                    PIMEM_BASE_ADDR +
                    PIMEM_DEVCFG_SIZE +
                    XBL_SEC_PIMEM_SIZE)
        case _:
            print("Unknown target. Aborting!")
            sys.exit(2)

# ----------------------------------------------------------------------------
# Command line parsing
# ----------------------------------------------------------------------------


def _parse_cmdline_args(args):
    """ Parse command line args """
    if len(args) != 4:
        raise Exception("Error: Wrong number of arguments.\
                       \nSyntax: python swiv_build_utility.py <target_file> <source_file> <chipset>")
    target_file = args[1]
    source_file = args[2]
    chipset = args[3]
    return target_file, source_file, chipset

# ----------------------------------------------------------------------------
# Main function
# ----------------------------------------------------------------------------


if __name__ == "__main__":
    """
    Usage: python swiv_build_utility.py <target_file> <source_file> <chipset>
    """

    target_file, source_file, chipset = _parse_cmdline_args(sys.argv)

    env = {
        'TARGET_FILE': target_file,
        'SOURCE_FILE': source_file,
        'CHIPSET': chipset,
    }

    # Get the address to load the SWIV segment
    addr = get_swiv_segment_address(env['CHIPSET'])

    # Add the SWIV segment to target elf file
    print("INFO: Generating elf with SWIV segment @ %#x" % addr)
    swiv_generate(source_file, True, addr, addr, SIZE_4KB, target_file)
