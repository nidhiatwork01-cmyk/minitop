#include "minitop/keyboard.h"

#ifdef _WIN32
#include <conio.h>

namespace minitop {

void InitKeyboard() {
    // Windows Console handles unbuffered input natively via conio.h
}

void RestoreKeyboard() {
    // No-op for Windows
}

bool KeyPressed() {
    return _kbhit() != 0;
}

char GetChar() {
    return static_cast<char>(_getch());
}

} // namespace minitop

#else

#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <cstdio>

namespace minitop {

static struct termios orig_termios;
static bool raw_mode_active = false;

void InitKeyboard() {
    if (raw_mode_active) return;
    if (tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
        struct termios raw = orig_termios;
        raw.c_lflag &= ~(ECHO | ICANON); // Disable terminal echoing and canonical line-buffering
        raw.c_cc[VMIN] = 1;              // Read minimum 1 char
        raw.c_cc[VTIME] = 0;             // Read timeout 0
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        raw_mode_active = true;
    }
}

void RestoreKeyboard() {
    if (!raw_mode_active) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    raw_mode_active = false;
}

bool KeyPressed() {
    struct timeval tv = {0, 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

char GetChar() {
    char ch = 0;
    if (read(STDIN_FILENO, &ch, 1) < 0) {
        return 0;
    }
    return ch;
}

} // namespace minitop

#endif
