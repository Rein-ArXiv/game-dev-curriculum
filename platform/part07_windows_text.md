## 들어가며

UTF-8, UTF-16, 코드 페이지의 기본 개념만으로는 Windows의 텍스트 경계를 안전하게 다루기 어렵다. 왜 `CreateFileA`와 `CreateFileW`가 따로 존재하는지, 한국어 Windows에서 콘솔 출력이 왜 깨지는지, `LoadStringA`가 MBCS 문자열을 어떻게 잘라 먹는지, `strcpy_s`와 `StringCchCopy`의 계약이 무엇이 다른지는 Windows 플랫폼 ABI와 NLS 정책의 문제다.

이 글은 Windows 플랫폼 경계에서 필요한 MBCS 처리, 콘솔 인코딩, 안전한 문자열 함수, locale과 NLS, TCHAR 마이그레이션, COM 경계의 문자열 할당자와 소유권, `WideCharToMultiByte`/`MultiByteToWideChar`의 계약을 다룬다. 공통 Unicode 모델을 반복하는 대신 Windows API가 요구하는 표현과 실패 조건에 집중한다.

전제는 다음과 같다. 내부 인코딩은 UTF-8로 통일하고, Windows API 경계에서만 UTF-16으로 변환한다. 그 변환을 안전하고 손실 없이 수행하기 위해 어떤 함수와 패턴을 알아야 하는가가 이 글의 주제다.


---

## 목차
1. [MBCS (Multi-Byte Character Set) 심화](#1-mbcs-multi-byte-character-set-심화)
2. [LoadString의 함정](#2-loadstring의-함정)
3. [Windows 콘솔 인코딩](#3-windows-콘솔-인코딩)
4. [Locale과 NLS (National Language Support)](#4-locale과-nls-national-language-support)
5. [안전한 문자열 함수](#5-안전한-문자열-함수)
6. [TCHAR 마이그레이션](#6-tchar-마이그레이션)
7. [Windows 문자열 타입 총정리](#7-windows-문자열-타입-총정리)
8. [COM 문자열: 할당자와 소유권](#8-com-문자열-할당자와-소유권)
9. [WideCharToMultiByte와 MultiByteToWideChar 심화](#9-widechartomultibyte와-multibytetowidechar-심화)
10. [파일 시스템과 인코딩](#10-파일-시스템과-인코딩)
11. [실전 패턴 모음](#11-실전-패턴-모음)
12. [문제 해결 체크리스트](#12-문제-해결-체크리스트)


---

## 1. MBCS (Multi-Byte Character Set) 심화

### MBCS의 본질

MBCS는 가변 길이 인코딩의 초기 형태다. 한 문자가 1바이트 또는 2바이트로 표현되며, Windows에서는 주로 CP949(한국어), CP932(일본어), CP936(중국어 간체), CP950(중국어 번체)이 사용된다.

| 코드 페이지 | 언어 | 특징 |
|-------------|------|------|
| CP949 | 한국어 | EUC-KR 확장, 11,172 한글 완성형 |
| CP932 | 일본어 | Shift-JIS 기반 |
| CP936 | 중국어 간체 | GBK |
| CP950 | 중국어 번체 | Big5 |

아래 바이트 예제는 소스/실행 문자 집합이 CP949, 시스템 ANSI 코드 페이지(ACP)가 949, 해당 CRT 로케일(LC_CTYPE)이 설정된 환경을 가정한다. UTF-8 소스로 빌드하거나 MSVC `/utf-8`을 쓰면 `"..."` literal의 바이트가 달라진다. `IsDBCSLeadByte`는 ACP를, `_mbs*` 계열은 별도의 CRT 코드 페이지 설정을 따른다.

### Lead Byte와 Trail Byte

MBCS에서 2바이트 문자는 Lead Byte(선행 바이트)와 Trail Byte(후행 바이트)로 구성된다.

```cpp
// CP949 바이트 범위
// Lead Byte:  0x81-0xFE
// Trail Byte: 0x41-0x5A, 0x61-0x7A, 0x81-0xFE

const char* str = "ABC가나다";
// 바이트: [41] [42] [43] [B0 A1] [B3 AA] [B4 D9]
//          A    B    C    가      나      다
```

### IsDBCSLeadByte와 IsDBCSLeadByteEx

```cpp
#include <windows.h>

// 현재 코드 페이지 기준
BOOL IsDBCSLeadByte(BYTE TestChar);

// 특정 코드 페이지 기준
BOOL IsDBCSLeadByteEx(UINT CodePage, BYTE TestChar);
```

사용 예시:

```cpp
void analyze_mbcs_string(const char* str) {
    const unsigned char* p = (const unsigned char*)str;
    int pos = 0;

    while (*p) {
        if (IsDBCSLeadByte(*p)) {
            // lead 뒤에 trail이 없으면(0) 잘린 입력이므로 멈춘다.
            if (p[1] == 0) break;
            // 2바이트 문자
            printf("Position %d: 2-byte char [%02X %02X]\n",
                   pos, p[0], p[1]);
            p += 2;
            pos += 2;
        } else {
            // 1바이트 문자 (ASCII)
            printf("Position %d: 1-byte char [%02X] '%c'\n",
                   pos, *p, *p);
            p += 1;
            pos += 1;
        }
    }
}
```

### CharNext와 CharPrev: 안전한 문자 탐색

MBCS 문자열에서 `str++`는 위험하다. 대신 Windows API를 사용해야 한다.

```cpp
// 다음 문자로 이동
LPSTR CharNextA(LPCSTR lpsz);
LPWSTR CharNextW(LPCWSTR lpsz);

// 이전 문자로 이동 (문자열 시작점 필요)
LPSTR CharPrevA(LPCSTR lpszStart, LPCSTR lpszCurrent);
LPWSTR CharPrevW(LPCWSTR lpszStart, LPCWSTR lpszCurrent);
```

비교:

| 방법 | MBCS 안전 | 성능 |
|------|-----------|------|
| `p++` | 위험 | $O(1)$ |
| `CharNextA(p)` | 안전 | $O(1)$ |
| `p--` | 위험 | $O(1)$ |
| `CharPrevA(start, p)` | 안전 | $O(n)$ 최악 |

`CharPrevA`는 문자열 시작부터 현재 위치까지 순회해야 할 수 있다.

```cpp
LPSTR CharPrevA(LPCSTR lpszStart, LPCSTR lpszCurrent) {
    // 시작 위치면 그대로 반환
    if (lpszCurrent == lpszStart)
        return (LPSTR)lpszStart;

    // Lead byte인지 확인하려면 앞에서부터 파싱해야 함
    // 왜냐하면 0x81-0xFE 범위는 trail byte와 겹칠 수 있음
    LPCSTR p = lpszStart;
    LPCSTR lastCharStart = lpszStart;

    while (p < lpszCurrent) {
        lastCharStart = p;
        if (IsDBCSLeadByte(*p) && p[1] != 0)
            p += 2;
        else
            p += 1;
    }

    return (LPSTR)lastCharStart;
}
```

### MBCS 경계 문제 실례

문제 시나리오: 버퍼 중간에서 자르기.

```cpp
char buffer[10];
const char* korean = "한글테스트";  // 10바이트 (5문자 × 2)

// 위험한 복사
strncpy(buffer, korean, 9);
buffer[9] = '\0';

// 결과: [C7 D1] [B1 DB] [C5 D7] [BD BA] [C6 ?]
//        한     글      테      스     ← 불완전
```

안전한 복사:

```cpp
// dest_capacity는 종료 NUL을 포함한 바이트 수다. 입력은 유효한
// NUL 종료 DBCS 문자열이어야 한다.
size_t mbcs_safe_copy(char* dest, const char* src, size_t dest_capacity) {
    size_t written = 0;
    const unsigned char* p = (const unsigned char*)src;

    if (dest_capacity == 0) return 0;

    while (*p) {
        size_t char_len = 1;
        if (IsDBCSLeadByte(*p)) {
            // trail이 실제로 존재하고 0이 아니어야 2바이트로 전진한다.
            if (p[1] == 0) break;
            char_len = 2;
        }

        // NUL 자리까지 확보되는지 확인한다.
        if (written + char_len + 1 > dest_capacity)
            break;

        memcpy(dest + written, p, char_len);
        written += char_len;
        p += char_len;
    }

    dest[written] = '\0';
    return written;
}
```

### `_ismbblead`와 `_ismbbtrail`

CRT는 자체 MBCS 함수를 제공한다.

```cpp
#include <mbctype.h>
#include <mbstring.h>

// Lead byte 검사
int _ismbblead(unsigned int c);

// Trail byte 검사 (독립적으로는 의미 없음)
int _ismbbtrail(unsigned int c);

// 문자열 다음 위치
unsigned char* _mbsinc(const unsigned char* str);

// 문자열 이전 위치
unsigned char* _mbsdec(const unsigned char* start, const unsigned char* current);

// n번째 문자 위치
unsigned char* _mbsninc(const unsigned char* str, size_t count);

// 문자 개수 (바이트 수 아님)
size_t _mbslen(const unsigned char* str);
```

`_mbslen` vs `strlen` 비교:

```cpp
const char* str = "ABC가나다";

strlen(str);                   // 9 (바이트)
_mbslen((unsigned char*)str);  // 6 (DBCS 문자 수)
```

`_mbslen`·`_ismbblead`는 CRT 로케일(LC_CTYPE)과 multibyte code page 설정에 의존하므로 빌드/실행 환경을 맞춰야 한다.

## 2. LoadString의 함정

### 기본 사용법

```cpp
int LoadStringA(
    HINSTANCE hInstance,
    UINT      uID,
    LPSTR     lpBuffer,
    int       cchBufferMax  // 버퍼 capacity (종료 NUL 포함, 문자 단위)
);

int LoadStringW(
    HINSTANCE hInstance,
    UINT      uID,
    LPWSTR    lpBuffer,
    int       cchBufferMax
);
```

### MBCS에서의 버퍼 잘림 문제

`LoadStringA`의 `cchBufferMax`는 종료 NUL을 포함한 char(바이트) 단위 capacity다. 반환값은 종료 NUL을 제외한 복사 문자 수다. DBCS에서는 char 단위와 사람이 보는 글자 수가 다르므로 이 불일치가 문제를 일으킨다.

```cpp
// 리소스 문자열: "한글 테스트입니다" (17바이트 — 한글 8자×2 + 공백 1)
char buffer[10];
int result = LoadStringA(hInstance, IDS_MESSAGE, buffer, 10);

// 내부 동작:
// 1. 10바이트까지 복사 시도
// 2. 문자 경계 확인 없이 자를 수 있음
// 3. 불완전한 MBCS 시퀀스 발생 가능
```

특정 환경에서 관측된 예(OS build·ACP·CRT locale 의존):

| 버퍼 크기 | 예상 동작 | 관측 동작 (특정 빌드·설정) |
|-----------|-----------|----------------------|
| 10 | 5문자 + null | 9바이트 = 5문자 (경계에 맞음) |
| 11 | 5문자 + null | 10바이트 (경계 무시 — '트'가 반으로 잘림) |

### 안전한 LoadString 래퍼

```cpp
// MBCS 경계를 존중하는 LoadString 래퍼
int SafeLoadStringA(HINSTANCE hInstance, UINT uID,
                    LPSTR lpBuffer, int cchBufferMax) {
    if (cchBufferMax <= 0) return 0;

    // 넉넉한 임시 버퍼. 이미 1024바이트에서 잘린 리소스 문자열을
    // 복구하는 장치는 아니다.
    char tempBuffer[1024];
    int len = LoadStringA(hInstance, uID, tempBuffer,
                          sizeof(tempBuffer));

    if (len == 0) {
        lpBuffer[0] = '\0';
        return 0;
    }

    // MBCS 안전 복사 — cchBufferMax는 종료 NUL을 포함한 capacity다.
    size_t written = mbcs_safe_copy(lpBuffer, tempBuffer,
                                     (size_t)cchBufferMax);
    return (int)written;
}
```

### Wide 버전 권장

현대 코드에서는 `LoadStringW` 사용을 권장한다.

```cpp
wchar_t buffer[256];
int len = LoadStringW(hInstance, IDS_MESSAGE, buffer, 256);

// UTF-8이 필요하면 변환
std::string utf8 = WideToUtf8(buffer);
```

## 3. Windows 콘솔 인코딩

### 콘솔의 이중 코드 페이지

Windows 콘솔은 입력과 출력에 별도의 코드 페이지를 사용한다.

```cpp
// 현재 코드 페이지 조회
UINT GetConsoleCP();         // 입력 코드 페이지
UINT GetConsoleOutputCP();   // 출력 코드 페이지

// 코드 페이지 설정
BOOL SetConsoleCP(UINT wCodePageID);
BOOL SetConsoleOutputCP(UINT wCodePageID);
```

한국어 Windows 기본값:

| 구분 | 코드 페이지 | 설명 |
|------|-------------|------|
| 입력 | 949 | CP949 (확장 완성형) |
| 출력 | 949 | CP949 (확장 완성형) |

### UTF-8 콘솔 모드 (Windows 10+)

Windows 10 버전 1903부터 진정한 UTF-8 콘솔을 지원한다.

```cpp
// UTF-8 설정
SetConsoleCP(CP_UTF8);
SetConsoleOutputCP(CP_UTF8);

// 이제 UTF-8 문자열 직접 출력 가능
printf("한글 UTF-8 출력 테스트\n");
```

주의사항:

```cpp
// stdout의 CRT text 모드와 콘솔 출력 코드 페이지는 별개다.
// wide + Unicode(text) CRT 출력을 위해 모드 변경이 필요하다.
// 이미 UTF-8 바이트인 narrow 문자열은 SetConsoleOutputCP(CP_UTF8) 경로로도 출력된다.

#include <io.h>
#include <fcntl.h>

// 방법 1: 파일 모드 변경
// 주의: _O_U8TEXT 모드에서는 wprintf 계열(와이드 출력)만 사용해야 한다.
//       narrow printf를 호출하면 CRT가 assertion을 발생시킨다.
_setmode(_fileno(stdout), _O_U8TEXT);

// 방법 2: WriteConsoleW 직접 사용
HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
WriteConsoleW(hConsole, L"유니코드 출력\n", 8, NULL, NULL);
```

### printf vs WriteConsole 비교

```cpp
// printf: CRT를 통함. narrow 문자열은 컴파일된 바이트 그대로 전달되며
//        CP949 바이트를 UTF-8로 자동 변환하지는 않는다.
printf("테스트");  // 실행 문자 집합과 콘솔 OutputCP에 따라 동작

// WriteConsoleA: 콘솔 출력 코드 페이지 사용
WriteConsoleA(hConsole, "테스트", 6, &written, NULL);

// WriteConsoleW: UTF-16 직접 출력 (권장)
WriteConsoleW(hConsole, L"테스트", 3, &written, NULL);
```

| 함수 | 인코딩 | 리다이렉션 지원 | 권장도 |
|------|--------|----------------|--------|
| printf | 로캘 의존 | O | 호환성 |
| WriteConsoleA | 콘솔 CP | X | 비권장 |
| WriteConsoleW | UTF-16 | X | 콘솔 전용 |
| WriteFile | 바이너리 | O | 파이프용 |

### 콘솔 출력 + 파이프 호환 패턴

```cpp
void OutputString(const wchar_t* str) {
    HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOutput == INVALID_HANDLE_VALUE || hOutput == NULL) return;
    DWORD mode;

    if (GetConsoleMode(hOutput, &mode)) {
        // 콘솔 출력 — WriteConsoleW는 부분 쓰기가 가능하므로 반환값을 확인한다.
        DWORD written = 0;
        if (!WriteConsoleW(hOutput, str, (DWORD)wcslen(str),
                           &written, NULL)) {
            // 실패 처리(진단 로그 등)
        }
    } else {
        // 파이프/파일로 리다이렉션됨 → UTF-8로 변환
        std::string utf8 = WideToUtf8(str);
        DWORD written = 0;
        if (!WriteFile(hOutput, utf8.c_str(), (DWORD)utf8.length(),
                       &written, NULL)) {
            // 실패 처리
        }
    }
}
```

### chcp 명령과 프로그램 설정

```batch
:: 현재 코드 페이지 확인
chcp

:: UTF-8로 변경
chcp 65001

:: 프로그램 실행
myapp.exe
```

프로그래밍 방식으로는 다음과 같이 한다.

```cpp
// 프로그램 시작 시 UTF-8 설정
void InitConsoleUtf8() {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    // C 런타임도 설정
    setlocale(LC_ALL, ".UTF-8");
}
```

### Windows Terminal vs 레거시 콘솔

비교는 console host(conhost)와 특정 Windows Terminal 버전을 기준으로 하며, shell(cmd/PowerShell)·호스트·인코딩 설정은 별개다.

| 기능 | 레거시 콘솔(conhost) | Windows Terminal |
|------|----------------------|------------------|
| UTF-8 지원 | OutputCP 설정·폰트에 의존 | 범위가 넓지만 폰트·호스트에 의존 |
| 이모지 | 폰트·glyph 지원에 의존 | 폰트·glyph 지원에 의존 |
| 색상 | 16색 기본, 최근 conhost는 24-bit color 지원 | TrueColor |
| 가변폭 글꼴 | 호스트·글꼴에 따라 다름 | 호스트·글꼴에 따라 다름 |
| GPU 렌더링 | 불가 | 지원(버전·설정에 의존) |

## 4. Locale과 NLS (National Language Support)

### C 런타임 Locale

```cpp
#include <locale.h>

// 전체 로캘 설정
char* setlocale(int category, const char* locale);

// 카테고리
// LC_ALL      - 모든 카테고리
// LC_COLLATE  - 문자열 비교
// LC_CTYPE    - 문자 분류
// LC_MONETARY - 통화 형식
// LC_NUMERIC  - 숫자 형식
// LC_TIME     - 날짜/시간 형식
```

로캘 문자열 형식:

```cpp
// Windows 형식
setlocale(LC_ALL, "Korean_Korea.949");      // CP949
setlocale(LC_ALL, "Korean_Korea.UTF-8");    // UTF-8 (Win10+)
setlocale(LC_ALL, "en-US");                 // 영어 (미국)

// POSIX 형식 (일부 지원)
setlocale(LC_ALL, "ko_KR.UTF-8");

// 시스템 기본값
setlocale(LC_ALL, "");
```

### Windows NLS API

GetLocaleInfo - 로캘 정보 조회:

```cpp
int GetLocaleInfoW(
    LCID   Locale,      // 로캘 ID
    LCTYPE LCType,      // 정보 유형
    LPWSTR lpLCData,    // 출력 버퍼
    int    cchData      // 버퍼 크기
);

// 예시: 현재 로캘의 언어 이름
wchar_t langName[256];
GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SLANGUAGE,
               langName, 256);
// 결과: "한국어(대한민국)" (관측값, Windows build/NLS 의존) — 순수 언어 이름은 LOCALE_SLOCALIZEDLANGUAGENAME
```

주요 LCTYPE 값:

| 상수 | 의미 | 예시 (한국어) |
|------|------|---------------|
| LOCALE_SISO639LANGNAME | ISO 639 언어 코드 | "ko" |
| LOCALE_SISO3166CTRYNAME | ISO 3166 국가 코드 | "KR" |
| LOCALE_SLANGUAGE | 로캘 전체 표시 이름 | "한국어(대한민국)" |
| LOCALE_SCOUNTRY | 지역화된 국가 이름 | "한국" |
| LOCALE_SDECIMAL | 소수점 | "." |
| LOCALE_STHOUSAND | 천 단위 구분자 | "," |
| LOCALE_SCURRENCY | 통화 기호 | "₩" |
| LOCALE_IDEFAULTANSICODEPAGE | 기본 ANSI 코드 페이지 | "949" |

### 문자열 비교: CompareString 계열

```cpp
int CompareStringW(
    LCID    Locale,
    DWORD   dwCmpFlags,
    LPCWSTR lpString1, int cchCount1,
    LPCWSTR lpString2, int cchCount2
);

// 반환값
// CSTR_LESS_THAN    (1) - str1 < str2
// CSTR_EQUAL        (2) - str1 == str2
// CSTR_GREATER_THAN (3) - str1 > str2
```

비교 플래그:

| 플래그 | 의미 |
|--------|------|
| NORM_IGNORECASE | 대소문자 무시 |
| NORM_IGNOREKANATYPE | 히라가나/가타카나 동일 취급 |
| NORM_IGNOREWIDTH | 전각/반각 동일 취급 |
| NORM_LINGUISTIC_CASING | 언어별 규칙 적용 |
| SORT_STRINGSORT | 기호를 문자 앞에 정렬 |

예시:

```cpp
// 대소문자 무시 비교
int result = CompareStringW(
    LOCALE_USER_DEFAULT,
    NORM_IGNORECASE,
    L"Apple", -1,
    L"apple", -1
);
// result == CSTR_EQUAL (2) (영어 로케일 기준 관측값)

// 언어별 정렬 (독일어 ß = ss)
result = CompareStringW(
    MAKELCID(MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN), SORT_DEFAULT),
    NORM_LINGUISTIC_CASING,
    L"straße", -1,
    L"strasse", -1
);
// result는 Windows NLS 버전에 따라 달라질 수 있다(관측값).
```

### LCMapString - 문자열 변환

```cpp
int LCMapStringW(
    LCID    Locale,
    DWORD   dwMapFlags,
    LPCWSTR lpSrcStr, int cchSrc,
    LPWSTR  lpDestStr, int cchDest
);
```

변환 플래그:

| 플래그 | 의미 |
|--------|------|
| LCMAP_LOWERCASE | 소문자로 변환 |
| LCMAP_UPPERCASE | 대문자로 변환 |
| LCMAP_TITLECASE | 제목 대소문자 (Win7+) |
| LCMAP_HIRAGANA | 히라가나로 변환 |
| LCMAP_KATAKANA | 가타카나로 변환 |
| LCMAP_HALFWIDTH | 반각으로 변환 |
| LCMAP_FULLWIDTH | 전각으로 변환 |
| LCMAP_SORTKEY | 정렬 키 생성 |

전각/반각 변환 예시:

```cpp
wchar_t src[] = L"ＡＢＣ１２３";  // 전각
wchar_t dest[256];

LCMapStringW(LOCALE_USER_DEFAULT, LCMAP_HALFWIDTH,
             src, -1, dest, 256);
// dest: "ABC123" (반각)
```

### GetCPInfo - 코드 페이지 정보

```cpp
typedef struct _cpinfo {
    UINT MaxCharSize;       // 최대 바이트 수 (SBCS는 1, DBCS는 2, 그 외 코드 페이지는 2보다 클 수 있음 — CP_UTF8은 4)
    BYTE DefaultChar[2];    // 변환 불가 시 대체 문자
    BYTE LeadByte[12];      // Lead byte 범위 쌍들
} CPINFO;

BOOL GetCPInfo(UINT CodePage, LPCPINFO lpCPInfo);
```

사용 예시:

```cpp
CPINFO cpInfo;
GetCPInfo(949, &cpInfo);

printf("MaxCharSize: %d\n", cpInfo.MaxCharSize);  // 2

// Lead byte 범위 출력 (0으로 끝남)
for (int i = 0; cpInfo.LeadByte[i]; i += 2) {
    printf("Lead byte range: 0x%02X - 0x%02X\n",
           cpInfo.LeadByte[i], cpInfo.LeadByte[i+1]);
}
// 출력: Lead byte range: 0x81 - 0xFE
```

## 5. 안전한 문자열 함수

### 두 가지 계열

Windows에서는 두 가지 안전한 문자열 함수 계열이 있다.

| 계열 | 헤더 | 접미사 | 예시 |
|------|------|--------|------|
| Strsafe | strsafe.h | Cch/Cb | `StringCchCopy` |
| CRT Secure | string.h | _s | `strcpy_s` |

### Strsafe 함수 (strsafe.h)

Microsoft가 권장하는 표준이다.

```cpp
#include <strsafe.h>

// 문자 단위 (Character Count)
HRESULT StringCchCopyA(STRSAFE_LPSTR pszDest,
                       size_t cchDest,
                       STRSAFE_LPCSTR pszSrc);

// 바이트 단위 (Byte Count)
HRESULT StringCbCopyA(STRSAFE_LPSTR pszDest,
                      size_t cbDest,
                      STRSAFE_LPCSTR pszSrc);
```

Cch vs Cb 차이:

```cpp
char buffer[100];

// 문자 단위: 100은 종료 NUL을 포함한 char 단위 capacity다 (payload 최대 99).
StringCchCopyA(buffer, 100, src);
StringCchCopyW(wbuffer, 100, wsrc);  // 100 UTF-16 code unit capacity

// 바이트 단위: 100은 종료 NUL을 포함한 byte capacity다.
StringCbCopyA(buffer, 100, src);
StringCbCopyW(wbuffer, 100, wsrc);   // 100바이트 = 50 UTF-16 code unit capacity
```

주요 함수:

| 함수 | 용도 |
|------|------|
| `StringCchCopy`/`StringCbCopy` | 문자열 복사 |
| `StringCchCat`/`StringCbCat` | 문자열 연결 |
| `StringCchPrintf`/`StringCbPrintf` | 서식 출력 |
| `StringCchLength`/`StringCbLength` | 길이 계산 |
| `StringCchGets`/`StringCbGets` | 라인 읽기 |

반환값 처리:

```cpp
HRESULT hr = StringCchCopy(dest, destSize, src);

if (SUCCEEDED(hr)) {
    // 성공
} else if (hr == STRSAFE_E_INSUFFICIENT_BUFFER) {
    // 버퍼 부족 (잘림 발생, 하지만 null 종료 보장)
} else if (hr == STRSAFE_E_INVALID_PARAMETER) {
    // 잘못된 매개변수
}
```

Ex 버전 - 추가 제어:

```cpp
HRESULT StringCchCopyExA(
    STRSAFE_LPSTR pszDest,
    size_t cchDest,
    STRSAFE_LPCSTR pszSrc,
    STRSAFE_LPSTR* ppszDestEnd,    // 끝 위치 반환
    size_t* pcchRemaining,          // 남은 공간
    DWORD dwFlags                   // 플래그
);

// 플래그
// STRSAFE_FILL_BEHIND_NULL  - null 뒤를 채움
// STRSAFE_IGNORE_NULLS      - null 매개변수 허용
// STRSAFE_FILL_ON_FAILURE   - 실패 시 버퍼 채움
// STRSAFE_NULL_ON_FAILURE   - 실패 시 빈 문자열
// STRSAFE_NO_TRUNCATION     - 잘림 시 에러 반환
```

효율적인 연쇄 연결로 Shlemiel 문제를 회피할 수 있다.

```cpp
char buffer[256];
char* current = buffer;
size_t remaining = 256;

// 연쇄 복사
StringCchCopyExA(current, remaining, "Hello",
                 &current, &remaining, 0);
StringCchCopyExA(current, remaining, " ",
                 &current, &remaining, 0);
StringCchCopyExA(current, remaining, "World",
                 &current, &remaining, 0);

// 결과: "Hello World"
```

### CRT Secure 함수 (_s 접미사)

C11 표준 Annex K 기반이다.

```cpp
errno_t strcpy_s(char* dest, rsize_t destsz, const char* src);
errno_t strncpy_s(char* dest, rsize_t destsz,
                  const char* src, rsize_t count);
errno_t strcat_s(char* dest, rsize_t destsz, const char* src);
int sprintf_s(char* buffer, rsize_t sizeOfBuffer,
              const char* format, ...);
```

Strsafe vs CRT Secure 비교:

| 측면 | Strsafe | CRT Secure |
|------|---------|------------|
| 반환 타입 | HRESULT | errno_t (sprintf_s 등은 int) |
| 실패 시 동작 | 유효한 크기의 destination에 NUL 종료 보장 | invalid parameter handler 호출 후 오류 반환 |
| 이식성 | Windows 전용 | C11 Annex K (선택적, MS 구현과 세부 동일성 없음) |
| 버퍼 오버플로 | 기본은 잘림+STRSAFE_E_INSUFFICIENT_BUFFER, STRSAFE_NO_TRUNCATION은 에러 | 기본 에러 |
| 매크로 오버로드 | 없음 | Microsoft CRT에는 C++ secure template overload 있음 |

CRT Secure 실패 핸들러:

```cpp
// 기본 동작: 프로그램 종료
strcpy_s(small_buffer, 10, "very long string");  // 종료

// 커스텀 핸들러 설정
void my_handler(const wchar_t* expression,
                const wchar_t* function,
                const wchar_t* file,
                unsigned int line,
                uintptr_t pReserved) {
    wprintf(L"Buffer overflow in %s at %s:%d\n",
            function, file, line);
}

_set_invalid_parameter_handler(my_handler);

// 디버그 빌드에서 assert 비활성화
_CrtSetReportMode(_CRT_ASSERT, 0);
```

handler가 반환하도록 바꾸면 CRT 함수는 작업을 중단하고 오류(`errno_t` 등)를 반환한다. 이후 동작은 함수군과 `_CRT_SECURE_INVALID_PARAMETER` 정책에 따른다.

### 권장 사항

| 상황 | 권장 |
|------|------|
| Windows 전용 코드 | Strsafe |
| 크로스 플랫폼 | CRT Secure + 폴백 |
| 레거시 호환 | 래퍼 함수 작성 |
| 성능 중요 | 수동 검증 + memcpy |

크로스 플랫폼 래퍼:

```cpp
// 매크로 대신 함수로 계약을 고정한다. dst_capacity는 종료 NUL을 포함한
// 바이트 수다. 0 용량과 부족 용량을 모두 처리한다.
enum class CopyResult { Ok, Truncated, NoSpace };

CopyResult SafeStrcpy(char* dst, size_t dst_capacity, const char* src) {
    if (dst_capacity == 0) return CopyResult::NoSpace;

    size_t i = 0;
    while (src[i] != '\0' && i + 1 < dst_capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
    return (src[i] == '\0') ? CopyResult::Ok : CopyResult::Truncated;
}
```

## 6. TCHAR 마이그레이션

### TCHAR의 역사

1990년대, Windows는 ANSI(Win9x)와 Unicode(NT) 두 계열로 분리되어 있었다. 하나의 소스로 양쪽을 지원하기 위해 TCHAR가 도입되었다.

```cpp
#include <tchar.h>
#include <windows.h>

// _T/_TCHAR/_tcs는 tchar.h가 _UNICODE(또는 _MBCS)로 결정한다.
#ifdef _UNICODE
    typedef wchar_t TCHAR;
    #define _T(x) L##x
#else
    typedef char TCHAR;
    #define _T(x) x
#endif

// TEXT는 windows.h가 UNICODE 매크로를 보고 정의한다.
#ifdef UNICODE
    #define TEXT(x) L##x
#else
    #define TEXT(x) x
#endif
```

### TCHAR 함수 매핑

| TCHAR 함수 | ANSI (A) | Unicode (W) |
|------------|----------|-------------|
| `_tcscpy` | `strcpy` | `wcscpy` |
| `_tcscat` | `strcat` | `wcscat` |
| `_tcslen` | `strlen` | `wcslen` |
| `_tcscmp` | `strcmp` | `wcscmp` |
| `_tcsncpy` | `strncpy` | `wcsncpy` |
| `_stprintf` | `sprintf` | `swprintf` |
| `_tfopen` | `fopen` | `_wfopen` |

`_UNICODE`가 없고 `_MBCS`가 정의된 빌드에서는 `_tcs*` 함수가 `char` 함수로 매핑되지만 함수마다 단위가 다르다. `_tcslen`은 바이트 수를, `_tcsclen`은 문자 수를 세는 식이다.

### Windows API와 TCHAR

```cpp
// 매크로 정의
#ifdef UNICODE
    #define CreateFile CreateFileW
    #define MessageBox MessageBoxW
#else
    #define CreateFile CreateFileA
    #define MessageBox MessageBoxA
#endif

// TCHAR 버전 사용
HANDLE hFile = CreateFile(
    _T("파일.txt"),
    GENERIC_READ,
    0, NULL, OPEN_EXISTING, 0, NULL
);

MessageBox(NULL, _T("메시지"), _T("제목"), MB_OK);
```

### 현대화 전략

Win9x 지원이 종료된 현재, TCHAR는 레거시다. 현대 코드는 직접 Wide 함수를 사용해야 한다.

마이그레이션 단계:

| 단계 | 작업 |
|------|------|
| 1 | 프로젝트 설정에서 UNICODE 정의 확인 |
| 2 | TCHAR를 wchar_t로 교체 |
| 3 | `_T` 매크로를 `L""`로 교체 |
| 4 | `_tcs*` 함수를 `wcs*` 함수로 교체 |
| 5 | A 함수 호출을 W 함수로 교체 |

자동화 정규식:

```text
// 찾기 → 바꾸기
TCHAR       → wchar_t
_T\("([^"]*)"\)  → L"\1"
TEXT\("([^"]*)"\) → L"\1"
_tcscpy     → wcscpy
_tcslen     → wcslen
CreateFile([^AW])  → CreateFileW\1
```

레거시 코드 호환 레이어:

```cpp
// UTF-8 입력 계약 레거시 라이브러리와 연동 시
std::wstring input = L"한글 입력";

// UTF-8로 변환 후 레거시 API 호출
std::string utf8 = WideToUtf8(input);
legacy_utf8_api(utf8.c_str());

// 결과를 다시 Wide로 (라이브러리 반환도 UTF-8)
std::string result_utf8 = legacy_utf8_api_result();
std::wstring result = Utf8ToWide(result_utf8);
```

## 7. Windows 문자열 타입 총정리

### 포인터 타입

| 타입 | 정의 | 용도 |
|------|------|------|
| LPSTR | `char*` | ANSI 문자열 (수정 가능) |
| LPCSTR | `const char*` | ANSI 문자열 (읽기 전용) |
| LPWSTR | `wchar_t*` | Wide 문자열 (수정 가능) |
| LPCWSTR | `const wchar_t*` | Wide 문자열 (읽기 전용) |
| LPTSTR | `TCHAR*` | 제네릭 (수정 가능) |
| LPCTSTR | `const TCHAR*` | 제네릭 (읽기 전용) |

LP 접두사의 의미:

```text
L  = Long (16비트 시절 far 포인터)
P  = Pointer
C  = Const
W  = Wide (Unicode)
T  = TCHAR
STR = String
```

### Hungarian Notation

Windows SDK는 헝가리안 표기법을 사용한다.

| 접두사 | 의미 | 예시 |
|--------|------|------|
| sz | null-terminated string | szName |
| psz | pointer to sz | pszBuffer |
| wsz | wide sz | wszPath |
| lpsz | long pointer to sz | lpszFileName |
| cch | count of characters | cchBuffer |
| cb | count of bytes | cbBuffer |
| dw | DWORD | dwFlags |
| h | handle | hFile |
| n | int/short | nCount |
| b | BOOL | bSuccess |

### ATL/MFC 문자열 클래스

CString (MFC/ATL):

```cpp
#include <atlstr.h>  // ATL
// 또는
#include <afx.h>     // MFC

CString str = _T("문자열");
str += _T(" 추가");
int len = str.GetLength();

// LPCTSTR 변환 (암묵적)
MessageBox(NULL, str, _T("제목"), MB_OK);

// 명시적 변환
LPCTSTR pszStr = (LPCTSTR)str;
```

CStringA vs CStringW:

```cpp
CStringA ansiStr = "ANSI 문자열";
CStringW wideStr = L"Wide 문자열";

// CString은 UNICODE 설정에 따라 결정
#ifdef UNICODE
    typedef CStringW CString;
#else
    typedef CStringA CString;
#endif

// 상호 변환
CStringW wide = CStringW(ansiStr);  // ANSI → Wide
CStringA ansi = CStringA(wideStr);  // Wide → ANSI (손실 가능)
```

CString과 std::string 변환:

```cpp
// CStringW → std::wstring
CStringW cstr = L"테스트";
std::wstring wstr(cstr.GetString());

// std::wstring → CStringW
std::wstring wstr2 = L"테스트";
CStringW cstr2(wstr2.c_str());

// CStringW → std::string (UTF-8)
std::string utf8 = WideToUtf8(cstr.GetString());
```

CString은 MFC/ATL 안에서만 쓰이는 타입이다. COM 인터페이스 경계를 넘는 문자열은 이 표의 어느 타입도 아닌 BSTR을 쓰고, 그 이유와 계약은 별개의 문제다.

## 8. COM 문자열: 할당자와 소유권

### 왜 경계 전용 할당자가 필요한가

COM 객체는 모듈 경계를 넘어 통신한다. DLL A가 만든 문자열을 EXE가 해제하는 상황이 인터페이스 설계상 정상 흐름이다. 문제는 각 모듈이 자기 CRT와 함께 링크되면 *서로 다른 힙* 을 쓴다는 데 있다.

| 상황 | 결과 | 필요한 것 |
|------|------|-----------|
| DLL A: `malloc` → EXE: `free` | 다른 CRT 힙이면 힙 손상 | 양쪽이 합의한 할당자 |
| DLL A: `new` → COM 객체: `delete` | 같은 문제 | 양쪽이 합의한 할당자 |
| DLL A: `SysAllocString` → EXE: `SysFreeString` | 안전 | 프로세스 공통 할당자 |

정적 CRT 링크, 서로 다른 Visual Studio 버전으로 빌드된 모듈, 서로 다른 언어 런타임이 섞이면 첫 두 줄이 곧바로 사고가 된다. COM이 자체 할당자를 정의한 이유가 이것이다.

### 경계에서 쓰는 할당자

| 함수 쌍 | 할당자 | 용도 |
|------|--------|------|
| `malloc` / `free` | CRT 힙 | 모듈 내부 전용 |
| `new` / `delete` | CRT 힙 | 모듈 내부 전용 |
| `HeapAlloc` / `HeapFree` | 지정한 프로세스 힙 | 저수준 Windows API |
| `CoTaskMemAlloc` / `CoTaskMemFree` | COM task allocator | COM 인터페이스 경계의 일반 버퍼 |
| `SysAllocString` / `SysFreeString` | OLE Automation의 BSTR 전용 할당자 | BSTR만 |

OLE Automation 시그니처와 IDL/API 문서가 할당자·해제자를 지정한다. 포인터 타입만으로 항상 결정되지는 않으므로 해당 인터페이스의 계약을 따른다.

```cpp
// 문자열 반환: 호출자가 SysFreeString으로 해제한다
HRESULT GetName(BSTR* pbstrName) {
    if (!pbstrName) return E_POINTER;
    *pbstrName = nullptr;
    BSTR value = SysAllocString(L"MyObject");
    if (!value) return E_OUTOFMEMORY;
    *pbstrName = value;  // 성공 시에만 소유권을 이전한다
    return S_OK;
}

// 일반 버퍼 반환: 호출자가 CoTaskMemFree로 해제한다
HRESULT GetData(BYTE** ppData, ULONG* pcb) {
    if (!ppData || !pcb) return E_POINTER;
    *ppData = nullptr;
    *pcb = 0;
    const ULONG size = 100;
    BYTE* data = (BYTE*)CoTaskMemAlloc(size);
    if (!data) return E_OUTOFMEMORY;
    // 여기서 data를 실제 내용으로 채운다(생략된 부분).
    *ppData = data;
    *pcb = size;  // 성공 시에만 길이와 소유권을 이전한다
    return S_OK;
}
```

### BSTR의 메모리 계약

BSTR은 문자열 시작을 가리키는 포인터지만, 그 *앞* 에 길이 필드가 붙어 있다.

| 오프셋 | 크기 | 내용 |
|:------:|:----:|------|
| -4 | 4바이트 | 바이트 길이 (종료 null 제외) |
| 0 | 가변 | UTF-16LE 데이터 |
| 끝 | 2바이트 | 종료 null |

이 배치에서 두 가지가 따라온다. 길이 조회가 탐색 없이 끝나고, 데이터 중간에 `U+0000`이 들어 있어도 길이가 보존된다. Windows API가 반환하는 다른 문자열 타입은 둘 다 보장하지 않는다.

```cpp
BSTR bstr = SysAllocString(L"Hello");

UINT units = SysStringLen(bstr);       // 5 (UTF-16 code unit 수, 종료 NUL 제외)
UINT bytes = SysStringByteLen(bstr);   // 10 (바이트 수, 종료 NUL 제외)

SysFreeString(bstr);
```

`BSTR`과 `LPWSTR`은 둘 다 `wchar_t*`로 정의되지만 계약이 다르다. 타입 시스템이 구분해 주지 않으므로 규약으로 지켜야 한다.

| 특성 | BSTR | LPWSTR |
|------|------|--------|
| 길이 정보 | 포인터 앞 4바이트 | 없음 (종료 null로 판단) |
| 할당 | `SysAllocString` 계열 | `malloc`, `new`, 스택 등 |
| 해제 | `SysFreeString` | 할당 방식에 따름 |
| 중간 null | 허용 | 문자열 끝을 의미 |
| COM 마샬링 | Automation 마샬러가 자동 처리 | IDL [string]·프록시 설정에 따라 자동 생성될 수 있음 |

```cpp
// 잘못된 코드: 리터럴에는 길이 필드가 없다.
// 현대 C++에서는 const wchar_t[]를 BSTR로 암시 변환할 수 없어 진단되고,
// 강제 캐스팅해도 BSTR 계약은 생기지 않는다.
BSTR bad = const_cast<BSTR>(L"Hello");
SysFreeString(bad);   // Automation 할당자가 만든 포인터가 아니다

// 올바른 코드
BSTR ok = SysAllocString(L"Hello");
SysFreeString(ok);
```

`NULL` BSTR은 오류가 아니라 *빈 문자열* 이다. `SysStringLen(NULL)`은 0을 반환한다. COM 인터페이스에서 문자열을 받을 때 `NULL`을 실패로 처리하면 정상 입력을 거부하게 된다.

```cpp
void ProcessString(BSTR bstr) {
    UINT len = bstr ? SysStringLen(bstr) : 0;   // NULL도 유효한 빈 문자열
    if (len > 0) {
        // 비어 있지 않은 문자열
    }
}
```

### SysAllocString 계열

| 함수 | 용도 | 반환값 |
|------|------|--------|
| `SysAllocString(LPCOLESTR)` | null 종료 문자열로부터 생성 | BSTR |
| `SysAllocStringLen(LPCOLESTR, UINT)` | 길이 지정, 중간 null 허용 | BSTR |
| `SysAllocStringByteLen(LPCSTR, UINT)` | 바이트 배열로부터 생성 | BSTR |
| `SysReAllocString(BSTR*, LPCOLESTR)` | 재할당 | BOOL |
| `SysFreeString(BSTR)` | 해제 | void |
| `SysStringLen(BSTR)` | UTF-16 code unit 수 반환 | UINT |
| `SysStringByteLen(BSTR)` | 바이트 수 반환 | UINT |

`SysAllocStringLen`이 별도로 존재하는 이유가 BSTR의 존재 이유와 같다. 길이를 명시적으로 받아야 중간에 `U+0000`이 있는 데이터를 손실 없이 담을 수 있다. `SysAllocString`은 인자를 null 종료 문자열로 읽으므로 그 지점에서 잘린다.

내부 할당자는 공개 계약이 아니다. 직접 구현해 대체할 수 없고, 다음은 오직 레이아웃을 설명하기 위한 의사 코드다.

```cpp
BSTR conceptual_SysAllocStringLen(const OLECHAR* psz, UINT len) {
    UINT bytes = len * sizeof(OLECHAR);
    // 길이 필드(4) + 데이터 + 종료 null(2)
    BYTE* raw = (BYTE*)automation_alloc(4 + bytes + sizeof(OLECHAR));
    if (!raw) return NULL;

    *((UINT*)raw) = bytes;              // 길이는 바이트 단위로 저장한다
    BSTR bstr = (BSTR)(raw + 4);
    if (psz) memcpy(bstr, psz, bytes);
    else     memset(bstr, 0, bytes);
    bstr[len] = L'\0';
    return bstr;
}
```

해제 시 4바이트 앞으로 되돌아가야 하므로, BSTR 포인터를 다른 할당자에 넘기면 진단되지 않는 힙 손상이 된다.

### 흔한 실수와 디버깅

| 실수 | 원인 | 대응 |
|------|------|------|
| BSTR 누수 | `SysFreeString` 누락 | RAII 래퍼 사용 |
| 이중 해제 | out 파라미터 소유권 혼란 | 인터페이스 문서의 소유권 규약 확인 |
| 잘못된 할당자 | `malloc` + `SysFreeString` 혼용 | 타입으로 계약 구분 |
| 리터럴 해제 | `BSTR b = L"Hello"; SysFreeString(b);` | 항상 `SysAllocString` 경유 |
| 길이 필드 훼손 | BSTR 버퍼에 직접 더 긴 문자열 기록 | `SysReAllocStringLen` 사용 |

디버거에서는 포인터 앞 4바이트를 직접 읽어 길이 필드를 확인할 수 있다.

```cpp
BSTR bstr = SysAllocString(L"Test");
UINT* pLen = (UINT*)bstr - 1;   // Watch에 *pLen: 8 (바이트)
```

### RAII 래퍼

`SysFreeString` 누락은 흔한 실수이고, 예외가 던져지는 경로까지 손으로 막기는 어렵다. COM 경계에는 표준 래퍼가 준비되어 있다.

```cpp
#include <comutil.h>

_bstr_t bstr1(L"Hello");
_bstr_t bstr2 = L"World";
_bstr_t bstr3 = bstr1 + L" " + bstr2;

BSTR raw = bstr3;                 // 소유권 유지 (또는 bstr3.GetBSTR())
BSTR detached = bstr3.Detach();   // 소유권 이전, 호출자가 해제
SysFreeString(detached);

_bstr_t fromAnsi = "ANSI";        // char*를 받으면 현재 ANSI 코드 페이지로 변환한다
```

마지막 줄이 함정이다. `_bstr_t`가 `char*`를 받거나 `const char*`로 변환될 때 개입하는 것은 UTF-8이 아니라 *현재 ANSI 코드 페이지* 다. UTF-8 중심으로 설계한 코드에서 이 변환을 무심코 타면 비ASCII 문자가 손실된다. UTF-8 문자열은 명시적으로 UTF-16으로 변환한 뒤 넘긴다.

| 메서드 | 동작 |
|--------|------|
| `GetBSTR` | 내부 BSTR 반환 (소유권 유지) |
| `GetAddress` | `BSTR*` 반환 (out 파라미터용) |
| `Detach` | 소유권 이전 |
| `Attach(BSTR)` | 기존 BSTR 소유권 획득 |
| `copy` | 복사본 생성 (호출자 해제 책임) |

ATL 기반 코드에서는 `CComBSTR`을 쓴다.

```cpp
#include <atlbase.h>

CComBSTR bstr1(L"Hello");
bstr1.Append(L" World");

// CComBSTR에는 CompareNoCase가 없다. SysStringLen으로 길이를 명시해
// CompareStringOrdinal로 비교한다. ordinal 비교는 언어학적 동등성과 다르다.
const int cmp = CompareStringOrdinal(
    bstr1, static_cast<int>(SysStringLen(bstr1)),
    L"HELLO WORLD", 11, TRUE);
if (cmp == 0) {
    // 비교 실패 — GetLastError() 확인
} else if (cmp == CSTR_EQUAL) {
    // 같음
}

// out 파라미터에는 비어 있는 객체를 사용한다
CComBSTR name;
HRESULT hr = pObj->GetName(&name);
if (SUCCEEDED(hr)) {
    bstr1 = name;
}
```

`CComBSTR::operator&`는 기존 문자열을 자동 해제하지 않는다. 값이 들어 있는 객체에 `&`를 사용하면 원래 포인터를 잃어 누수가 되고, 디버그 빌드는 이를 assertion으로 잡는다. 값이 있는 변수와 분리된 빈 객체로 out 값을 받은 뒤 성공했을 때 대입한다.

COM 객체 포인터 자체에도 같은 문제가 있다. `AddRef`/`Release` 짝을 손으로 맞추는 대신 스마트 포인터를 쓴다.

```cpp
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

ComPtr<IUnknown> pUnk;
HRESULT hr = CoCreateInstance(CLSID_Something, nullptr,
    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pUnk));
if (FAILED(hr)) {
    // 활성화 실패 — pUnk는 null이다
}

ComPtr<IDispatch> pDisp;
if (pUnk) {
    hr = pUnk.As(&pDisp);        // QueryInterface + 소유권 획득 (실패 시 hr 확인)
}
// 범위를 벗어나면 Release가 호출된다
```

| 래퍼 | 헤더 | 대상 |
|------|------|------|
| `_bstr_t` | comutil.h | BSTR (가벼움) |
| `CComBSTR` | atlbase.h | BSTR (ATL) |
| `CComPtr<T>` | atlbase.h | COM 포인터 (ATL) |
| `ComPtr<T>` | wrl/client.h | COM 포인터 (현대) |

새로 작성하는 코드라면 ATL 의존성이 없는 `_bstr_t`와 `ComPtr<T>` 조합이 가장 가볍다. 기존 ATL 프로젝트는 `CComBSTR`/`CComPtr`을 그대로 쓴다. 어느 쪽이든 원칙은 하나다. **COM 경계를 넘는 자원은 그 자원을 만든 할당자와 짝이 맞는 래퍼에 즉시 담는다.**

## 9. WideCharToMultiByte와 MultiByteToWideChar 심화

### 함수 시그니처

```cpp
int MultiByteToWideChar(
    UINT   CodePage,       // 소스 코드 페이지
    DWORD  dwFlags,        // 변환 플래그
    LPCCH  lpMultiByteStr, // 소스 문자열
    int    cbMultiByte,    // 소스 길이 (-1: null 종료)
    LPWSTR lpWideCharStr,  // 대상 버퍼
    int    cchWideChar     // 대상 버퍼 크기 (문자)
);

int WideCharToMultiByte(
    UINT   CodePage,
    DWORD  dwFlags,
    LPCWCH lpWideCharStr,
    int    cchWideChar,    // 소스 길이 (-1: null 종료)
    LPSTR  lpMultiByteStr,
    int    cbMultiByte,    // 대상 버퍼 크기 (바이트)
    LPCCH  lpDefaultChar,  // 변환 불가 시 대체 문자
    LPBOOL lpUsedDefaultChar // 대체 문자 사용 여부
);
```

### 코드 페이지 상수

| 상수 | 값 | 의미 |
|------|-----|------|
| CP_ACP | 0 | 시스템 기본 ANSI |
| CP_OEMCP | 1 | 시스템 기본 OEM |
| CP_UTF8 | 65001 | UTF-8 |
| CP_UTF7 | 65000 | UTF-7 |

### 변환 플래그

MultiByteToWideChar 플래그:

| 플래그 | 의미 |
|--------|------|
| MB_PRECOMPOSED | 합성 문자 사용 (기본값) |
| MB_COMPOSITE | 분해 문자 사용 |
| MB_ERR_INVALID_CHARS | 잘못된 바이트 시 실패 |
| MB_USEGLYPHCHARS | 글리프 문자 사용 |

WideCharToMultiByte 플래그:

| 플래그 | 의미 |
|--------|------|
| WC_COMPOSITECHECK | 합성 문자 확인 |
| WC_DISCARDNS | WC_COMPOSITECHECK와 함께 쓰여 비간격 결합 문자(nonspacing character)를 버림 |
| WC_SEPCHARS | 합성 문자 분리 |
| WC_DEFAULTCHAR | 기본 대체 문자 사용 |
| WC_ERR_INVALID_CHARS | 잘못된 문자 시 실패 |
| WC_NO_BEST_FIT_CHARS | 유사 문자 매핑 금지 |

`WC_COMPOSITECHECK` 계열 플래그는 합성/분해 변환에서만 의미가 있고, 코드 페이지에 따라 허용되는 플래그가 다르다. UTF-8 변환에는 임의의 flag가 허용되지 않는다.

### 2단계 변환 패턴

```cpp
#include <limits>
#include <stdexcept>

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";

    // Win32 길이 인자는 int다. INT_MAX 초과는 잘림/음수 길이를 만든다.
    if (utf8.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        throw std::length_error("UTF-8 input exceeds Win32 int length");

    // 1단계: 필요한 버퍼 크기 계산
    int wideLen = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.c_str(), (int)utf8.length(),
        nullptr, 0
    );

    if (wideLen == 0) {
        // 변환 실패
        throw std::runtime_error("Invalid UTF-8 sequence");
    }

    // 2단계: 실제 변환
    std::wstring wide(wideLen, L'\0');
    const int written = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.c_str(), (int)utf8.length(),
        &wide[0], wideLen
    );
    if (written != wideLen)
        throw std::runtime_error("conversion size changed unexpectedly");

    return wide;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";

    // Win32 길이 인자는 int다. INT_MAX 초과는 잘림/음수 길이를 만든다.
    if (wide.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        throw std::length_error("UTF-16 input exceeds Win32 int length");

    // 1단계: 필요한 버퍼 크기 계산
    int utf8Len = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS,
        wide.c_str(), (int)wide.length(),
        nullptr, 0,
        nullptr, nullptr
    );

    if (utf8Len == 0) {
        throw std::runtime_error("Invalid Unicode string");
    }

    // 2단계: 실제 변환
    std::string utf8(utf8Len, '\0');
    const int written = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS,
        wide.c_str(), (int)wide.length(),
        &utf8[0], utf8Len,
        nullptr, nullptr
    );
    if (written != utf8Len)
        throw std::runtime_error("conversion size changed unexpectedly");

    return utf8;
}
```

### 대체 문자 처리

```cpp
// 변환 불가 문자 감지
BOOL usedDefault = FALSE;
char defaultChar = '?';

int len = WideCharToMultiByte(
    949,  // CP949로 변환
    0,
    L"Hello 😀 World",  // 이모지는 CP949에 없음
    -1,
    buffer, sizeof(buffer),
    &defaultChar,    // 대체 문자
    &usedDefault     // 사용 여부
);

if (usedDefault) {
    printf("일부 문자가 변환되지 않았다\n");
}
// 결과: "Hello ? World" (flags·기본 문자·코드 페이지 의존 관측값)
```

### 변환 실패 처리

```cpp
#include <limits>

// 엄격한 변환 (실패 시 예외)
std::wstring StrictUtf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};

    // Win32 길이 인자는 int다. INT_MAX 초과를 먼저 거부한다.
    if (utf8.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        throw std::length_error("UTF-8 input exceeds Win32 int length");

    // 입력 길이를 명시하면 출력에 null 종결자가 포함되지 않는다.
    int srcLen = static_cast<int>(utf8.size());
    int len = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.data(), srcLen, nullptr, 0
    );

    if (len == 0) {
        DWORD err = GetLastError();
        if (err == ERROR_NO_UNICODE_TRANSLATION) {
            throw std::invalid_argument("Invalid UTF-8");
        }
        throw std::runtime_error("Conversion failed");
    }

    std::wstring result(len, L'\0');
    const int written = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.data(), srcLen, result.data(), len
    );
    if (written != len)
        throw std::runtime_error("conversion size changed unexpectedly");

    return result;
}
```

## 10. 파일 시스템과 인코딩

### NTFS와 Unicode

NTFS는 파일명을 UTF-16으로 저장한다.

| 항목 | 제한 |
|------|------|
| 파일명 최대 길이 | 255 UTF-16 코드 유닛 |
| 경로 최대 길이 | 기본 260자 (MAX_PATH) |
| 확장 경로 | 약 32,767자 |
| 허용 문자 | NUL, \\, /, :, *, ?, ", <, >, \| 와 제어 문자는 제외. Win32는 CON·PRN·AUX·NUL 등 예약 이름도 금지 |

### Short Name(8.3 형식)

호환성을 위해 NTFS는 긴 파일명에 짧은 이름을 자동 생성하지만, 볼륨 설정(`NtfsDisable8dot3NameCreation`)에 따라 생성되지 않을 수 있다. 아래는 특정 볼륨/namespace에서 얻은 예시일 뿐이며, 동일한 표시 문자열만으로 같은 short name이 보장되지는 않는다. 필요하면 실제로 조회한다.

```text
긴 이름:     "매우 긴 한글 파일명.txt"
짧은 이름:   "B0EB5~1.TXT"

긴 이름:     "Document2023.docx"
짧은 이름:   "DOCUME~1.DOC"
```

조회 방법:

```cpp
wchar_t shortPath[MAX_PATH];
GetShortPathNameW(L"매우 긴 파일명.txt", shortPath, MAX_PATH);
// shortPath: 실제 조회 결과 (예: "B0EB5~1.TXT")

wchar_t longPath[MAX_PATH];
GetLongPathNameW(shortPath, longPath, MAX_PATH);
// longPath: "매우 긴 파일명.txt"
```

### 긴 경로 지원 (Windows 10+)

기본 MAX_PATH(260자) 제한을 다루는 방법은 두 가지 범주다. `\\?\` 확장 경로 문법과, 지원되는 Win32 API에 대한 opt-in이다. opt-in은 매니페스트의 `longPathAware=true`와 시스템의 `LongPathsEnabled=1` 정책이 **함께** 충족되어야 하며, 둘 중 하나만으로 항상 해제되지 않는다. 상대 경로나 모든 API가 무제한이 되는 것도 아니다.

방법 1: `\\?\` 확장 경로 문법

```cpp
// 확장 경로 문법
HANDLE hFile = CreateFileW(
    L"\\\\?\\C:\\Very\\Long\\Path\\...\\file.txt",
    GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL
);

// 주의: \\?\ 경로는 정규화되지 않음
// . 과 .. 가 동작하지 않음
```

방법 2: 지원 API에 대한 opt-in — `longPathAware` 매니페스트 AND `LongPathsEnabled` 정책

```xml
<!-- app.manifest -->
<application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
        <longPathAware
            xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">
            true
        </longPathAware>
    </windowsSettings>
</application>
```

```text
컴퓨터 구성 → 관리 템플릿 → 시스템 → 파일 시스템
→ Win32 긴 경로 사용
```

### FindFirstFile과 인코딩

```cpp
WIN32_FIND_DATAW findData;
HANDLE hFind = FindFirstFileW(L"C:\\폴더\\*", &findData);

if (hFind != INVALID_HANDLE_VALUE) {
    do {
        wprintf(L"파일: %s\n", findData.cFileName);

        // 대체 이름 (8.3)
        if (findData.cAlternateFileName[0]) {
            wprintf(L"  8.3: %s\n", findData.cAlternateFileName);
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);
}
```

### 파일명 정규화

```cpp
#include <pathcch.h>
#pragma comment(lib, "pathcch.lib")

wchar_t normalized[MAX_PATH];
HRESULT hr = PathCchCanonicalize(
    normalized, MAX_PATH,
    L"C:\\Folder\\..\\Other\\file.txt"
);
// 결과: "C:\\Other\\file.txt"

// 또는 GetFullPathName
wchar_t fullPath[MAX_PATH];
GetFullPathNameW(L"relative\\path.txt", MAX_PATH,
                 fullPath, NULL);
```

## 11. 실전 패턴 모음

### 패턴 1: UTF-8 중심 아키텍처

```cpp
class Application {
    // 모든 내부 문자열은 UTF-8
    std::string appName_;
    std::string configPath_;

public:
    // Windows API 호출 시에만 변환
    void CreateMainWindow() {   // CreateWindow는 winuser.h의 매크로라 이름 충돌
        std::wstring wTitle = Utf8ToWide(appName_);
        HWND hwnd = CreateWindowW(
            L"MyClass", wTitle.c_str(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
            NULL, NULL, GetModuleHandle(NULL), NULL
        );
    }

    // 파일 I/O
    std::vector<uint8_t> ReadFile(const std::string& utf8Path) {
        std::wstring wPath = Utf8ToWide(utf8Path);
        HANDLE hFile = CreateFileW(
            wPath.c_str(), GENERIC_READ,
            FILE_SHARE_READ, NULL,
            OPEN_EXISTING, 0, NULL
        );
        // ...
    }
};
```

### 패턴 2: 레거시 ANSI 라이브러리 연동

```cpp
// 레거시 라이브러리가 ANSI만 지원하는 경우
class LegacyWrapper {
public:
    void ProcessText(const std::wstring& input) {
        // 시스템 코드 페이지로 변환
        int len = WideCharToMultiByte(
            CP_ACP, 0, input.c_str(), -1,
            nullptr, 0, nullptr, nullptr
        );
        if (len == 0) throw std::runtime_error("ACP conversion failed");

        std::string ansi(len, '\0');
        int written = WideCharToMultiByte(
            CP_ACP, 0, input.c_str(), -1,
            &ansi[0], len, nullptr, nullptr
        );
        if (written != len) throw std::runtime_error("ACP conversion failed");

        // 레거시 API 호출
        legacy_ansi_process(ansi.c_str());
    }
};
```

### 패턴 3: 안전한 리소스 문자열

```cpp
class ResourceString {
public:
    static std::wstring Load(HINSTANCE hInst, UINT id) {
        // 먼저 길이 확인
        const wchar_t* ptr = nullptr;
        int len = LoadStringW(hInst, id,
                             (LPWSTR)&ptr, 0);

        if (len == 0) return L"";

        // 문자열 복사
        return std::wstring(ptr, len);
    }

    static std::string LoadUtf8(HINSTANCE hInst, UINT id) {
        return WideToUtf8(Load(hInst, id));
    }
};

// 사용
std::wstring msg = ResourceString::Load(hInst, IDS_MESSAGE);
```

### 패턴 4: 로그 출력 통합

```cpp
class Logger {
    HANDLE hConsole_;
    FILE* logFile_;

public:
    void Log(const char* utf8Message) {
        std::wstring wide = Utf8ToWide(utf8Message);

        // 콘솔 출력
        DWORD mode;
        if (GetConsoleMode(hConsole_, &mode)) {
            DWORD written;
            WriteConsoleW(hConsole_, wide.c_str(),
                         wide.length(), &written, NULL);
            WriteConsoleW(hConsole_, L"\n", 1,
                         &written, NULL);
        }

        // 파일 출력 (UTF-8)
        if (logFile_) {
            fprintf(logFile_, "%s\n", utf8Message);
            fflush(logFile_);
        }
    }
};
```

### 패턴 5: 명령줄 인수 처리

```cpp
int wmain(int argc, wchar_t* argv[]) {
    // Wide 문자열로 직접 받음
    for (int i = 1; i < argc; i++) {
        std::string utf8Arg = WideToUtf8(argv[i]);
        ProcessArgument(utf8Arg);
    }
    return 0;
}

// 또는 main에서 변환
int main(int argc, char* argv[]) {
    // 명령줄을 Wide로 다시 가져오기
    int wargc = 0;
    wchar_t** wargv = CommandLineToArgvW(
        GetCommandLineW(), &wargc
    );
    if (!wargv) {
        return 1;  // 변환 실패 — GetLastError() 확인
    }

    // 변환 중 예외가 나도 LocalFree가 실행되도록 소유권을 잡는다.
    struct LocalFreeGuard {
        wchar_t** p;
        ~LocalFreeGuard() { LocalFree(p); }
    } guard{wargv};

    for (int i = 1; i < wargc; i++) {
        std::string utf8Arg = WideToUtf8(wargv[i]);
        ProcessArgument(utf8Arg);
    }

    return 0;
}
```

## 12. 문제 해결 체크리스트

### 한글 깨짐 진단

| 증상 | 원인 | 해결책 |
|------|------|--------|
| ??? 표시 | 대체 문자로 변환됨 | 코드 페이지 확인 |
| 엉뚱한 문자 | 잘못된 코드 페이지 | CP_UTF8 사용 |
| 물음표 박스 | 폰트 미지원 | 폰트 변경 |
| 반쪽 문자 | MBCS 경계 문제 | CharNext 사용 |
| null 포함 | UTF-16을 ANSI로 해석 | W 함수 사용 |

### 디버깅 팁

```cpp
// 바이트 단위 덤프
void DumpBytes(const char* label, const void* data, size_t len) {
    printf("%s [%zu bytes]: ", label, len);
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < len && i < 32; i++) {
        printf("%02X ", p[i]);
    }
    printf("\n");
}

// 사용
std::string s = "한글";
DumpBytes("UTF-8", s.c_str(), s.length());
// 출력: UTF-8 [6 bytes]: ED 95 9C EA B8 80
```

### 인코딩 감지

```cpp
enum class Encoding {
    Unknown,
    Utf8,
    Utf8Bom,
    Utf16LE,
    Utf16BE,
    Ansi
};

// 인코딩 추정이 아니라, 주어진 바이트 span이 UTF-8로 유효한지 검사한다.
// overlong, UTF-16 surrogate, U+10FFFF 초과, F5-FF 선행 바이트를 거부한다.
// ASCII와 내장 U+0000은 유효하다.
bool ValidUtf8(const uint8_t* s, size_t len) {
    size_t i = 0;
    while (i < len) {
        const uint8_t b = s[i++];
        if (b <= 0x7F) continue;

        size_t cont;
        uint32_t cp, min_cp;
        if (b >= 0xC2 && b <= 0xDF) { cont = 1; cp = b & 0x1F; min_cp = 0x80; }
        else if (b >= 0xE0 && b <= 0xEF) { cont = 2; cp = b & 0x0F; min_cp = 0x800; }
        else if (b >= 0xF0 && b <= 0xF4) { cont = 3; cp = b & 0x07; min_cp = 0x10000; }
        else return false;  // 0xC0/0xC1(overlong), 0xF5-0xFF

        if (len - i < cont) return false;
        for (size_t k = 0; k < cont; ++k) {
            const uint8_t c = s[i++];
            if ((c & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (c & 0x3F);
        }
        // overlong, U+10FFFF 초과, surrogate 영역을 배제한다.
        if (cp < min_cp || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
    }
    return true;
}

Encoding DetectEncoding(const uint8_t* data, size_t len) {
    if (len >= 3 && data[0] == 0xEF &&
        data[1] == 0xBB && data[2] == 0xBF) {
        // BOM은 형식 힌트일 뿐이다. BOM 뒤 payload가 유효하지 않으면
        // 완전한 UTF-8 텍스트로 승인하지 않는다.
        return ValidUtf8(data + 3, len - 3)
            ? Encoding::Utf8Bom : Encoding::Unknown;
    }

    if (len >= 2 && data[0] == 0xFF && data[1] == 0xFE)
        return Encoding::Utf16LE;

    if (len >= 2 && data[0] == 0xFE && data[1] == 0xFF)
        return Encoding::Utf16BE;

    // UTF-8 유효성 검사와 인코딩 추정을 분리한다. 고비트가 있으면서
    // 유효한 UTF-8일 때만 Utf8로 판정한다.
    bool hasHighBit = false;
    for (size_t i = 0; i < len; ++i) {
        if (data[i] >= 0x80) { hasHighBit = true; break; }
    }

    if (hasHighBit && ValidUtf8(data, len))
        return Encoding::Utf8;

    // BOM 없는 ASCII는 UTF-8과 legacy 인코딩이 구별되지 않는다.
    return Encoding::Unknown;  // ASCII와 임의의 legacy 바이트는 인코딩을 확정할 수 없다
}
```


---

## 결론

Windows 문자열 처리는 단순해 보이지만 함정이 많다. 핵심 원칙은 다음과 같다.

| 원칙 | 설명 |
|------|------|
| 내부는 UTF-8 | 저장, 네트워크, 로직 모두 UTF-8 |
| API 경계에서 변환 | Windows API 호출 시에만 UTF-16 |
| W 함수 사용 | A 함수는 레거시 |
| 안전한 함수 사용 | strsafe.h 또는 _s 함수 |
| MBCS 경계 주의 | CharNext/CharPrev 사용 |
| 명시적 코드 페이지 | CP_UTF8 직접 지정 |
| 모듈 경계는 전용 할당자 | COM 경계는 `SysAllocString`·`CoTaskMemAlloc`과 짝이 맞는 래퍼 |

레거시 코드를 유지보수할 때는 기존 인코딩 전략을 파악하는 것이 우선이다. 새 코드에서는 처음부터 UTF-8 중심 아키텍처를 설계하는 것이 장기적으로 유리하다. Win9x 시대의 산물인 TCHAR 매크로 시스템은 새 코드에서는 사용하지 않는다. 직접 `wchar_t`와 W 계열 API를 사용하고, 내부 데이터는 UTF-8 `std::string`으로 유지한다.


## 더 읽을거리

- [Windows Data Types](https://learn.microsoft.com/en-us/windows/win32/winprog/windows-data-types)
- [Microsoft Learn: BSTR](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/automat/bstr)
- [Wine의 oleaut32 구현](https://source.winehq.org/git/wine.git/tree/HEAD:/dlls/oleaut32) - BSTR/VARIANT 오픈소스 구현 (URL 접근·리비전 미검증; 확인 시 정확한 file+commit permalink로 대체)
- Box, D. (1998). *Essential COM* - 배경 자료(판본·장 미지정)
- Rector, B. & Sells, C. (1999). *ATL Internals* - ATL 3.0 대상 역사적 배경. 현재 CComBSTR/ATL 계약은 Microsoft Learn(msvc-170) 기준
- [Unicode in the Windows API](https://learn.microsoft.com/en-us/windows/win32/intl/unicode-in-the-windows-api)
- [Strsafe Functions](https://learn.microsoft.com/en-us/windows/win32/menurc/strsafe-ovw)
- [National Language Support](https://learn.microsoft.com/en-us/windows/win32/intl/national-language-support)
- [UTF-8 Everywhere Manifesto](https://utf8everywhere.org/) - URL 내용 미검증
- [The Old New Thing - Raymond Chen's Blog](https://devblogs.microsoft.com/oldnewthing/) - Windows 내부 동작 일반 참고용 아카이브(개별 글 인용 아님)
- [Windows Console Documentation](https://learn.microsoft.com/en-us/windows/console/)
- [ICU - International Components for Unicode](https://icu.unicode.org/) - 국제화 추가 읽을거리 (Win32 COM 계약 근거 아님)
- [utfcpp - UTF-8 with C++ in a Portable Way](https://github.com/nemtrif/utfcpp) - UTF-8 유효성 검사·변환 구현 대안
- [simdutf - Unicode validation and transcoding at billions of characters per second](https://github.com/simdutf/simdutf) - 고성능 UTF-8 유효성 검사·변환 구현 대안
