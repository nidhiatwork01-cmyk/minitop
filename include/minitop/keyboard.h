#ifndef MINITOP_KEYBOARD_H
#define MINITOP_KEYBOARD_H

namespace minitop {

// Prepares the terminal for non-blocking raw input (disables buffering/echoing on Linux)
void InitKeyboard();

// Restores the original terminal state on exit
void RestoreKeyboard();

// Non-blocking check for pending keyboard input
bool KeyPressed();

// Fetches the pressed character without blocking
char GetChar();

} // namespace minitop

#endif // MINITOP_KEYBOARD_H
