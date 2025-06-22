# Mascon Lever Input Translator

An easy-to-use Windows console application that translates lever and button inputs from a mascon (train controller) or joystick into keyboard and mouse events, enabling use with PC games and emulators. 
This is based on a python script created by andrewe1 in the Densha De GO!! Unofficial discord.

## Features

- **Joystick/mascon to keyboard/mouse translation**  
  Map lever positions to various input methods.
- **Special input mapping (for arcade players)**  
  Assign joystick buttons to inputs like the big horn, small horn, credit, test menu, and debug mission select.
- **Profile management**  
  Create, switch, copy, and delete configuration profiles for different controllers and/or games.
- **Multi-language support**  
  Menus and messages are available in 16+ languages **(note: translations are AI-generated. there may be missing or incorrect translations)**.

- **Output mode selection**  
  Choose between Arrow Keys, Mouse Scroll, or Lever-to-Key output modes.
- **Joystick selection**  
  Select which connected joystick or mascon to use.
- **Remap lever positions**  
  Assign custom button combinations to each lever position.

- **Parameter adjustment**  
  Fine-tune various parameters to balance input responsiveness and stability:
    - Debounce time
    - Up/Down arrow delay adjustment
    - Mouse scroll delay adjustment

## Requirements

- Windows 7 or later
- [SDL2](https://www.libsdl.org/) library (see build instructions)
- A compatible joystick or mascon controller

## Building

### Installing g++ with MSYS2

1. Download and install [MSYS2](https://www.msys2.org/).
2. Open the MSYS2 MinGW 64-bit terminal (not the default MSYS terminal).
3. Update the package database and core system packages:
   ```
   pacman -Syu
   ```
   (If prompted, close the terminal and reopen it, then run the command again.)
4. Install the 64-bit MinGW g++ compiler:
   ```
   pacman -S mingw-w64-x86_64-gcc
   ```
5. Add the MSYS2 MinGW 64-bit `bin` directory to your Windows PATH environment variable. This is usually:
   ```
   C:\msys64\mingw64\bin
   ```
6. Open a new terminal or command prompt and run:
   ```
   g++ --version
   ```
   to verify the installation.

### Build the project

1. Make sure SDL2 development libraries are installed and available.
2. Open a terminal in the project directory.
3. Build using the provided command (adjust paths as needed):

   ```
   g++ -std=c++11 -IC:/libs/SDL2/x86_64-w64-mingw32/include/SDL2 -I./include -I. -LC:/libs/SDL2/x86_64-w64-mingw32/lib Untitled-1.cpp -lmingw32 -lSDL2main -lSDL2 -o .\build\mascon_translator.exe
   ```

## Usage

1. Run `mascon_translator.exe`.
2. On first launch, select your language, joystick, and input mode.
3. Use the lever and buttons to send keyboard/mouse events to your games.
4. Press `Tab` to open the settings menu at any time.
5. Use the profile system to save and switch between different configurations.

## Configuration

- Settings are saved in `mascon_translator.cfg`.
- Translation files are in the `lang/` directory (`lang_xx.json`).
- All user-facing text is translatable; you can add or improve translations by editing these files.

## License

This project is provided as-is, with no warranty.  
SDL2 is licensed under the zlib license.

# Note: I am not a developer, so the code for this is an absolute mess. While I did put in many, many hours refining this code and getting it to run and function properly, AI was used heavily in this process. I am also not an avid github user, so I apologize for any issues with the way this was uploaded.
