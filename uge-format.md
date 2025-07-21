# hUGETracker .UGE v6 format (as reverse-engineered from GB Studio and hUGETracker)

## Data types

| Name          | Byte length | Description                                                                                                                    |
| ------------- | ----------- | ------------------------------------------------------------------------------------------------------------------------------ |
| `uint8`       | 1           | Also known as char, ranges from 0 to 255.                                                                                      |
| `uint32`      | 4           | Also known as word, ranges from 0 to 4,294,967,295.                                                                            |
| `int8`        | 1           | Ranges from -127 to 127.                                                                                                       |
| `bool`        | 1           | If not-zero, then True.                                                                                                        |
| `shortstring` | 256         | Consists of a byte defining the readable length and then 255 characters.                                                       |
| `string`      | -           | Consists of a uint32 defining the number of characters, and then a stream of characters, with 0x00 being the terminator value. |
|               |             |                                                                                                                                |

All types are little endian unless noted otherwise.

---

## File Structure (Section Offsets for Minimal File)

| Section           | Offset (hex) | Notes                               |
| ----------------- | ------------ | ----------------------------------- |
| version           | 0x000000     | uint32                              |
| name              | 0x000004     | shortstring (256 bytes)             |
| artist            | 0x000104     | shortstring (256 bytes)             |
| comment           | 0x000204     | shortstring (256 bytes)             |
| duty instruments  | 0x000304     | 15 × instrument block               |
| wave instruments  | 0x00542b     | 15 × instrument block               |
| noise instruments | 0x00a552     | 15 × instrument block               |
| wavetable         | 0x00f679     | 16 × 32 bytes                       |
| ticks_per_row     | 0x00f879     | uint32                              |
| timer_enabled     | 0x00f87d     | int8                                |
| timer_divider     | 0x00f87e     | uint32                              |
| patterns          | 0x00f882     | see below                           |
| order matrix      | 0x013cc6     | see below                           |
| routines          | 0x013d26     | 16 × string (uint32 length + bytes) |

---

## Header

- `uint32` Version number (6)
- `shortstring` Song name
- `shortstring` Song artist
- `shortstring` Song comment

---

## Instruments (Duty, Wave, Noise)

- Each bank: 15 instruments
- **All fields are written for every instrument, even if not used.**
- **The full subpattern block (64 × 17 bytes) is always written, even if subpattern_enabled is false.**

### Duty Instrument Block

- `uint32` Type (0)
- `shortstring` Instrument name
- `uint32` Length
- `uint8` Length enabled
- `uint8` Initial volume
- `uint32` Volume sweep direction (0 = Increase, 1 = Decrease)
- `uint8` Volume sweep change
- `uint32` Frequency sweep time
- `uint32` Frequency sweep direction
- `uint32` Frequency sweep shift
- `uint8` Duty cycle
- `uint32` Unused
- `uint32` Unused
- `uint32` Unused
- `uint32` Unused
- `uint8` Subpattern enabled
- **Subpattern block:**
  - Repeat 64 times:
    - `uint32` Row note (0 through 72, 90 if not used)
    - `uint32` Unused
    - `uint32` Jump command value (0 if empty)
    - `uint32` Effect code
    - `uint8` Effect parameter

### Wave Instrument Block

- `uint32` Type (1)
- `shortstring` Instrument name
- `uint32` Length
- `uint8` Length enabled
- `uint8` Unused
- `uint32` Unused
- `uint8` Unused
- `uint32` Unused
- `uint32` Unused
- `uint32` Unused
- `uint8` Unused
- `uint32` Volume
- `uint32` Wave index
- `uint32` Unused
- `uint32` Unused
- `uint8` Subpattern enabled
- **Subpattern block:** (same as above)

### Noise Instrument Block

- `uint32` Type (2)
- `shortstring` Instrument name
- `uint32` Length
- `uint8` Length enabled
- `uint8` Initial volume
- `uint32` Volume sweep direction (0 = Increase, 1 = Decrease)
- `uint8` Volume sweep change
- `uint32` Unused
- `uint32` Unused
- `uint32` Unused
- `uint8` Unused
- `uint32` Unused
- `uint32` Unused
- `uint32` Noise mode (0 = 15 bit, 1 = 7 bit)
- `uint8` Subpattern enabled
- **Subpattern block:** (same as above)

---

## Wavetable

- 16 × 32 bytes (all zero for minimal file)

---

## Song Patterns

- `uint32` Number of song patterns (N)
- Repeat N times:
  - `uint32` Pattern index
  - Repeat 64 times:
    - `uint32` Row note (0 through 72, 90 if not used)
    - `uint32` Instrument value (0 if not used)
    - `uint32` Unused
    - `uint32` Effect code
    - `uint8` Effect parameter

---

## Order Matrix

- Repeat 4 times (Duty 1, Duty 2, Wave, Noise):
  - `uint32` Order length + 1 (off-by-one bug)
  - Repeat Order length times:
    - `uint32` Order index (for minimal: [0, 4, 8, 12] for channel 0, [1, 5, 9, 13] for channel 1, etc.)
  - `uint32` Off-by-one bug filler (0)

---

## Routines

- Repeat 16 times:
  - `uint32` Length (0 for empty)
  - (if length > 0) bytes of routine code
  - `uint8` 0x00 terminator

---

## Implementation Notes

- **All fields must be written, even if not used.**
- **Unused/reserved fields:** All unused or reserved fields must be written as zero.
- **Endian-ness:** All multi-byte values are little-endian.
- **Strict field order and padding:** No fields may be omitted or reordered. No extra padding is allowed between fields or sections; the file must be packed exactly as specified.
- **ShortString format:** Always 256 bytes: 1 byte for length, up to 255 bytes for data, zero-padded. Do not null-terminate.
- **Subpattern blocks:** The full subpattern block (64×17 bytes) must be written for every instrument, regardless of whether `subpattern_enabled` is true or false. If not used, all subpattern rows should be zeroed (or set to "unused" values, e.g., note=90).
- **Wavetable:** All 16 wavetable slots must be present, each 32 bytes. For minimal files, all wavetable data can be zero.
- **Patterns per channel:** Patterns are referenced by 8-bit indices in the order matrix, so a maximum of 256 unique patterns per channel is supported.
- **Pattern section:** The number of patterns written must match the highest pattern index referenced in the order matrix, plus one. All 64 rows must be present for every pattern, even if unused (set to "unused" values).
- **Order matrix:**
  - The order matrix and pattern indices must match; order matrix entries are 8-bit indices referencing patterns present in the file.
  - The "order length" field is always written as the number of entries plus one (off-by-one bug).
  - The final entry for each channel is a zero (`uint32_t 0`), even if not referenced.
  - Order indices must reference valid pattern indices (i.e., patterns that exist in the file).
- **Routines:** All 16 routine slots must be present, each with a `uint32` length and a `0x00` terminator, even if empty.
- **File size and section offsets:** For best compatibility with hUGETracker/GB Studio, file size and section offsets should match those produced by reference exporters. If in doubt, pad the file to match the reference file size.
- **No extra data:** Do not append any extra data or metadata after the last routine section.
- **Testing:** Always test generated files in both hUGETracker and GB Studio, as each may have slightly different tolerance for edge cases.
- **Error messages:** Common errors like `EReadError` or `EGridException` almost always indicate a structural or offset mismatch, not a content error.
- **Future-proofing:** If supporting future versions, always check for new fields or changes in the official hUGETracker/GB Studio exporters.

---

This documentation reflects the precise, working structure as reverse-engineered from GB Studio and hUGETracker, and validated by binary diff and debug output.

---

## Caveats and Requirements

- **Strict Field Order and Padding:**

  - No fields may be omitted or reordered. Even unused or reserved fields must be present and zeroed.
  - No extra padding is allowed between fields or sections; the file must be packed exactly as specified.

- **ShortString Format:**

  - The Pascal-style `shortstring` is always 256 bytes: 1 byte for length, up to 255 bytes for data, and the remainder zero-padded.
  - Do not null-terminate shortstrings; only the length byte and data are used.

- **Subpattern Block:**

  - The entire 64×17 byte subpattern block must be written for every instrument, regardless of whether `subpattern_enabled` is true or false.
  - If not used, all subpattern rows should be zeroed (or set to "unused" values, e.g., note=90).

- **Order Matrix:**

  - The "order length" field is always written as the number of entries plus one (off-by-one bug).
  - The final entry for each channel is a zero (`uint32_t 0`), even if not referenced.
  - Order indices must reference valid pattern indices (i.e., patterns that exist in the file).

- **Pattern Section:**

  - The number of patterns written must match the highest pattern index referenced in the order matrix, plus one.
  - All 64 rows must be present for every pattern, even if unused (set to "unused" values).

- **Routines:**

  - All 16 routine slots must be present, even if empty.
  - For empty routines, write a length of 0 and a single 0x00 terminator byte.

- **Wavetable:**

  - All 16 wavetable slots must be present, each 32 bytes.
  - For minimal files, all wavetable data can be zero.

- **Version Number:**

  - The version number must be set to 6 for full compatibility with modern hUGETracker/GB Studio.

- **Endian-ness:**

  - All multi-byte values are little-endian.

- **File Size and Section Offsets:**

  - hUGETracker/GB Studio may expect section offsets and file size to match those produced by their own exporters.
  - If in doubt, pad the file to match the reference file size.

- **Unused/Reserved Fields:**

  - All unused or reserved fields must be written as zero.

- **No Extra Data:**

  - Do not append any extra data or metadata after the last routine section.

- **Testing:**

  - Always test generated files in both hUGETracker and GB Studio, as each may have slightly different tolerance for edge cases.

- **Error Messages:**

  - Common errors like `EReadError` or `EGridException` almost always indicate a structural or offset mismatch, not a content error.

- **Future-Proofing:**
  - If supporting future versions, always check for new fields or changes in the official hUGETracker/GB Studio exporters.
