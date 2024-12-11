# Zen Text Editor

Zen Text Editor is a lightweight terminal-based text editor written in C, inspired by classic text editors like `vim` and `nano`. It provides basic text editing features, syntax highlighting, and a simple, intuitive interface.

This project is implemented in just **1571 lines of code** in a single C file, showcasing the power of simplicity.

## Features

- **Terminal-Based Interface:** Runs directly in the terminal.
- **Raw Mode Input:** Handles keyboard input for smooth operation.
- **Syntax Highlighting:** Supports syntax highlighting for C, C++, and header files.
- **File Operations:** Open, edit, and save files seamlessly.
- **Search Functionality:** Search for text within a file using `Ctrl-F`.
- **Keyboard Navigation:** Page Up/Down keys for scrolling through the file.
- **Keyboard Shortcuts:**
  - `Ctrl-S` - Save the file.
  - `Ctrl-Q` - Quit the editor.
  - `Ctrl-F` - Find text.
  - Arrow Keys - Navigate the text.
  - Page Up/Down - Scroll through the text.

## Getting Started

### Prerequisites

- A C compiler (e.g., `gcc`).
- A Unix-like terminal (works with `WSL`).

### Installation

1. Clone this repository:
   ```bash
   git clone https://github.com/your-username/zen-text-editor.git
   cd zen-text-editor
   ```

2. Compile the program:
   ```bash
   gcc -o zen_editor zen_editor.c -Wall -Wextra -pedantic -std=c99
   ```

3. Run the editor:
   ```bash
   ./zen_editor [filename]
   ```

   If a file name is provided, the editor will open it. Otherwise, it starts with a blank editor.

## Usage

- **Opening a File:** 
  Run `./zen_editor <filename>` to open an existing file or create a new one.
  
- **Saving Changes:** 
  Press `Ctrl-S` to save your changes.

- **Quitting the Editor:** 
  Press `Ctrl-Q`. If there are unsaved changes, press `Ctrl-Q` multiple times to confirm quitting.

- **Searching for Text:**
  Use `Ctrl-F` to search. Navigate through matches using arrow keys.

- **Scrolling:**
  Use `Page Up` and `Page Down` to quickly navigate through the file.

## Key Bindings

| Key Combination | Action                        |
| --------------- | ----------------------------- |
| `Ctrl-S`        | Save file                     |
| `Ctrl-Q`        | Quit editor                   |
| `Ctrl-F`        | Find text                     |
| `Arrow Keys`    | Move cursor                   |
| `Page Up`       | Scroll up                     |
| `Page Down`     | Scroll down                   |
| `Home`          | Move to the beginning of line |
| `End`           | Move to the end of line       |
| `Ctrl-H`/`Del`  | Delete character              |
| `Ctrl-L`        | Refresh screen                |

## Syntax Highlighting

Zen Text Editor supports syntax highlighting for:
- **C** (`.c` files)
- **C++** (`.cpp` files)
- **Header Files** (`.h`)

The highlighting includes:
- Keywords (`if`, `while`, `return`, etc.)
- Types (`int`, `char`, `void`, etc.)
- Strings and numbers
- Single-line (`//`) and multi-line comments (`/* ... */`)

## Project Structure

- `zen_editor.c`: Main program file containing all the code for the text editor (1571 lines of code).

## License

This project is open-source and available under the [MIT License](LICENSE).

## Acknowledgements

- Made with ❤️ by Harsh Kishorani. 
