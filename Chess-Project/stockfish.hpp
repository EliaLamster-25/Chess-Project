// stockfish_engine.hpp
#ifndef STOCKFISH_ENGINE_HPP
#define STOCKFISH_ENGINE_HPP

#include <string>
#include <vector>
#include <windows.h>

class StockfishEngine {
public:
    StockfishEngine(const std::wstring& path_to_exe);
    ~StockfishEngine();

    bool initialize();
    std::string getNextMove(int thinkingTimeMs = 1000);
    void opponentMove(const std::string& move);
    void reset();

private:
    std::wstring path;
    std::vector<std::string> moves;
    HANDLE hChildStdInRead;
    HANDLE hChildStdInWrite;
    HANDLE hChildStdOutRead;
    HANDLE hChildStdOutWrite;
    PROCESS_INFORMATION pi;
    std::string outputBuffer;

    void writeToPipe(const std::string& cmd);
    std::string readLine();
    void closeHandles();
};

#endif // STOCKFISH_ENGINE_HPP