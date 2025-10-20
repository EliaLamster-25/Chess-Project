// stockfish_engine.cpp
#include "stockfish.hpp"
#include <iostream>
#include <sstream>

StockfishEngine::StockfishEngine(const std::wstring& path_to_exe) : path(path_to_exe) {
    hChildStdInRead = NULL;
    hChildStdInWrite = NULL;
    hChildStdOutRead = NULL;
    hChildStdOutWrite = NULL;
    ZeroMemory(&pi, sizeof(pi));
}

StockfishEngine::~StockfishEngine() {
    closeHandles();
}

bool StockfishEngine::initialize() {
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hChildStdOutRead, &hChildStdOutWrite, &saAttr, 0)) {
        std::cerr << "CreatePipe for stdout failed" << std::endl;
        return false;
    }

    if (!SetHandleInformation(hChildStdOutRead, HANDLE_FLAG_INHERIT, 0)) {
        std::cerr << "SetHandleInformation for stdout read failed" << std::endl;
        return false;
    }

    if (!CreatePipe(&hChildStdInRead, &hChildStdInWrite, &saAttr, 0)) {
        std::cerr << "CreatePipe for stdin failed" << std::endl;
        return false;
    }

    if (!SetHandleInformation(hChildStdInWrite, HANDLE_FLAG_INHERIT, 0)) {
        std::cerr << "SetHandleInformation for stdin write failed" << std::endl;
        return false;
    }

    STARTUPINFO siStartInfo;
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.hStdError = hChildStdOutWrite;
    siStartInfo.hStdOutput = hChildStdOutWrite;
    siStartInfo.hStdInput = hChildStdInRead;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    BOOL bSuccess = CreateProcessW(NULL,
        const_cast<wchar_t*>(path.c_str()),
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,  // Add this to prevent console window pop-up
        NULL,
        NULL,
        &siStartInfo,
        &pi);

    if (!bSuccess) {
        std::cerr << "CreateProcess failed (" << GetLastError() << ")" << std::endl;
        return false;
    }

    CloseHandle(hChildStdOutWrite);
    CloseHandle(hChildStdInRead);

    // Initialize UCI
    writeToPipe("uci\r\n");  // Try with \r\n
    Sleep(1000);  // Wait 1 second after sending
    std::string line;
    bool uciOkFound = false;
    auto start = GetTickCount64();
    while (GetTickCount64() - start < 5000) { // wait up to 5 seconds
        std::string line = readLine();
        if (!line.empty()) {
            std::cerr << "UCI response: " << line << std::endl;
            if (line == "uciok") {
                uciOkFound = true;
                break;
            }
        }
        else {
            Sleep(50);
        }
    }

    writeToPipe("isready\r\n");
    Sleep(500);  // Wait 0.5 second
    bool readyOkFound = false;
    while ((line = readLine()) != "") {
        std::cerr << "Isready response: " << line << std::endl;
        if (line == "readyok") {
            readyOkFound = true;
            break;
        }
    }
    if (!readyOkFound) {
        std::cerr << "Failed to receive readyok" << std::endl;
        return false;
    }

    return true;
}

std::string StockfishEngine::getNextMove(int thinkingTimeMs) {
    std::stringstream posCmd;
    posCmd << "position startpos";
    if (!moves.empty()) {
        posCmd << " moves";
        for (const auto& m : moves) {
            posCmd << " " << m;
        }
    }
    posCmd << "\r\n";
    writeToPipe(posCmd.str());
    Sleep(1000);  // Add delay

    writeToPipe("isready\r\n");
    Sleep(1000);
    std::string line;
    while ((line = readLine()) != "readyok") {
        std::cerr << "Position isready response: " << line << std::endl;
        if (line.empty()) {
            std::cerr << "Error waiting for readyok" << std::endl;
            return "";
        }
    }

    std::stringstream goCmd;
    goCmd << "go movetime " << thinkingTimeMs << "\r\n";
    writeToPipe(goCmd.str());
    Sleep(1000);

    std::string bestMove;
    while (true) {
        line = readLine();
        std::cerr << "Go response: " << line << std::endl;
        if (line.empty()) {
            std::cerr << "Error reading bestmove" << std::endl;
            return "";
        }
        if (line.rfind("bestmove ", 0) == 0) {
            size_t start = 9; // After "bestmove "
            size_t end = line.find(" ", start);
            bestMove = line.substr(start, (end == std::string::npos) ? end : end - start);
            break;
        }
    }

    moves.push_back(bestMove);
    return bestMove;
}

void StockfishEngine::opponentMove(const std::string& move) {
    moves.push_back(move);
}

void StockfishEngine::reset() {
    moves.clear();
}

void StockfishEngine::writeToPipe(const std::string& cmd) {
    DWORD dwWritten;
    BOOL success = WriteFile(hChildStdInWrite, cmd.c_str(), static_cast<DWORD>(cmd.size()), &dwWritten, NULL);
    if (!success || dwWritten != cmd.size()) {
        std::cerr << "Failed to write to pipe: " << cmd << " (wrote " << dwWritten << " bytes)" << std::endl;
    }
}

std::string StockfishEngine::readLine() {
    while (true) {
        size_t pos = outputBuffer.find('\n');
        if (pos != std::string::npos) {
            std::string line = outputBuffer.substr(0, pos);
            outputBuffer.erase(0, pos + 1);
            // Trim trailing \r if present
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return line;
        }

        char buf[1024];
        DWORD dwRead;
        BOOL bSuccess = ReadFile(hChildStdOutRead, buf, 1024, &dwRead, NULL);
        if (!bSuccess || dwRead == 0) {
            std::cerr << "ReadFile failed or end of file (" << GetLastError() << ")" << std::endl;
            return "";
        }
        outputBuffer.append(buf, dwRead);
    }
}

void StockfishEngine::closeHandles() {
    if (pi.hProcess) {
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    if (hChildStdInWrite) CloseHandle(hChildStdInWrite);
    if (hChildStdOutRead) CloseHandle(hChildStdOutRead);
}