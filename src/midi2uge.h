#pragma once
#include <string>
#include <optional>
#include <array>

// Converts a MIDI file to a UGE file. Returns true on success.
bool convertMidiToUge(const std::string& midiPath, const std::string& ugePath, std::optional<std::array<int, 4>> user_channel_map = std::nullopt); 