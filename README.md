# midi2uge

A command-line tool to convert MIDI files to UGE format for hUGETracker and Game Boy music production.

> **Note:** This tool is a work in progress. The UGE format is complex and evolving, and not all MIDI features or edge cases are supported. Please report issues and expect rapid changes.

## Build Instructions

1. Clone this repo and initialize submodules:
   ```sh
   git clone <repo-url>
   cd midi2uge
   git submodule update --init --recursive
   ```
2. Build with CMake:
   ```sh
   mkdir build
   cd build
   cmake ..
   make
   ```

## Usage

Basic conversion:

```
./midi2uge -i <input.mid> -o <output.uge>
```

Or (legacy positional):

```
./midi2uge <input.mid> <output.uge>
```

### Optional: Channel Mapping

You can explicitly map MIDI channels to UGE channels (Duty1, Duty2, Wave, Noise) using the `-m` or `--map` option:

```
./midi2uge -i <input.mid> -o <output.uge> -m <duty1,duty2,wave,noise>
```

- Use a comma-separated list of up to 4 MIDI channel indices (0–15).
- Use `-1` for any channel to leave it empty.
- If not provided, the tool auto-selects the three most active melodic channels and maps Noise to MIDI 9.

**Examples:**

- Map Duty1 to MIDI 0, Duty2 to MIDI 1, Wave to MIDI 2, Noise to MIDI 9:
  ```
  ./midi2uge -i song.mid -o song.uge -m 0,1,2,9
  ```
- Map Duty1 to MIDI 5, leave Duty2 empty, Wave to MIDI 3, Noise empty:
  ```
  ./midi2uge -i song.mid -o song.uge -m 5,-1,3,-1
  ```

## Dependencies

- [midifile](https://github.com/craigsapp/midifile) (included as submodule in `third_party/`)

## References

- [MIDI 1.0 Spec](https://www.freqsound.com/SIRA/MIDI%20Specification.pdf)
- [UGE Format Spec](https://github.com/SuperDisk/hUGETracker/blob/hUGETracker/manual/src/hUGETracker/uge-format.md)
- [hUGETracker](https://github.com/SuperDisk/hUGETracker)
