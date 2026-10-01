/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * WinAPI archive browser. Only kernel32 is linked; UI APIs are resolved at runtime.
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include "xxfc_readers.h"
#include <xxfclib/strings/xx_string.h>

#define API(ret, name, args) typedef ret (WINAPI *name##_fn) args; static name##_fn ui_##name
API(HCURSOR, LoadCursorW, (HINSTANCE,LPCWSTR));
API(ATOM, RegisterClassW, (const WNDCLASSW *));
API(HWND, CreateWindowExW, (DWORD,LPCWSTR,LPCWSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID));
API(LRESULT, DefWindowProcW, (HWND,UINT,WPARAM,LPARAM));
API(BOOL, DestroyWindow, (HWND));
API(void, PostQuitMessage, (int));
API(BOOL, ShowWindow, (HWND,int));
API(BOOL, GetMessageW, (LPMSG,HWND,UINT,UINT));
API(BOOL, TranslateMessage, (const MSG *));
API(LRESULT, DispatchMessageW, (const MSG *));
API(BOOL, IsDialogMessageW, (HWND,LPMSG));
API(BOOL, MoveWindow, (HWND,int,int,int,int,BOOL));
API(BOOL, GetClientRect, (HWND,LPRECT));
API(BOOL, SetWindowTextW, (HWND,LPCWSTR));
API(int, GetWindowTextW, (HWND,LPWSTR,int));
API(BOOL, EnableWindow, (HWND,BOOL));
API(LRESULT, SendMessageW, (HWND,UINT,WPARAM,LPARAM));
API(UINT_PTR, SetTimer, (HWND,UINT_PTR,UINT,TIMERPROC));
API(BOOL, KillTimer, (HWND,UINT_PTR));
API(BOOL, GetOpenFileNameW, (LPOPENFILENAMEW));
API(DWORD, CommDlgExtendedError, (void));

#define PATH_CAP 32768
#define TEXT_CAP (4 * 1024 * 1024)
#define MEMBER_LIMIT 100000
#define ID_BROWSE 101
#define ID_OPEN 102
static HWND path_edit, browse_button, open_button, contents, status_label;
static HINSTANCE instance;
static HANDLE worker;
static wchar_t archive_path[PATH_CAP];
/* Worker owns these until its handle is signaled. Only the UI reads afterwards. */
static wchar_t *listing;
static const wchar_t *result_status;
static size_t used;

static BOOL append(const wchar_t *text) {
    size_t n = 0;
    while (text[n]) ++n;
    if (n >= TEXT_CAP - used) return FALSE;
    while (*text) listing[used++] = *text++;
    listing[used] = 0;
    return TRUE;
}

static void number(uint64_t value) {
    wchar_t digits[32];
    unsigned n = 0;
    do { digits[n++] = (wchar_t)(L'0' + value % 10); value /= 10; } while (value);
    while (n) { wchar_t digit[2] = {digits[--n], 0}; append(digit); }
}

static DWORD WINAPI read_archive(void *unused) {
    xx_io_device *device = NULL;
    xxfc_opened opened = {0};
    xx_archive_record_state *state = NULL;
    char *path = NULL;
    unsigned count = 0;
    BOOL incomplete_notice = FALSE;
    (void)unused;
    used = 0;
    listing = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, TEXT_CAP * sizeof(wchar_t));
    result_status = L"Out of memory.";
    if (!listing) return 0;
    path = xx_str_unicode_to_utf8(archive_path);
    if (!path) goto done;
    result_status = L"Cannot open the file.";
    device = xx_io_file_open(path, "rb");
    if (!device) goto done;
    result_status = L"Unknown format or reader unavailable.";
    if (!xxfc_open(&opened, device, 0)) goto done;
    result_status = L"Archive header is invalid or could not be parsed.";
    if (!xx_format_is_valid(opened.format, NULL) ||
        !xx_format_handle_base_info(opened.format, NULL)) goto done;
    xxfc_attach_source_files(&opened, path);
    result_status = L"This file is not an archive.";
    if (!opened.format->is_archive) goto done;
    result_status = L"Cannot read archive members.";
    state = xx_format_create_archive_records_reading(opened.format, NULL, NULL);
    if (!state) goto done;
    append(L"Format: ");
    {
        wchar_t *type = xx_str_utf8_to_unicode(xx_format_file_type_to_string(opened.type));
        if (type) { append(type); xx_str_free_unicode(type); }
    }
    append(L"\r\n\r\nSize (bytes)\tPacked (bytes)\tName\r\n");
    if (xxfc_is_incomplete(&opened)) {
        append(L"[Incomplete archive: showing recovered members]\r\n");
        incomplete_notice = TRUE;
    }
    result_status = L"Archive opened.";
    for (;;) {
        const xx_archive_record *record = xx_format_get_current_archive_record(opened.format, state);
        const wchar_t *name;
        wchar_t *owned = NULL;
        size_t name_len = 0;
        if (!record) break;
        name = xx_archive_record_get_original_name_w(record);
        if (!name || !*name) {
            const char *utf8 = xx_archive_record_get_original_name(record);
            if (utf8) owned = xx_str_utf8_to_unicode(utf8);
            name = owned && *owned ? owned : L"<unnamed>";
        }
        while (name[name_len]) ++name_len;
        /* Reserve space for sizes and footer before writing a complete row. */
        if (name_len + 256 >= TEXT_CAP - used || count == MEMBER_LIMIT) {
            xx_str_free_unicode(owned);
            result_status = L"Listing truncated at the display limit.";
            append(L"\r\n[Listing truncated]\r\n");
            break;
        }
        { uint64_t size = xx_archive_record_get_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, UINT64_MAX);
          if (size == UINT64_MAX) append(L"?"); else number(size); } append(L"\t");
        if (record->compressed_size < 0) append(L"?"); else number((uint64_t)record->compressed_size); append(L"\t");
        append(name); append(L"\r\n");
        xx_str_free_unicode(owned);
        ++count;
        if (!xx_format_archive_record_move_to_next(opened.format, state, NULL)) break;
    }
    append(L"\r\n"); number(count); append(L" member(s) displayed.\r\n");
    if (!incomplete_notice && xxfc_is_incomplete(&opened))
        append(L"\r\n[Incomplete archive: showing recovered members]\r\n");
done:
    if (state) xx_format_free_archive_records_reading(opened.format, state);
    xxfc_close(&opened);
    if (device) xx_io_close(device);
    xx_str_free(path);
    return 0;
}

static void start_open(HWND window) {
    if (worker) return;
    ui_GetWindowTextW(path_edit, archive_path, PATH_CAP);
    if (!archive_path[0]) { ui_SetWindowTextW(status_label, L"Choose an archive or enter its path."); return; }
    ui_SetWindowTextW(contents, L"");
    ui_SetWindowTextW(status_label, L"Reading archive...");
    ui_EnableWindow(path_edit, FALSE);
    ui_EnableWindow(browse_button, FALSE);
    ui_EnableWindow(open_button, FALSE);
    /* Set the completion timer before starting the thread. */
    if (ui_SetTimer(window, 1, 100, NULL)) worker = CreateThread(NULL, 0, read_archive, NULL, 0, NULL);
    if (!worker) {
        ui_KillTimer(window, 1);
        ui_SetWindowTextW(status_label, L"Could not start archive reader.");
        ui_EnableWindow(path_edit, TRUE); ui_EnableWindow(browse_button, TRUE); ui_EnableWindow(open_button, TRUE);
    }
}

static void browse(HWND window) {
    OPENFILENAMEW dialog = {0};
    static wchar_t chosen[PATH_CAP];
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter = L"Archives\0*.zip;*.7z;*.rar;*.tar;*.gz;*.bz2;*.xz;*.cab;*.iso;*.lha;*.arj\0All files\0*.*\0\0";
    dialog.lpstrFile = chosen;
    dialog.nMaxFile = PATH_CAP;
    dialog.lpstrTitle = L"Open archive";
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (ui_GetOpenFileNameW(&dialog)) {
        ui_SetWindowTextW(path_edit, chosen);
        start_open(window);
    } else if (ui_CommDlgExtendedError()) ui_SetWindowTextW(status_label, L"The file dialog failed. Enter a path and press Open.");
}

static HWND control(HWND parent, const wchar_t *type, const wchar_t *text, DWORD style, int id) {
    return ui_CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
                             0,0,0,0,parent,(HMENU)(INT_PTR)id,instance,NULL);
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_CREATE:
        path_edit = control(window, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 100);
        browse_button = control(window, L"BUTTON", L"&Browse...", WS_TABSTOP, ID_BROWSE);
        open_button = control(window, L"BUTTON", L"&Open", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_OPEN);
        contents = control(window, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_MULTILINE | ES_READONLY |
                           WS_VSCROLL | WS_HSCROLL | ES_AUTOHSCROLL | ES_AUTOVSCROLL, 103);
        status_label = control(window, L"STATIC", L"Choose an archive to view its contents.", 0, 104);
        if (!path_edit || !browse_button || !open_button || !contents || !status_label) return -1;
        ui_SendMessageW(path_edit, EM_SETLIMITTEXT, PATH_CAP - 1, 0);
        ui_SendMessageW(contents, EM_SETLIMITTEXT, TEXT_CAP - 1, 0);
        return 0;
    case WM_SIZE: {
        RECT area; int width, height;
        ui_GetClientRect(window, &area);
        width = area.right > 340 ? area.right : 340;
        height = area.bottom > 140 ? area.bottom : 140;
        ui_MoveWindow(path_edit, 12,12,width-220,26,TRUE);
        ui_MoveWindow(browse_button,width-200,12,100,26,TRUE);
        ui_MoveWindow(open_button,width-88,12,76,26,TRUE);
        ui_MoveWindow(contents,12,50,width-24,height-90,TRUE);
        ui_MoveWindow(status_label,12,height-30,width-24,22,TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED && LOWORD(wp) == ID_BROWSE && !worker) browse(window);
        if (HIWORD(wp) == BN_CLICKED && (LOWORD(wp) == ID_OPEN || LOWORD(wp) == IDOK)) start_open(window);
        return 0;
    case WM_TIMER:
        if (worker && WaitForSingleObject(worker, 0) == WAIT_OBJECT_0) {
            CloseHandle(worker); worker = NULL; ui_KillTimer(window,1);
            ui_SetWindowTextW(contents, listing ? listing : L"");
            ui_SetWindowTextW(status_label, result_status);
            if (listing) HeapFree(GetProcessHeap(),0,listing);
            listing = NULL;
            ui_EnableWindow(path_edit,TRUE); ui_EnableWindow(browse_button,TRUE); ui_EnableWindow(open_button,TRUE);
        }
        return 0;
    case WM_CLOSE: ui_DestroyWindow(window); return 0;
    case WM_DESTROY: ui_PostQuitMessage(0); return 0;
    }
    return ui_DefWindowProcW(window,message,wp,lp);
}

void WINAPI xxfc_gui_entry(void) {
    HMODULE user32 = LoadLibraryExW(L"user32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE comdlg32 = LoadLibraryExW(L"comdlg32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    WNDCLASSW wc = {0};
    HWND window;
    MSG msg;
    BOOL message_result;
    if (!user32 || !comdlg32) ExitProcess(1);
#define LOAD(module, name) ui_##name = (name##_fn)GetProcAddress(module, #name); if (!ui_##name) ExitProcess(1)
    LOAD(user32, LoadCursorW); LOAD(user32, RegisterClassW); LOAD(user32, CreateWindowExW); LOAD(user32, DefWindowProcW);
    LOAD(user32, DestroyWindow); LOAD(user32, PostQuitMessage); LOAD(user32, ShowWindow);
    LOAD(user32, GetMessageW); LOAD(user32, TranslateMessage); LOAD(user32, DispatchMessageW);
    LOAD(user32, IsDialogMessageW); LOAD(user32, MoveWindow); LOAD(user32, GetClientRect);
    LOAD(user32, SetWindowTextW); LOAD(user32, GetWindowTextW); LOAD(user32, EnableWindow);
    LOAD(user32, SendMessageW); LOAD(user32, SetTimer); LOAD(user32, KillTimer);
    LOAD(comdlg32, GetOpenFileNameW); LOAD(comdlg32, CommDlgExtendedError);
#undef LOAD
    instance = GetModuleHandleW(NULL);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = ui_LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"xxfc_archive_gui";
    if (!ui_RegisterClassW(&wc)) ExitProcess(1);
    window = ui_CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"xxfclib - Archive browser",
                               WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,CW_USEDEFAULT,960,640,NULL,NULL,instance,NULL);
    if (!window) ExitProcess(1);
    ui_ShowWindow(window, SW_SHOWDEFAULT);
    while ((message_result = ui_GetMessageW(&msg,NULL,0,0)) > 0) {
        if (!ui_IsDialogMessageW(window,&msg)) { ui_TranslateMessage(&msg); ui_DispatchMessageW(&msg); }
    }
    /* Closing exits the process even during parsing; no worker touches UI objects. */
    ExitProcess(message_result == -1 ? 1 : 0);
}
