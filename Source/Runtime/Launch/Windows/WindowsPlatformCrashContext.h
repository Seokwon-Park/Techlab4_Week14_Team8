#pragma once

#include <filesystem>

namespace WindowsPlatformCrashContext
{
    // 처리되지 않은 예외를 잡아 미니덤프를 남긴다. 처음 덤프 폴더는 <exe 폴더>/Saved/Crashes.
    void RegisterUnhandledExceptionHandler();
    // 덤프 폴더를 바꾼다. 프로젝트가 정해진 뒤 <프로젝트>/Saved/Crashes로 옮길 때 쓴다.
    void SetCrashDumpDirectory(const std::filesystem::path& Directory);

    void CrashDefault();
    void CrashDivideByZero();
    void CrashNullPointer();
    void TriggerStackOverflow();
    void CrashInvalidParameter();
    void CrashPureCall();
    void CrashTerminate();
    void CrashAbort();
}
