#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include "nlohmann_json.hpp"
#include "uge_writer.h"

using json = nlohmann::json;

struct FieldInfo {
    std::string name;
    size_t size;
    std::string type;
    json value;
};

// Helper to read little-endian values
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
        // Skip subpattern block for now
        in.seekg(64 * 17, std::ios::cur);
        duty_arr.push_back(inst);
    }
    root["duty_instruments"] = duty_arr;
    // Wave instruments
    json wave_arr = json::array();
    for (int i = 0; i < 15; ++i) {
        json inst;
        inst["type"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["name"] = { {"size", 256}, {"type", "shortstring"}, {"value", read_shortstring(in)} };
        inst["length"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["length_enabled"] = { {"size", 1}, {"type", "uint8"}, {"value", read_u8(in)} };
        inst["unused1"] = { {"size", 1}, {"type", "uint8"}, {"value", read_u8(in)} };
        inst["unused2"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["unused3"] = { {"size", 1}, {"type", "uint8"}, {"value", read_u8(in)} };
        inst["unused4"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["unused5"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["unused6"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["unused7"] = { {"size", 1}, {"type", "uint8"}, {"value", read_u8(in)} };
        inst["volume"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["wave_index"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["unused8"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["unused9"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["subpattern_enabled"] = { {"size", 1}, {"type", "uint8"}, {"value", read_u8(in)} };
        in.seekg(64 * 17, std::ios::cur);
        wave_arr.push_back(inst);
    }
    root["wave_instruments"] = wave_arr;
    // Noise instruments
    json noise_arr = json::array();
    for (int i = 0; i < 15; ++i) {
        json inst;
        inst["type"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["name"] = { {"size", 256}, {"type", "shortstring"}, {"value", read_shortstring(in)} };
        inst["length"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["length_enabled"] = { {"size", 1}, {"type", "uint8"}, {"value", read_u8(in)} };
        inst["initial_volume"] = { {"size", 1}, {"type", "uint8"}, {"value", read_u8(in)} };
        inst["volume_sweep_direction"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["volume_sweep_change"] = { {"size", 1}, {"type", "uint8"}, {"value", read_u8(in)} };
        inst["unused1"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["unused2"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["unused3"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["unused4"] = { {"size", 1}, {"type", "uint8"}, {"value", read_u8(in)} };
        inst["unused5"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["unused6"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["noise_mode"] = { {"size", 4}, {"type", "uint32"}, {"value", read_u32(in)} };
        inst["subpattern_enabled"] = { {"size", 1}, {"type", "uint8"}, {"value", read_u8(in)} };
        in.seekg(64 * 17, std::ios::cur);
        noise_arr.push_back(inst);
    }
    root["noise_instruments"] = noise_arr;
    // Wavetable
    json wavetable_arr = json::array();
    for (int i = 0; i < 16; ++i) {
        json wave = json::array();
        for (int j = 0; j < 32; ++j) {
            wave.push_back(read_u8(in));
        }
        wavetable_arr.push_back(wave);
    }
    root["wavetable"] = wavetable_arr;
    // TEMP PATCH: Seek to patterns offset for this file
    in.seekg(0xf882, std::ios::beg);
    // Patterns
    std::cout << "[uge2json debug] File pointer before reading num_patterns: 0x" << std::hex << in.tellg() << std::dec << std::endl;
    int num_patterns = read_u32(in);
    std::cout << "[uge2json debug] num_patterns read: " << num_patterns << std::endl;
    std::cout << "[uge2json debug] File pointer after reading num_patterns: 0x" << std::hex << in.tellg() << std::dec << std::endl;
    json patterns_arr = json::array();
    for (int p = 0; p < num_patterns; ++p) {
        json pat;
        pat["index"] = read_u32(in);
        json rows = json::array();
        for (int r = 0; r < 64; ++r) {
            json row;
            row["note"] = read_u32(in);
            row["instrument"] = read_u32(in);
            row["unused"] = read_u32(in);
            row["effect"] = read_u32(in);
            row["effect_param"] = read_u8(in);
            rows.push_back(row);
        }
        pat["rows"] = rows;
        patterns_arr.push_back(pat);
    }
    root["patterns"] = patterns_arr;
    // Order matrix
    json orders_arr = json::array();
    for (int ch = 0; ch < 4; ++ch) {
        int len = read_u32(in);
        json order = json::array();
        for (int i = 0; i < len; ++i) order.push_back(read_u32(in));
        orders_arr.push_back(order);
    }
    root["orders"] = orders_arr;
    // Routines
    json routines_arr = json::array();
    for (int i = 0; i < 16; ++i) {
        int len = read_u32(in);
        std::string s(len, '\0');
        in.read(&s[0], len);
        // Encode as base64 to avoid invalid UTF-8
        std::string base64;
        static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        int val = 0, valb = -6;
        for (uint8_t c : s) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                base64.push_back(b64[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) base64.push_back(b64[((val << 8) >> (valb + 8)) & 0x3F]);
        while (base64.size() % 4) base64.push_back('=');
        routines_arr.push_back(base64);
    }
    root["routines"] = routines_arr;
    return root;
}

int main(int argc, char* argv[]) {
    if (argc == 2) {
        std::string ugefile = argv[1];
        if (ugefile.size() > 4 && ugefile.substr(ugefile.size()-4) == ".uge") {
            try {
                json j = parse_uge(ugefile);
                std::string outpath = ugefile + ".json";
                std::ofstream out(outpath);
                out << std::setw(2) << j << std::endl;
                std::cout << "Wrote " << outpath << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
                return 1;
            }
            return 0;
        }
    }
    std::cerr << "Usage: uge2json <file.uge>" << std::endl;
    return 1;
} 