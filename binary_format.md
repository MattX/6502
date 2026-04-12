# Loadable binary format

Specifies where to load each section and in which bank.

Kaitai spec (not used to generate code, just for reference):

```kaitai
meta:
  id: mattbrew_executable
  title: Mattbrew executable file
  file-extension: pack.bin
  endian: le

seq:
  - id: header
    type: header
  - id: sections
    type: section
    repeat: expr
    repeat_expr: header.section_count

types:
  header:
    seq:
      - id: magic
        contents: [0x45, 0x69]
        doc: Magic bytes for format identification.
      - id: version
        type: u1
        doc: Format version. Must currently be 1.
      - id: entry_point
        type: u2
        doc: |
          CPU address to begin execution after all sections are loaded
          (target of JMP or JSR).
      - id: section_count
        type: u1
        doc: |
          Number of executable sections.

  section:
    seq:
      - id: load_addr
        type: u2
        doc: |
          CPU-visible load address for this section's data.
          For banked sections this will typically be within the bank window
          (e.g. $A000-$DFFF). For unbanked sections it can be any address
          in the main address space ($0000-$DFFF).
      - id: bank
        type: u1
        doc: |
          Bank number to select before copying data. Valid bank numbers
          are 0-31.
          The sentinel value 0xFF means "main RAM / no bank switching
          required".
      - id: len
        type: u2
        doc: |
          Length of section data in bytes.
      - id: data
        size: len
        if: not is_bss
        doc: |
          Raw section payload.
```
