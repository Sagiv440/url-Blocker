#pragma once
// Minimal dependency-free terminal UI using ANSI escape codes.
// Supports Linux (termios) and Windows (Console API with VT processing).

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

// ANSI color/style codes
namespace ansi {
    constexpr const char* RESET    = "\033[0m";
    constexpr const char* BOLD     = "\033[1m";
    constexpr const char* DIM      = "\033[2m";
    constexpr const char* REVERSE  = "\033[7m";

    constexpr const char* RED      = "\033[31m";
    constexpr const char* GREEN    = "\033[32m";
    constexpr const char* YELLOW   = "\033[33m";
    constexpr const char* BLUE     = "\033[34m";
    constexpr const char* MAGENTA  = "\033[35m";
    constexpr const char* CYAN     = "\033[36m";
    constexpr const char* WHITE    = "\033[37m";

    constexpr const char* BRED     = "\033[91m";
    constexpr const char* BGREEN   = "\033[92m";
    constexpr const char* BYELLOW  = "\033[93m";
    constexpr const char* BCYAN    = "\033[96m";
    constexpr const char* BWHITE   = "\033[97m";

    constexpr const char* BG_BLUE  = "\033[44m";
    constexpr const char* BG_BLACK = "\033[40m";

    constexpr const char* HIDE_CURSOR   = "\033[?25l";
    constexpr const char* SHOW_CURSOR   = "\033[?25h";
    constexpr const char* ALT_SCREEN    = "\033[?1049h";
    constexpr const char* NORMAL_SCREEN = "\033[?1049l";
    constexpr const char* CLEAR         = "\033[2J\033[H";
    constexpr const char* HOME          = "\033[H";
    constexpr const char* CLEAR_LINE    = "\033[2K";
    constexpr const char* CLEAR_EOL     = "\033[K";
}

class Tui {
public:
    void init() {
#ifdef _WIN32
        hOut_ = GetStdHandle(STD_OUTPUT_HANDLE);
        GetConsoleMode(hOut_, &orig_out_mode_);
        // Enable ANSI escape code processing and suppress automatic newline on line wrap
        SetConsoleMode(hOut_, orig_out_mode_
            | ENABLE_VIRTUAL_TERMINAL_PROCESSING
            | DISABLE_NEWLINE_AUTO_RETURN);

        hIn_ = GetStdHandle(STD_INPUT_HANDLE);
        GetConsoleMode(hIn_, &orig_in_mode_);
        SetConsoleMode(hIn_, 0); // raw mode: no echo, no line buffering
#else
        tcgetattr(STDIN_FILENO, &orig_);
        termios raw = orig_;
        raw.c_lflag &= ~(uint32_t)(ICANON | ECHO | ISIG);
        raw.c_cc[VMIN]  = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
#endif
        write_raw(ansi::ALT_SCREEN);
        write_raw(ansi::HIDE_CURSOR);
        write_raw(ansi::CLEAR);
    }

    void fini() {
        write_raw(ansi::SHOW_CURSOR);
        write_raw(ansi::NORMAL_SCREEN);
#ifdef _WIN32
        SetConsoleMode(hOut_, orig_out_mode_);
        SetConsoleMode(hIn_,  orig_in_mode_);
#else
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_);
#endif
    }

    void get_size(int& rows, int& cols) const {
#ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO csbi{};
        if (GetConsoleScreenBufferInfo(hOut_, &csbi)) {
            cols = csbi.srWindow.Right  - csbi.srWindow.Left + 1;
            rows = csbi.srWindow.Bottom - csbi.srWindow.Top  + 1;
        } else {
            rows = 24; cols = 80;
        }
#else
        winsize ws{};
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
        rows = ws.ws_row > 0 ? ws.ws_row : 24;
        cols = ws.ws_col > 0 ? ws.ws_col : 80;
#endif
    }

    // Non-blocking key read; returns -1 if no key pending.
    int getch() {
#ifdef _WIN32
        if (_kbhit()) return _getch();
        return -1;
#else
        unsigned char c = 0;
        return (read(STDIN_FILENO, &c, 1) == 1) ? c : -1;
#endif
    }

    // --- Frame buffering ---
    // Call begin_frame() → emit() calls → end_frame() each render cycle.

    void begin_frame() {
        buf_.clear();
        buf_.reserve(16384);
        buf_ += ansi::HOME;
    }

    void end_frame() {
        fwrite(buf_.data(), 1, buf_.size(), stdout);
        fflush(stdout);
    }

    // Move cursor to 1-based (row, col)
    void move(int row, int col) {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "\033[%d;%dH", row, col);
        buf_ += tmp;
    }

    void emit(const char* s)        { buf_ += s; }
    void emit(const std::string& s) { buf_ += s; }

    // Printf-style emit
#ifdef __GNUC__
    void emitf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
#else
    void emitf(const char* fmt, ...) {
#endif
        char tmp[1024];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(tmp, sizeof(tmp), fmt, ap);
        va_end(ap);
        buf_ += tmp;
    }

    // Fill a row with a horizontal rule character
    void hline(int row, int cols, char c = '-', const char* color = nullptr) {
        move(row, 1);
        if (color) buf_ += color;
        buf_.append(cols, c);
        if (color) buf_ += ansi::RESET;
    }

    // Print text at position, right-padding or truncating to `width` chars
    void print_at(int row, int col, int width, const char* color,
                  const std::string& text) {
        move(row, col);
        if (color) buf_ += color;
        if (width <= 0) {
            buf_ += text;
        } else {
            const int len = static_cast<int>(text.size());
            if (len >= width) buf_.append(text, 0, width);
            else { buf_ += text; buf_.append(width - len, ' '); }
        }
        if (color) buf_ += ansi::RESET;
    }

    // Fill the rest of the current line with spaces (to visually overwrite stale content)
    void clear_eol() { buf_ += ansi::CLEAR_EOL; }

private:
#ifdef _WIN32
    HANDLE hOut_          = INVALID_HANDLE_VALUE;
    HANDLE hIn_           = INVALID_HANDLE_VALUE;
    DWORD  orig_out_mode_ = 0;
    DWORD  orig_in_mode_  = 0;
#else
    termios orig_{};
#endif
    std::string buf_;

    static void write_raw(const char* s) {
        fwrite(s, 1, strlen(s), stdout);
        fflush(stdout);
    }
};
