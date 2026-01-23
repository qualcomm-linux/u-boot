/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RAM partition table definitions
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 */

#define SMEM_USABLE_RAM_PARTITION_TABLE		402

#define RAM_PARTITION_H_MAJOR  03
#define RAM_PARTITION_H_MINOR  00

typedef u8 uint8;
typedef u32 uint32;
typedef u64 uint64;

/**
 * Total length of zero filled name string. This is not a C
 * string, as it can occupy the total number of bytes, and if
 * it does, it does not require a zero terminator. It cannot
 * be manipulated with standard string handling library functions.
 */
#define RAM_PART_NAME_LENGTH 16

/**
 * Number of RAM partition entries which are usable by APPS.
 */
#define RAM_NUM_PART_ENTRIES 32

/**
 * @name: Magic numbers
 * Used in identifying valid RAM partition table.
 */
#define RAM_PART_MAGIC1     0x9DA5E0A8
#define RAM_PART_MAGIC2     0xAF9EC4E2

/**
 * Must increment this version number whenever RAM structure of
 * RAM partition table changes.
 */
#define RAM_PARTITION_VERSION   0x3

/**
 * Value which indicates the partition can grow to fill the
 * rest of RAM. Must only be used on the last partition.
 */
#define RAM_PARTITION_GROW  0xFFFFFFFF

/**
 * RAM partition API return types.
 */
enum  ram_partition_return_type {
	RAM_PART_SUCCESS = 0,             /* Successful return from API */
	RAM_PART_NULL_PTR_ERR,            /* Partition table/entry null pointer */
	RAM_PART_OUT_OF_BOUND_PTR_ERR,    /* Partition table pointer is not in SMEM */
	RAM_PART_TABLE_EMPTY_ERR,         /* Trying to delete entry from empty table */
	RAM_PART_TABLE_FULL_ERR,          /* Trying to add entry to full table */
	RAM_PART_CATEGORY_NOT_EXIST_ERR,  /* Partition doesn't belong to any memory category */
	RAM_PART_OTHER_ERR,               /* Unknown error */
	RAM_PART_RETURN_MAX_SIZE = 0x7FFFFFFF
};

/**
 * RAM partition attributes.
 */
enum ram_partition_attribute_t {
	RAM_PARTITION_DEFAULT_ATTRB = ~0,  /* No specific attribute definition */
	RAM_PARTITION_READ_ONLY = 0,       /* Read-only RAM partition */
	RAM_PARTITION_READWRITE,           /* Read/write RAM partition */
	RAM_PARTITION_ATTRIBUTE_MAX_SIZE = 0x7FFFFFFF
};

/**
 * RAM partition categories.
 */
enum ram_partition_category_t {
	RAM_PARTITION_DEFAULT_CATEGORY = ~0,  /* No specific category definition */
	RAM_PARTITION_IRAM = 4,                   /* IRAM RAM partition */
	RAM_PARTITION_IMEM = 5,                   /* IMEM RAM partition */
	RAM_PARTITION_SDRAM = 14,                  /* SDRAM type without specific bus information**/
	RAM_PARTITION_CATEGORY_MAX_SIZE = 0x7FFFFFFF
};

/**
 * RAM Partition domains.
 * @note: For shared RAM partition, domain value would be 0b11:\n
 * RAM_PARTITION_APPS_DOMAIN | RAM_PARTITION_MODEM_DOMAIN.
 */
enum ram_partition_domain_t {
	RAM_PARTITION_DEFAULT_DOMAIN = 0,  /* 0b00: No specific domain definition */
	RAM_PARTITION_APPS_DOMAIN = 1,     /* 0b01: APPS RAM partition */
	RAM_PARTITION_MODEM_DOMAIN = 2,    /* 0b10: MODEM RAM partition */
	RAM_PARTITION_DOMAIN_MAX_SIZE = 0x7FFFFFFF
};

/**
 * RAM Partition types.
 * @note: The RAM_PARTITION_SYS_MEMORY type represents DDR rams that are attached
 * to the current system.
 */
enum ram_partition_type_t {
	RAM_PARTITION_SYS_MEMORY = 1,        /* system memory */
	RAM_PARTITION_BOOT_REGION_MEMORY1,   /* boot loader memory 1 */
	RAM_PARTITION_BOOT_REGION_MEMORY2,   /* boot loader memory 2, reserved */
	RAM_PARTITION_APPSBL_MEMORY,         /* apps boot loader memory */
	RAM_PARTITION_APPS_MEMORY,           /* apps usage memory */
	RAM_PARTITION_TOOLS_FV_MEMORY,       /* tools usage memory */
	RAM_PARTITION_QUANTUM_FV_MEMORY,     /* quantum usage memory */
	RAM_PARTITION_QUEST_FV_MEMORY,       /* quest usage memory */
	RAM_PARTITION_TYPE_MAX_SIZE = 0x7FFFFFFF
};

/**
 * @brief: Holds information for an entry in the RAM partition table.
 */
struct ram_partition_entry {
	char name[RAM_PART_NAME_LENGTH];  /* Partition name, unused for now */
	uint64 start_address;             /* Partition start address in RAM */
	uint64 length;                    /* Partition length in RAM in Bytes */
	uint32 partition_attribute;       /* Partition attribute */
	uint32 partition_category;        /* Partition category */
	uint32 partition_domain;          /* Partition domain */
	uint32 partition_type;            /* Partition type */
	uint32 num_partitions;            /* Number of partitions on device */
	uint32 hw_info;                   /* hw information such as type and frequency */
	uint8 highest_bank_bit;           /* Highest bit corresponding to a bank */
	uint8 reserve0;                   /* Reserved for future use */
	uint8 reserve1;                   /* Reserved for future use */
	uint8 reserve2;                   /* Reserved for future use */
	uint32 min_pasr_size;             /* Minimum PASR size in MB */
	uint64 available_length;          /* Available Partition length in RAM in Bytes */
};

/**
 * @brief: Defines the RAM partition table structure
 * @note: No matter how you change the structure, do not change the placement of the
 * first four elements so that future compatibility will always be guaranteed
 * at least for the identifiers.
 *
 * @note: The other portion of the structure may be changed as necessary to accommodate
 * new features. Be sure to increment version number if you change it.
 */
struct usable_ram_partition_table {
	uint32 magic1;          /* Magic number to identify valid RAM partition table */
	uint32 magic2;          /* Magic number to identify valid RAM partition table */
	uint32 version;         /* Version number to track structure definition changes */
	uint32 reserved1;       /* Reserved for future use */

	uint32 num_partitions;  /* Number of RAM partition table entries */

	uint32 reserved2;       /* Added for 8 bytes alignment of header */

	/* RAM partition table entries */
	struct ram_partition_entry ram_part_entry[RAM_NUM_PART_ENTRIES];
};

/**
 * Version 1 structure 32 Bit
 * @brief: Holds information for an entry in the RAM partition table.
 */
struct ram_partition_entry_v1 {
	char name[RAM_PART_NAME_LENGTH];  /* Partition name, unused for now */
	uint64 start_address;             /* Partition start address in RAM */
	uint64 length;                    /* Partition length in RAM in Bytes */
	uint32 partition_attribute;       /* Partition attribute */
	uint32 partition_category;        /* Partition category */
	uint32 partition_domain;          /* Partition domain */
	uint32 partition_type;            /* Partition type */
	uint32 num_partitions;            /* Number of partitions on device */
	uint32 hw_info;                   /* hw information such as type and frequency */
	uint32 reserved4;                 /* Reserved for future use */
	uint32 reserved5;                 /* Reserved for future use */
};

/**
 * @brief: Defines the RAM partition table structure
 * @note: No matter how you change the structure, do not change the placement of the
 * first four elements so that future compatibility will always be guaranteed
 * at least for the identifiers.
 *
 * @note: The other portion of the structure may be changed as necessary to accommodate
 * new features. Be sure to increment version number if you change it.
 */
struct usable_ram_partition_table_v1 {
	uint32 magic1;          /* Magic number to identify valid RAM partition table */
	uint32 magic2;          /* Magic number to identify valid RAM partition table */
	uint32 version;         /* Version number to track structure definition changes */
	uint32 reserved1;       /* Reserved for future use */

	uint32 num_partitions;  /* Number of RAM partition table entries */

	uint32 reserved2;       /* Added for 8 bytes alignment of header */

	/* RAM partition table entries */
	struct ram_partition_entry_v1 ram_part_entry_v1[RAM_NUM_PART_ENTRIES];
};

/**
 * Version 0 structure 32 Bit
 * @brief: Holds information for an entry in the RAM partition table.
 */
struct ram_partition_entry_v0 {
	char name[RAM_PART_NAME_LENGTH];  /* Partition name, unused for now */
	uint32 start_address;             /* Partition start address in RAM */
	uint32 length;                    /* Partition length in RAM in Bytes */
	uint32 partition_attribute;       /* Partition attribute */
	uint32 partition_category;        /* Partition category */
	uint32 partition_domain;          /* Partition domain */
	uint32 partition_type;            /* Partition type */
	uint32 num_partitions;            /* Number of partitions on device */
	uint32 reserved3;                 /* Reserved for future use */
	uint32 reserved4;                 /* Reserved for future use */
	uint32 reserved5;                 /* Reserved for future use */
};

/**
 * @brief: Defines the RAM partition table structure
 * @note: No matter how you change the structure, do not change the placement of the
 * first four elements so that future compatibility will always be guaranteed
 * at least for the identifiers.
 *
 * @note: The other portion of the structure may be changed as necessary to accommodate
 * new features. Be sure to increment version number if you change it.
 */
struct usable_ram_partition_table_v0 {
	uint32 magic1;          /* Magic number to identify valid RAM partition table */
	uint32 magic2;          /* Magic number to identify valid RAM partition table */
	uint32 version;         /* Version number to track structure definition changes */
	uint32 reserved1;       /* Reserved for future use */

	uint32 num_partitions;  /* Number of RAM partition table entries */

	/* RAM partition table entries */
	struct ram_partition_entry_v0 ram_part_entry_v0[RAM_NUM_PART_ENTRIES];
};
