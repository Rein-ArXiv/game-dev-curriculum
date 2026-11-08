#pragma once

// 토글 패널의 기하 계산. raylib/raygui 심볼을 하나도 참조하지 않는 순수 계산이라
// headless 테스트 타깃(parts_core_tests)에서 그대로 검증된다.
//
// 여기 있는 상수와 수식은 hud.cpp 가 위젯을 배치할 때 쓰는 바로 그 값이다.
// 문서용 복제본이 아니다.

namespace ppl {

// 정수 좌표 사각형. raylib Rectangle(float 필드)로의 변환은 hud.cpp 가 한다.
struct UiRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// 두 사각형이 한 픽셀이라도 겹치는가. 경계만 맞닿는 것은 겹침이 아니다.
// 0 이하 크기는 면적이 0이므로 어떤 사각형과도 겹치지 않는다(음수 크기는 미지원).
constexpr bool RectsOverlap(const UiRect& a, const UiRect& b) {
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0) return false;
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

// 토글 패널 한 행의 치수.
inline constexpr int kPanelRowH   = 22;   // 위젯 높이
inline constexpr int kPanelRowGap = 2;    // 행 사이 여백 → 행 간격 24
inline constexpr int kPanelLabelW = 96;   // 라벨 폭. 필드는 그 오른쪽에 붙는다
inline constexpr int kPanelFieldW = 130;  // 콤보박스 폭
inline constexpr int kPanelColGap = 8;    // 컬럼 간 여백 단위

// r 번째 행의 y.
constexpr int PanelRowY(int y, int r) {
    return y + r * (kPanelRowH + kPanelRowGap);
}

// r 번째 행의 필드(콤보박스) 사각형. 라벨 오른쪽에 놓인다.
constexpr UiRect PanelFieldRect(int x, int y, int r) {
    return UiRect{x + kPanelLabelW, PanelRowY(y, r), kPanelFieldW, kPanelRowH};
}

// 두 번째 컬럼의 원점 x.
constexpr int PanelColumn2X(int x) {
    return x + kPanelLabelW + kPanelFieldW + kPanelColGap * 4;
}

// raygui 4.0 이 쓰는 드롭다운 항목 간격의 기본값(GuiLoadStyleDefault 에서
// GuiSetStyle(DROPDOWNBOX, DROPDOWN_ITEMS_SPACING, 2)).
inline constexpr int kDropdownItemsSpacing = 2;

// GuiDropdownBox 가 *펼쳐졌을 때* 차지하는 사각형. raygui 4.0 의 계산과 같다.
//   boundsOpen.height = (itemCount + 1) * (bounds.height + DROPDOWN_ITEMS_SPACING)
// 접힌 상자 자리에서 시작해 아래로 자란다.
constexpr UiRect DropdownOpenRect(const UiRect& field, int itemCount) {
    return UiRect{field.x, field.y, field.w,
                  (itemCount + 1) * (field.h + kDropdownItemsSpacing)};
}

// 패널에서 가장 항목이 많은 콤보(allocator = 4개). 펼친 높이 계산의 상한이다.
inline constexpr int kComboMaxItems = 4;

// 좌측 컬럼의 콤보 행 수(entities, allocator, layout).
inline constexpr int kPanelLeftRows = 3;

// 좌측 컬럼에서 펼친 콤보가 가장 낮게 끝나는 경우의 여유분.
// 2번째 행(allocator, 4항목)의 y 오프셋 1*24 에 펼친 높이 5*24=120 을 더한 144 다.
inline constexpr int kPanelOpenReserve =
    (kPanelRowH + kPanelRowGap) + (kComboMaxItems + 1) * (kPanelRowH + kPanelRowGap);

// raygui 4.0은 드롭다운을 아래로 펼치므로, 기본 배치에서는 720 창에서 allocator
// 팝업 하단이 736이 되어 마지막 항목이 화면 밖으로 나간다. 팝업 위치 대신 패널
// 상단을 필요한 만큼 위로 올려 모든 열린 rect가 창 안에 오게 한다. 그리기 rect와
// hit-test rect가 같은 값을 쓰므로 "잘라 그리기만 하는" 회피가 아니다.
inline int PanelOriginY(int desiredY, int windowH) {
    const int bottom = desiredY + kPanelOpenReserve;
    return bottom <= windowH ? desiredY : desiredY - (bottom - windowH);
}

// 좌측 컬럼 세 콤보(항목 수 3,4,3)를 모두 펼쳤을 때 창 안에 들어오는가.
inline bool PanelOpenFitsWindow(int panelY, int windowH) {
    constexpr int kItems[kPanelLeftRows] = {3, kComboMaxItems, 3};
    for (int r = 0; r < kPanelLeftRows; ++r) {
        const UiRect f = PanelFieldRect(0, panelY, r);
        const UiRect open = DropdownOpenRect(f, kItems[r]);
        if (open.y + open.h > windowH) return false;
    }
    return true;
}

// 펼친 목록은 아래 행 위젯을 덮는다(위 두 함수로 확인할 수 있다). 그래서 hud.cpp 는
// 콤보가 열려 있는 동안 나머지 위젯을 GuiLock 으로 잠그고, 열린 콤보만 두 번째
// 패스에서 마지막에 그린다. 이 판정이 "이번 패스에 이 위젯을 그리는가" 다.
constexpr bool PanelDrawsWidget(bool widgetOpen, bool openPass) {
    return widgetOpen == openPass;
}

}  // namespace ppl
