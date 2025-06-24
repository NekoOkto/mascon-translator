#include <SDL.h>
#include <windows.h>
#include <iostream>
#include <vector>
#include <set>
#include <string>
#include <thread>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <limits>
#include <cctype>
#include <sstream>
#include <conio.h> // For _kbhit and _getch
#include "nlohmann/json.hpp"

nlohmann::json translations; // Global translation object

#define FOREGROUND_YELLOW   (FOREGROUND_RED | FOREGROUND_GREEN)
#define FOREGROUND_CYAN     (FOREGROUND_GREEN | FOREGROUND_BLUE)
#define FOREGROUND_PINK     (FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define FOREGROUND_LIME     (FOREGROUND_GREEN | FOREGROUND_INTENSITY) // bright lime
#define FOREGROUND_ORANGE   (FOREGROUND_RED | FOREGROUND_YELLOW | FOREGROUND_INTENSITY)  // bright orange

// Standard Windows console color definitions
#define COLOR_DEFAULT      (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define COLOR_ERROR        (FOREGROUND_RED | FOREGROUND_INTENSITY)
#define COLOR_WARNING      (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY) // yellow
#define COLOR_SUCCESS      (FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define COLOR_INFO         (FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY) // cyan
#define CONSOLE_HIGHLIGHT  (FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define COLOR_PROMPT       (FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define COLOR_PINK         (FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY)

// Helper to send mouse scroll
void sendMouseScroll(int amount) {
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = amount;
    SendInput(1, &input, sizeof(INPUT));
}

// Helper to send arrow key presses
void sendArrowKey(int key, int hold_ms) {
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
    input.ki.dwFlags = 0;
    SendInput(1, &input, sizeof(INPUT));

    // Sleep to simulate key press duration
    std::this_thread::sleep_for(std::chrono::milliseconds(hold_ms));

    // Release the key
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

// Helper to match the largest subset first
int match_combo(const std::set<int>& pressed, const std::vector<std::set<int>>& combos) {
    int best = -1;
    int best_size = -1;
    for (size_t i = 0; i < combos.size(); ++i) {
        const auto& combo = combos[i];
        if (std::includes(pressed.begin(), pressed.end(), combo.begin(), combo.end())) {
            if ((int)combo.size() > best_size) {
                best = (int)i;
                best_size = (int)combo.size();
            }
        }
    }
    return best;
}

// Config structure and defaults
struct Config {
    int debounce_ms = 30;
    int up_down_delay_ms = 25;
    int mouse_scroll_delay_ms = 20;
    int key_hold_time_ms = 10; // New: how long to hold arrow key down (ms)
    int last_mode = 0;
    int last_joystick = 0;
    std::string language = "en";
    std::string profile = "Default"; // Profile name
    // Lever mapping: 15 positions, each a set of button indices
    std::vector<std::set<int>> lever_mappings;
    // Optional pedal mappings
    int big_horn_button = -1; // -1 = not set
    int small_horn_button = -1; // -1 = not set
    int credit_button = -1;    // -1 = not set (LeftShift+C)
    int test_menu_button = -1; // -1 = not set (RightShift)
    int debug_mission_button = -1; // -1 = not set (LeftShift)
    std::vector<int> lever_keycodes; // New: keycode for each lever position (mode 2)
    Config() {
        // Default lever mapping (original ordered_combos)
        lever_mappings = {
            {9}, {8}, {8,9}, {7}, {7,9}, {7,8}, {7,8,9}, {6}, {6,9}, {6,8},
            {6,8,9}, {6,7}, {6,7,9}, {6,7,8}, {6,7,8,9}
        };
        big_horn_button = -1;
        small_horn_button = -1;
        credit_button = -1;
        test_menu_button = -1;
        debug_mission_button = -1;
        language = "en";
        profile = "Default";
        lever_keycodes = std::vector<int>(15, 0); // Default: all 0 (no key)
    }
};

const Config default_config{};

void save_config(const Config& cfg, const std::string& filename) {
    std::ofstream ofs(filename);
    if (ofs) {
        ofs << "# Mascon Lever Input Translator Config\n";
        ofs << "debounce_ms=" << cfg.debounce_ms << "\n";
        ofs << "up_down_delay_ms=" << cfg.up_down_delay_ms << "\n";
        ofs << "mouse_scroll_delay_ms=" << cfg.mouse_scroll_delay_ms << "\n";
        ofs << "key_hold_time_ms=" << cfg.key_hold_time_ms << "\n"; // New
        ofs << "last_mode=" << cfg.last_mode << "\n";
        ofs << "last_joystick=" << cfg.last_joystick << "\n";
        ofs << "language=" << cfg.language << "\n"; // New: save language
        ofs << "big_horn_button=" << cfg.big_horn_button << "\n";
        ofs << "small_horn_button=" << cfg.small_horn_button << "\n";
        ofs << "credit_button=" << cfg.credit_button << "\n";
        ofs << "test_menu_button=" << cfg.test_menu_button << "\n";
        ofs << "debug_mission_button=" << cfg.debug_mission_button << "\n";
        ofs << "profile=" << cfg.profile << "\n"; // New: save profile
        ofs << "# Lever mappings: 15 lines, each line is a space-separated list of button indices for a position\n";
        for (const auto& s : cfg.lever_mappings) {
            for (int b : s) ofs << b << ' ';
            ofs << '\n';
        }
        ofs << "# Lever keycodes: 15 lines, each line is a virtual-key code for a position (mode 2)\n";
        for (int k : cfg.lever_keycodes) ofs << k << '\n';
    }
}

bool load_config(Config& cfg, const std::string& filename) {
    std::ifstream ifs(filename);
    if (!ifs) return false;
    std::string line;
    int loaded = 0;
    int lever_keycode_count = 0;
    cfg.lever_mappings.clear(); // Ensure clean state
    cfg.lever_keycodes = std::vector<int>(15, 0); // Reset keycodes
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto get_value = [](const std::string& line, size_t prefix_len) -> std::string {
            std::string val = line.substr(prefix_len);
            // Trim whitespace
            val.erase(0, val.find_first_not_of(" \t"));
            val.erase(val.find_last_not_of(" \t") + 1);
            return val;
        };
        if (line.find("debounce_ms=") == 0) {
            try {
                std::string val = get_value(line, 12);
                cfg.debounce_ms = val.empty() ? default_config.debounce_ms : std::stoi(val);
            } catch (const std::exception&) { cfg.debounce_ms = default_config.debounce_ms; }
            ++loaded; continue;
        }
        if (line.find("up_down_delay_ms=") == 0) {
            try {
                std::string val = get_value(line, 17);
                cfg.up_down_delay_ms = val.empty() ? default_config.up_down_delay_ms : std::stoi(val);
            } catch (const std::exception&) { cfg.up_down_delay_ms = default_config.up_down_delay_ms; }
            ++loaded; continue;
        }
        if (line.find("mouse_scroll_delay_ms=") == 0) {
            try {
                std::string val = get_value(line, 22);
                cfg.mouse_scroll_delay_ms = val.empty() ? default_config.mouse_scroll_delay_ms : std::stoi(val);
            } catch (const std::exception&) { cfg.mouse_scroll_delay_ms = default_config.mouse_scroll_delay_ms; }
            ++loaded; continue;
        }
        if (line.find("key_hold_time_ms=") == 0) {
            try {
                std::string val = get_value(line, 16);
                cfg.key_hold_time_ms = val.empty() ? default_config.key_hold_time_ms : std::stoi(val);
            } catch (const std::exception&) { cfg.key_hold_time_ms = default_config.key_hold_time_ms; }
            ++loaded; continue;
        }
        if (line.find("last_mode=") == 0) {
            try {
                std::string val = get_value(line, 10);
                cfg.last_mode = val.empty() ? default_config.last_mode : std::stoi(val);
            } catch (const std::exception&) { cfg.last_mode = default_config.last_mode; }
            ++loaded; continue;
        }
        if (line.find("last_joystick=") == 0) {
            try {
                std::string val = get_value(line, 14);
                cfg.last_joystick = val.empty() ? default_config.last_joystick : std::stoi(val);
            } catch (const std::exception&) { cfg.last_joystick = default_config.last_joystick; }
            ++loaded; continue;
        }
        if (line.find("language=") == 0) {
            std::string val = get_value(line, 9);
            cfg.language = val.empty() ? default_config.language : val;
            ++loaded; continue;
        }
        if (line.find("big_horn_button=") == 0) {
            try {
                std::string val = get_value(line, 16);
                cfg.big_horn_button = val.empty() ? default_config.big_horn_button : std::stoi(val);
            } catch (const std::exception&) { cfg.big_horn_button = default_config.big_horn_button; }
            ++loaded; continue;
        }
        if (line.find("small_horn_button=") == 0) {
            try {
                std::string val = get_value(line, 18);
                cfg.small_horn_button = val.empty() ? default_config.small_horn_button : std::stoi(val);
            } catch (const std::exception&) { cfg.small_horn_button = default_config.small_horn_button; }
            ++loaded; continue;
        }
        if (line.find("credit_button=") == 0) {
            try {
                std::string val = get_value(line, 14);
                cfg.credit_button = val.empty() ? default_config.credit_button : std::stoi(val);
            } catch (const std::exception&) { cfg.credit_button = default_config.credit_button; }
            ++loaded; continue;
        }
        if (line.find("test_menu_button=") == 0) {
            try {
                std::string val = get_value(line, 17);
                cfg.test_menu_button = val.empty() ? default_config.test_menu_button : std::stoi(val);
            } catch (const std::exception&) { cfg.test_menu_button = default_config.test_menu_button; }
            ++loaded; continue;
        }
        if (line.find("debug_mission_button=") == 0) {
            try {
                std::string val = get_value(line, 21);
                cfg.debug_mission_button = val.empty() ? default_config.debug_mission_button : std::stoi(val);
            } catch (const std::exception&) { cfg.debug_mission_button = default_config.debug_mission_button; }
            ++loaded; continue;
        }
        if (line.find("profile=") == 0) {
            std::string val = get_value(line, 8);
            cfg.profile = val.empty() ? default_config.profile : val;
            ++loaded; continue;
        }
        // Lever mappings: after 11 values, next 15 lines are mappings
        if (loaded >= 11 && cfg.lever_mappings.size() < 15) {
            std::istringstream iss(line);
            std::set<int> s;
            int b;
            while (iss >> b) s.insert(b);
            cfg.lever_mappings.push_back(s);
            continue;
        }
        // Lever keycodes: after lever mappings, next 15 lines are keycodes
        if (loaded >= 26 && lever_keycode_count < 15) {
            try {
                std::string val = get_value(line, 0);
                cfg.lever_keycodes[lever_keycode_count++] = val.empty() ? 0 : std::stoi(val);
            } catch (const std::exception&) {
                cfg.lever_keycodes[lever_keycode_count++] = 0;
            }
            continue;
        }
    }
    // If not enough keycodes, fill with 0
    while (lever_keycode_count < 15) cfg.lever_keycodes[lever_keycode_count++] = 0;
    // If not enough mappings, fill with defaults
    while (cfg.lever_mappings.size() < 15) {
        cfg.lever_mappings.push_back(default_config.lever_mappings[cfg.lever_mappings.size()]);
    }
    return loaded >= 5; // still require at least 5 for legacy support
}

// Helper to print colored text in Windows console
void print_colored(const std::string& text, WORD color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    GetConsoleScreenBufferInfo(hConsole, &info);
    WORD original = info.wAttributes;
    // Do not remap any colors; always use the color provided
    SetConsoleTextAttribute(hConsole, color);
    std::cout << text;
    SetConsoleTextAttribute(hConsole, original);
}

// Loads translations from lang/lang_xx.json
void load_translations(const std::string& lang_code) {
    std::string path = "lang/lang_" + lang_code + ".json";
    std::ifstream ifs(path);
    if (ifs) {
        ifs >> translations;
    } else {
        translations = nlohmann::json();
    }
}

// Translation function using loaded JSON
// No debug output

template<typename... Args>
std::string tr(const std::string& text, Args&&... args) {
    if (translations.contains(text)) {
        if (translations[text].is_string()) {
            return translations[text].get<std::string>();
        }
    }
    return text;
}

// Enhanced language select function with AI translation notice
std::string select_language(const std::string& current) {
    while (true) {
        std::cout << "\n--- Language Selection ---\n";
        std::cout << "NOTICE: All translations were done by AI and have not been checked for accuracy.\n";
        std::cout << "Current: ";
        if (current == "en") std::cout << "English";
        else if (current == "es") std::cout << "Spanish";
        else if (current == "de") std::cout << "German";
        else if (current == "ko") std::cout << "Korean";
        else if (current == "zh") std::cout << "Chinese";
        else if (current == "ja") std::cout << "Japanese";
        else if (current == "fr") std::cout << "French";
        else if (current == "it") std::cout << "Italian";
        else if (current == "pt") std::cout << "Portuguese";
        else if (current == "ru") std::cout << "Russian";
        else if (current == "tr") std::cout << "Turkish";
        else if (current == "ar") std::cout << "Arabic";
        else if (current == "hi") std::cout << "Hindi";
        else if (current == "vi") std::cout << "Vietnamese";
        else if (current == "id") std::cout << "Indonesian";
        else if (current == "ms") std::cout << "Malay";
        else std::cout << current;
        std::cout << "\nAvailable languages:\n";
        std::cout << " 1. English (en) (English)\n";
        std::cout << " 2. Spanish (es) (Español)\n";
        std::cout << " 3. German (de) (Deutsch)\n";
        std::cout << " 4. Korean (ko) (한국어)\n";
        std::cout << " 5. Chinese (zh) (中文)\n";
        std::cout << " 6. Japanese (ja) (日本語)\n";
        std::cout << " 7. French (fr) (Français)\n";
        std::cout << " 8. Italian (it) (Italiano)\n";
        std::cout << " 9. Portuguese (pt) (Português)\n";
        std::cout << "10. Russian (ru) (Русский)\n";
        std::cout << "11. Turkish (tr) (Türkçe)\n";
        std::cout << "12. Arabic (ar) (العربية)\n";
        std::cout << "13. Hindi (hi) (हिन्दी)\n";
        std::cout << "14. Vietnamese (vi) (Tiếng Việt)\n";
        std::cout << "15. Indonesian (id) (Bahasa Indonesia)\n";
        std::cout << "16. Malay (ms) (Bahasa Melayu)\n";
        std::cout << "Select language (1-16): ";
        std::string input;
        std::getline(std::cin, input);
        if (input == "1") return "en";
        if (input == "2") return "es";
        if (input == "3") return "de";
        if (input == "4") return "ko";
        if (input == "5") return "zh";
        if (input == "6") return "ja";
        if (input == "7") return "fr";
        if (input == "8") return "it";
        if (input == "9") return "pt";
        if (input == "10") return "ru";
        if (input == "11") return "tr";
        if (input == "12") return "ar";
        if (input == "13") return "hi";
        if (input == "14") return "vi";
        if (input == "15") return "id";
        if (input == "16") return "ms";
        std::cout << "Invalid selection. Please choose 1-16.\n";
    }
}

// Add language select to settings_menu
void settings_menu(Config& cfg, const std::string& filename, int& mode, int& selected_id, int num_joysticks) {
    auto get_profile_filename = [&cfg]() -> std::string {
        return (cfg.profile == "Default") ? "mascon_translator.cfg" : (cfg.profile + ".cfg");
    };
    save_config(cfg, get_profile_filename());
    while (true) {
        std::cout << "\n--- " << tr("Settings", cfg.language) << " Menu (" << tr("Profile", cfg.language) << ": " << cfg.profile << ") (press ";
        print_colored(tr("Enter", cfg.language), FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << " to keep current value) ---\n";
        std::cout << tr("Current values:", cfg.language) << std::endl;
        print_colored("0. " + tr("Profile", cfg.language) + ": " + cfg.profile + "\n", FOREGROUND_CYAN);
        print_colored("1. " + tr("Joystick debounce ms: ", cfg.language), FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << cfg.debounce_ms << "\n";
        print_colored("2. " + tr("Up/Down Arrow delay ms: ", cfg.language), FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << cfg.up_down_delay_ms << "\n";
        print_colored("3. " + tr("Mouse scroll delay ms: ", cfg.language), FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << cfg.mouse_scroll_delay_ms << "\n";
        print_colored("4. " + tr("Key hold time ms: ", cfg.language), FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
        std::cout << cfg.key_hold_time_ms << "\n";
        print_colored("5. " + tr("Output mode: ", cfg.language), COLOR_WARNING);
        std::cout << (mode == 0 ? tr("Arrow Keys", cfg.language) : mode == 1 ? tr("Mouse Scroll", cfg.language) : "Lever-to-Key") << "\n";
        print_colored("6. " + tr("Joystick: ", cfg.language), FOREGROUND_RED | FOREGROUND_GREEN);
        std::cout << selected_id << std::endl;
        print_colored("7. " + tr("Remap lever positions", cfg.language) + "\n", FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        print_colored("8. " + tr("Other input mapping (horns, credit, test, debug)", cfg.language) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
        print_colored("9. " + tr("Language", cfg.language) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
        if (mode == 2) {
            print_colored("10. " + tr("Set lever-to-key mapping (mode 2)", cfg.language) + "\n", COLOR_PROMPT);
        }
        std::cout << tr("Enter number to change, '", cfg.language);
        print_colored("r", FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << tr("' to reset to default, '", cfg.language);
        print_colored("h", FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << tr("' for help, or '", cfg.language);
        print_colored("q", FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << tr("' to leave settings: ", cfg.language);
        std::string input;
        std::getline(std::cin, input);
        if (input.empty()) continue;
        // Trim whitespace from input
        std::string trimmed = input;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
        // Handle non-numeric commands first
        if (trimmed == "q" || trimmed == "Q") break;
        if (trimmed == "r" || trimmed == "R") {
            cfg = default_config;
            save_config(cfg, get_profile_filename());
            print_colored(tr("Settings reset to default.", cfg.language) + "\n", FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            continue;
        }
        if (trimmed == "h" || trimmed == "H") {
            system("cls");
            // Colorful help menu (now translated)
            print_colored("\n--- " + tr("Settings Help", cfg.language) + " ---\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            print_colored("1. " + tr("Joystick debounce ms", cfg.language) + "\n", FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            std::cout << "   - " << tr("Increase if the lever input \"teleports\" or jumps unexpectedly.", cfg.language) << "\n";
            std::cout << "   - " << tr("Decrease to reduce input lag, but too low may cause instability.", cfg.language) << "\n\n";
            print_colored("2. " + tr("Up/Down Arrow delay ms", cfg.language) + "\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "   - " << tr("Increase if some up/down arrow keypresses are not registered in your game.", cfg.language) << "\n";
            std::cout << "   - " << tr("Decrease to reduce input lag, but too low may cause missed or repeated inputs.", cfg.language) << "\n\n";
            print_colored("3. " + tr("Mouse scroll delay ms", cfg.language) + "\n", FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            std::cout << "   - " << tr("Increase if some mouse scroll events are not registered in your game.", cfg.language) << "\n";
            std::cout << "   - " << tr("Decrease to reduce input lag, but too low may cause missed or repeated scrolls.", cfg.language) << "\n\n";
            print_colored("4. " + tr("Output mode", cfg.language) + "\n", FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            print_colored("   - 0: " + tr("Up/Down Arrow Keys", cfg.language) + "\n", FOREGROUND_RED | FOREGROUND_GREEN);
            print_colored("   - 1: " + tr("Mouse Scroll", cfg.language) + "\n", FOREGROUND_RED | FOREGROUND_GREEN);
            print_colored("   - 2: " + tr("Lever-to-Key", cfg.language) + "\n\n", FOREGROUND_RED | FOREGROUND_GREEN);
            print_colored("5. " + tr("Change joystick", cfg.language) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
            std::cout << "   - " << tr("Select a different joystick by number.", cfg.language) << "\n\n";
            print_colored("6. " + tr("Remap lever positions", cfg.language) + "\n", FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            std::cout << "   - " << tr("Move the lever to each position as prompted, then press Enter.", cfg.language) << "\n";
            std::cout << "   - " << tr("Press Enter without moving to skip a position.", cfg.language) << "\n\n";
            print_colored("7. " + tr("Other input mapping (horns, credit, test, debug)", cfg.language) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
            std::cout << "   - " << tr("Map joystick buttons to special functions like the big horn pedal (Enter), small horn pedal (Space), credit (coin), test menu (RightShift), and debug mission select (LeftShift).", cfg.language) << "\n\n";
            print_colored("8. " + tr("Language", cfg.language) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
            std::cout << "   - " << tr("Change the language used for all menus and messages.", cfg.language) << "\n\n";
            if (mode == 2) {
                print_colored("9. " + tr("Set lever-to-key mapping (mode 2)", cfg.language) + "\n", COLOR_PROMPT);
                std::cout << "   - " << tr("Assign a keyboard key to each lever position (for mode 2).", cfg.language) << "\n\n";
            }
            print_colored(tr("Adjust these settings to balance responsiveness and reliability for your setup.", cfg.language) + "\n", FOREGROUND_LIME | FOREGROUND_INTENSITY);
            print_colored("---------------------\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            continue;
        }
        // Only parse as integer if input is all digits (after trimming)
        bool is_number = !trimmed.empty() && std::all_of(trimmed.begin(), trimmed.end(), ::isdigit);
        int opt = 0;
        if (is_number) {
            try { opt = std::stoi(trimmed); } catch (...) { opt = 0; }
        } else {
            print_colored(tr("Invalid input! Please enter a number, 'r', 'h', or 'q'.", cfg.language) + "\n\n", COLOR_ERROR);
            continue;
        }
        if (opt == 1) {
            print_colored("Enter new debounce ms (current: ", FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << cfg.debounce_ms << "): ";
            std::getline(std::cin, input);
            if (!input.empty()) {
                try {
                    cfg.debounce_ms = std::max(1, std::stoi(input));
                    save_config(cfg, get_profile_filename());
                } catch (...) {
                    print_colored(tr("Invalid input! Please enter a valid integer.", cfg.language) + "\n\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
                }
            }
        } else if (opt == 0) {
            // Profile menu
            while (true) {
                system("cls"); // Clear screen at the start of each profile menu loop
                // List all available profiles
                std::vector<std::string> profiles;
                WIN32_FIND_DATAA findFileData;
                HANDLE hFind = FindFirstFileA("*.cfg", &findFileData);
                if (hFind != INVALID_HANDLE_VALUE) {
                    do {
                        std::string fname = findFileData.cFileName;
                        if (fname.size() > 4 && fname.substr(fname.size() - 4) == ".cfg") {
                            profiles.push_back(fname.substr(0, fname.size() - 4));
                        }
                    } while (FindNextFileA(hFind, &findFileData));
                    FindClose(hFind);
                }
                std::sort(profiles.begin(), profiles.end());
                // Always show "Default" as the first profile, and display as "Default" (not mascon_translator)
                // Remove "mascon_translator" if present, and ensure "Default" is present and first
                auto it = std::find(profiles.begin(), profiles.end(), "mascon_translator");
                if (it != profiles.end()) profiles.erase(it);
                if (std::find(profiles.begin(), profiles.end(), "Default") == profiles.end()) {
                    profiles.insert(profiles.begin(), "Default");
                } else {
                    // Move Default to the front if not already
                    auto def_it = std::find(profiles.begin(), profiles.end(), "Default");
                    if (def_it != profiles.begin()) {
                        profiles.erase(def_it);
                        profiles.insert(profiles.begin(), "Default");
                    }
                }
                int current_idx = -1;
                for (size_t i = 0; i < profiles.size(); ++i) {
                    if (profiles[i] == cfg.profile) current_idx = (int)i;
                }
                print_colored("\n", FOREGROUND_CYAN);
                print_colored("==============================\n", FOREGROUND_LIME | FOREGROUND_INTENSITY);
                print_colored("   ", FOREGROUND_LIME | FOREGROUND_INTENSITY);
                print_colored(tr("Profile", cfg.language), FOREGROUND_PINK | FOREGROUND_INTENSITY);
                print_colored(" ", FOREGROUND_PINK | FOREGROUND_INTENSITY);
                print_colored("Menu\n", FOREGROUND_PINK | FOREGROUND_INTENSITY);
                print_colored("==============================\n", FOREGROUND_LIME | FOREGROUND_INTENSITY);
                print_colored(tr("Available profiles:", cfg.language) + "\n", FOREGROUND_CYAN);
                for (size_t i = 0; i < profiles.size(); ++i) {
                    WORD color = (i % 2 == 0) ? FOREGROUND_LIME | FOREGROUND_INTENSITY : FOREGROUND_PINK | FOREGROUND_INTENSITY;
                    if ((int)i == current_idx) {
                        print_colored("* ", FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
                    } else {
                        print_colored("  ", COLOR_DEFAULT);
                    }
                    print_colored(std::to_string(i + 1) + ". ", color);
                    print_colored(profiles[i] + "\n", (profiles[i] == "Default") ? FOREGROUND_CYAN | FOREGROUND_INTENSITY : color);
                }
                print_colored("\n", FOREGROUND_CYAN);
                print_colored(tr("Enter profile number to switch, 'n' for new, 'd' to delete, 'c' to copy/duplicate, 'r' to rename, or 'q' to cancel:", cfg.language), FOREGROUND_LIME | FOREGROUND_INTENSITY);
                std::string profile_input;
                std::getline(std::cin, profile_input);
                if (profile_input == "q" || profile_input == "Q" || profile_input.empty()) { system("cls"); break; }
                // Switch profile by number
                bool is_number = !profile_input.empty() && std::all_of(profile_input.begin(), profile_input.end(), ::isdigit);
                if (is_number) {
                    int idx = std::stoi(profile_input) - 1;
                    if (idx >= 0 && idx < (int)profiles.size()) {
                        if (profiles[idx] == cfg.profile) {
                            system("cls");
                            print_colored(tr("Already using this profile.", cfg.language) + "\n", COLOR_INFO);
                        } else {
                            system("cls");
                            Config new_cfg;
                            // Fix: If profile is Default, load from mascon_translator.cfg
                            std::string load_file = (profiles[idx] == "Default") ? "mascon_translator.cfg" : (profiles[idx] + ".cfg");
                            if (load_config(new_cfg, load_file)) {
                                cfg = new_cfg;
                                if (profiles[idx] == "Default") {
                                    cfg.profile = "Default";
                                }
                                print_colored(tr("Profile switched!", cfg.language) + "\n", COLOR_SUCCESS);
                                // Do NOT save_config here! Only save after user changes settings.
                            } else {
                                print_colored(tr("Failed to load profile.", cfg.language) + "\n", COLOR_ERROR);
                            }
                        }
                        continue;
                    } else {
                        system("cls");
                        print_colored(tr("Invalid profile number.", cfg.language) + "\n", COLOR_ERROR);
                        continue;
                    }
                }
                if (profile_input == "c" || profile_input == "C") {
                    system("cls");
                    // Duplicate current profile with a suffix
                    std::string base = cfg.profile;
                    std::string new_profile = base + "_copy";
                    int copy_idx = 2;
                    while (std::find(profiles.begin(), profiles.end(), new_profile) != profiles.end()) {
                        new_profile = base + "_copy" + std::to_string(copy_idx++);
                    }
                    Config new_cfg = cfg;
                    new_cfg.profile = new_profile;
                    save_config(new_cfg, new_profile + ".cfg");
                    print_colored(tr("Profile duplicated!", cfg.language) + "\n", COLOR_SUCCESS);
                    cfg.profile = new_profile;
                    save_config(cfg, new_profile + ".cfg");
                    continue;
                } else if (profile_input == "n" || profile_input == "N") {
                    system("cls");
                    print_colored(tr("Enter new profile name:", cfg.language), COLOR_PROMPT);
                    std::string new_profile_name;
                    std::getline(std::cin, new_profile_name);
                    // Trim whitespace
                    new_profile_name.erase(0, new_profile_name.find_first_not_of(" \t"));
                    new_profile_name.erase(new_profile_name.find_last_not_of(" \t") + 1);
                    if (new_profile_name.empty() || std::find(profiles.begin(), profiles.end(), new_profile_name) != profiles.end()) {
                        print_colored(tr("Invalid or duplicate profile name.", cfg.language) + "\n", COLOR_ERROR);
                        continue;
                    }
                    Config new_cfg = cfg;
                    new_cfg.profile = new_profile_name;
                    save_config(new_cfg, new_profile_name + ".cfg");
                    print_colored(tr("New profile created!", cfg.language) + "\n", COLOR_SUCCESS);
                    cfg.profile = new_profile_name;
                    save_config(cfg, new_profile_name + ".cfg");
                    continue;
                } else if (profile_input == "r" || profile_input == "R") {
                    system("cls");
                    print_colored(tr("Enter new profile name:", cfg.language), COLOR_PROMPT);
                    std::string new_name;
                    std::getline(std::cin, new_name);
                    // Trim whitespace
                    new_name.erase(0, new_name.find_first_not_of(" \t"));
                    new_name.erase(new_name.find_last_not_of(" \t") + 1);
                    if (new_name.empty() || new_name == cfg.profile || std::find(profiles.begin(), profiles.end(), new_name) != profiles.end()) {
                        print_colored(tr("Invalid or duplicate profile name.", cfg.language) + "\n", COLOR_ERROR);
                        continue;
                    }
                    // Rename: save config under new name, delete old file
                    Config renamed_cfg = cfg;
                    renamed_cfg.profile = new_name;
                    save_config(renamed_cfg, new_name + ".cfg");
                    std::string old_file = (cfg.profile == "Default") ? "mascon_translator.cfg" : (cfg.profile + ".cfg");
                    std::remove(old_file.c_str());
                    print_colored(tr("Profile renamed!", cfg.language) + "\n", COLOR_SUCCESS);
                    cfg.profile = new_name;
                    save_config(cfg, new_name + ".cfg");
                    continue;
                } else if (profile_input == "d" || profile_input == "D") {
                    system("cls");
                    if (cfg.profile == "Default") {
                        print_colored(tr("Cannot delete the default profile.", cfg.language) + "\n", COLOR_ERROR);
                        continue;
                    }
                    print_colored(tr("Are you sure you want to delete this profile? Type 'yes' to confirm:", cfg.language), COLOR_WARNING);
                    std::string confirm;
                    std::getline(std::cin, confirm);
                    if (confirm != "yes") {
                        print_colored(tr("Profile deletion cancelled.", cfg.language) + "\n", COLOR_INFO);
                        continue;
                    }
                    std::string profile_cfg_file = (cfg.profile == "Default") ? "mascon_translator.cfg" : (cfg.profile + ".cfg");
                    if (std::remove(profile_cfg_file.c_str()) == 0) {
                        print_colored(tr("Profile deleted!", cfg.language) + "\n", COLOR_SUCCESS);
                        // Switch to Default profile after deletion
                        auto it = std::find(profiles.begin(), profiles.end(), "Default");
                        if (it != profiles.end()) {
                            Config def_cfg;
                            if (load_config(def_cfg, "mascon_translator.cfg")) {
                                cfg = def_cfg;
                                print_colored(tr("Switched to Default profile.", cfg.language) + "\n", COLOR_INFO);
                                save_config(cfg, "mascon_translator.cfg");
                            } else {
                                print_colored(tr("Failed to load Default profile.", cfg.language) + "\n", COLOR_ERROR);
                            }
                        } else {
                            cfg = default_config;
                            cfg.profile = "Default";
                            save_config(cfg, "mascon_translator.cfg");
                            print_colored(tr("Default profile recreated.", cfg.language) + "\n", COLOR_INFO);
                        }
                        continue;
                    } else {
                        print_colored(tr("Failed to delete profile file.", cfg.language) + "\n", COLOR_ERROR);
                        continue;
                    }
                } else {
                    system("cls");
                    print_colored(tr("Invalid option.", cfg.language) + "\n", COLOR_ERROR);
                    continue;
                }
            }
        } else if (opt == 2) {
            print_colored("Enter new up/down delay ms (current: ", FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << cfg.up_down_delay_ms << "): ";
            std::getline(std::cin, input);
            if (!input.empty()) {
                try {
                    cfg.up_down_delay_ms = std::max(1, std::stoi(input));
                    save_config(cfg, get_profile_filename());
                } catch (...) {
                    print_colored(tr("Invalid input! Please enter a valid integer.", cfg.language) + "\n\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
                }
            }
        } else if (opt == 3) {
            print_colored("Enter new mouse scroll delay ms (current: ", FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << cfg.mouse_scroll_delay_ms << "): ";
            std::getline(std::cin, input);
            if (!input.empty()) {
                try {
                    cfg.mouse_scroll_delay_ms = std::max(1, std::stoi(input));
                    save_config(cfg, get_profile_filename());
                } catch (...) {
                    print_colored(tr("Invalid input! Please enter a valid integer.", cfg.language) + "\n\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
                }
            }
        } else if (opt == 4) {
            print_colored("Enter new key hold time ms (current: ", FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << cfg.key_hold_time_ms << "): ";
            std::getline(std::cin, input);
            if (!input.empty()) {
                try {
                    cfg.key_hold_time_ms = std::max(1, std::stoi(input));
                    save_config(cfg, get_profile_filename());
                } catch (...) {
                    print_colored(tr("Invalid input! Please enter a valid integer.", cfg.language) + "\n\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
                }
            }
        } else if (opt == 5) {
            print_colored("Enter new output mode (", COLOR_PROMPT);
            print_colored("0", COLOR_PROMPT);
            std::cout << " = Arrow Keys, ";
            print_colored("1", COLOR_PROMPT);
            std::cout << " = Mouse Scroll, ";
            print_colored("2", COLOR_PROMPT);
            std::cout << " = Lever-to-Key), current: " << mode << ": ";
            std::getline(std::cin, input);
            if (!input.empty()) {
                if (input == "0") mode = 0;
                else if (input == "1") mode = 1;
                else if (input == "2") mode = 2;
                else print_colored(tr("Invalid input! Please enter 0, 1 or 2.", cfg.language) + "\n\n", COLOR_ERROR);
                save_config(cfg, get_profile_filename());
            }
        } else if (opt == 6) {
            print_colored("Available joysticks:\n", FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
            for (int i = 0; i < num_joysticks; ++i) {
                print_colored(std::to_string(i), FOREGROUND_PINK | FOREGROUND_INTENSITY);
                std::cout << ": " << SDL_JoystickNameForIndex(i) << std::endl;
            }
            print_colored("Enter new joystick number (current: ", FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << selected_id << "): ";
            std::getline(std::cin, input);
            if (!input.empty()) {
                try {
                    int new_id = std::stoi(input);
                    if (new_id >= 0 && new_id < num_joysticks) {
                        selected_id = new_id;
                        save_config(cfg, get_profile_filename());
                    } else {
                        print_colored(tr("Invalid joystick number.", cfg.language) + "\n\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
                    }
                } catch (...) {
                    print_colored(tr("Invalid input! Please enter a valid integer.", cfg.language) + "\n\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
                }
            }
        } else if (opt == 7) {
            // Remap lever positions
            static const std::vector<std::string> lever_names = {
                "B9", "B8", "B7", "B6", "B5", "B4", "B3", "B2", "B1", "Neutral",
                "P1", "P2", "P3", "P4", "P5"
            };
            print_colored("\nLever remapping: Move the lever to each position as prompted, then press Enter.\n", FOREGROUND_LIME);
            print_colored("If you want to skip a position, just press Enter without moving.\n", FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
            print_colored("Press Backspace to go back to the previous position.\n", FOREGROUND_ORANGE);
            SDL_Joystick* joy = SDL_JoystickOpen(selected_id);
            if (!joy) {
                print_colored(tr("Failed to open joystick for remapping.", cfg.language) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
                continue;
            }
            std::vector<std::set<int>> new_mappings;
            int i = 0;
            while (i < (int)lever_names.size()) {
                print_colored("Position ", FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
                print_colored(lever_names[i], FOREGROUND_PINK | FOREGROUND_INTENSITY);
                std::cout << ": Move lever, then press Enter... (Backspace to go back) ";
                std::cout.flush();
                // Wait for Enter or Backspace
                std::string dummy;
                int key = 0;
                dummy.clear();
                while (true) {
                    if (_kbhit()) {
                        key = _getch();
                        if (key == 13) { // Enter
                            break;
                        } else if (key == 8) { // Backspace
                            break;
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
                if (key == 8) { // Backspace
                    if (i > 0) {
                        new_mappings.pop_back();
                        --i;
                        print_colored("\nWent back to previous position.\n", FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
                    } else {
                        print_colored("\nAlready at the first position.\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
                    }
                    continue;
                }
                SDL_JoystickUpdate();
                std::set<int> pressed;
                int num_buttons = SDL_JoystickNumButtons(joy);
                for (int b = 0; b < num_buttons; ++b) {
                    if (SDL_JoystickGetButton(joy, b)) pressed.insert(b);
                }
                new_mappings.push_back(pressed);
                print_colored("  Recorded buttons: ", FOREGROUND_LIME);
                if (pressed.empty()) std::cout << "(none)";
                else for (int b : pressed) std::cout << b << " ";
                std::cout << std::endl;
                ++i;
            }
            SDL_JoystickClose(joy);
            cfg.lever_mappings = new_mappings;
            save_config(cfg, get_profile_filename());
            print_colored("Remapping complete!\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            continue;
        } else if (opt == 8) {
            // Other input mapping sub-menu
            while (true) {
                print_colored("\n--- " + tr("Other Input Mapping", cfg.language) + " ---\n", FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
                print_colored("1. " + tr("Big Horn Pedal", cfg.language) + "\n", FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
                print_colored("2. " + tr("Small Horn Pedal", cfg.language) + "\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                print_colored("3. " + tr("Credit", cfg.language) + "\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                print_colored("4. " + tr("Test Menu", cfg.language) + "\n", FOREGROUND_PINK | FOREGROUND_INTENSITY);
                print_colored("5. " + tr("Debug Mission Select", cfg.language) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
                print_colored("6. " + tr("Clear all mappings", cfg.language) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
                print_colored("q. " + tr("Return to settings", cfg.language) + "\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                std::cout << tr("Current:", cfg.language) << " " << tr("Big Horn", cfg.language) << ": ";
                if (cfg.big_horn_button == -1) std::cout << tr("(not set)", cfg.language); else std::cout << cfg.big_horn_button;
                std::cout << ", " << tr("Small Horn", cfg.language) << ": ";
                if (cfg.small_horn_button == -1) std::cout << tr("(not set)", cfg.language); else std::cout << cfg.small_horn_button;
                std::cout << ", " << tr("Credit", cfg.language) << ": ";
                if (cfg.credit_button == -1) std::cout << tr("(not set)", cfg.language); else std::cout << cfg.credit_button;
                std::cout << ", " << tr("Test", cfg.language) << ": ";
                if (cfg.test_menu_button == -1) std::cout << tr("(not set)", cfg.language); else std::cout << cfg.test_menu_button;
                std::cout << ", " << tr("Debug", cfg.language) << ": ";
                if (cfg.debug_mission_button == -1) std::cout << tr("(not set)", cfg.language); else std::cout << cfg.debug_mission_button;
                std::cout << std::endl;
                std::cout << tr("Select option:", cfg.language) << " ";
                std::string other_input;
                std::getline(std::cin, other_input);
                if (other_input == "q" || other_input == "Q") break;
                int* mapping_ptr = nullptr;
                std::string map_label;
                WORD color = FOREGROUND_YELLOW | FOREGROUND_INTENSITY;
                if (other_input == "1") { mapping_ptr = &cfg.big_horn_button; map_label = tr("big horn pedal", cfg.language); color = FOREGROUND_YELLOW | FOREGROUND_INTENSITY; }
                else if (other_input == "2") { mapping_ptr = &cfg.small_horn_button; map_label = tr("small horn pedal", cfg.language); color = FOREGROUND_GREEN | FOREGROUND_INTENSITY; }
                else if (other_input == "3") { mapping_ptr = &cfg.credit_button; map_label = tr("credit", cfg.language); color = FOREGROUND_GREEN | FOREGROUND_INTENSITY; }
                else if (other_input == "4") { mapping_ptr = &cfg.test_menu_button; map_label = tr("test menu (RightShift)", cfg.language); color = FOREGROUND_PINK | FOREGROUND_INTENSITY; }
                else if (other_input == "5") { mapping_ptr = &cfg.debug_mission_button; map_label = tr("debug mission select (LeftShift)", cfg.language); color = FOREGROUND_RED | FOREGROUND_INTENSITY; }
                if (mapping_ptr) {
                    print_colored(("\n" + tr("Map ", cfg.language) + map_label + ": " + tr("Press the joystick button you want to use, or press Backspace to clear.", cfg.language) + "\n").c_str(), color);
                    SDL_Joystick* joy = SDL_JoystickOpen(selected_id);
                    if (!joy) {
                        print_colored(tr("Failed to open joystick.", cfg.language) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
                        continue;
                    }
                    int mapped = -1;
                    // Capture currently pressed buttons at the start
                    std::set<int> initially_pressed;
                    SDL_JoystickUpdate();
                    int num_buttons = SDL_JoystickNumButtons(joy);
                    for (int b = 0; b < num_buttons; ++b) {
                        if (SDL_JoystickGetButton(joy, b)) initially_pressed.insert(b);
                    }
                    while (true) {
                        if (_kbhit()) {
                            int key = _getch();
                            if (key == 8) { mapped = -1; break; }
                        }
                        SDL_JoystickUpdate();
                        for (int b = 0; b < num_buttons; ++b) {
                            if (SDL_JoystickGetButton(joy, b) && initially_pressed.find(b) == initially_pressed.end()) {
                                mapped = b;
                                break;
                            }
                        }
                        if (mapped != -1) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    }
                    SDL_JoystickClose(joy);
                    *mapping_ptr = mapped;
                    if (mapped == -1) print_colored((map_label + " " + tr("mapping cleared.", cfg.language) + "\n").c_str(), color);
                    else {
                        print_colored((map_label + " " + tr("mapped to button ", cfg.language)).c_str(), color);
                        std::cout << mapped << std::endl;
                    }
                    save_config(cfg, get_profile_filename());
                } else if (other_input == "6") {
                    cfg.big_horn_button = -1;
                    cfg.small_horn_button = -1;
                    cfg.credit_button = -1;
                    cfg.test_menu_button = -1;
                    cfg.debug_mission_button = -1;
                    print_colored(tr("All input mappings cleared.", cfg.language) + "\n", FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
                    save_config(cfg, get_profile_filename());
                } else {
                    print_colored(tr("Invalid option.", cfg.language) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
                }
            }
            continue;
        } else if (opt == 9) {
            std::string new_lang = select_language(cfg.language);
            cfg.language = new_lang;
            save_config(cfg, get_profile_filename());
            load_translations(cfg.language); // reload translations
            print_colored(tr("Language changed!", cfg.language) + "\n", FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
            // Update header after language change
            system("cls");
            print_colored("=================================\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            print_colored(tr("Mascon Lever Input Translator", cfg.language) + "\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            print_colored("=================================\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << tr("Using joystick #", cfg.language);
            print_colored(std::to_string(selected_id), FOREGROUND_PINK | FOREGROUND_INTENSITY);
            std::cout << ": ";
            std::cout << SDL_JoystickNameForIndex(selected_id);
            std::cout << std::endl;
            std::cout << tr("Output mode: ", cfg.language);
            print_colored((mode == 0 ? tr("Up/Down Arrow Keys", cfg.language) : tr("Mouse Scroll", cfg.language)), FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
            std::cout << ": ";
            std::cout << SDL_JoystickNameForIndex(selected_id);
            std::cout << std::endl;
            std::cout << tr("Output mode: ", cfg.language);
            print_colored((mode == 0 ? tr("Up/Down Arrow Keys", cfg.language) : tr("Mouse Scroll", cfg.language)), FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
            std::cout << std::endl;
            std::cout << "---------------------------------\n";
            std::cout << tr("Press ", cfg.language);
            print_colored(tr("Tab", cfg.language), FOREGROUND_LIME);
            std::cout << tr(" to open settings menu.", cfg.language) << std::endl;
            std::cout << tr("Press ", cfg.language);
            print_colored(tr("Esc", cfg.language), FOREGROUND_RED | FOREGROUND_INTENSITY);
            std::cout << tr(" to exit.", cfg.language) << std::endl;
            std::cout << "---------------------------------\n";
            continue;
        } else if (opt == 10 && mode == 2) { // Only allow option 9 if mode 2
            // Set lever-to-key mapping (mode 2)
            static const std::vector<std::string> lever_names = {
                "B9", "B8", "B7", "B6", "B5", "B4", "B3", "B2", "B1", "Neutral",
                "P1", "P2", "P3", "P4", "P5"
            };
            print_colored("\n" + tr("Set a key for each lever position. Enter a single character (e.g. a, 1, space), or a Windows virtual-key code (e.g. 0x41 for 'A', 0x31 for '1', 0x25 for Left Arrow, etc). Enter 0 for none.", cfg.language) + "\n", COLOR_PROMPT);
            for (int i = 0; i < 15; ++i) {
                std::cout << lever_names[i] << " (current: 0x" << std::hex << cfg.lever_keycodes[i] << std::dec << "): ";
                std::string key_input;
                std::getline(std::cin, key_input);
                if (!key_input.empty()) {
                    try {
                        // Trim whitespace
                        key_input.erase(0, key_input.find_first_not_of(" \t"));
                        key_input.erase(key_input.find_last_not_of(" \t") + 1);
                        if (key_input == "0") {
                            cfg.lever_keycodes[i] = 0;
                        } else if (key_input.length() == 1) {
                            // Single character: convert to virtual-key code
                            char ch = key_input[0];
                            SHORT vk = VkKeyScanA(ch);
                            if (vk != -1) {
                                cfg.lever_keycodes[i] = vk & 0xFF;
                            } else {
                                print_colored(tr("Unrecognized character. Please enter a valid key or code.\n", cfg.language), COLOR_ERROR);
                            }
                        } else if (key_input.length() > 1 && (key_input[0] == '0' && (key_input[1] == 'x' || key_input[1] == 'X'))) {
                            cfg.lever_keycodes[i] = std::stoi(key_input, nullptr, 16);
                        } else {
                            cfg.lever_keycodes[i] = std::stoi(key_input, nullptr, 0);
                        }
                    } catch (...) {
                        print_colored(tr("Invalid input! Please enter a valid key or code.\n", cfg.language), COLOR_ERROR);
                    }
                }
            }
            save_config(cfg, get_profile_filename());
            print_colored(tr("Lever-to-key mapping updated!", cfg.language) + "\n", COLOR_SUCCESS);
            continue;
        } else {
            print_colored("Invalid option.\n\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
        }
        cfg.last_mode = mode;
        cfg.last_joystick = selected_id;
        save_config(cfg, get_profile_filename());
    }
}

// Forward declaration for language selection
std::string select_language(const std::string& current);

int main(int argc, char* argv[]) {
    Config config;
    bool config_exists = load_config(config, "mascon_translator.cfg");

    // Always show language selection menu first if config file does not exist
    if (!config_exists || config.language.empty()) {
        config.language = select_language("");
        save_config(config, "mascon_translator.cfg");
        system("cls"); // Clear screen after language selection
    }
    std::string lang = config.language;

    // Load translations from JSON
    load_translations(lang);

    int mode = config.last_mode;
    int selected_id = config.last_joystick;

    if (SDL_Init(SDL_INIT_JOYSTICK) < 0) {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    int num_joysticks = SDL_NumJoysticks();
    while (num_joysticks == 0) {
        print_colored(tr("Mascon not detected. Plug in your mascon and press Enter to retry.", lang) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
        // Wait for either Enter or Tab
        while (true) {
            if (GetAsyncKeyState(VK_TAB) & 0x8000) {
                system("cls");
                print_colored("\nTab pressed. Opening settings menu...\n", FOREGROUND_LIME);
                settings_menu(config, "mascon_translator.cfg", mode, selected_id, num_joysticks);
                lang = config.language; // Update language after settings menu
                system("cls");
                print_colored(tr("Mascon not detected. Plug in your mascon and press Enter to retry.", lang) + " " + tr("Press ", lang) + tr("Tab", lang) + tr(" to open settings menu.", lang) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            }
            if (_kbhit()) {
                int ch = _getch();
                if (ch == 13) { // Enter key
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        SDL_Quit();
        SDL_Init(SDL_INIT_JOYSTICK);
        num_joysticks = SDL_NumJoysticks();
    }
    if (num_joysticks == 0) {
        print_colored(tr("No joystick assigned. Some features may be unavailable.", lang) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
        // Continue running, but skip joystick-dependent logic later
    }
    
    if (!config_exists) {
        // Print header with color at the start
        print_colored("=================================\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        print_colored(tr("Mascon Lever Input Translator", lang) + "\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        print_colored("=================================\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << std::endl;
        // Print available joysticks with color
        print_colored(tr("Available joysticks:", lang) + "\n", FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
        for (int i = 0; i < num_joysticks; ++i) {
            print_colored(std::to_string(i), FOREGROUND_PINK | FOREGROUND_INTENSITY);
            std::cout << ": ";
            std::cout << SDL_JoystickNameForIndex(i);
            std::cout << std::endl;
        }
        print_colored(tr("Select joystick number: ", lang), FOREGROUND_LIME);
        std::string input;
        std::getline(std::cin, input);
        if (!input.empty()) {
            try {
                int temp_id = std::stoi(input);
                if (temp_id >= 0 && temp_id < num_joysticks) {
                    selected_id = temp_id;
                } else {
                    print_colored(tr("Invalid joystick number.", lang) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
                    SDL_Quit();
                    return 1;
                }
            } catch (...) {
                print_colored(tr("Invalid input! Please enter a valid integer.", lang) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
                SDL_Quit();
                return 1;
            }
        }

        // Clear screen after joystick selection
        system("cls");

        SDL_Joystick* joy = SDL_JoystickOpen(selected_id);
        if (!joy) {
            print_colored(tr("Failed to open joystick.", lang) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
            SDL_Quit();
            return 1;
        }
        std::string joystick_name = SDL_JoystickName(joy);
        // Print header again for mode selection
        print_colored("=================================\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        print_colored(tr("Mascon Lever Input Translator", lang) + "\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        print_colored("=================================\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << std::endl;
        print_colored(tr("Using joystick #", lang), FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
        print_colored(std::to_string(selected_id), FOREGROUND_PINK | FOREGROUND_INTENSITY);
        std::cout << ": ";
        std::cout << joystick_name;
        std::cout << std::endl << std::endl;
        print_colored(tr("Select output mode:", lang) + "\n", FOREGROUND_LIME);
        print_colored("0", FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
        std::cout << ": " << tr("Up/Down Arrow Keys", lang) << "\n";
        print_colored("1", FOREGROUND_PINK | FOREGROUND_INTENSITY);
        std::cout << ": " << tr("Mouse Scroll", lang) << "\n";
        print_colored("2", FOREGROUND_CYAN | FOREGROUND_INTENSITY);
        std::cout << ": Lever-to-Key\n";
        print_colored(tr("Enter mode (0 or 1): ", lang), FOREGROUND_LIME);
        std::getline(std::cin, input);
        if (!input.empty()) {
            try {
                int temp_mode = std::stoi(input);
                if (temp_mode == 0 || temp_mode == 1) {
                    mode = temp_mode;
                } else {
                    print_colored(tr("Invalid mode. Defaulting to Arrow Keys.", lang) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
                    mode = 0;
                }
            } catch (...) {
                print_colored(tr("Invalid input! Please enter 0 or 1.", lang) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
                mode = 0;
            }
        }
        // Clear screen after mode selection
        system("cls");
        SDL_JoystickClose(joy);
        // Save config for next boot
        config.last_joystick = selected_id;
        config.last_mode = mode;
        save_config(config, "mascon_translator.cfg");
    }

    // Clear screen before main loop
    system("cls");
    print_colored("=================================\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    print_colored(tr("Mascon Lever Input Translator", lang) + "\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    print_colored("=================================\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    std::cout << tr("Using joystick #", lang);
    print_colored(std::to_string(selected_id), FOREGROUND_PINK | FOREGROUND_INTENSITY);
    std::cout << ": ";
    std::cout << SDL_JoystickNameForIndex(selected_id);
    std::cout << std::endl;
    std::cout << tr("Output mode: ", lang);
    print_colored((mode == 0 ? tr("Up/Down Arrow Keys", lang) : tr("Mouse Scroll", lang)), FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
    std::cout << std::endl;
    std::cout << "---------------------------------\n";
    std::cout << tr("Press ", lang);
    print_colored(tr("Tab", lang), FOREGROUND_LIME);
    std::cout << tr(" to open settings menu.", lang) << std::endl;
    std::cout << tr("Press ", lang);
    print_colored(tr("Esc", lang), FOREGROUND_RED | FOREGROUND_INTENSITY);
    std::cout << tr(" to exit.", lang) << std::endl;
    std::cout << "---------------------------------\n";
    // Use lever mappings from config
    std::vector<std::set<int>>& ordered_combos = config.lever_mappings;
    std::vector<std::string> names = {
        "B9", "B8", "B7", "B6", "B5", "B4", "B3", "B2", "B1", "Neutral",
        "P1", "P2", "P3", "P4", "P5"
    };
    // Open joystick for main loop
    SDL_Joystick* joy = SDL_JoystickOpen(selected_id);
    if (!joy) {
        print_colored(tr("Failed to open joystick.", lang) + "\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
        SDL_Quit();
        return 1;
    }
    HWND consoleWnd = GetConsoleWindow();
    HWND parentWnd = GetParent(consoleWnd);
    int last_idx = -1;
    std::set<int> last_pressed;
    int stable_idx = -1;
    auto last_event_time = std::chrono::steady_clock::now();
    // For credit repeat
    auto last_credit_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(250);
    bool credit_prev_pressed = false;
    print_colored("\x1b[35m" + tr("Input translation is active! Move the lever to send input ^w^", lang) + "\x1b[0m\n\n", FOREGROUND_PINK | FOREGROUND_INTENSITY);
    std::set<int> pressed;
    while (true) {
        HWND fgWnd = GetForegroundWindow();
        // --- Always process other input buttons, regardless of focus ---
        bool credit_pressed = false;
        static bool big_horn_prev_pressed = false;
        static bool small_horn_prev_pressed = false;
        static bool test_menu_prev_pressed = false;
        static bool debug_mission_prev_pressed = false;
        if (config.big_horn_button >= 0 || config.small_horn_button >= 0 || config.credit_button >= 0 || config.test_menu_button >= 0 || config.debug_mission_button >= 0) {
            SDL_JoystickUpdate();
            int num_buttons = SDL_JoystickNumButtons(joy);
            bool big_horn_now = false, small_horn_now = false, test_menu_now = false, debug_mission_now = false;
            for (int b = 0; b < num_buttons; ++b) {
                if (SDL_JoystickGetButton(joy, b)) {
                    if (b == config.big_horn_button) big_horn_now = true;
                    if (b == config.small_horn_button) small_horn_now = true;
                    if (b == config.credit_button) credit_pressed = true;
                    if (b == config.test_menu_button) test_menu_now = true;
                    if (b == config.debug_mission_button) debug_mission_now = true;
                }
            }
            // --- Big Horn Pedal (Enter) HOLD logic ---
            static bool big_horn_key_down = false;
            if (big_horn_now && !big_horn_key_down) {
                // Send Enter key down
                INPUT input = {0};
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = VK_RETURN;
                input.ki.wScan = MapVirtualKey(VK_RETURN, MAPVK_VK_TO_VSC);
                input.ki.dwFlags = KEYEVENTF_SCANCODE;
                input.ki.dwExtraInfo = GetMessageExtraInfo();
                SendInput(1, &input, sizeof(INPUT));
                print_colored("[Big Horn Pedal] Enter DOWN\n", FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
                big_horn_key_down = true;
            } else if (!big_horn_now && big_horn_key_down) {
                // Send Enter key up
                INPUT input = {0};
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = VK_RETURN;
                input.ki.wScan = MapVirtualKey(VK_RETURN, MAPVK_VK_TO_VSC);
                input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
                input.ki.dwExtraInfo = GetMessageExtraInfo();
                SendInput(1, &input, sizeof(INPUT));
                print_colored("[Big Horn Pedal] Enter UP\n", FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
                big_horn_key_down = false;
            }
            // --- Small Horn Pedal (Space) HOLD logic ---
            static bool small_horn_key_down = false;
            if (small_horn_now && !small_horn_key_down) {
                // Send Space key down
                INPUT input = {0};
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = VK_SPACE;
                input.ki.wScan = MapVirtualKey(VK_SPACE, MAPVK_VK_TO_VSC);
                input.ki.dwFlags = KEYEVENTF_SCANCODE;
                input.ki.dwExtraInfo = GetMessageExtraInfo();
                SendInput(1, &input, sizeof(INPUT));
                print_colored("[Small Horn Pedal] Spacebar DOWN\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                small_horn_key_down = true;
            } else if (!small_horn_now && small_horn_key_down) {
                // Send Space key up
                INPUT input = {0};
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = VK_SPACE;
                input.ki.wScan = MapVirtualKey(VK_SPACE, MAPVK_VK_TO_VSC);
                input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
                input.ki.dwExtraInfo = GetMessageExtraInfo();
                SendInput(1, &input, sizeof(INPUT));
                print_colored("[Small Horn Pedal] Spacebar UP\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                small_horn_key_down = false;
            }
            // --- Test Menu (Right Shift) logic ---
            static bool test_menu_prev_pressed = false;
            if (test_menu_now && !test_menu_prev_pressed) {
                // Send Right Shift using scan code
                INPUT input = {0};
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = VK_RSHIFT;
                input.ki.wScan = MapVirtualKey(VK_RSHIFT, MAPVK_VK_TO_VSC);
                input.ki.dwFlags = KEYEVENTF_SCANCODE;
                input.ki.dwExtraInfo = GetMessageExtraInfo();
                SendInput(1, &input, sizeof(INPUT));
                print_colored("[Test Menu] RightShift DOWN\n", FOREGROUND_PINK | FOREGROUND_INTENSITY);
            } else if (!test_menu_now && test_menu_prev_pressed) {
                // Release Right Shift
                INPUT input = {0};
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = VK_RSHIFT;
                input.ki.wScan = MapVirtualKey(VK_RSHIFT, MAPVK_VK_TO_VSC);
                input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
                input.ki.dwExtraInfo = GetMessageExtraInfo();
                SendInput(1, &input, sizeof(INPUT));
                print_colored("[Test Menu] RightShift UP\n", FOREGROUND_PINK | FOREGROUND_INTENSITY);
            }
            test_menu_prev_pressed = test_menu_now;
            // --- Debug Mission (Left Shift) logic ---
            static bool debug_mission_prev_pressed = false;
            if (debug_mission_now && !debug_mission_prev_pressed) {
                // Send Left Shift using scan code
                INPUT input = {0};
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = VK_LSHIFT;
                input.ki.wScan = MapVirtualKey(VK_LSHIFT, MAPVK_VK_TO_VSC);
                input.ki.dwFlags = KEYEVENTF_SCANCODE;
                input.ki.dwExtraInfo = GetMessageExtraInfo();
                SendInput(1, &input, sizeof(INPUT));
                print_colored("[Debug Mission] LeftShift DOWN\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
            } else if (!debug_mission_now && debug_mission_prev_pressed) {
                // Release Left Shift
                INPUT input = {0};
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = VK_LSHIFT;
                input.ki.wScan = MapVirtualKey(VK_LSHIFT, MAPVK_VK_TO_VSC);
                input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
                input.ki.dwExtraInfo = GetMessageExtraInfo();
                SendInput(1, &input, sizeof(INPUT));
                print_colored("[Debug Mission] LeftShift UP\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
            }
            debug_mission_prev_pressed = debug_mission_now;
            // ...existing code for credit button and rest of loop...
        }
        // Credit repeat logic
        if (config.credit_button >= 0) {
            if (credit_pressed) {
                auto now = std::chrono::steady_clock::now();
                if (!credit_prev_pressed || std::chrono::duration_cast<std::chrono::milliseconds>(now - last_credit_time).count() >= 50) {
                    // Send [ key (VK_OEM_4) using scan code
                    INPUT input = {0};
                    input.type = INPUT_KEYBOARD;
                    input.ki.wVk = VK_OEM_4;
                    input.ki.wScan = MapVirtualKey(VK_OEM_4, MAPVK_VK_TO_VSC);
                    input.ki.dwFlags = KEYEVENTF_SCANCODE;
                    input.ki.dwExtraInfo = GetMessageExtraInfo();
                    SendInput(1, &input, sizeof(INPUT));
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
                    SendInput(1, &input, sizeof(INPUT));
                    print_colored("[Credit] [ key sent\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                    last_credit_time = now;
                }
                credit_prev_pressed = true;
            } else {
                credit_prev_pressed = false;
            }
        }
        if (fgWnd == consoleWnd || fgWnd == parentWnd) {
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                print_colored("Esc pressed. Exiting...\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
                SDL_JoystickClose(joy);
                SDL_Quit();
                return 0;
            }
            // Settings menu hotkey: Tab
            if (GetAsyncKeyState(VK_TAB) & 0x8000) {
                system("cls");
                print_colored("\nTab pressed. Opening settings menu...\n", FOREGROUND_LIME);
                settings_menu(config, "mascon_translator.cfg", mode, selected_id, num_joysticks);
                lang = config.language; // Update language after settings menu
                // Refresh header after returning from settings
                system("cls");
                print_colored("=================================\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                print_colored("  Mascon Lever Input Translator\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                print_colored("=================================\n", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                std::cout << "Using joystick #";
                print_colored(std::to_string(selected_id), FOREGROUND_PINK | FOREGROUND_INTENSITY);
                std::cout << ": ";
                std::cout << SDL_JoystickNameForIndex(selected_id);
                std::cout << std::endl;
                std::cout << "Output mode: ";
                print_colored((mode == 0 ? tr("Up/Down Arrow Keys", lang) : tr("Mouse Scroll", lang)), FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
                std::cout << std::endl;
                std::cout << "---------------------------------\n";
                std::cout << tr("Press ", lang);
                print_colored(tr("Tab", lang), FOREGROUND_LIME);
                std::cout << tr(" to open settings menu.", lang) << std::endl;
                std::cout << tr("Press ", lang);
                print_colored(tr("Esc", lang), FOREGROUND_RED | FOREGROUND_INTENSITY);
                std::cout << tr(" to exit.", lang) << std::endl;
                std::cout << "---------------------------------\n";
                print_colored("Input translation is active! Move the lever to send input ^w^\n\n", FOREGROUND_PINK | FOREGROUND_INTENSITY);
                std::this_thread::sleep_for(std::chrono::milliseconds(300)); // debounce
            }
        }
        // Lever/arrow/mouse logic should always run, regardless of focus
        SDL_JoystickUpdate();
        pressed.clear();
        int num_buttons = SDL_JoystickNumButtons(joy);
        for (int i = 0; i < num_buttons; ++i) {
            if (SDL_JoystickGetButton(joy, i)) {
                pressed.insert(i);
            }
        }
        int idx = match_combo(pressed, ordered_combos);
        if (mode == 2 && idx >= 0 && idx < 15) {
            int vk = config.lever_keycodes[idx];
            if (vk > 0) {
                // Send the key as a press and release
                INPUT input = {0};
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = vk;
                input.ki.dwFlags = 0;
                SendInput(1, &input, sizeof(INPUT));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                input.ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(1, &input, sizeof(INPUT));
                print_colored("[Lever-to-Key] Sent key VK=0x" + std::to_string(vk) + "\n", COLOR_PINK);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            continue;
        }
        auto now = std::chrono::steady_clock::now();
        if (idx != stable_idx) {
            stable_idx = idx;
            last_event_time = now;
        }
        // Debounce logic: Only config.debounce_ms is used for debounce timing.
        // up_down_delay_ms and mouse_scroll_delay_ms are NOT used for debounce.
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_event_time).count();
        if (idx != -1 && idx != last_idx && elapsed >= config.debounce_ms) {
            if (last_idx != -1) {
                int diff = idx - last_idx;
                // Only move one step per debounce period for consistent timing
                int step = (diff > 0) ? 1 : -1;
                int next_idx = last_idx + step;
                if (mode == 0) {
                    sendArrowKey((step > 0) ? VK_DOWN : VK_UP, config.key_hold_time_ms);
                    std::this_thread::sleep_for(std::chrono::milliseconds(config.up_down_delay_ms));
                } else if (mode == 1) {
                    sendMouseScroll((step > 0) ? -120 : 120);
                    std::this_thread::sleep_for(std::chrono::milliseconds(config.mouse_scroll_delay_ms));
                }
                print_colored(names[last_idx] + " -> " + names[next_idx] + " : ", (step > 0) ? (FOREGROUND_YELLOW | FOREGROUND_INTENSITY) : (FOREGROUND_CYAN | FOREGROUND_INTENSITY));
                print_colored((step > 0) ? "v" : "^", (step > 0) ? (FOREGROUND_GREEN | FOREGROUND_INTENSITY) : (FOREGROUND_PINK | FOREGROUND_INTENSITY));
                std::cout << std::endl;
                last_idx = next_idx;
            } else if (idx == 9) {
                print_colored(tr("Neutral position!", lang) + "\n", FOREGROUND_PINK | FOREGROUND_INTENSITY);
                last_idx = idx;
            }
            last_event_time = std::chrono::steady_clock::now();
        }
        last_pressed = pressed;
        // No sleep for high-frequency polling
    }
    SDL_JoystickClose(joy);
    SDL_Quit();
    return 0;
}
