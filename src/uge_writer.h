#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <fstream>

constexpr int UGE_NUM_DUTY = 15;
constexpr int UGE_NUM_WAVE = 15;
constexpr int UGE_NUM_NOISE = 15;
constexpr int UGE_NUM_WAVETABLE = 16;
constexpr int UGE_WAVETABLE_SIZE = 32;
constexpr int UGE_NUM_CHANNELS = 4;
constexpr int UGE_NUM_ROUTINES = 16;
constexpr int UGE_PATTERN_ROWS = 64;

#pragma pack(push, 1)

struct UgeShortString {
    uint8_t length;
    char data[255];
};

struct UgeDutyInstrument {
    uint32_t type; // 0
    UgeShortString name;
    uint32_t length;
    uint8_t length_enabled;
    uint8_t initial_volume;
    uint32_t volume_sweep_direction; // 0 = increase, 1 = decrease
    uint8_t volume_sweep_change;
    uint32_t frequency_sweep_time;
    uint32_t frequency_sweep_direction;
    uint32_t frequency_sweep_shift;
    uint8_t duty;
    uint32_t unused1;
    uint32_t unused2;
    uint32_t unused3;
    uint8_t subpattern_enabled;
    // ...subpattern block (not shown here)...
};

struct UgeWaveInstrument {
    uint32_t type; // 1
    UgeShortString name;
    uint32_t length;
    uint8_t length_enabled;
    uint8_t unused1;
    uint32_t unused2;
    uint8_t unused3;
    uint32_t unused4;
    uint32_t unused5;
    uint32_t unused6;
    uint8_t unused7;
    uint32_t volume;
    uint32_t wave_index;
    uint32_t unused8;
    uint32_t unused9;
    uint8_t subpattern_enabled;
    // ...subpattern block (not shown here)...
};

struct UgeNoiseInstrument {
    uint32_t type; // 2
    UgeShortString name;
    uint32_t length;
    uint8_t length_enabled;
    uint8_t initial_volume;
    uint32_t volume_sweep_direction; // 0 = increase, 1 = decrease
    uint8_t volume_sweep_change;
    uint32_t unused1;
    uint32_t unused2;
    uint32_t unused3;
    uint8_t unused4;
    uint32_t unused5;
    uint32_t unused6;
    uint32_t noise_mode; // 0 = 15-bit, 1 = 7-bit
    uint8_t subpattern_enabled;
    // ...subpattern block (not shown here)...
};

using UgeDutyBank = std::array<UgeDutyInstrument, UGE_NUM_DUTY>;
using UgeWaveBank = std::array<UgeWaveInstrument, UGE_NUM_WAVE>;
using UgeNoiseBank = std::array<UgeNoiseInstrument, UGE_NUM_NOISE>;

struct UgeInstrumentCollection {
    UgeDutyBank duty;
    UgeWaveBank wave;
    UgeNoiseBank noise;
};

using UgeWavetable = std::array<std::array<uint8_t, UGE_WAVETABLE_SIZE>, UGE_NUM_WAVETABLE>;

struct UgePatternRow {
    uint8_t note;
    uint8_t instrument;
    uint8_t unused1;
    uint8_t effect;
    uint8_t effect_param;
    uint8_t unused2;
    uint8_t unused3;
    uint8_t unused4;
    uint8_t unused5[12]; // pad to 20 bytes
};

struct UgePattern {
    uint32_t index;
    std::array<UgePatternRow, UGE_PATTERN_ROWS> rows;
};

using UgeOrderMatrix = std::array<std::vector<uint32_t>, UGE_NUM_CHANNELS>;
using UgeRoutineBank = std::array<std::string, UGE_NUM_ROUTINES>;

struct UgeSongHeader {
    uint32_t version;
    UgeShortString name;
    UgeShortString artist;
    UgeShortString comment;
    UgeInstrumentCollection instruments;
    UgeWavetable wavetable;
    uint32_t ticks_per_row;
    uint8_t timer_enabled;
    uint32_t timer_divider;
};

#pragma pack(pop)

// Helper functions for writing
UgeShortString make_shortstring(const std::string& s);
void write_shortstring(std::ofstream& out, const UgeShortString& s);
template<typename T>
void write_le(std::ofstream& out, T value);

// Main write function
bool writeUgeFile(
    const std::string& ugePath,
    const UgeSongHeader& header,
    const std::vector<UgePattern>& patterns,
    const UgeOrderMatrix& orders,
    const UgeRoutineBank& routines
); 