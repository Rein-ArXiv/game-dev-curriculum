## 들어가며

UTF-8, UTF-16, 코드 페이지의 기본 개념만으로는 Windows의 텍스트 경계를 안전하게 다루기 어렵다. 왜 `CreateFileA`와 `CreateFileW`가 따로 존재하는지, 한국어 Windows에서 콘솔 출력이 왜 깨지는지, `LoadStringA`가 MBCS 문자열을 어떻게 잘라 먹는지, `strcpy_s`와 `StringCchCopy`의 계약이 무엇이 다른지는 Windows 플랫폼 ABI와 NLS 정책의 문제다.

이 글은 Windows 플랫폼 경계에서 필요한 MBCS 처리, 콘솔 인코딩, 안전한 문자열 함수, locale과 NLS, TCHAR 마이그레이션, `WideCharToMultiByte`/`MultiByteToWideChar`의 계약을 다룬다. 공통 Unicode 모델을 반복하는 대신 Windows API가 요구하는 표현과 실패 조건에 집중한다.

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
8. [WideCharToMultiByte와 MultiByteToWideChar 심화](#8-widechartomultibyte와-multibytetowidechar-심화)
9. [파일 시스템과 인코딩](#9-파일-시스템과-인코딩)
10. [실전 패턴 모음](#10-실전-패턴-모음)
11. [문제 해결 체크리스트](#11-문제-해결-체크리스트)


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
size_t mbcs_safe_copy(char* dest, const char* src, size_t max_bytes) {
    size_t written = 0;
    const unsigned char* p = (const unsigned char*)src;

    while (*p && written < max_bytes) {
        size_t char_len = IsDBCSLeadByte(*p) ? 2 : 1;

        // 전체 문자가 들어가는지 확인
        if (written + char_len > max_bytes)
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
_mbslen((unsigned char*)str);  // 6 (문자)
```

## 2. LoadString의 함정

### 기본 사용법

```cpp
int LoadStringA(
    HINSTANCE hInstance,
    UINT      uID,
    LPSTR     lpBuffer,
    int       cchBufferMax  // 버퍼 크기 (문자 단위)
);

int LoadStringW(
    HINSTANCE hInstance,
    UINT      uID,
    LPWSTR    lpBuffer,
    int       cchBufferMax
);
```

### MBCS에서의 버퍼 잘림 문제

`LoadStringA`의 `cchBufferMax`는 바이트 수로 해석되지만, 반환값은 복사된 문자 수다. 이 불일치가 문제를 일으킨다.

```cpp
// 리소스 문자열: "한글 테스트입니다" (17바이트 — 한글 8자×2 + 공백 1)
char buffer[10];
int result = LoadStringA(hInstance, IDS_MESSAGE, buffer, 10);

// 내부 동작:
// 1. 10바이트까지 복사 시도
// 2. 문자 경계 확인 없이 자를 수 있음
// 3. 불완전한 MBCS 시퀀스 발생 가능
```

실제 테스트 결과:

| 버퍼 크기 | 예상 동작 | 실제 동작 (일부 버전) |
|-----------|-----------|----------------------|
| 10 | 5문자 + null | 9바이트 = 5문자 (경계에 맞음) |
| 11 | 5문자 + null | 10바이트 (경계 무시 — '트'가 반으로 잘림) |

### 안전한 LoadString 래퍼

```cpp
// MBCS 경계를 존중하는 LoadString 래퍼
int SafeLoadStringA(HINSTANCE hInstance, UINT uID,
                    LPSTR lpBuffer, int cchBufferMax) {
    if (cchBufferMax <= 0) return 0;

    // 넉넉한 임시 버퍼 사용
    char tempBuffer[1024];
    int len = LoadStringA(hInstance, uID, tempBuffer,
                          sizeof(tempBuffer));

    if (len == 0) {
        lpBuffer[0] = '\0';
        return 0;
    }

    // MBCS 안전 복사
    size_t written = mbcs_safe_copy(lpBuffer, tempBuffer,
                                     cchBufferMax - 1);
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
// stdout은 기본적으로 ANSI 모드
// UTF-8 출력을 위해 모드 변경 필요

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
// printf: CRT를 통함 (코드 페이지 변환 발생)
printf("테스트");  // 현재 로캘에 따라 동작

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
    DWORD mode;

    if (GetConsoleMode(hOutput, &mode)) {
        // 콘솔 출력
        DWORD written;
        WriteConsoleW(hOutput, str, wcslen(str), &written, NULL);
    } else {
        // 파이프/파일로 리다이렉션됨 → UTF-8로 변환
        std::string utf8 = WideToUtf8(str);
        DWORD written;
        WriteFile(hOutput, utf8.c_str(), utf8.length(),
                  &written, NULL);
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

| 기능 | 레거시 콘솔 | Windows Terminal |
|------|-------------|------------------|
| UTF-8 지원 | 제한적 | 완전 지원 |
| 이모지 | 일부 | 완전 지원 |
| 색상 | 16색 | TrueColor |
| 가변폭 글꼴 | 불가 | 지원 |
| GPU 렌더링 | 불가 | 지원 |

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
// 결과: "한국어(대한민국)" — 순수 언어 이름은 LOCALE_SLOCALIZEDLANGUAGENAME
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
// result == CSTR_EQUAL (2)

// 언어별 정렬 (독일어 ß = ss)
result = CompareStringW(
    MAKELCID(MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN), SORT_DEFAULT),
    NORM_LINGUISTIC_CASING,
    L"straße", -1,
    L"strasse", -1
);
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

// 문자 단위 - 100자까지
StringCchCopyA(buffer, 100, src);
StringCchCopyW(wbuffer, 100, wsrc);  // 100 wchar_t

// 바이트 단위 - 100바이트까지
StringCbCopyA(buffer, 100, src);
StringCbCopyW(wbuffer, 100, wsrc);   // 100바이트 = 50 wchar_t
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
| 반환 타입 | HRESULT | errno_t |
| 실패 시 동작 | null 종료 보장 | 핸들러 호출 |
| 이식성 | Windows 전용 | C11 표준 (일부) |
| 버퍼 오버플로 | 잘림 허용 | 기본 에러 |
| 매크로 오버로드 | 있음 | 없음 |

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

### 권장 사항

| 상황 | 권장 |
|------|------|
| Windows 전용 코드 | Strsafe |
| 크로스 플랫폼 | CRT Secure + 폴백 |
| 레거시 호환 | 래퍼 함수 작성 |
| 성능 중요 | 수동 검증 + memcpy |

크로스 플랫폼 래퍼:

```cpp
#ifdef _WIN32
    #include <strsafe.h>
    #define SAFE_STRCPY(d, sz, s) StringCchCopyA(d, sz, s)
#else
    #define SAFE_STRCPY(d, sz, s) do { \
        strncpy(d, s, sz - 1); \
        d[sz - 1] = '\0'; \
    } while(0)
#endif
```

## 6. TCHAR 마이그레이션

### TCHAR의 역사

1990년대, Windows는 ANSI(Win9x)와 Unicode(NT) 두 계열로 분리되어 있었다. 하나의 소스로 양쪽을 지원하기 위해 TCHAR가 도입되었다.

```cpp
#include <tchar.h>

// UNICODE 정의 여부에 따라
#ifdef UNICODE
    typedef wchar_t TCHAR;
    #define _T(x) L##x
    #define TEXT(x) L##x
#else
    typedef char TCHAR;
    #define _T(x) x
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
// 기존 ANSI 라이브러리와 연동 시
std::wstring input = L"한글 입력";

// UTF-8로 변환 후 레거시 API 호출
std::string utf8 = WideToUtf8(input);
legacy_api(utf8.c_str());

// 결과를 다시 Wide로
std::string result_utf8 = legacy_api_result();
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

### `_bstr_t` (COM용)

BSTR 래퍼 클래스다.

```cpp
#include <comutil.h>

_bstr_t bstr = L"COM 문자열";
BSTR raw = bstr.GetBSTR();

// 자동 변환
_bstr_t fromAnsi = "ANSI";     // ANSI → Wide 변환
_bstr_t fromWide = L"Wide";

// std::string 변환
std::string utf8((const char*)bstr);  // 주의: ANSI 변환
```

## 8. WideCharToMultiByte와 MultiByteToWideChar 심화

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
| WC_DISCARDNS | 분해 시 비공백 무시 |
| WC_SEPCHARS | 합성 문자 분리 |
| WC_DEFAULTCHAR | 기본 대체 문자 사용 |
| WC_ERR_INVALID_CHARS | 잘못된 문자 시 실패 |
| WC_NO_BEST_FIT_CHARS | 유사 문자 매핑 금지 |

### 2단계 변환 패턴

```cpp
std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";

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
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.c_str(), (int)utf8.length(),
        &wide[0], wideLen
    );

    return wide;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";

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
    WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS,
        wide.c_str(), (int)wide.length(),
        &utf8[0], utf8Len,
        nullptr, nullptr
    );

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
// 결과: "Hello ? World"
```

### 변환 실패 처리

```cpp
// 엄격한 변환 (실패 시 예외)
std::wstring StrictUtf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};

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
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.data(), srcLen, result.data(), len
    );

    return result;
}
```

## 9. 파일 시스템과 인코딩

### NTFS와 Unicode

NTFS는 파일명을 UTF-16으로 저장한다.

| 항목 | 제한 |
|------|------|
| 파일명 최대 길이 | 255 UTF-16 코드 유닛 |
| 경로 최대 길이 | 기본 260자 (MAX_PATH) |
| 확장 경로 | 약 32,767자 |
| 허용 문자 | NUL, \\, /, :, *, ?, ", <, >, \| 제외 전부 |

### Short Name(8.3 형식)

호환성을 위해 NTFS는 긴 파일명에 짧은 이름을 자동 생성한다.

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
// shortPath: "B0EB5~1.TXT"

wchar_t longPath[MAX_PATH];
GetLongPathNameW(shortPath, longPath, MAX_PATH);
// longPath: "매우 긴 파일명.txt"
```

### 긴 경로 지원 (Windows 10+)

기본 MAX_PATH(260자) 제한을 우회하는 방법은 세 가지다.

방법 1: `\\?\` 접두사

```cpp
// 확장 경로 문법
HANDLE hFile = CreateFileW(
    L"\\\\?\\C:\\Very\\Long\\Path\\...\\file.txt",
    GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL
);

// 주의: \\?\ 경로는 정규화되지 않음
// . 과 .. 가 동작하지 않음
```

방법 2: 매니페스트 설정 (Win10 1607+)

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

방법 3: 그룹 정책

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

## 10. 실전 패턴 모음

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

        std::string ansi(len, '\0');
        WideCharToMultiByte(
            CP_ACP, 0, input.c_str(), -1,
            &ansi[0], len, nullptr, nullptr
        );

        // 레거시 API 호출
        legacy_process(ansi.c_str());
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
    int wargc;
    wchar_t** wargv = CommandLineToArgvW(
        GetCommandLineW(), &wargc
    );

    for (int i = 1; i < wargc; i++) {
        std::string utf8Arg = WideToUtf8(wargv[i]);
        ProcessArgument(utf8Arg);
    }

    LocalFree(wargv);
    return 0;
}
```

## 11. 문제 해결 체크리스트

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

Encoding DetectEncoding(const uint8_t* data, size_t len) {
    if (len >= 3 && data[0] == 0xEF &&
        data[1] == 0xBB && data[2] == 0xBF)
        return Encoding::Utf8Bom;

    if (len >= 2 && data[0] == 0xFF && data[1] == 0xFE)
        return Encoding::Utf16LE;

    if (len >= 2 && data[0] == 0xFE && data[1] == 0xFF)
        return Encoding::Utf16BE;

    // UTF-8 휴리스틱
    bool validUtf8 = true;
    bool hasHighBit = false;

    for (size_t i = 0; i < len && validUtf8; ) {
        if (data[i] < 0x80) {
            i++;
        } else if ((data[i] & 0xE0) == 0xC0) {
            if (i + 1 >= len || (data[i+1] & 0xC0) != 0x80)
                validUtf8 = false;
            else { i += 2; hasHighBit = true; }
        } else if ((data[i] & 0xF0) == 0xE0) {
            if (i + 2 >= len ||
                (data[i+1] & 0xC0) != 0x80 ||
                (data[i+2] & 0xC0) != 0x80)
                validUtf8 = false;
            else { i += 3; hasHighBit = true; }
        } else if ((data[i] & 0xF8) == 0xF0) {
            if (i + 3 >= len ||
                (data[i+1] & 0xC0) != 0x80 ||
                (data[i+2] & 0xC0) != 0x80 ||
                (data[i+3] & 0xC0) != 0x80)
                validUtf8 = false;
            else { i += 4; hasHighBit = true; }
        } else {
            validUtf8 = false;
        }
    }

    if (validUtf8 && hasHighBit)
        return Encoding::Utf8;

    return Encoding::Ansi;  // 또는 Unknown
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

레거시 코드를 유지보수할 때는 기존 인코딩 전략을 파악하는 것이 우선이다. 새 코드에서는 처음부터 UTF-8 중심 아키텍처를 설계하는 것이 장기적으로 유리하다. Win9x 시대의 산물인 TCHAR 매크로 시스템은 새 코드에서는 사용하지 않는다. 직접 `wchar_t`와 W 계열 API를 사용하고, 내부 데이터는 UTF-8 `std::string`으로 유지한다.


## 더 읽을거리

- [Windows Data Types](https://learn.microsoft.com/en-us/windows/win32/winprog/windows-data-types)
- [Unicode in the Windows API](https://learn.microsoft.com/en-us/windows/win32/intl/unicode-in-the-windows-api)
- [Strsafe Functions](https://learn.microsoft.com/en-us/windows/win32/menurc/strsafe-ovw)
- [National Language Support](https://learn.microsoft.com/en-us/windows/win32/intl/national-language-support)
- [UTF-8 Everywhere Manifesto](https://utf8everywhere.org/)
- [The Old New Thing - Raymond Chen's Blog](https://devblogs.microsoft.com/oldnewthing/)
- [Windows Console Documentation](https://learn.microsoft.com/en-us/windows/console/)
- [ICU - International Components for Unicode](https://icu.unicode.org/)
- [utfcpp - UTF-8 with C++ in a Portable Way](https://github.com/nemtrif/utfcpp)
- [simdutf - Unicode validation and transcoding at billions of characters per second](https://github.com/simdutf/simdutf)
