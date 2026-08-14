#pragma once

#include "io/io_backend.h"

#include <memory>

namespace skl {

enum class BackendType : int {
    Read  = 0,
    Pread = 1,
    Mmap  = 2,
    Aio   = 3,
    Uring = 4,
};

constexpr int kBackendTypeCount = 5;

// 콤보박스 라벨 — UI가 BeginCombo + Selectable 패턴으로 표시.
inline constexpr const char* kBackendTypeLabels[kBackendTypeCount] = {
    "read", "pread", "mmap", "aio", "io_uring",
};

// 컴파일 타임에 해당 backend가 빌드에 들어갔는지. UI가 비활성 옵션을
// gray 표시하는 데 쓴다. *런타임 가용성* 은 별개 — io_uring은 커널이
// 지원하지 않으면 인스턴스 생성에서 throw, factory가 catch해 폴백.
bool BackendCompiledIn(BackendType type);

// type에 해당하는 backend 인스턴스를 만든다. 요청한 backend가 런타임에
// 초기화되지 않으면 ReadBackend로 폴백하고 actualType에 실제 타입을 쓴다.
// 호출자는 요청 타입이 아니라 actualType을 HUD와 측정 메타데이터에 기록해야 한다.
std::unique_ptr<IIoBackend> CreateBackend(
    BackendType type, BackendType* actualType = nullptr);

}  // namespace skl
