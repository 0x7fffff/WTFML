/**
 * @file StallSampler.cpp
 * @brief 主线程卡住时把它的调用栈走一遍（测量构建专用，见 PerfProbe.cpp）。
 *
 * 办法是采样分析器那一套：另一个线程看主线程的心跳，心跳停了就
 * `SuspendThread` → `GetThreadContext` → `StackWalk64` → `ResumeThread`。
 *
 * 两个已知的取舍：
 *  1. **暂停期间只走栈、不查符号**。dbghelp 查符号要分配内存，主线程要是正卡在
 *     分配器里（这类"卡住"很常见），暂停它再去要内存就可能互等。所以先只取地址，
 *     `ResumeThread` 之后再补名字；对系统 DLL 能补出函数名，对本程序只有地址。
 *     本程序的地址事后交给 `tools/symbolize-stalls.ps1`（addr2line）解析。
 *  2. 一次卡顿最多采一份栈（同一个卡顿里反复采只会得到同一个答案），
 *     一次测量最多留 32 份，免得把报告撑爆。
 */

#include "perf/StallSampler.hpp"

#include <QJsonArray>
#include <QString>

#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace {

constexpr int kPollMs = 10;
constexpr int kMaxFrames = 24;
constexpr int kMaxSamples = 32;
constexpr long long kWaitResumeMs = 5000;

struct Frame {
    QString module;
    quint64 rva = 0;
    /// 运行时地址（模块基址 + RVA）。只在补名字时用，报告里不写 —— 基址随 ASLR 变。
    quint64 address = 0;
    QString symbol;
};

struct Sample {
    qint64 gapMs = -1;
    int stepIndex = 0;
    std::vector<Frame> frames;
};

std::atomic<bool> g_running{false};
std::atomic<long long> g_lastBeatMs{0};
std::atomic<int> g_stepIndex{0};
DWORD g_mainThreadId = 0;
int g_thresholdMs = 50;
std::thread g_thread;
std::mutex g_mutex;
std::vector<Sample> g_samples;

long long nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

QString moduleName(HMODULE module)
{
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(module, path, MAX_PATH) == 0) {
        return QStringLiteral("?");
    }
    const QString full = QString::fromWCharArray(path);
    const qsizetype slash = full.lastIndexOf(QLatin1Char('\\'));
    return slash >= 0 ? full.mid(slash + 1) : full;
}

/// 在暂停期间把主线程的栈走一遍。地址只记模块 + RVA（模块基址随 ASLR 变，RVA 不变）。
void walk(HANDLE thread, Sample &sample)
{
    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(thread, &context)) {
        return;
    }
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    const HANDLE process = GetCurrentProcess();
    for (int depth = 0; depth < kMaxFrames; ++depth) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            break;
        }
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                               | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(frame.AddrPC.Offset), &module);
        Frame entry;
        entry.module = module != nullptr ? moduleName(module) : QStringLiteral("?");
        entry.rva = module != nullptr ? frame.AddrPC.Offset - reinterpret_cast<quint64>(module) : 0;
        entry.address = frame.AddrPC.Offset;
        sample.frames.push_back(entry);
    }
}

Sample capture()
{
    Sample sample;
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                               FALSE, g_mainThreadId);
    if (thread == nullptr) {
        return sample;
    }
    if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
        CloseHandle(thread);
        return sample;
    }
    walk(thread, sample);
    ResumeThread(thread);
    CloseHandle(thread);

    // 已经放行了，这时再问名字是安全的。
    for (Frame &entry : sample.frames) {
        char buffer[sizeof(SYMBOL_INFO) + 256] = {};
        auto *symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        const HANDLE process = GetCurrentProcess();
        if (SymFromAddr(process, entry.address, &displacement, symbol)) {
            // `SYMBOL_INFO::Name` 声明成 `CHAR Name[1]`（名字跟在结构体后面）。
            // 直接写 `fromLatin1(symbol->Name)` 会被 Qt 6 挑成按数组长度取的那个重载，
            // 长度恒为 1 —— 量到的是 "C"、"S" 这种首字母。先退化成指针。
            entry.symbol = QString::fromLatin1(static_cast<const char *>(symbol->Name),
                                               static_cast<qsizetype>(symbol->NameLen));
            // 系统 DLL 的符号表只有导出表：地址落在某个导出函数中间时 dbghelp 给的是
            // "最近的导出 + 偏移"。偏移写出来，读的人才不会当成精确的当前函数。
            if (displacement > 0) {
                entry.symbol += QStringLiteral("+0x%1").arg(displacement, 0, 16);
            }
        }
    }
    return sample;
}

void loop()
{
    bool sampledThisStall = false;
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        const long long beat = g_lastBeatMs.load();
        if (nowMs() - beat <= g_thresholdMs) {
            sampledThisStall = false;
            continue;
        }
        if (sampledThisStall) {
            continue;
        }
        {
            const std::lock_guard<std::mutex> guard(g_mutex);
            if (g_samples.size() >= kMaxSamples) {
                sampledThisStall = true;
                continue;
            }
        }

        Sample sample = capture();
        const int stepIndex = g_stepIndex.load();

        // 放行之后等主线程重新打拍子 —— 这一等才知道它到底卡了多久。
        long long resumedBeat = beat;
        const long long deadline = nowMs() + kWaitResumeMs;
        while (g_running.load() && nowMs() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
            const long long current = g_lastBeatMs.load();
            if (current != beat) {
                resumedBeat = current;
                break;
            }
        }
        if (!sample.frames.empty()) {
            sample.gapMs = resumedBeat - beat;
            sample.stepIndex = stepIndex;
            const std::lock_guard<std::mutex> guard(g_mutex);
            g_samples.push_back(std::move(sample));
        }
        sampledThisStall = true;
    }
}

} // namespace

namespace StallSampler {

void start(int thresholdMs)
{
    if (g_running.exchange(true)) {
        return;
    }
    g_thresholdMs = thresholdMs > 0 ? thresholdMs : 50;
    g_mainThreadId = GetCurrentThreadId();
    g_lastBeatMs.store(nowMs());
    g_stepIndex.store(0);
    {
        const std::lock_guard<std::mutex> guard(g_mutex);
        g_samples.clear();
    }
    SymSetOptions(SYMOPT_UNDNAME);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    g_thread = std::thread(loop);
}

void beat(int stepIndex)
{
    g_stepIndex.store(stepIndex);
    g_lastBeatMs.store(nowMs());
}

void stop()
{
    if (!g_running.exchange(false)) {
        return;
    }
    if (g_thread.joinable()) {
        g_thread.join();
    }
    SymCleanup(GetCurrentProcess());
}

QList<QJsonObject> samples()
{
    QList<QJsonObject> result;
    const std::lock_guard<std::mutex> guard(g_mutex);
    for (const Sample &sample : g_samples) {
        QJsonArray frames;
        for (const Frame &frame : sample.frames) {
            frames.append(frame.symbol.isEmpty()
                              ? QStringLiteral("%1+0x%2").arg(frame.module).arg(frame.rva, 0, 16)
                              : QStringLiteral("%1+0x%2 %3")
                                    .arg(frame.module)
                                    .arg(frame.rva, 0, 16)
                                    .arg(frame.symbol));
        }
        result.append(QJsonObject{{QStringLiteral("gapMs"), sample.gapMs},
                                  {QStringLiteral("stepIndex"), sample.stepIndex},
                                  {QStringLiteral("frames"), frames}});
    }
    return result;
}

} // namespace StallSampler
