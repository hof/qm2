// Windows UCI entry point for the current canonical WSL Release build.
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static HANDLE engine_input;

static int write_input(const char *text) {
    DWORD written;
    size_t remaining = strlen(text);
    while (remaining) {
        if (!WriteFile(engine_input, text, (DWORD)remaining, &written, NULL) || !written) {
            return 0;
        }
        text += written;
        remaining -= written;
    }
    return 1;
}

static DWORD WINAPI forward_input(LPVOID unused) {
    (void)unused;
    char line[65536];
    const char *path_options[] = {"EvalFile", "BookFile", "SyzygyPath"};
    while (fgets(line, sizeof(line), stdin)) {
        for (unsigned i = 0; i < sizeof(path_options) / sizeof(path_options[0]); ++i) {
            char prefix[128];
            snprintf(prefix, sizeof(prefix), "setoption name %s value ", path_options[i]);
            size_t length = strlen(prefix);
            if (strncmp(line, prefix, length)) {
                continue;
            }
            char *value = line + length;
            char converted[65536];
            if (isalpha((unsigned char)value[0]) && value[1] == ':' &&
                    (value[2] == '\\' || value[2] == '/')) {
                snprintf(converted, sizeof(converted), "%.*s/mnt/%c/%s",
                         (int)length, line, tolower((unsigned char)value[0]), value + 3);
                for (char *p = converted + length; *p; ++p) {
                    if (*p == '\\') {
                        *p = '/';
                    }
                }
                if (!write_input(converted)) {
                    goto done;
                }
                goto next_line;
            }
        }
        if (!write_input(line)) {
            break;
        }
next_line:;
    }
done:
    CloseHandle(engine_input);
    return 0;
}

int main(void) {
    wchar_t wsl_path[MAX_PATH];
    if (!GetSystemDirectoryW(wsl_path, MAX_PATH - 9)) {
        return 1;
    }
    lstrcatW(wsl_path, L"\\wsl.exe");
    wchar_t command[] = L"wsl.exe -d Ubuntu --cd /mnt/c/dev/maxima --exec /bin/sh -c \""
        MAXIMA_ENGINE_COMMAND L"\"";
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    HANDLE child_input;
    if (!CreatePipe(&child_input, &engine_input, &security, 0)) {
        return 1;
    }
    SetHandleInformation(engine_input, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = child_input;
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (!CreateProcessW(wsl_path, command, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                        NULL, NULL, &startup, &process)) {
        fprintf(stderr, "Cannot start WSL Maxima: Windows error %lu\n", GetLastError());
        CloseHandle(child_input);
        CloseHandle(engine_input);
        return 1;
    }
    CloseHandle(child_input);
    CloseHandle(process.hThread);
    HANDLE input_thread = CreateThread(NULL, 0, forward_input, NULL, 0, NULL);
    if (!input_thread) {
        CloseHandle(engine_input);
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hProcess);
        return 1;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    CloseHandle(input_thread);
    return (int)exit_code;
}
