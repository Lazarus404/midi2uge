#include "uge_writer.h"
#include <fstream>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <iomanip> // Required for std::hex, std::setw, std::setfill

// Helper to write a value as little-endian
// (for 1, 2, 4 byte types)
template<typename T>
void write_le(std::ofstream& out, T value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template void write_le<uint32_t>(std::ofstream&, uint32_t);
template void write_le<uint8_t>(std::ofstream&, uint8_t);

UgeShortString make_shortstring(const std::string& s) {
    UgeShortString ss{};
    ss.length = std::min<size_t>(s.size(), 255);
    std::memset(ss.data, 0, 255);
    std::memcpy(ss.data, s.data(), ss.length);
    return ss;
}

void write_shortstring(std::ofstream& out, const UgeShortString& s) {
    out.put(s.length);
    out.write(s.data, 255);
}

bool writeUgeFile(
    const std::string& ugePath,
    const UgeSongHeader& header,
    const std::vector<UgePattern>& patterns,
    const UgeOrderMatrix& orders,
    const UgeRoutineBank& routines
) {
    std::ofstream out(ugePath, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    auto logSection = [&](const char* name) {
        std::cout << "[midi2uge debug] Offset 0x" << std::hex << std::setw(6) << std::setfill('0') << out.tellp() << ": " << name << std::dec << std::endl;
    };

    logSection("version");
    write_le(out, header.version);
    logSection("name");
    write_shortstring(out, header.name);
    logSection("artist");
    write_shortstring(out, header.artist);
    logSection("comment");
    write_shortstring(out, header.comment);
    logSection("duty instruments");
    // Duty instruments
    std::streampos offset_before_instruments = out.tellp();
    // Write duty instruments (15)
    for (const auto& inst : header.instruments.duty) {
        write_le(out, uint32_t(0)); // type
        write_shortstring(out, inst.name);
        write_le(out, uint32_t(0)); // length
        write_le(out, uint8_t(0)); // length_enabled
        write_le(out, uint8_t(15)); // initial_volume
        // QUESTION: Is 15 the best default for initial_volume, or should this be instrument-specific?
        write_le(out, uint32_t(0)); // volume_sweep_direction
        write_le(out, uint8_t(0)); // volume_sweep_amount
        write_le(out, uint32_t(0)); // frequency_sweep_time
        write_le(out, uint32_t(0)); // frequency_sweep_direction
        write_le(out, uint32_t(0)); // frequency_sweep_shift
        write_le(out, uint8_t(0)); // duty
        write_le(out, uint32_t(0)); // wave_output_level
        write_le(out, uint32_t(0)); // wave_waveform_index
        write_le(out, uint32_t(0)); // noise_counter_step
        write_le(out, uint8_t(0)); // subpattern_enabled
        // Always write the full subpattern block (64 x 17 bytes)
        // QUESTION: Is 64 always the correct number of rows for every pattern/instrument? Is this a UGE v6 spec?
        for (int i = 0; i < 64; ++i) {
            write_le(out, uint32_t(90)); // note
            // QUESTION: Is 90 always the correct value for unused note rows?
            write_le(out, uint32_t(0));  // unused
            write_le(out, uint32_t(0));  // jump
            write_le(out, uint32_t(0));  // effectcode
            write_le(out, uint8_t(0));   // effectparam
        }
    }
    logSection("wave instruments");
    for (const auto& inst : header.instruments.wave) {
        write_le(out, uint32_t(1));
        write_shortstring(out, inst.name);
        write_le(out, uint32_t(0));
        write_le(out, uint8_t(0));
        write_le(out, uint8_t(0));
        write_le(out, uint32_t(0));
        write_le(out, uint8_t(0));
        write_le(out, uint32_t(0));
        write_le(out, uint32_t(0));
        write_le(out, uint32_t(0));
        write_le(out, uint8_t(0));
        write_le(out, uint32_t(0));
        write_le(out, uint32_t(0));
        write_le(out, uint32_t(0));
        write_le(out, uint8_t(0));
        // Always write the full subpattern block
        for (int i = 0; i < 64; ++i) {
            write_le(out, uint32_t(90));
            write_le(out, uint32_t(0));
            write_le(out, uint32_t(0));
            write_le(out, uint32_t(0));
            write_le(out, uint8_t(0));
        }
    }
    logSection("noise instruments");
    for (const auto& inst : header.instruments.noise) {
        write_le(out, inst.type);
        write_shortstring(out, inst.name);
        write_le(out, inst.length);
        write_le(out, inst.length_enabled);
        write_le(out, inst.initial_volume);
        write_le(out, inst.volume_sweep_direction);
        write_le(out, inst.volume_sweep_change);
        write_le(out, inst.unused1);
        write_le(out, inst.unused2);
        write_le(out, inst.unused3);
        write_le(out, inst.unused4);
        write_le(out, inst.unused5);
        write_le(out, inst.unused6);
        write_le(out, inst.noise_mode);
        write_le(out, inst.subpattern_enabled);
        // Always write the full subpattern block
        for (int i = 0; i < 64; ++i) {
            write_le(out, uint32_t(90));
            write_le(out, uint32_t(0));
            write_le(out, uint32_t(0));
            write_le(out, uint32_t(0));
            write_le(out, uint8_t(0));
        }
    }
    logSection("wavetable");
    std::streampos offset_before_wavetable = out.tellp();
    for (const auto& wave : header.wavetable) {
        out.write(reinterpret_cast<const char*>(wave.data()), UGE_WAVETABLE_SIZE);
    }
    std::streampos offset_after_wavetable = out.tellp();
    std::cout << "[UGE DEBUG] Offset after wavetable: 0x" << std::hex << offset_after_wavetable << std::dec << std::endl;
    logSection("ticks_per_row");
    std::streampos offset_before_tempo = out.tellp();
    write_le(out, header.ticks_per_row);
    logSection("timer_enabled");
    out.put(header.timer_enabled);
    logSection("timer_divider");
    write_le(out, header.timer_divider);
    std::streampos offset_after_tempo = out.tellp();
    std::cout << "[UGE DEBUG] Offset before tempo fields: 0x" << std::hex << offset_before_tempo << ", after: 0x" << offset_after_tempo << std::dec << std::endl;
    logSection("patterns");
    // Write pattern section from input
    uint32_t num_patterns = patterns.size();
    std::cout << "[UGE DEBUG] Number of patterns: " << num_patterns << std::endl;
    write_le(out, num_patterns);
    for (const auto& pat : patterns) {
        write_le(out, pat.index);
        for (const auto& row : pat.rows) {
            write_le(out, uint32_t(row.note));
            write_le(out, uint32_t(row.instrument));
            write_le(out, uint32_t(0)); // unused
            write_le(out, uint32_t(row.effect));
            write_le(out, uint8_t(row.effect_param));
        }
    }
    logSection("order matrix");
    // Write order matrix from input
    for (int ch = 0; ch < UGE_NUM_CHANNELS; ++ch) {
        uint32_t len = orders[ch].size();
        write_le(out, len + 1); // off-by-one bug: write length+1
        // QUESTION: Why does the order matrix length field require +1? Is this a quirk of hUGETracker?
        for (uint32_t i = 0; i < len; ++i) {
            write_le(out, orders[ch][i]);
        }
        write_le(out, uint32_t(0)); // off-by-one bug filler
        // QUESTION: Is this final zero always required, even if not referenced?
    }
    logSection("routines");
    // Write routines section from input
    for (int i = 0; i < UGE_NUM_ROUTINES; ++i) {
        uint32_t len = (i < routines.size()) ? routines[i].size() : 0;
        std::cout << "[uge_writer] Routine " << i << " length: " << len << std::endl;
        write_le(out, len);
        if (len > 0) out.write(routines[i].data(), len);
        out.put(0x00); // Terminator
    }
    out.flush();
    // Pad file to 81254 bytes (reference length)
    std::streampos final_size = out.tellp();
    const size_t REF_SIZE = 81254;
    // QUESTION: Is 81254 bytes the canonical file size for minimal UGE files, or should this be dynamically determined?
    if (final_size < REF_SIZE) {
        std::vector<char> padding(REF_SIZE - final_size, 0);
        out.write(padding.data(), padding.size());
    }
    out.flush();
    return true;
} 