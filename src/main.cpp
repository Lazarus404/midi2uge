#include "midi2uge.h"
#include <iostream>
#include <string>
#include <fstream>
#include <iomanip>
#include "nlohmann_json.hpp"
#include "uge_writer.h"
#include "third_party/midifile/include/MidiFile.h"
#include <map>
#include <vector>
#include <sstream>
#include <optional>
#include <array>

using json = nlohmann::json;

// Helper to read little-endian values (for uge2json logic)
uint32_t read_u32(std::ifstream& in) {
    uint8_t b[4]; in.read((char*)b, 4);
    return b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24);
}
uint8_t read_u8(std::ifstream& in) {
    uint8_t b; in.read((char*)&b, 1); return b;
}
std::string read_shortstring(std::ifstream& in) {
    uint8_t len = read_u8(in);
    char buf[255]; in.read(buf, 255);
    return std::string(buf, buf + len);
}

json parse_uge(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open file");
    json root;
    // Header
    root["header"]["version"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
    root["header"]["name"] = { {"size", 256}, {"type", "shortstring"}, {"value", read_shortstring(in)} };
    root["header"]["artist"] = { {"size", 256}, {"type", "shortstring"}, {"value", read_shortstring(in)} };
    root["header"]["comment"] = { {"size", 256}, {"type", "shortstring"}, {"value", read_shortstring(in)} };
    // Duty instruments
    json duty_arr = json::array();
    for (int i = 0; i < 15; ++i) {
        json inst;
        inst["type"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["name"] = { {"size", 256}, {"type", "shortstring"}, {"value", read_shortstring(in)} };
        inst["length"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["length_enabled"] = { {"size", 1}, {"type", "uint8"}, {"value", read_u8(in)} };
        inst["initial_volume"] = { {"size", 1}, {"type", "uint8"}, {"value", read_u8(in)} };
        inst["volume_sweep_direction"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["volume_sweep_change"] = { {"size", 1}, {"type", "uint8"}, {"value", read_u8(in)} };
        inst["frequency_sweep_time"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["frequency_sweep_direction"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["frequency_sweep_shift"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["duty"] = { {"size", 1}, {"type", "uint8"}, {"value", read_u8(in)} };
        inst["unused1"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["unused2"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["unused3"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["subpattern_enabled"] = { {"size", 1}, {"type", "uint8"}, {"value", read_u8(in)} };
        in.seekg(64 * 17, std::ios::cur);
        duty_arr.push_back(inst);
    }
    root["duty_instruments"] = duty_arr;
    // TODO: Parse wave, noise, wavetable, patterns, orders, routines
    return root;
}

json midi_to_json(const std::string& midi_path) {
    smf::MidiFile midi;
    if (!midi.read(midi_path)) {
        throw std::runtime_error("Failed to read MIDI file: " + midi_path);
    }
    midi.doTimeAnalysis();
    midi.linkNotePairs();
    json j;
    j["header"]["format"] = (midi.getTrackCount() == 1 ? 0 : 1);
    j["header"]["tracks"] = midi.getTrackCount();
    j["header"]["ticks_per_quarter"] = midi.getTicksPerQuarterNote();
    for (int t = 0; t < midi.getTrackCount(); ++t) {
        json track_events = json::array();
        for (int e = 0; e < midi[t].size(); ++e) {
            const auto& ev = midi[t][e];
            json jev;
            jev["tick"] = ev.tick;
            if (ev.isNoteOn()) {
                jev["type"] = "note_on";
                jev["channel"] = ev.getChannel();
                jev["note"] = ev.getKeyNumber();
                jev["velocity"] = ev.getVelocity();
            } else if (ev.isNoteOff()) {
                jev["type"] = "note_off";
                jev["channel"] = ev.getChannel();
                jev["note"] = ev.getKeyNumber();
                jev["velocity"] = ev.getVelocity();
            } else if (ev.isTimbre()) {
                jev["type"] = "program_change";
                jev["channel"] = ev.getChannel();
                jev["program"] = ev.getP1();
            } else if (ev.isMeta()) {
                jev["type"] = "meta";
                jev["meta_type"] = ev.getMetaType();
                if (ev.getMetaType() == 0x03) {
                    jev["text"] = ev.getMetaContent();
                }
                if (ev.getMetaType() == 0x51) {
                    jev["tempo_us_per_quarter"] = ev.getTempoMicro();
                }
            } else {
                jev["type"] = "other";
            }
            track_events.push_back(jev);
        }
        j["tracks"][t] = track_events;
    }
    std::map<int, std::vector<json>> program_notes;
    std::vector<json> percussion_notes;
    for (int t = 0; t < midi.getTrackCount(); ++t) {
        for (int e = 0; e < midi[t].size(); ++e) {
            const auto& ev = midi[t][e];
            if (ev.isNoteOn()) {
                int ch = ev.getChannel();
                int note = ev.getKeyNumber();
                int vel = ev.getVelocity();
                int start_tick = ev.tick;
                int end_tick = -1;
                for (int f = e + 1; f < midi[t].size(); ++f) {
                    const auto& ev2 = midi[t][f];
                    if ((ev2.isNoteOff() || (ev2.isNoteOn() && ev2.getVelocity() == 0)) && ev2.getKeyNumber() == note && ev2.getChannel() == ch) {
                        end_tick = ev2.tick;
                        break;
                    }
                }
                json note_obj = {
                    {"note", note},
                    {"start_tick", start_tick},
                    {"end_tick", end_tick},
                    {"velocity", vel},
                    {"track", t},
                    {"channel", ch}
                };
                if (ch == 9) {
                    percussion_notes.push_back(note_obj);
                } else {
                    int prog = 0;
                    for (int f = e; f >= 0; --f) {
                        const auto& ev2 = midi[t][f];
                        if (ev2.isTimbre() && ev2.getChannel() == ch) {
                            prog = ev2.getP1();
                            break;
                        }
                    }
                    program_notes[prog].push_back(note_obj);
                }
            }
        }
    }
    for (const auto& kv : program_notes) {
        j["programs"][kv.first] = kv.second;
    }
    j["percussion"] = percussion_notes;
    return j;
}

int main(int argc, char* argv[]) {
    std::string midiPath, ugePath;
    std::optional<std::array<int, 4>> user_channel_map = std::nullopt;
    // Parse flags
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-i" || arg == "--input") && i+1 < argc) {
            midiPath = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i+1 < argc) {
            ugePath = argv[++i];
        } else if ((arg == "-m" || arg == "--map") && i+1 < argc) {
            std::string map_arg = argv[++i];
            std::array<int, 4> mapping = {-1, -1, -1, -1};
            std::stringstream ss(map_arg);
            std::string item;
            int idx = 0;
            while (std::getline(ss, item, ',') && idx < 4) {
                try {
                    mapping[idx] = std::stoi(item);
                } catch (...) {
                    mapping[idx] = -1;
                }
                ++idx;
            }
            user_channel_map = mapping;
        }
    }
    // Fallback to positional arguments for backward compatibility
    if (midiPath.empty() && ugePath.empty() && argc == 3) {
        midiPath = argv[1];
        ugePath = argv[2];
    }
    // If input is .mid and output is .json or missing, call midi_to_json
    if (!midiPath.empty() && midiPath.size() >= 4 && midiPath.substr(midiPath.size() - 4) == ".mid" &&
        (ugePath.empty() || (ugePath.size() >= 5 && ugePath.substr(ugePath.size() - 5) == ".json"))) {
        std::string outpath = ugePath.empty() ? midiPath + ".json" : ugePath;
        try {
            json j = midi_to_json(midiPath);
            std::ofstream ofs(outpath);
            ofs << j.dump(2);
            std::cout << "Wrote " << outpath << std::endl;
        } catch (const std::exception& e) {
            std::cerr << e.what() << std::endl;
            return 1;
        }
        return 0;
    }
    // UGE to JSON mode
    if (!midiPath.empty() && midiPath.size() > 4 && midiPath.substr(midiPath.size()-4) == ".uge") {
        std::string outPath = ugePath;
        if (ugePath.empty()) outPath = midiPath + ".json";
        if (!outPath.empty() && outPath.size() > 5 && outPath.substr(outPath.size()-5) == ".json") {
            try {
                json j = parse_uge(midiPath);
                std::ofstream out(outPath);
                out << std::setw(2) << j << std::endl;
                std::cout << "Wrote " << outPath << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
                return 1;
            }
            return 0;
        }
    }
    // MIDI to UGE mode
    if (midiPath.empty() || ugePath.empty()) {
        std::cerr << "Usage: " << argv[0] << " -i <input.mid> -o <output.uge>\n"
                  << "   or: " << argv[0] << " <input.mid> <output.uge>\n"
                  << "   or: " << argv[0] << " -i <input.uge> [-o <output.json>]" << std::endl;
        return 1;
    }
    if (!convertMidiToUge(midiPath, ugePath, user_channel_map)) {
        std::cerr << "Failed to convert MIDI to UGE." << std::endl;
        return 1;
    }
    std::cout << "Wrote " << ugePath << std::endl;
    return 0;
}
