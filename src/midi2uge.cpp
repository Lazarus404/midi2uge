#include "midi2uge.h"
#include "uge_writer.h"
#include "MidiFile.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>
#include <unordered_map>
#include <optional>

constexpr int TICKS_PER_ROW = 6; // Set to 6 to match hUGETracker default
// QUESTION: Is 6 always the best default for TICKS_PER_ROW, or should this be user-configurable?
constexpr int UGE_EMPTY_NOTE = 90;

// Helper to zero-initialize all fields of an instrument
static void init_duty_instrument(UgeDutyInstrument& inst, const std::string& name, uint8_t initial_volume = 15, uint8_t sweep_amt = 7, int duty_idx = 0) {
    std::memset(&inst, 0, sizeof(UgeDutyInstrument));
    inst.type = 0;
    inst.name = make_shortstring(name);
    inst.length = 0;
    inst.length_enabled = 0;
    inst.initial_volume = initial_volume;
    inst.volume_sweep_direction = 1; // 1 = decrease (default)
    inst.volume_sweep_change = sweep_amt;
    inst.frequency_sweep_time = 0;
    inst.frequency_sweep_direction = 0;
    inst.frequency_sweep_shift = 0;
    inst.duty = duty_idx % 4; // Cycle through 0-3 for 12.5%, 25%, 50%, 75%
    inst.unused1 = 0;
    inst.unused2 = 0;
    inst.unused3 = 0;
    inst.subpattern_enabled = 0;
}
static void init_wave_instrument(UgeWaveInstrument& inst, const std::string& name, uint8_t initial_volume = 15, uint8_t sweep_amt = 7, int wave_idx = 0) {
    std::memset(&inst, 0, sizeof(UgeWaveInstrument));
    inst.type = 1;
    inst.name = make_shortstring(name);
    inst.length = 0;
    inst.length_enabled = 0;
    inst.unused1 = 0;
    inst.unused2 = 0;
    inst.unused3 = 0;
    inst.unused4 = 0;
    inst.unused5 = 0;
    inst.unused6 = 0;
    inst.unused7 = 0;
    inst.volume = initial_volume;
    inst.wave_index = wave_idx;
    inst.unused8 = 0;
    inst.unused9 = 0;
    inst.subpattern_enabled = 0;
}
static void init_noise_instrument(UgeNoiseInstrument& inst, const std::string& name, uint8_t initial_volume = 15, uint8_t sweep_amt = 7, uint8_t noise_mode = 0) {
    std::memset(&inst, 0, sizeof(UgeNoiseInstrument));
    inst.type = 2;
    inst.name = make_shortstring(name);
    inst.length = 0;
    inst.length_enabled = 0;
    inst.initial_volume = initial_volume;
    inst.volume_sweep_direction = 1; // 1 = decrease
    inst.volume_sweep_change = sweep_amt;
    inst.unused1 = 0;
    inst.unused2 = 0;
    inst.unused3 = 0;
    inst.unused4 = 0;
    inst.unused5 = 0;
    inst.unused6 = 0;
    inst.noise_mode = noise_mode; // 0 = 15-bit
    inst.subpattern_enabled = 0;
}

bool convertMidiToUge(const std::string& midiPath, const std::string& ugePath, std::optional<std::array<int, 4>> user_channel_map) {
    smf::MidiFile midi;
    if (!midi.read(midiPath)) {
        std::cerr << "Failed to read MIDI file: " << midiPath << std::endl;
        return false;
    }
    midi.joinTracks();
    midi.doTimeAnalysis();
    midi.linkNotePairs();
    int tpq = midi.getTicksPerQuarterNote();

    constexpr int UGE_NUM_CHANNELS = 4;
    constexpr int UGE_PATTERN_ROWS = 64;
    constexpr int UGE_NUM_DUTY = 15;
    constexpr int UGE_NUM_WAVE = 15;
    constexpr int UGE_NUM_NOISE = 15;

    UgeSongHeader header{};
    header.version = 6;
    // QUESTION: Is version 6 always correct for all UGE files, or should this be checked against the input or user-specified?
    header.name = make_shortstring("");
    header.artist = make_shortstring("");
    header.comment = make_shortstring("");

    // --- Instrument mapping ---
    std::map<int, int> midiProgToUgeInst; // MIDI program -> UGE Duty instrument (channels 0,1)
    std::map<int, int> midiProgToUgeWaveInst; // MIDI program -> UGE Wave instrument (channel 2)
    int nextUgeInst = 0;
    int nextUgeWaveInst = 0;
    std::array<int, UGE_NUM_CHANNELS> channelProgram;
    channelProgram.fill(0);
    std::array<std::vector<int>, UGE_NUM_CHANNELS> channel_instruments;
    std::array<std::vector<uint8_t>, UGE_NUM_CHANNELS> channel_notes;
    std::array<std::vector<uint8_t>, UGE_NUM_CHANNELS> channel_velocities;
    for (int ch = 0; ch < UGE_NUM_CHANNELS; ++ch) {
        channel_instruments[ch].clear();
        channel_notes[ch].clear();
        channel_velocities[ch].clear();
    }
    // Percussion mapping: MIDI note -> UGE Noise instrument
    std::map<int, int> percussionNoteToUgeInst;
    int nextNoiseInst = 0;

    // --- Velocity tracking ---
    std::map<int, int> progMaxVelocity; // MIDI program -> max velocity (Duty)
    std::map<int, int> waveProgMaxVelocity; // MIDI program -> max velocity (Wave)
    std::map<int, int> percMaxVelocity; // Perc note -> max velocity

    // --- Tempo handling ---
    // --- Extract MIDI tempo and set UGE timer fields ---
    int midi_tempo_us_per_qn = 500000; // default 120 BPM
    // QUESTION: Is 500000 (120 BPM) the best default if no tempo is found, or should we warn the user?
    for (int t = 0; t < midi.getTrackCount(); ++t) {
        for (int i = 0; i < midi[t].size(); ++i) {
            const auto& ev = midi[t][i];
        if (ev.isMeta() && ev.getMetaType() == 0x51) {
                midi_tempo_us_per_qn = (ev[3] << 16) | (ev[4] << 8) | ev[5];
                goto found_tempo;
            }
        }
    }
found_tempo:
    // --- User-configurable rows per quarter note ---
    int user_rows_per_qn = 4; // You can change this value for different musical feels
    int ticks_per_row = std::max(1, std::min(tpq / user_rows_per_qn, 16)); // Clamp to max 16
    // QUESTION: Why clamp ticks_per_row to 16? Is this a hUGETracker limit or just a safe default?
    int timer_enabled = 1;
    int timer_divider = 0;
    double rows_per_qn = static_cast<double>(tpq) / ticks_per_row;
    double row_rate = 1000000.0 / midi_tempo_us_per_qn * rows_per_qn;
    timer_divider = static_cast<int>(4194304.0 / (16.0 * row_rate) + 0.5);
    bool divider_clamped = false;
    if (timer_divider < 1) { timer_divider = 1; divider_clamped = true; }
    if (timer_divider > 255) { timer_divider = 255; divider_clamped = true; }
    // QUESTION: Are 1 and 255 the true hardware/driver limits for timer_divider, or just what hUGETracker expects?
    header.ticks_per_row = ticks_per_row;
    header.timer_enabled = timer_enabled;
    header.timer_divider = timer_divider;
    std::cout << "[UGE DEBUG] MIDI tempo: " << (60000000.0 / midi_tempo_us_per_qn) << " BPM, PPQN: " << tpq << ", ticks_per_row: " << ticks_per_row << ", row_rate: " << row_rate << ", UGE timer_divider: " << timer_divider << std::endl;
    if (divider_clamped) {
        std::cerr << "[UGE WARNING] Timer divider was clamped. Try reducing ticks_per_row or increasing rows_per_quarter_note for better tempo accuracy." << std::endl;
    }

    // Find max tick to determine song length
    int max_tick = 0;
    for (int i = 0; i < midi[0].size(); ++i) {
        max_tick = std::max(max_tick, midi[0][i].tick);
    }
    int total_rows = max_tick / TICKS_PER_ROW + 1;
    int num_patterns = (total_rows + UGE_PATTERN_ROWS - 1) / UGE_PATTERN_ROWS;

    // Pre-size channel note/instrument/velocity arrays
    for (int ch = 0; ch < UGE_NUM_CHANNELS; ++ch) {
        channel_notes[ch].resize(total_rows, UGE_EMPTY_NOTE);
        channel_instruments[ch].resize(total_rows, 0);
        channel_velocities[ch].resize(total_rows, 0);
    }

    // --- Flexible channel-to-UGE mapping ---
    // Count note-on events per MIDI channel (excluding percussion channel 9)
    std::array<int, 16> channel_note_counts = {0};
    for (int i = 0; i < midi[0].size(); ++i) {
        const auto& ev = midi[0][i];
        if (ev.isNoteOn() && ev.getVelocity() > 0) {
            int ch = ev.getChannel();
            if (ch >= 0 && ch < 16 && ch != 9) channel_note_counts[ch]++;
        }
    }
    // --- Print note-on event count for all MIDI channels ---
    std::cout << "[UGE DEBUG] Note-on event count per MIDI channel:" << std::endl;
    for (int ch = 0; ch < 16; ++ch) {
        std::cout << "  MIDI channel " << ch << ": " << channel_note_counts[ch] << " note-on events" << std::endl;
    }
    // Find the three most active melodic channels
    std::vector<std::pair<int, int>> channel_activity;
    for (int ch = 0; ch < 16; ++ch) {
        if (ch != 9) channel_activity.push_back({channel_note_counts[ch], ch});
    }
    std::sort(channel_activity.rbegin(), channel_activity.rend());
    std::array<int, 4> midi_to_uge;
    if (user_channel_map && user_channel_map->size() == 4) {
        // Use user-supplied mapping
        for (int i = 0; i < 4; ++i) {
            midi_to_uge[i] = (*user_channel_map)[i];
        }
        std::cout << "[UGE DEBUG] Using user-supplied MIDI channel mapping:" << std::endl;
        for (int i = 0; i < 4; ++i) {
            if (midi_to_uge[i] >= 0 && midi_to_uge[i] < 16) {
                std::cout << "  UGE " << (i == 0 ? "Duty1" : (i == 1 ? "Duty2" : (i == 2 ? "Wave" : "Noise"))) << " <= MIDI channel " << midi_to_uge[i] << std::endl;
            } else {
                std::cout << "  UGE " << (i == 0 ? "Duty1" : (i == 1 ? "Duty2" : (i == 2 ? "Wave" : "Noise"))) << " <= (empty)" << std::endl;
            }
        }
    } else {
        // Auto-mapping (current logic)
        // Find the three most active melodic channels
        std::vector<std::pair<int, int>> channel_activity;
        for (int ch = 0; ch < 16; ++ch) {
            if (ch != 9) channel_activity.push_back({channel_note_counts[ch], ch});
        }
        std::sort(channel_activity.rbegin(), channel_activity.rend());
        for (int i = 0; i < 3; ++i) midi_to_uge[i] = channel_activity[i].second;
        midi_to_uge[3] = 9; // Noise always maps to MIDI channel 9
        std::cout << "[UGE DEBUG] MIDI channel to UGE channel mapping (auto):" << std::endl;
        for (int i = 0; i < 4; ++i) {
            if (i < 3)
                std::cout << "  UGE " << (i == 0 ? "Duty1" : (i == 1 ? "Duty2" : "Wave")) << " <= MIDI channel " << midi_to_uge[i] << std::endl;
            else
                std::cout << "  UGE Noise <= MIDI channel 9" << std::endl;
        }
    }
    // --- Note-on/off handling with velocity tracking and correct note lifetimes ---
    std::array<std::map<int, std::tuple<int, int, int>>, UGE_NUM_CHANNELS> active_notes; // note -> (start_row, inst, velocity)

    // Fill all channels with empty notes by default
    for (int ch = 0; ch < UGE_NUM_CHANNELS; ++ch) {
        for (int row = 0; row < total_rows; ++row) {
            channel_notes[ch][row] = UGE_EMPTY_NOTE;
            channel_instruments[ch][row] = 0;
            channel_velocities[ch][row] = 0;
        }
    }
    // Only process events for mapped channels
    for (int i = 0; i < midi[0].size(); ++i) {
        const auto& ev = midi[0][i];
        int tick = ev.tick;
        int row = tick / TICKS_PER_ROW;
        if (row >= total_rows) continue;
        int channel = ev.getChannel();
        if (channel < 0 || channel > 15) continue;
        // For each UGE channel, check if this MIDI channel is mapped
        for (int uge_ch = 0; uge_ch < UGE_NUM_CHANNELS; ++uge_ch) {
            int mapped_midi_ch = midi_to_uge[uge_ch];
            if (mapped_midi_ch < 0 || mapped_midi_ch > 15) continue; // skip unmapped
            if (channel != mapped_midi_ch) continue;
            // --- Begin original note-on/note-off/instrument/velocity logic ---
            if (uge_ch == 3) { // Noise
            if (ev.isNoteOn() && ev.getVelocity() > 0) {
                int note = ev.getKeyNumber();
                int velocity = ev.getVelocity();
                if (percussionNoteToUgeInst.count(note) == 0 && nextNoiseInst < UGE_NUM_NOISE) {
                    percussionNoteToUgeInst[note] = nextNoiseInst++;
                }
                int ugeInst = percussionNoteToUgeInst.count(note) ? percussionNoteToUgeInst[note] : 0;
                // For percussion, treat as one-row hit (clear on next row)
                    channel_notes[uge_ch][row] = note;
                    channel_instruments[uge_ch][row] = ugeInst;
                    channel_velocities[uge_ch][row] = velocity;
                if (percMaxVelocity[note] < velocity) percMaxVelocity[note] = velocity;
                if (row + 1 < total_rows) {
                        channel_notes[uge_ch][row + 1] = UGE_EMPTY_NOTE;
                        channel_instruments[uge_ch][row + 1] = 0;
                        channel_velocities[uge_ch][row + 1] = 0;
                }
            }
            } else { // Melodic
            if (ev.isTimbre()) {
                int prog = ev.getP1();
                channelProgram[channel] = prog;
                    if (uge_ch == 2) { // Wave
                if (midiProgToUgeWaveInst.count(prog) == 0 && nextUgeWaveInst < UGE_NUM_WAVE) {
                    midiProgToUgeWaveInst[prog] = nextUgeWaveInst++;
                        }
                    } else { // Duty
                        if (midiProgToUgeInst.count(prog) == 0 && nextUgeInst < UGE_NUM_DUTY) {
                            midiProgToUgeInst[prog] = nextUgeInst++;
                        }
                }
            } else if (ev.isNoteOn() && ev.getVelocity() > 0) {
                int note = ev.getKeyNumber();
                int velocity = ev.getVelocity();
                int prog = channelProgram[channel];
                    int ugeInst = 0;
                    if (uge_ch == 2) { // Wave
                if (midiProgToUgeWaveInst.count(prog) == 0 && nextUgeWaveInst < UGE_NUM_WAVE) {
                    midiProgToUgeWaveInst[prog] = nextUgeWaveInst++;
                }
                        ugeInst = midiProgToUgeWaveInst.count(prog) ? midiProgToUgeWaveInst[prog] : 0;
                if (waveProgMaxVelocity[prog] < velocity) waveProgMaxVelocity[prog] = velocity;
                    } else { // Duty
                if (midiProgToUgeInst.count(prog) == 0 && nextUgeInst < UGE_NUM_DUTY) {
                    midiProgToUgeInst[prog] = nextUgeInst++;
                }
                        ugeInst = midiProgToUgeInst.count(prog) ? midiProgToUgeInst[prog] : 0;
                        if (progMaxVelocity[prog] < velocity) progMaxVelocity[prog] = velocity;
                    }
                // Record note start
                    active_notes[uge_ch][note] = std::make_tuple(row, ugeInst, velocity);
            } else if (ev.isNoteOff() || (ev.isNoteOn() && ev.getVelocity() == 0)) {
                int note = ev.getKeyNumber();
                    auto it = active_notes[uge_ch].find(note);
                    if (it != active_notes[uge_ch].end()) {
                    int start_row = std::get<0>(it->second);
                    int ugeInst = std::get<1>(it->second);
                    int velocity = std::get<2>(it->second);
                    int off_row = row;
                    // Fill all rows from start_row to off_row-1
                    for (int r = start_row; r < off_row && r < total_rows; ++r) {
                            channel_notes[uge_ch][r] = note;
                            channel_instruments[uge_ch][r] = ugeInst;
                            channel_velocities[uge_ch][r] = velocity;
                    }
                    // Clear the note at the off_row
                    if (off_row < total_rows) {
                            channel_notes[uge_ch][off_row] = UGE_EMPTY_NOTE;
                            channel_instruments[uge_ch][off_row] = 0;
                            channel_velocities[uge_ch][off_row] = 0;
                    }
                        active_notes[uge_ch].erase(it);
                    }
                }
            }
            // --- End original note-on/note-off/instrument/velocity logic ---
        }
    }
    // Add debug output for which channels are filled
    for (int uge_ch = 0; uge_ch < UGE_NUM_CHANNELS; ++uge_ch) {
        int mapped_midi_ch = midi_to_uge[uge_ch];
        if (mapped_midi_ch < 0 || mapped_midi_ch > 15) {
            std::cout << "[UGE DEBUG] UGE channel " << uge_ch << " is empty (no MIDI mapping)" << std::endl;
        } else {
            std::cout << "[UGE DEBUG] UGE channel " << uge_ch << " mapped to MIDI channel " << mapped_midi_ch << std::endl;
        }
    }

    // --- Polyphony handling ---
    // For each tick/row, gather all active notes for each MIDI channel
    // and assign up to 3 notes to UGE channels 0, 1, 2 (Duty 1, Duty 2, Wave)
    // For percussion, pick the highest velocity note per row
    //
    // We'll build a new set of arrays: uge_notes, uge_instruments, uge_velocities
    std::array<std::vector<uint8_t>, UGE_NUM_CHANNELS> uge_notes;
    std::array<std::vector<int>, UGE_NUM_CHANNELS> uge_instruments;
    std::array<std::vector<uint8_t>, UGE_NUM_CHANNELS> uge_velocities;
    for (int ch = 0; ch < UGE_NUM_CHANNELS; ++ch) {
        uge_notes[ch].resize(total_rows, UGE_EMPTY_NOTE);
        uge_instruments[ch].resize(total_rows, 0);
        uge_velocities[ch].resize(total_rows, 0);
    }
    // --- Strict channel mapping and debug output ---
    // For each row, assign MIDI channel 0 to UGE 0, 1 to 1, 2 to 2
    for (int row = 0; row < total_rows; ++row) {
        for (int ch = 0; ch < 3; ++ch) { // Duty 1, Duty 2, Wave
            uge_notes[ch][row] = channel_notes[ch][row];
            uge_instruments[ch][row] = channel_instruments[ch][row];
            uge_velocities[ch][row] = channel_velocities[ch][row];
        }
        uge_notes[3][row] = channel_notes[3][row];
        uge_instruments[3][row] = channel_instruments[3][row];
        uge_velocities[3][row] = channel_velocities[3][row];
    }
    // --- Find first non-empty row ---
    int first_nonempty_row = total_rows;
    for (int row = 0; row < total_rows; ++row) {
        for (int ch = 0; ch < UGE_NUM_CHANNELS; ++ch) {
            if (uge_notes[ch][row] != UGE_EMPTY_NOTE) {
                first_nonempty_row = row;
                goto found_first;
            }
        }
    }
found_first:;
    int first_nonempty_page = first_nonempty_row / UGE_PATTERN_ROWS;
    // --- Warn if no notes on channels 0,1,2 ---
    bool has_duty_wave = false;
    for (int ch = 0; ch < 3; ++ch) {
        for (int row = 0; row < total_rows; ++row) {
            if (uge_notes[ch][row] != UGE_EMPTY_NOTE) {
                has_duty_wave = true;
                break;
            }
        }
    }
    if (!has_duty_wave) {
        std::cerr << "[UGE WARNING] No notes found on MIDI channels 0, 1, or 2 (Duty/Wave). Only Noise channel will be populated." << std::endl;
    }
    // --- Debug: print mapping for first 16 non-empty rows ---
    std::cout << "[UGE DEBUG] Row | Duty1 (note,inst) | Duty2 (note,inst) | Wave (note,inst) | Noise (note,inst)" << std::endl;
    int debug_rows_printed = 0;
    for (int row = first_nonempty_row; row < total_rows && debug_rows_printed < 16; ++row, ++debug_rows_printed) {
        std::cout << "[UGE DEBUG] " << row << " | ";
        for (int ch = 0; ch < 4; ++ch) {
            if (uge_notes[ch][row] != UGE_EMPTY_NOTE)
                std::cout << (int)uge_notes[ch][row] << "," << uge_instruments[ch][row];
            else
                std::cout << "--,--";
            if (ch < 3) std::cout << " | ";
        }
        std::cout << std::endl;
    }
    // Use uge_notes, uge_instruments, uge_velocities for pattern writing below

    // --- Debug: print first 16 rows of channel_notes for mapped channels ---
    std::cout << "[UGE DEBUG] First 16 rows of channel_notes for mapped UGE channels:" << std::endl;
    for (int row = 0; row < std::min(16, total_rows); ++row) {
        std::cout << "Row " << row << ": ";
        for (int ch = 0; ch < 3; ++ch) {
            std::cout << "Ch" << ch << " (MIDI " << midi_to_uge[ch] << ") note=" << (int)channel_notes[ch][row]
                      << ", inst=" << channel_instruments[ch][row]
                      << ", vel=" << (int)channel_velocities[ch][row] << " | ";
        }
        std::cout << std::endl;
    }
    // --- Track note lengths for each instrument ---
    std::unordered_map<int, std::vector<int>> progNoteLengths; // MIDI program -> vector of note lengths (Duty/Wave)
    std::unordered_map<int, std::vector<int>> percNoteLengths; // Perc note -> vector of note lengths
    // For each channel, track note-on row for each note
    std::array<std::unordered_map<int, int>, UGE_NUM_CHANNELS> noteOnRow;
    for (int i = 0; i < midi[0].size(); ++i) {
        const auto& ev = midi[0][i];
        int tick = ev.tick;
        int row = tick / TICKS_PER_ROW;
        if (row >= total_rows) continue;
        int channel = ev.getChannel();
        if (channel < 0 || channel > 15) continue;
        if (channel == 9) { // Percussion/Noise
            if (ev.isNoteOn() && ev.getVelocity() > 0) {
                int note = ev.getKeyNumber();
                noteOnRow[3][note] = row;
            } else if (ev.isNoteOff() || (ev.isNoteOn() && ev.getVelocity() == 0)) {
                int note = ev.getKeyNumber();
                auto it = noteOnRow[3].find(note);
                if (it != noteOnRow[3].end()) {
                    int start_row = it->second;
                    int len = row - start_row;
                    if (len > 0) percNoteLengths[note].push_back(len);
                    noteOnRow[3].erase(it);
                }
            }
        } else if (channel == 2) { // Wave
            if (ev.isTimbre()) {
                // handled elsewhere
            } else if (ev.isNoteOn() && ev.getVelocity() > 0) {
                int note = ev.getKeyNumber();
                int prog = channelProgram[channel];
                noteOnRow[2][note] = row;
            } else if (ev.isNoteOff() || (ev.isNoteOn() && ev.getVelocity() == 0)) {
                int note = ev.getKeyNumber();
                int prog = channelProgram[channel];
                auto it = noteOnRow[2].find(note);
                if (it != noteOnRow[2].end()) {
                    int start_row = it->second;
                    int len = row - start_row;
                    if (len > 0) progNoteLengths[prog].push_back(len);
                    noteOnRow[2].erase(it);
                }
            }
        } else if (channel >= 0 && channel < 2) { // Duty
            if (ev.isTimbre()) {
                // handled elsewhere
            } else if (ev.isNoteOn() && ev.getVelocity() > 0) {
                int note = ev.getKeyNumber();
                int prog = channelProgram[channel];
                noteOnRow[channel][note] = row;
            } else if (ev.isNoteOff() || (ev.isNoteOn() && ev.getVelocity() == 0)) {
        int note = ev.getKeyNumber();
                int prog = channelProgram[channel];
                auto it = noteOnRow[channel].find(note);
                if (it != noteOnRow[channel].end()) {
                    int start_row = it->second;
                    int len = row - start_row;
                    if (len > 0) progNoteLengths[prog].push_back(len);
                    noteOnRow[channel].erase(it);
                }
            }
        }
    }
    // --- Compute average note length for each instrument ---
    std::unordered_map<int, int> progAvgLen;
    for (const auto& kv : progNoteLengths) {
        int sum = 0;
        for (int l : kv.second) sum += l;
        progAvgLen[kv.first] = kv.second.empty() ? 0 : sum / (int)kv.second.size();
    }
    std::unordered_map<int, int> percAvgLen;
    for (const auto& kv : percNoteLengths) {
        int sum = 0;
        for (int l : kv.second) sum += l;
        percAvgLen[kv.first] = kv.second.empty() ? 0 : sum / (int)kv.second.size();
    }
    // --- Map average note length to envelope sweep amount ---
    auto lenToSweep = [](int len) {
        if (len <= 2) return 7; // short
        if (len <= 8) return 4; // medium
        return 1; // long
    };
    // --- Map percussion note to noise mode ---
    auto noteToNoiseMode = [](int note) {
        // Hi-hats/cymbals (per GM spec)
        if (note == 42 || note == 44 || note == 46 || note == 49 || note == 51 || note == 52 || note == 55 || note == 57 || note == 59) return 1;
        return 0;
    };
    // --- Update instrument initialization to use these mappings ---
    // Build reverse maps: UGE instrument index -> MIDI program/note
    std::map<int, int> ugeInstToProg;
    for (const auto& kv : midiProgToUgeInst) {
        ugeInstToProg[kv.second] = kv.first;
    }
    std::map<int, int> ugeWaveInstToProg;
    for (const auto& kv : midiProgToUgeWaveInst) {
        ugeWaveInstToProg[kv.second] = kv.first;
    }
    std::map<int, int> ugeNoiseInstToNote;
    for (const auto& kv : percussionNoteToUgeInst) {
        ugeNoiseInstToNote[kv.second] = kv.first;
    }
    // Duty instruments
    for (int i = 0; i < UGE_NUM_DUTY; ++i) {
        if (ugeInstToProg.count(i)) {
            int prog = ugeInstToProg[i];
            std::string name = "MIDI Prog " + std::to_string(prog);
            uint8_t vol = 15;
            int sweep_amt = 4; // moderate decay by default
            int duty_val = i % 4;
            int sweep_dir = 1; // fade out
            int len_enabled = 0;
            int len = 0;
            if (progMaxVelocity.count(prog))
                vol = std::max(1, std::min(15, (progMaxVelocity[prog] * 15 + 63) / 127));
            if (progAvgLen.count(prog) && progAvgLen[prog] > 0) {
                sweep_amt = lenToSweep(progAvgLen[prog]);
                len_enabled = 1;
                len = progAvgLen[prog] * TICKS_PER_ROW;
            }
            UgeDutyInstrument& inst = header.instruments.duty[i];
            init_duty_instrument(inst, name, vol, sweep_amt, duty_val);
            inst.volume_sweep_direction = sweep_dir;
            inst.length_enabled = len_enabled;
            inst.length = len;
        } else {
            std::string name = "(unused)";
            int duty_val = i % 4;
            init_duty_instrument(header.instruments.duty[i], name, 15, 4, duty_val);
            header.instruments.duty[i].volume_sweep_direction = 1;
        }
    }
    // Wave instruments
    for (int i = 0; i < UGE_NUM_WAVE; ++i) {
        if (ugeWaveInstToProg.count(i)) {
            int prog = ugeWaveInstToProg[i];
            std::string name = "MIDI Prog " + std::to_string(prog);
            uint8_t vol = 15;
            int sweep_amt = 4;
            int wave_idx = 0;
            if (waveProgMaxVelocity.count(prog))
                vol = std::max(1, std::min(15, (waveProgMaxVelocity[prog] * 15 + 63) / 127));
            if (progAvgLen.count(prog) && progAvgLen[prog] > 0) {
                sweep_amt = lenToSweep(progAvgLen[prog]);
                // Set length based on MIDI note length
                header.instruments.wave[i].length_enabled = 1;
                header.instruments.wave[i].length = progAvgLen[prog] * TICKS_PER_ROW;
            } else {
                header.instruments.wave[i].length_enabled = 0;
                header.instruments.wave[i].length = 0;
            }
            UgeWaveInstrument& inst = header.instruments.wave[i];
            init_wave_instrument(inst, name, vol, sweep_amt, wave_idx);
            // No volume_sweep_direction, length_enabled, or length for UgeWaveInstrument
        } else {
            std::string name = "(unused)";
            init_wave_instrument(header.instruments.wave[i], name, 15, 4, 0);
        }
    }
    // Noise instruments
    for (int i = 0; i < UGE_NUM_NOISE; ++i) {
        if (ugeNoiseInstToNote.count(i)) {
            int note = ugeNoiseInstToNote[i];
            std::string name = "Perc Note " + std::to_string(note);
            uint8_t vol = 15;
            int sweep_amt = 4;
            int noise_mode = noteToNoiseMode(note);
            int sweep_dir = 1;
            int len_enabled = 0;
            int len = 0;
            if (percMaxVelocity.count(note))
                vol = std::max(1, std::min(15, (percMaxVelocity[note] * 15 + 63) / 127));
            if (percAvgLen.count(note) && percAvgLen[note] > 0) {
                sweep_amt = lenToSweep(percAvgLen[note]);
                len_enabled = 1;
                len = percAvgLen[note] * TICKS_PER_ROW;
            }
            UgeNoiseInstrument& inst = header.instruments.noise[i];
            init_noise_instrument(inst, name, vol, sweep_amt, noise_mode);
            inst.volume_sweep_direction = sweep_dir;
            inst.length_enabled = len_enabled;
            inst.length = len;
        } else {
            std::string name = "(unused)";
            init_noise_instrument(header.instruments.noise[i], name, 15, 4, 0);
            header.instruments.noise[i].volume_sweep_direction = 1;
        }
    }
    for (auto& wave : header.wavetable) wave.fill(0);

    // --- Automatic truncation to fit UGE/hUGETracker limits ---
    constexpr int MAX_PATTERNS_PER_CHANNEL = 256;
    constexpr int MAX_PATTERN_DATA_BYTES = 0x4000; // 16KB
    // QUESTION: Are these limits (256 patterns, 16KB) strictly enforced by hUGETracker, or can they be relaxed for custom tools?
    int max_patterns = num_patterns;
    // Truncate by pattern count if needed
    if (num_patterns > MAX_PATTERNS_PER_CHANNEL) {
        std::cerr << "[UGE WARNING] Song too long: truncating to " << MAX_PATTERNS_PER_CHANNEL << " patterns per channel (" << (MAX_PATTERNS_PER_CHANNEL * UGE_PATTERN_ROWS) << " rows)." << std::endl;
        max_patterns = MAX_PATTERNS_PER_CHANNEL;
    }
    // Estimate max patterns by data size
    int max_patterns_by_size = MAX_PATTERN_DATA_BYTES / (UGE_PATTERN_ROWS * sizeof(UgePatternRow) + sizeof(uint32_t));
    if (max_patterns > max_patterns_by_size) {
        std::cerr << "[UGE WARNING] Song data too large: truncating to " << max_patterns_by_size << " patterns per channel to fit 16KB limit." << std::endl;
        max_patterns = max_patterns_by_size;
    }
    // --- Patterns: skip initial empty pages, assign new sequential indices with deduplication ---
    std::vector<UgePattern> patterns;
    UgeOrderMatrix orders;
    int start_pattern = first_nonempty_page;
    int new_pattern_idx = 0;
    // Map: (channel, pattern hash) -> pattern index
    std::unordered_map<size_t, int> pattern_hash_to_index[UGE_NUM_CHANNELS];
    std::hash<std::string> hasher;
    for (int ch = 0; ch < UGE_NUM_CHANNELS; ++ch) {
        orders[ch].clear();
        for (int pat = start_pattern; pat < num_patterns; ++pat) {
            // Build pattern data string for hashing
            std::string pat_data;
            for (int row = 0; row < UGE_PATTERN_ROWS; ++row) {
                int song_row = pat * UGE_PATTERN_ROWS + row;
                uint8_t note = (song_row < total_rows) ? uge_notes[ch][song_row] : UGE_EMPTY_NOTE;
                uint8_t inst = (song_row < total_rows) ? uge_instruments[ch][song_row] : 0;
                uint8_t eff = 0;
                uint8_t eff_param = 0;
                pat_data.push_back(note);
                pat_data.push_back(inst);
                pat_data.push_back(eff);
                pat_data.push_back(eff_param);
            }
            size_t hash = hasher(pat_data);
            auto it = pattern_hash_to_index[ch].find(hash);
            int pat_idx;
            if (it != pattern_hash_to_index[ch].end()) {
                pat_idx = it->second; // Reuse existing pattern
                    } else {
                UgePattern p{};
                p.index = new_pattern_idx;
                for (int row = 0; row < UGE_PATTERN_ROWS; ++row) {
                    int song_row = pat * UGE_PATTERN_ROWS + row;
                    p.rows[row].note = (song_row < total_rows) ? uge_notes[ch][song_row] : UGE_EMPTY_NOTE;
                    p.rows[row].instrument = (song_row < total_rows) ? uge_instruments[ch][song_row] : 0;
                    p.rows[row].unused1 = 0;
                    p.rows[row].effect = 0;
                    p.rows[row].effect_param = 0;
            }
            patterns.push_back(p);
                pat_idx = new_pattern_idx;
                pattern_hash_to_index[ch][hash] = new_pattern_idx;
                ++new_pattern_idx;
            }
            orders[ch].push_back(pat_idx);
        }
    }

    // Routines: empty
    UgeRoutineBank routines;
    for (auto& r : routines) r = "";

    if (!writeUgeFile(ugePath, header, patterns, orders, routines)) {
        std::cerr << "Failed to write UGE file: " << ugePath << std::endl;
        return false;
    }
    // Debug: print all fields of each Noise instrument
    std::cout << "[UGE DEBUG] Noise instrument fields:" << std::endl;
    for (int i = 0; i < UGE_NUM_NOISE; ++i) {
        const auto& inst = header.instruments.noise[i];
        std::cout << "[UGE DEBUG] NoiseInst " << i << ": name='" << std::string(inst.name.data, inst.name.length) << "'"
                  << ", initial_volume=" << (int)inst.initial_volume
                  << ", sweep_dir=" << inst.volume_sweep_direction
                  << ", sweep_amt=" << (int)inst.volume_sweep_change
                  << ", noise_mode=" << inst.noise_mode
                  << ", length_enabled=" << (int)inst.length_enabled
                  << ", subpattern_enabled=" << (int)inst.subpattern_enabled
                  << std::endl;
    }
    // --- Debug: print first 16 rows of uge_notes after pattern filling ---
    std::cout << "[UGE DEBUG] First 16 rows of uge_notes after pattern filling:" << std::endl;
    for (int row = 0; row < std::min(16, total_rows); ++row) {
        std::cout << "Row " << row << ": ";
        for (int ch = 0; ch < 3; ++ch) {
            std::cout << "Ch" << ch << " note=" << (int)uge_notes[ch][row]
                      << ", inst=" << uge_instruments[ch][row]
                      << ", vel=" << (int)uge_velocities[ch][row] << " | ";
        }
        std::cout << std::endl;
    }
    // --- Debug: print first non-empty row for each mapped UGE channel ---
    for (int ch = 0; ch < 3; ++ch) {
        int first_row = -1;
        for (int row = 0; row < total_rows; ++row) {
            if (uge_notes[ch][row] != UGE_EMPTY_NOTE) {
                first_row = row;
                break;
            }
        }
        if (first_row != -1) {
            std::cout << "[UGE DEBUG] First non-empty row for UGE channel " << ch << " (MIDI " << midi_to_uge[ch] << "): row " << first_row
                      << ", note=" << (int)uge_notes[ch][first_row]
                      << ", inst=" << uge_instruments[ch][first_row]
                      << ", vel=" << (int)uge_velocities[ch][first_row] << std::endl;
        } else {
            std::cout << "[UGE DEBUG] No notes found for UGE channel " << ch << " (MIDI " << midi_to_uge[ch] << ")" << std::endl;
        }
    }
    return true;
} 