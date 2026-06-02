#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
"""
FvUpdate.xml to Capsule-Compatible FIT Image and Capsule Converter

Complete end-to-end converter that:
1. Processes FvUpdate.xml files
2. Creates FIT images compatible with Qualcomm U-Boot capsule update system
3. Generates GUIDs using mkeficapsule guidgen
4. Creates final capsule files ready for deployment

Usage: ./fvupdate_to_fit.py <FvUpdate.xml>
"""

import xml.etree.ElementTree as ET
import os
import sys
import subprocess
import shutil
import re
from pathlib import Path
import argparse

class FvUpdateToFitConverter:
    """
    Complete FvUpdate.xml to Capsule converter.

    This converter provides end-to-end functionality:
    1. Converts FvUpdate.xml to FIT images
    2. Generates GUIDs using mkeficapsule guidgen
    3. Creates final capsule files
    4. Auto-installs missing tools
    5. Ensures compatibility with Qualcomm U-Boot capsule update system
    """

    def __init__(self, xml_path, mkeficapsule_path=None):
        self.xml_path = Path(xml_path)
        self.mkeficapsule_path = mkeficapsule_path or "mkeficapsule"
        self.base_dir = self.xml_path.parent
        self.images_dir = self.base_dir / "Images"
        self.fw_entries = []

    def parse_xml(self):
        """
        Parse FvUpdate.xml and extract UPDATE operations only.

        Only processes FwEntry elements with Operation="UPDATE" to ensure
        compatibility with capsule update requirements.
        """
        print(f"Parsing XML file: {self.xml_path}")

        try:
            tree = ET.parse(self.xml_path)
            root = tree.getroot()
        except ET.ParseError as e:
            raise ValueError(f"Invalid XML format: {e}")
        except FileNotFoundError:
            raise FileNotFoundError(f"XML file not found: {self.xml_path}")

        # Find all FwEntry elements
        fw_entries = root.findall('.//FwEntry')
        if not fw_entries:
            raise ValueError("No FwEntry elements found in XML")

        print(f"Found {len(fw_entries)} FwEntry elements")

        for fw_entry in fw_entries:
            # Check operation type
            operation_elem = fw_entry.find('Operation')
            if operation_elem is None:
                print("Warning: FwEntry missing Operation element, skipping")
                continue

            operation = operation_elem.text
            if operation != "UPDATE":
                print(f"Skipping FwEntry with operation: {operation}")
                continue

            # Extract required elements
            try:
                dest = fw_entry.find('.//Dest')
                if dest is None:
                    raise ValueError("FwEntry missing Dest element")

                partition_name_elem = dest.find('PartitionName')
                if partition_name_elem is None:
                    raise ValueError("Dest missing PartitionName element")
                partition_name = partition_name_elem.text

                binary_file_elem = fw_entry.find('InputBinary')
                if binary_file_elem is None:
                    raise ValueError("FwEntry missing InputBinary element")
                binary_file = binary_file_elem.text

                # Validate partition name
                if not partition_name or not partition_name.strip():
                    raise ValueError("Empty partition name")

                # Validate binary file name
                if not binary_file or not binary_file.strip():
                    raise ValueError("Empty binary file name")

                self.fw_entries.append({
                    'partition_name': partition_name.strip(),
                    'binary_file': binary_file.strip(),
                    'binary_path': self.images_dir / binary_file.strip()
                })

                print(f"  Added: {partition_name} -> {binary_file}")

            except ValueError as e:
                raise ValueError(f"Invalid FwEntry: {e}")

        if not self.fw_entries:
            raise ValueError("No valid UPDATE FwEntry elements found")

        print(f"Successfully parsed {len(self.fw_entries)} UPDATE entries")

    def validate_files(self):
        """
        Validate all binary files exist.

        Fails completely if any binary file is missing, as requested.
        """
        print("Validating binary files...")

        if not self.images_dir.exists():
            raise FileNotFoundError(f"Images directory not found: {self.images_dir}")

        missing_files = []
        for entry in self.fw_entries:
            if not entry['binary_path'].exists():
                missing_files.append(str(entry['binary_path']))
            else:
                # Check file size
                size = entry['binary_path'].stat().st_size
                print(f"  {entry['binary_file']}: {size} bytes")

        if missing_files:
            raise FileNotFoundError(f"Missing binary files: {missing_files}")

        print("All binary files validated successfully")

    def generate_its(self, its_path):
        """
        Generate capsule-compatible ITS file.

        Creates ITS content that works with the Qualcomm capsule update flow:
        - Node names match partition names for DFU mapping
        - All images marked as "firmware" type
        - SHA256 hashes for integrity verification
        - Single configuration referencing all firmware
        """
        print(f"Generating ITS file: {its_path}")

        its_content = self._build_its_content()

        try:
            with open(its_path, 'w') as f:
                f.write(its_content)
        except IOError as e:
            raise IOError(f"Failed to write ITS file: {e}")

        print(f"ITS file generated successfully")

    def _build_its_content(self):
        """
        Build ITS content compatible with capsule update flow.

        Format ensures compatibility with:
        - qcom_configure_capsule_updates() partition discovery
        - fit_update() processing requirements
        - fit_image_verify() hash validation
        - dfu_write_by_name() partition targeting
        """
        images_section = ""
        firmware_list = []

        for entry in self.fw_entries:
            # Node name format: partition_name (without @1 to avoid reg issues)
            # This MUST match the partition name discovered by U-Boot
            node_name = entry['partition_name']
            firmware_list.append(f'"{node_name}"')

            # Generate image node without reg properties to avoid DTC warnings
            images_section += f'''
        {node_name} {{
            description = "{entry['partition_name']} Firmware";
            data = /incbin/("Images/{entry['binary_file']}");
            type = "firmware";        /* Required for fit_update() */
            arch = "arm64";           /* Target architecture */
            compression = "none";     /* No compression */
            load = <0x00000000>;      /* Required by fit_image_get_load() */

            hash-1 {{
                algo = "sha256";      /* For fit_image_verify() */
            }};
        }};'''

        # Build firmware reference list for configuration
        firmware_refs = ", ".join(firmware_list)

        # Complete ITS content
        return f'''/dts-v1/;

/ {{
    description = "Qualcomm Firmware Update Package";

    images {{{images_section}
    }};

    configurations {{
        default = "config-1";
        config-1 {{
            description = "Qualcomm Multi-Partition Update";
            firmware = {firmware_refs};
        }};
    }};
}};
'''

    def compile_fit(self, its_path, fit_path):
        """
        Compile ITS to FIT using mkimage.

        Uses U-Boot's mkimage tool to create the final FIT image.
        """
        print(f"Compiling FIT image: {fit_path}")

        # Check if mkimage is available
        try:
            subprocess.run(["mkimage", "-V"], capture_output=True, check=True)
        except (subprocess.CalledProcessError, FileNotFoundError):
            raise RuntimeError("mkimage tool not found. Please install U-Boot tools.")

        # Compile ITS to FIT
        cmd = ["mkimage", "-f", str(its_path), str(fit_path)]
        print(f"Running: {' '.join(cmd)}")

        try:
            result = subprocess.run(cmd, capture_output=True, text=True,
                                  cwd=self.base_dir, check=True)

            if result.stdout:
                print("mkimage output:")
                print(result.stdout)

        except subprocess.CalledProcessError as e:
            error_msg = f"mkimage compilation failed (exit code {e.returncode})"
            if e.stderr:
                error_msg += f": {e.stderr}"
            raise RuntimeError(error_msg)

        # Verify output file was created
        if not fit_path.exists():
            raise RuntimeError("FIT image was not created")

        fit_size = fit_path.stat().st_size
        print(f"FIT image compiled successfully: {fit_size} bytes")

    def check_required_tools(self):
        """Check if required tools are available, auto-install if missing"""
        required_tools = {
            'mkimage': 'u-boot-tools',
            'mkeficapsule': 'u-boot-tools'
        }

        missing_tools = []
        for tool, package in required_tools.items():
            if not self.tool_available(tool):
                missing_tools.append((tool, package))

        if missing_tools:
            print("Installing missing tools...")
            packages_to_install = set()
            for tool, package in missing_tools:
                packages_to_install.add(package)

            for package in packages_to_install:
                self.install_package(package)
                print(f"  Installed {package} ✓")

    def tool_available(self, tool_name):
        """Check if a tool is available in PATH"""
        return shutil.which(tool_name) is not None

    def install_package(self, package):
        """Install package using system package manager"""
        # Detect package manager and install
        if shutil.which('apt-get'):
            cmd = ['sudo', 'apt-get', 'install', '-y', package]
        elif shutil.which('yum'):
            cmd = ['sudo', 'yum', 'install', '-y', package]
        elif shutil.which('dnf'):
            cmd = ['sudo', 'dnf', 'install', '-y', package]
        elif shutil.which('pacman'):
            cmd = ['sudo', 'pacman', '-S', '--noconfirm', package]
        else:
            raise RuntimeError(f"Cannot auto-install {package}. Please install manually.")

        try:
            result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        except subprocess.CalledProcessError as e:
            raise RuntimeError(f"Failed to install {package}: {e.stderr}")

    def get_primary_partition_name(self):
        """Get partition name from first FwEntry with Operation='UPDATE'"""
        if not self.fw_entries:
            raise ValueError("No UPDATE FwEntry found in XML")

        # Return first UPDATE partition found
        primary_partition = self.fw_entries[0]['partition_name']
        print(f"Primary partition selected: {primary_partition} (first UPDATE entry)")
        return primary_partition

    def is_valid_guid(self, guid):
        """Validate GUID format"""
        # GUID format: 8-4-4-4-12 hex digits
        guid_pattern = r'^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$'
        return re.match(guid_pattern, guid) is not None

    def create_capsule(self, fit_path, guid, capsule_path, fw_version=None, monotonic_count=None, private_key=None, certificate=None):
        """Create capsule using mkeficapsule with optional firmware version and signing"""
        print(f"Creating capsule: {capsule_path}")

        cmd = [
            self.mkeficapsule_path,
            '--guid', guid,
            '--index', '1',
        ]

        # Add firmware version if provided
        if fw_version is not None:
            version_parts = fw_version.split('.')
            try:
                major = int(version_parts[2])
                minor = int(version_parts[3])
                encoded_version = (major << 16) | minor
                cmd.extend(['--fw-version', str(encoded_version)])
                print(f"  Encoded Firmware version: {encoded_version} (from {fw_version})")
            except (ValueError, IndexError):
                raise ValueError(f"Invalid version string: {fw_version}")

        # Add signing parameters if provided
        if private_key and certificate and monotonic_count is not None:
            print("Signing capsule...")
            cmd.extend([
                '--monotonic-count', str(monotonic_count),
                '--private-key', str(private_key),
                '--certificate', str(certificate),
            ])
        elif any([private_key, certificate, monotonic_count is not None]):
            raise ValueError("All signing parameters (--private-key, --certificate, --monotonic-count) must be provided together.")

        cmd.extend([str(fit_path), str(capsule_path)])

        print(f"  Command: {' '.join(cmd)}")

        try:
            result = subprocess.run(cmd, capture_output=True, text=True,
                                  check=True, timeout=60)

            if result.stdout:
                print("mkeficapsule output:")
                print(result.stdout)

        except subprocess.CalledProcessError as e:
            error_msg = f"Capsule creation failed (exit code {e.returncode})"
            if e.stderr:
                error_msg += f": {e.stderr}"
            raise RuntimeError(error_msg)
        except subprocess.TimeoutExpired:
            raise RuntimeError("Capsule creation timed out")

        # Verify output file was created
        if not capsule_path.exists():
            raise RuntimeError("Capsule file was not created")

        capsule_size = capsule_path.stat().st_size
        print(f"Capsule created successfully: {capsule_size} bytes ✓")
        return capsule_size

    def convert_complete(self, fit_output_name="system.fit", capsule_output_name="firmware.capsule", fw_version=None, guid=None, monotonic_count=None, private_key=None, certificate=None):
        """
        Complete conversion workflow: XML → FIT → Capsule

        Performs the full end-to-end conversion:
        1. Check and install required tools
        2. XML parsing and validation
        3. Binary file validation
        4. ITS generation
        5. FIT compilation
        6. Capsule creation with provided GUID and optional firmware version
        7. Keep all intermediate files
        """
        print(f"Converting {self.xml_path} to capsule...")
        print("=" * 60)

        # Step 1: Check and install required tools
        self.check_required_tools()

        # Step 2-5: Generate FIT
        self.parse_xml()
        self.validate_files()

        # Generate file paths
        its_path = self.base_dir / "system.its"
        fit_path = self.base_dir / fit_output_name
        capsule_path = self.base_dir / capsule_output_name

        self.generate_its(its_path)
        self.compile_fit(its_path, fit_path)

        # Step 6: Create capsule with provided GUID and optional firmware version
        if not guid:
            raise ValueError("GUID is required for capsule creation. Use --guid option.")

        print(f"Using GUID: {guid}")
        capsule_size = self.create_capsule(fit_path, guid, capsule_path, fw_version, monotonic_count, private_key, certificate)

        # Step 7: Summary (keep all files)
        print("=" * 60)
        print("SUCCESS: Complete capsule workflow completed!")
        print()
        print("Files created:")
        print(f"  ITS file: {its_path}")
        print(f"  FIT file: {fit_path}")
        print(f"  Capsule file: {capsule_path} ({capsule_size / (1024*1024):.1f} MB)")
        print()
        print(f"Capsule GUID: {guid}")
        print()
        print("Ready for deployment:")
        print("  1. Copy firmware.capsule to boot partition")
        print("  2. In U-Boot: fatload mmc 0:1 $loadaddr firmware.capsule")
        print("  3. In U-Boot: efidebug capsule update $loadaddr")

        return capsule_path

    def convert(self, output_name="system.fit"):
        """
        Main conversion workflow.

        Converts FvUpdate.xml to capsule-compatible FIT image through:
        1. XML parsing and validation
        2. Binary file validation
        3. ITS generation
        4. FIT compilation
        """
        print(f"Converting {self.xml_path} to FIT image...")
        print("=" * 60)

        # Parse and validate XML
        self.parse_xml()

        # Validate binary files
        self.validate_files()

        # Generate ITS file
        its_path = self.base_dir / "system.its"
        self.generate_its(its_path)

        # Compile FIT image
        fit_path = self.base_dir / output_name
        self.compile_fit(its_path, fit_path)

        print("=" * 60)
        print("CONVERSION SUMMARY:")
        print(f"  Input XML: {self.xml_path}")
        print(f"  Generated ITS: {its_path}")
        print(f"  Output FIT: {fit_path}")
        print(f"  Partitions: {len(self.fw_entries)}")
        for entry in self.fw_entries:
            print(f"    - {entry['partition_name']} ({entry['binary_file']})")

        return fit_path

def main():
    """Main entry point with command line argument parsing."""
    parser = argparse.ArgumentParser(
        description="Convert FvUpdate.xml to capsule-compatible FIT image and capsule",
        epilog="""
Examples:
  # Generate FIT image only:
  %(prog)s FvUpdate.xml

  # Generate FIT image and create capsule:
  %(prog)s FvUpdate.xml --mkeficapsule <path_to_mkeficapsule_tool> --guid <guid_of_the_board> --fw-version <version_of_the_capsule_payload>

The script can operate in two modes:
1. FIT-only mode: Creates FIT image from FvUpdate.xml
2. Complete mode: Creates FIT image and capsule (requires --guid and --mkeficapsule)

Workflow:
1. Auto-install missing tools (mkimage, mkeficapsule)
2. Parse FvUpdate.xml for UPDATE operations
3. Validate all binary files in Images/ directory
4. Generate system.its file
5. Compile system.fit image using mkimage
6. Create firmware.capsule using mkeficapsule (if --guid provided)

The generated capsule is ready for deployment in Qualcomm U-Boot systems.
        """,
        formatter_class=argparse.RawDescriptionHelpFormatter
    )

    parser.add_argument('xml_file',
                       help='Path to FvUpdate.xml file')
    parser.add_argument('--guid',
                       help='Capsule GUID for capsule generation. Format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx. '
                            'Board-specific GUIDs: '
                            'qcs615: 9FD379D2-670E-4BB3-86A1-40497E6E17B0, '
                            'qcs6490-rb3gen2: 6f25bfd2-a165-468b-980f-ac51a0a45c52, '
                            'lemans-evk: 78462415-6133-431c-9fae-48f2bafd5c71')
    parser.add_argument('--mkeficapsule',
                       help='Path to mkeficapsule binary (default: use system PATH)')
    parser.add_argument('-o', '--output',
                       default='system.fit',
                       help='Output FIT image name (default: %(default)s)')
    parser.add_argument('-c', '--capsule-output',
                       default='firmware.capsule',
                       help='Output capsule file name (default: %(default)s)')
    parser.add_argument('--fw-version',
                       help='Firmware version for capsule in "0.0.A.B" format (e.g., "0.0.1.0"). This version will be stored in ESRT.')
    parser.add_argument('--monotonic-count',
                       type=int,
                       help='Monotonic count for capsule signing.')
    parser.add_argument('--private-key',
                       help='Path to the private key for signing.')
    parser.add_argument('--certificate',
                       help='Path to the certificate for signing.')
    parser.add_argument('-v', '--verbose',
                       action='store_true',
                       help='Enable verbose output')

    args = parser.parse_args()

    # Validate input file
    xml_path = Path(args.xml_file)
    if not xml_path.exists():
        print(f"ERROR: XML file not found: {xml_path}")
        sys.exit(1)

    if not xml_path.is_file():
        print(f"ERROR: Path is not a file: {xml_path}")
        sys.exit(1)

    # Validate GUID format if provided
    if args.guid:
        guid_pattern = r'^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$'
        if not re.match(guid_pattern, args.guid):
            print(f"ERROR: Invalid GUID format: {args.guid}")
            print("Expected format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx")
            sys.exit(1)

    # Validate fw-version format if provided
    if args.fw_version:
        version_pattern = r'^\d+\.\d+\.\d+\.\d+$'
        if not re.match(version_pattern, args.fw_version):
            print(f"ERROR: Invalid firmware version format: {args.fw_version}")
            print('Expected format: "0.0.A.B" (e.g., "0.0.1.0")')
            sys.exit(1)

    # Create converter
    converter = FvUpdateToFitConverter(xml_path, args.mkeficapsule)

    try:
        if args.guid:
            # Complete capsule workflow
            capsule_path = converter.convert_complete(
                args.output,
                args.capsule_output,
                args.fw_version,
                args.guid,
                args.monotonic_count,
                args.private_key,
                args.certificate
            )

            print("\n" + "=" * 60)
            print("SUCCESS: Complete capsule workflow completed!")
            print(f"Final capsule: {capsule_path}")
            if args.fw_version:
                print(f"Firmware version: {args.fw_version}")

        else:
            # FIT-only mode
            fit_path = converter.convert(args.output)

            print("\n" + "=" * 60)
            print("SUCCESS: FIT image created successfully!")
            print(f"Output: {fit_path}")
            print("\nTo create capsule, run again with:")
            print(f"   {sys.argv[0]} {args.xml_file} --mkeficapsule <path_to_tool> --guid <board_guid> --fw-version <version>")

    except Exception as e:
        print(f"\nERROR: {e}")
        if args.verbose:
            import traceback
            traceback.print_exc()
        sys.exit(1)

if __name__ == "__main__":
    main()
