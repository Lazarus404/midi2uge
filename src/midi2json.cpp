#include "MidiFile.h"
#include "nlohmann_json.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <map>

using namespace smf;
using json = nlohmann::json;

json midi_to_json(const std::string& midi_path) {
    MidiFile midi;
    if (!midi.read(midi_path)) {
        throw std::runtime_error("Failed to read MIDI file: " + midi_path);
    }
    midi.doTimeAnalysis();
    midi.linkNotePairs();
    json j;
    j["header"]["format"] = (midi.getTrackCount() == 1 ? 0 : 1);
    j["header"]["tracks"] = midi.getTrackCount();
    j["header"]["ticks_per_quarter"] = midi.getTicksPerQuarterNote();
    // Track events
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
    // Notes per program and percussion
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
                // Find matching note off
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
                    // Find program for this channel up to this event
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
    std::string input, output;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-i" && i + 1 < argc) {
            input = argv[++i];
        } else if (arg == "-o" && i + 1 < argc) {
            output = argv[++i];
        }
    }
    if (input.empty()) {
        std::cerr << "Usage: midi2json -i <input.mid> [-o <output.json>]" << std::endl;
        return 1;
    }
    if (output.empty()) {
        output = input + ".json";
    }
    if (input.size() >= 4 && input.substr(input.size() - 4) == ".mid" &&
        (output.size() >= 5 && output.substr(output.size() - 5) == ".json")) {
        try {
            json j = midi_to_json(input);
            std::ofstream ofs(output);
            ofs << j.dump(2);
            std::cout << "Wrote " << output << std::endl;
        } catch (const std::exception& e) {
            std::cerr << e.what() << std::endl;
            return 1;
        }
        return 0;
    } else {
        std::cerr << "Input must be .mid and output must be .json" << std::endl;
        return 1;
    }
} 