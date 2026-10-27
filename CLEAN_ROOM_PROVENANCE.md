# TitanF2FS — Clean-Room Architecture & Legal Provenance

## 1. Statutory Grounding & Legal Precedent
TitanF2FS is an independent, clean-room implementation of the Samsung Flash-Friendly File System (F2FS) on-disk file system format for Windows NT 10.0+ and MicaNT. 

This implementation is developed in accordance with:
- **17 U.S.C. § 1201(f) (Reverse Engineering for Interoperability)**: Authorizing the analysis and independent recreation of file structures and communication protocols solely for achieving software interoperability.
- **_Sega Enterprises Ltd. v. Accolade, Inc._, 977 F.2d 1510 (9th Cir. 1992)**: Affirming the legality of reverse engineering functional specifications and interface data to produce compatible software.
- **_Sony Computer Entertainment, Inc. v. Connectix Corp._, 203 F.3d 596 (9th Cir. 2000)**: Reaffirming clean-room derivation of functional system interfaces.
- **_Google LLC v. Oracle America, Inc._, 141 S. Ct. 1183 (2021)**: Confirming that declaring code, structural file format headers, and interoperability interfaces are non-infringing subject matter under fair use.

## 2. Engineering Boundary & Methodology
- **Clean-Room Boundary**: No proprietary Windows source code, Microsoft internal headers, or proprietary OEM drivers were used.
- **Specification Derivation**: Format structures (F2FS Superblock magic `0xF2F52010`, Checkpoint, Segment Information Table, Node Address Table, Inodes, Extents, and Directory entries) were implemented purely from open on-disk specifications and public file formats.
- **Userland Architecture**: TitanF2FS operates 100% in user mode via WinFsp (`winfsp-x64.dll`), eliminating kernel instability, blue screens (BSOD), and driver signing encumbrances.
- **License**: Released under the Apache License, Version 2.0.

## 3. Provenance Chain
- **Architect & Author**: Daniel Elliott `<ssfdre38@gmail.com>`
- **Project**: MicaNT-Kernel / TitanF2FS
- **Target OS**: Windows NT 10.0+ (x86_64, ARM64) and MicaNT Operating System
- **Compiler Target**: ISO C++23 (Clang / GCC / MSVC)
