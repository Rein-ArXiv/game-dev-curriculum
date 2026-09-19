## 들어가며

게임 엔진은 컴파일 시 사라지는 타입 구조를 런타임 메타데이터로 복원해야 한다. 에디터에서 `Health`라는 변수를 슬라이더로 보여주려면, 스크립트에서 `GetHealth`를 호출하려면, 네트워크로 객체를 동기화하려면, 런타임에 "이 클래스는 어떤 멤버를 가지는가?"라는 질문에 답해야 한다. C++는 컴파일 타임에는 이 정보를 알지만, 기본 실행 모델은 필요한 메타데이터를 자동으로 보존하지 않는다.

Unreal Engine이 첫 세대부터 UObject 시스템에 자체 타입 정보를 구축한 이유가 여기에 있다. RTTI는 있지만 그것만으로는 턱없이 부족하다. 게임 엔진은 자신만의 리플렉션 시스템을 구축한다.

이 글은 RTTI와 별도로 선택적 런타임 타입 정보를 구축하는 세 가지 접근(매크로, 코드 생성, 컴파일러 AST 활용)을 정리하고, C++26 작업 초안에 채택된 static reflection이 런타임 메타데이터 생성을 어떻게 바꾸는지 살펴본다.


---

## 목차
1. [리플렉션이란 무엇인가](#1-리플렉션이란-무엇인가)
2. [런타임 메타데이터의 비용 모델](#2-런타임-메타데이터의-비용-모델)
3. [RTTI의 한계](#3-rtti의-한계)
4. [매크로 기반 리플렉션](#4-매크로-기반-리플렉션)
5. [코드 생성: Unreal의 UHT](#5-코드-생성-unreal의-uht)
6. [컴파일러 확장: libclang 활용](#6-컴파일러-확장-libclang-활용)
7. [C++26 static reflection](#7-c26-static-reflection)
8. [실전 적용: 에디터, 스크립팅, 직렬화](#8-실전-적용-에디터-스크립팅-직렬화)


---

## 1. 리플렉션이란 무엇인가

리플렉션(Reflection)은 프로그램이 타입·멤버·선언 같은 자기 구조를 값처럼 조회하고, 그 정보로 코드를 생성하거나 런타임 동작을 구성하는 능력이다. 컴파일 타임 리플렉션과 런타임 리플렉션은 시점과 비용 모델이 다르다. 전자는 컴파일러가 선언을 열거해 코드를 생성하고, 후자는 실행 파일에 보존된 타입 테이블을 이름이나 ID로 조회한다.

게임 엔진이 리플렉션에서 얻고자 하는 것은 세 가지다.

**1) 타입 정보 (Type Introspection)**
```cpp
Class* type = object->GetClass();
const char* name = type->GetName();  // "ACharacter"
Class* parent = type->GetParent();    // "AActor"
bool isA = object->IsA<AActor>();     // true
```

**2) 멤버 접근 (Member Access)**
```cpp
// 이름으로 프로퍼티에 접근
Property* prop = type->FindProperty("Health");
float value = prop->GetValue<float>(object);
prop->SetValue(object, 100.0f);

// 이름으로 메서드 호출
Method* method = type->FindMethod("TakeDamage");
method->Invoke(object, 25.0f);
```

**3) 메타데이터 (Metadata)**
```cpp
UPROPERTY(EditAnywhere, Category="Stats", meta=(ClampMin=0, ClampMax=100))
float Health = 100.0f;

float minValue = prop->GetMetadata<float>("ClampMin");  // 0
```

이 세 가지가 가능하면, 에디터는 클래스의 모든 프로퍼티를 자동으로 UI로 만들 수 있고, 스크립팅 시스템은 C++ 객체를 스크립트에 노출할 수 있으며, 직렬화 시스템은 객체를 파일이나 네트워크로 저장할 수 있다.

전통적인 C++23까지의 표준 RTTI만으로는 이 세 요구를 직접 충족할 수 없다. C++26 static reflection은 컴파일 타임 열거와 코드 생성을 제공하지만, 런타임 이름 조회·버전 정책·에디터 메타데이터 저장은 애플리케이션이 생성한 테이블과 규약이 여전히 소유한다.

## 2. 런타임 메타데이터의 비용 모델

범용 런타임 리플렉션은 타입 이름, 멤버 목록, 접근 함수와 메타데이터를 실행 파일에 보존해야 한다. 그 비용은 바이너리 크기와 로딩·메모리뿐 아니라 링크 시 제거 가능성, ABI 안정성, 접근 제어와 버전 마이그레이션 정책까지 포함한다. 모든 타입에 자동으로 이 비용을 부과하는 대신 필요한 타입과 멤버를 선택해 등록하는 모델이 C++의 zero-overhead 원칙과 잘 맞는다.

에디터·스크립트 바인딩·일반 직렬화는 풍부한 메타데이터가 유용하지만 반드시 범용 리플렉션만으로 구현되는 것은 아니다. 수동 serializer, 명시적 schema와 binding 함수도 가능하다. 규모가 커질수록 중복 등록과 schema drift를 줄이기 위해 하나의 타입 정의에서 에디터·직렬화·네트워크 코드를 생성하는 리플렉션 파이프라인의 가치가 커진다.

역사적으로 세 가지 접근법이 등장했다.

1. **매크로 기반**: 개발자가 수동으로 타입 정보 등록(많은 엔진들).
2. **코드 생성**: 헤더를 파싱하여 코드 자동 생성(Unreal, Qt).
3. **컴파일러 AST 활용**: Clang tooling 같은 파서로 등록 코드 자동 생성.

각각의 장단점을 차례로 본다.

## 3. RTTI의 한계

C++의 RTTI는 두 가지 기능만 제공한다.

```cpp
// 1) 타입 이름 얻기
const char* name = typeid(obj).name();  // "class ACharacter" (컴파일러마다 다름)

// 2) 다운캐스팅 검사
AActor* actor = GetActor();
ACharacter* character = dynamic_cast<ACharacter*>(actor);
if (character) { /* 성공 */ }
```

이것만으로는 게임 엔진의 요구사항을 만족시킬 수 없다.

**문제 1: 멤버 정보가 없다**
```cpp
typeid(ACharacter).GetProperties();  // 없음
typeid(ACharacter).GetMethods();     // 없음
```

**문제 2: 메타데이터가 없다**
```cpp
Property* health = ...;
health->GetMetadata("EditAnywhere");  // RTTI에는 메타데이터 개념이 없음
```

**문제 3: 선택성과 ABI**

표준은 RTTI의 물리 레이아웃을 정하지 않는다. 흔한 ABI에서는 다형 타입의 vtable과 타입 정보 객체가 연결되고, 이미 다형인 객체에 RTTI 때문에 별도 per-object 포인터가 추가되지는 않는다. 실제 비용은 타입 정보·이름·cast 지원 코드가 남기는 바이너리와 링크 결과, `dynamic_cast` 경로에 따라 달라진다. 빌드 플래그로 RTTI를 끄는 코드베이스도 있지만, 자체 메타데이터가 필요하다는 사실만으로 RTTI 비활성화가 항상 이득인 것은 아니다. 사용 중인 ABI와 링크 맵을 측정해야 한다.

```cpp
// 구현별 RTTI 비활성화 플래그를 사용하면 이런 RTTI 의존 표현이 제한된다.
typeid(polymorphicObj);
dynamic_cast<Derived*>(basePtr);
```

자체 리플렉션의 핵심 이점은 *선택성* 과 *도메인 정보* 다. 노출할 타입만 등록하고, property 범위·직렬화 버전·스크립트 권한 같은 메타데이터를 붙일 수 있다. 이 타입 테이블은 가상 함수 dispatch를 위한 vptr를 반드시 대체하지 않는다. 한 객체가 C++ 가상 함수용 vptr와 엔진의 타입 ID 또는 class descriptor 접근 경로를 함께 가질 수 있고, 관리형 런타임은 또 다른 객체 모델을 사용한다. 표준 RTTI, 엔진 타입 정보와 언어 런타임 메타데이터를 같은 물리 포인터 구조로 일반화하지 않는다.

## 4. 매크로 기반 리플렉션

가장 간단한 리플렉션은 매크로로 수동 등록하는 것이다. CryEngine, Source Engine, 많은 인디 엔진들이 이 방식을 사용한다.

### 기본 구조

```cpp
// 1) 프로퍼티 하나를 기술하는 정보
enum class PropertyType { Unknown, Bool, Int, Float };

class PropertyInfo {
public:
    const char* name;
    size_t offset;
    size_t size;
    PropertyType type;
    const char* description;
};

// 2) 타입 정보를 담을 클래스
class TypeInfo {
public:
    const char* name;
    TypeInfo* parent;
    std::vector<PropertyInfo*> properties;

    void AddProperty(const char* name, size_t offset, size_t size,
                     PropertyType type, const char* description);
    static TypeInfo* Find(const char* name);
};

// 3) 기본 클래스
class Object {
public:
    virtual TypeInfo* GetTypeInfo() const = 0;

    template<typename T>
    bool IsA() const {
        TypeInfo* type = GetTypeInfo();
        TypeInfo* target = T::StaticType();
        while (type) {
            if (type == target) return true;
            type = type->parent;
        }
        return false;
    }
};
```

### 매크로로 등록하기

```cpp
// 4) 헤더에서 선언
class Character : public Actor {
public:
    DECLARE_CLASS(Character, Actor)

    float Health = 100.0f;
    int Ammo = 30;
};

// DECLARE_CLASS 매크로 정의
#define DECLARE_CLASS(ThisClass, ParentClass) \
public: \
    static TypeInfo* StaticType() { \
        static TypeInfo type(#ThisClass, ParentClass::StaticType()); \
        return &type; \
    } \
    virtual TypeInfo* GetTypeInfo() const override { \
        return StaticType(); \
    }
```

```cpp
// 5) cpp에서 프로퍼티 등록
IMPLEMENT_CLASS(Character) {
    REGISTER_PROPERTY(Character, Health, "Health displayed in editor");
    REGISTER_PROPERTY(Character, Ammo, "Current ammo count");
}

// 매크로 정의
#define IMPLEMENT_CLASS(ClassName) \
    struct ClassName##_Registration { \
        ClassName##_Registration(); \
    }; \
    static ClassName##_Registration s_##ClassName##_Registration; \
    ClassName##_Registration::ClassName##_Registration()

// 멤버 타입 → PropertyType 매핑
template<typename T> constexpr PropertyType PropertyTypeOf() { return PropertyType::Unknown; }
template<> constexpr PropertyType PropertyTypeOf<bool>()  { return PropertyType::Bool; }
template<> constexpr PropertyType PropertyTypeOf<int>()   { return PropertyType::Int; }
template<> constexpr PropertyType PropertyTypeOf<float>() { return PropertyType::Float; }

#define REGISTER_PROPERTY(Class, Member, Description) \
    Class::StaticType()->AddProperty( \
        #Member, \
        offsetof(Class, Member), \
        sizeof(Class::Member), \
        PropertyTypeOf<decltype(Class::Member)>(), \
        Description \
    )
```

### 사용 예제

```cpp
// 에디터에서 모든 프로퍼티 표시
void Editor::ShowProperties(Object* obj) {
    TypeInfo* type = obj->GetTypeInfo();

    for (PropertyInfo* prop : type->properties) {
        void* dataPtr = (char*)obj + prop->offset;

        if (prop->type == PropertyType::Float) {
            float* value = (float*)dataPtr;
            ImGui::SliderFloat(prop->name, value, 0.0f, 100.0f);
        }
        else if (prop->type == PropertyType::Int) {
            int* value = (int*)dataPtr;
            ImGui::InputInt(prop->name, value);
        }
    }
}

// 직렬화
void Serialize(Object* obj, BinaryWriter& writer) {
    TypeInfo* type = obj->GetTypeInfo();
    writer.Write(type->name);

    for (PropertyInfo* prop : type->properties) {
        void* dataPtr = (char*)obj + prop->offset;
        writer.WriteBytes(dataPtr, prop->size);
    }
}
```

### 장점과 단점

장점:
- 구현이 간단하다.
- 외부 도구가 필요 없다.
- 컴파일러 독립적이다.
- 정확히 필요한 것만 등록할 수 있다.

단점:
- 모든 프로퍼티를 수동으로 등록해야 한다.
- 타이핑 실수가 발생하기 쉽다(`"Health"` vs `Health`).
- 멤버 추가 시 두 곳을 수정해야 한다(헤더와 등록 코드).
- 메서드 호출은 더 복잡하다(함수 포인터 필요).

많은 중소형 엔진들이 이 방식으로 시작하지만, 프로젝트가 커지면 유지보수가 힘들어진다. 100개의 클래스에 각각 10개의 프로퍼티가 있다면, 1,000개의 등록 코드를 작성하고 관리해야 한다.

## 5. 코드 생성: Unreal의 UHT

Unreal Engine은 다른 접근을 택했다. 헤더 파일을 파싱하여 등록 코드를 자동 생성한다.

### Unreal Header Tool (UHT)

UHT는 빌드 프로세스에 통합된 코드 생성기다.

```mermaid
flowchart LR
    A[헤더 파일] --> B[UnrealBuildTool 스캔]
    B --> C[UCLASS, UPROPERTY 매크로 검출]
    C --> D[UHT 실행]
    D --> E[.generated.h 파일 생성]
    E --> F[C++ 컴파일러가 포함하여 컴파일]
```

### 개발자가 작성하는 코드

```cpp
// Character.h
#include "Character.generated.h"

UCLASS(BlueprintType)
class ACharacter : public AActor {
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats")
    float Health = 100.0f;

    UPROPERTY(EditAnywhere, Category="Stats", meta=(ClampMin=0, ClampMax=100))
    int Ammo = 30;

    UFUNCTION(BlueprintCallable, Category="Combat")
    void TakeDamage(float Amount);
};
```

### UHT가 생성하는 코드

```cpp
// Character.generated.h
#define CURRENT_FILE_ID MyProject_Source_Character_h

#define MyProject_Source_Character_h_12_GENERATED_BODY \
public: \
    static UClass* StaticClass() { \
        static UClass* Class = nullptr; \
        if (!Class) { \
            Class = ConstructUClass(...); \
        } \
        return Class; \
    } \
    virtual UClass* GetClass() const override { return StaticClass(); }
```

```cpp
// Character.gen.cpp (생성되는 cpp 파일)
void ACharacter::StaticRegisterNatives() {
    UClass* Class = ACharacter::StaticClass();

    // 프로퍼티 등록
    new (Class->Properties) FFloatProperty(
        TEXT("Health"),
        offsetof(ACharacter, Health),
        EPropertyFlags::CPF_Edit | CPF_BlueprintVisible
    );

    FIntProperty* AmmoProp = new (Class->Properties) FIntProperty(
        TEXT("Ammo"),
        offsetof(ACharacter, Ammo),
        EPropertyFlags::CPF_Edit
    );

    // 메타데이터 등록 (meta는 클래스가 아니라 해당 프로퍼티에 저장된다)
    AmmoProp->SetMetaData(TEXT("ClampMin"), TEXT("0"));
    AmmoProp->SetMetaData(TEXT("ClampMax"), TEXT("100"));

    // 함수 등록
    new (Class->Functions) UFunction(
        TEXT("TakeDamage"),
        &ACharacter::execTakeDamage,
        EFunctionFlags::FUNC_BlueprintCallable
    );
}

// Thunk 함수: Blueprint VM -> C++ 브리지
DEFINE_FUNCTION(ACharacter::execTakeDamage) {
    P_GET_PROPERTY(FFloatProperty, Amount);
    P_FINISH;

    P_THIS->TakeDamage(Amount);
}
```

### UHT의 작동 방식

UHT는 완전한 C++ 파서가 아니다. 손으로 작성한 토크나이저와 제한적인 재귀 하강 파서로 UCLASS/UPROPERTY 같은 매크로 선언만 인식한다.

```cpp
// UHT가 하는 일을 설명하기 위한 개념용 의사코드 (실제 UHT는 정규표현식을 쓰지 않는다)
void ParseHeader(const string& content) {
    regex uclassPattern(R"(UCLASS\((.*?)\)\s*class\s+(\w+))");
    for (auto match : FindMatches(content, uclassPattern)) {
        string className = match[2];
        string meta = match[1];
        GenerateClassCode(className, meta);
    }

    regex upropPattern(R"(UPROPERTY\((.*?)\)\s*(\w+)\s+(\w+))");
    for (auto match : FindMatches(content, upropPattern)) {
        string type = match[2];
        string name = match[3];
        string meta = match[1];
        GeneratePropertyCode(name, type, meta);
    }
}
```

이 방식의 한계:
- 템플릿 복잡도에 약하다.
- 조건부 컴파일을 완전히 이해하지 못한다.
- 때로 주석 안의 매크로도 파싱한다.

그러나 실용적이다. 99%의 경우에 작동하며, 개발자가 명시적으로 매크로를 작성하므로 의도가 명확하다.

### 장점과 단점

장점:
- 등록 코드를 자동 생성한다.
- 메타데이터가 풍부하다(EditAnywhere, BlueprintCallable 등).
- 헤더만 보면 전체 정보를 알 수 있다.
- 타이핑 실수가 불가능하다(컴파일러가 검증).

단점:
- 빌드 프로세스가 복잡해진다.
- UHT가 실패하면 에러 메시지가 불친절하다.
- 매크로 문법에 제약이 있다(GENERATED_BODY 위치 등).
- 코드 생성기 자체를 유지보수해야 한다.

Unreal이 이 복잡도를 감수하는 이유는 규모 때문이다. AAA 게임은 수천 개의 클래스와 수만 개의 프로퍼티를 가진다. 수동 등록은 불가능하다.

## 6. 컴파일러 확장: libclang 활용

가장 정교한 접근은 컴파일러의 AST를 직접 사용하는 것이다. clang 기반 코드 생성 도구들이 이 방식으로 등록 코드를 자동 생성한다. 한편 RTTR, metapp 같은 라이브러리는 표준 C++ 템플릿/매크로 기반 수동 등록을 택하되, libclang 코드젠과 결합해 등록을 자동화할 수 있다.

### libclang이란

libclang은 Clang 컴파일러의 프론트엔드를 라이브러리로 제공한다. C++ 코드를 완전히 파싱하여 AST(Abstract Syntax Tree)를 제공한다.

```cpp
// libclang으로 헤더 파싱
#include <clang-c/Index.h>

void ParseHeader(const char* filename) {
    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit unit = clang_parseTranslationUnit(
        index, filename, nullptr, 0, nullptr, 0, CXTranslationUnit_None
    );

    CXCursor cursor = clang_getTranslationUnitCursor(unit);
    clang_visitChildren(cursor, VisitorFunc, nullptr);
}

CXChildVisitResult VisitorFunc(CXCursor cursor, CXCursor parent, CXClientData data) {
    CXCursorKind kind = clang_getCursorKind(cursor);

    if (kind == CXCursor_ClassDecl) {
        CXString name = clang_getCursorSpelling(cursor);
        printf("Found class: %s\n", clang_getCString(name));

        clang_visitChildren(cursor, [](CXCursor c, CXCursor p, CXClientData d) {
            if (clang_getCursorKind(c) == CXCursor_FieldDecl) {
                // 멤버 변수 발견
            }
            return CXChildVisit_Continue;
        }, nullptr);
    }

    return CXChildVisit_Recurse;
}
```

### RTTR (Run Time Type Reflection)

RTTR은 매크로/템플릿 기반 수동 등록을 사용하는 오픈소스 런타임 리플렉션 라이브러리다(libclang 기반이 아니다). 등록 코드는 직접 작성하지만, 표준 C++만 사용해 다양한 컴파일러에서 동작한다.

```cpp
#include <rttr/registration.h>

RTTR_REGISTRATION {
    rttr::registration::class_<Character>("Character")
        .constructor<>()
        .property("Health", &Character::Health)
            (rttr::metadata("min", 0.0f), rttr::metadata("max", 100.0f))
        .property("Ammo", &Character::Ammo)
        .method("TakeDamage", &Character::TakeDamage);
}

// 사용
rttr::type t = rttr::type::get<Character>();
for (auto& prop : t.get_properties()) {
    std::cout << "Property: " << prop.get_name() << "\n";

    auto minVal = prop.get_metadata("min");
    if (minVal.is_valid()) {
        std::cout << "  Min: " << minVal.get_value<float>() << "\n";
    }
}

// 이름으로 메서드 호출
rttr::method m = t.get_method("TakeDamage");
Character character;
m.invoke(character, 25.0f);
```

RTTR은 완전한 타입 시스템을 제공한다.

```cpp
// 배열과 포인터 처리
rttr::type arrayType = rttr::type::get<std::vector<int>>();
bool isSeq = arrayType.is_sequential_container();  // true

// 템플릿 인자 추출
rttr::type elemType = *arrayType.get_template_arguments().begin();  // int — array_range에는 첨자 연산자가 없다

// 열거형
enum class State { Idle, Running };
RTTR_REGISTRATION {
    rttr::registration::enumeration<State>("State")
        (
            rttr::value("Idle", State::Idle),
            rttr::value("Running", State::Running)
        );
}
```

### RTTR의 장점과 단점

장점:
- 표준 C++만 사용해 다양한 컴파일러에서 동작.
- 메타데이터, 정책, 변환기 등 풍부한 기능.
- IDE 자동완성과 리팩토링이 잘 작동.
- 타입 안전성 보장.

단점:
- 등록 코드는 수동 작성(자동 생성하려면 코드젠 도구 필요).
- 학습 곡선이 가파르다.
- 실행 파일 크기가 크다(RTTR 자체가 무겁다).
- 런타임 lookup 비용이 매크로 기반 자체 시스템보다 무겁다.

이 방식은 고급 사용자를 위한 것이다. Unreal처럼 자체 생태계를 가진 대형 엔진보다는, 범용 라이브러리로 사용하기 적합하다.

## 7. C++26 static reflection

C++26 작업 초안에는 P2996 계열의 컴파일 타임 리플렉션이 들어갔다. 여기서 “채택”은 기능이 작업 초안에 통합됐다는 뜻이며, 모든 컴파일러의 출시 버전에서 즉시 사용할 수 있다는 뜻은 아니다.

### P2996: Reflection for C++26

P2996R13은 `^^` 연산자(reflection 표현식), `std::meta::info`, 메타함수와 splice 문법을 정의한다. 과거 제안의 `reflexpr` 표기와는 다르다. 다음은 제안서의 형태를 축약한 예이며, 실제 사용 가능 여부는 컴파일러의 C++26 reflection 구현 상태를 확인해야 한다.

```cpp
#include <meta>
#include <iostream>

struct Character {
    float Health = 100.0f;
    int Ammo = 30;
    void TakeDamage(float amount);
};

// 컴파일 타임에 비정적 데이터 멤버 순회
template <typename T>
void PrintMembers() {
    constexpr auto ctx = std::meta::access_context::current();
    template for (constexpr auto m : std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, ctx))) {
        std::cout << std::meta::identifier_of(m) << "\n";
    }
}

// 컴파일 타임에 멤버 메타정보로 직렬화 코드 생성
template <typename T>
void Serialize(const T& obj, Writer& w) {
    constexpr auto ctx = std::meta::access_context::current();
    template for (constexpr auto m : std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, ctx))) {
        w.Write(std::meta::identifier_of(m), obj.[:m:]);
    }
}
```

핵심 변화는 두 가지다.

1. `^^expr`가 선언이나 타입의 reflection 값을 만들고, reflection 값은 `std::meta::info`로 표현된다.
2. `template for` expansion statement가 컴파일 타임 목록을 코드로 펼치며, `[:m:]` splice가 reflection이 가리키는 엔티티를 문법 위치에 삽입한다.

멤버 접근은 access context와 언어 접근 제어의 영향을 받고, 직렬화에서 어떤 멤버를 포함할지는 annotation이나 별도 opt-in 정책이 필요하다. “모든 필드를 자동 저장”하면 캐시, transient 상태, 포인터와 버전 경계를 잘못 직렬화할 수 있다.

### 컴파일 타임 vs 런타임

중요한 점은 reflection query와 expansion이 컴파일 타임에 실행된다는 것이다. 실행 중 임의의 문자열을 `^^` 연산자에 넣어 새 reflection을 만들 수는 없다. 대신 컴파일 타임에 선택한 멤버로 런타임 테이블과 접근 함수를 생성할 수 있다.

```cpp
// 구조를 보여 주는 축약 코드다. MakeAccessor는 reflection m을 캡처한
// type-erased getter/setter를 생성한다고 가정한다.
template <typename T>
RuntimeType BuildRuntimeType() {
    RuntimeType out{std::meta::identifier_of(^^T)};
    constexpr auto ctx = std::meta::access_context::current();

    template for (constexpr auto m : std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, ctx))) {
        if constexpr (IsRuntimeExposed(m)) {
            out.properties.push_back(PropertyInfo{
                std::meta::identifier_of(m),
                MakeAccessor<T, m>()
            });
        }
    }
    return out;
}
```

에디터의 이름 기반 property lookup, 스크립트 호출과 저장 파일의 type ID 해석은 이렇게 생성한 런타임 테이블이 담당한다. static reflection은 테이블의 원천을 C++ 선언과 연결해 중복 등록을 줄이지만, 이름 충돌, stable ID, 버전 마이그레이션, 접근 권한과 어떤 멤버를 노출할지는 엔진의 런타임 계약으로 남는다.

### 표준화 현황

- C++23에는 reflection이 포함되지 않았다.
- 2025년 6월 Sofia 회의에서 P2996R13과 관련 기능들이 C++26 작업 초안에 채택되었다. 최종 국제 표준 발행과 각 컴파일러의 완전한 구현·ABI 지원은 별개의 일정이다.

표준에 들어갔어도, 게임 엔진이 즉시 채택하기는 어렵다.

1. 프로젝트가 지원하는 모든 컴파일러와 플랫폼의 구현 성숙도 확인.
2. 기존 시스템과의 호환성(Unreal은 이미 UHT 생태계가 확립됨).
3. 표준 annotation(`[[=expr]]`, P3394)이 같은 회의에서 채택됐지만, 컴파일러의 구현 성숙도와 기존 엔진 메타데이터 문법에서의 이행 비용이 남는다.

표준 기능이 툴체인의 일부 매크로와 외부 파서 의존을 줄일 가능성은 크다. 다만 기존 엔진의 annotation 문법, schema 안정성, hot reload와 이전 데이터 호환성은 언어 reflection만으로 사라지지 않는다.

## 8. 실전 적용: 에디터, 스크립팅, 직렬화

리플렉션 시스템을 구축하는 목적은 세 가지 핵심 기능을 구현하기 위해서다.

### 1) 에디터: 자동 UI 생성

```cpp
// 리플렉션 정보로 ImGui UI 자동 생성
void PropertyEditor::Show(Object* obj) {
    Class* type = obj->GetClass();

    for (Property* prop : type->GetProperties()) {
        const char* category = prop->GetMetadata("Category");
        if (!currentCategory || std::strcmp(category, currentCategory) != 0) {   // 포인터가 아니라 내용을 비교한다
            ImGui::SeparatorText(category);
            currentCategory = category;
        }

        void* data = prop->GetValuePtr(obj);

        // 위젯을 그리기 전에 EditCondition을 평가해야 현재 프로퍼티에 적용된다
        bool disabled = false;
        if (prop->HasMetadata("EditCondition")) {
            const char* condition = prop->GetMetadata("EditCondition");
            Property* condProp = type->FindProperty(condition);
            bool* enabled = (bool*)condProp->GetValuePtr(obj);

            if (!*enabled) {
                ImGui::BeginDisabled();
                disabled = true;
            }
        }

        if (prop->IsA<FloatProperty>()) {
            float* value = (float*)data;
            float min = prop->GetMetadataFloat("ClampMin", 0.0f);
            float max = prop->GetMetadataFloat("ClampMax", 100.0f);

            if (prop->HasMetadata("ClampMin")) {
                ImGui::SliderFloat(prop->GetName(), value, min, max);
            } else {
                ImGui::DragFloat(prop->GetName(), value);
            }
        }
        else if (prop->IsA<IntProperty>()) {
            int* value = (int*)data;
            ImGui::InputInt(prop->GetName(), value);
        }
        else if (prop->IsA<BoolProperty>()) {
            bool* value = (bool*)data;
            ImGui::Checkbox(prop->GetName(), value);
        }
        else if (prop->IsA<EnumProperty>()) {
            EnumProperty* enumProp = (EnumProperty*)prop;
            int* value = (int*)data;

            if (ImGui::BeginCombo(prop->GetName(),
                                  enumProp->GetValueName(enumProp->FindIndexByValue(*value)))) {
                for (int i = 0; i < enumProp->GetValueCount(); i++) {
                    if (ImGui::Selectable(enumProp->GetValueName(i))) {
                        *value = enumProp->GetValue(i);
                    }
                }
                ImGui::EndCombo();
            }
        }
        else if (prop->IsA<ObjectProperty>()) {
            Object** value = (Object**)data;

            if (ImGui::Button("Select...")) {
                OpenAssetPicker(value, prop->GetObjectClass());
            }
            ImGui::SameLine();
            ImGui::Text(*value ? (*value)->GetName() : "None");
        }

        if (disabled) {
            ImGui::EndDisabled();
        }
    }
}
```

이 코드는 모든 클래스에 대해 작동한다. 새로운 클래스를 추가해도 에디터 코드를 수정할 필요가 없다. 리플렉션 정보만 있으면 UI가 자동 생성된다.

### 2) 스크립팅: C++와 스크립트 바인딩

```cpp
// Lua 바인딩 자동 생성
void ScriptEngine::RegisterClass(Class* type) {
    lua_State* L = state;

    luaL_newmetatable(L, type->GetName());

    for (Method* method : type->GetMethods()) {
        lua_pushstring(L, method->GetName());
        lua_pushlightuserdata(L, method);
        lua_pushcclosure(L, &MethodThunk, 1);
        lua_settable(L, -3);
    }

    lua_pushstring(L, "__index");
    lua_pushcfunction(L, &PropertyGetter);
    lua_settable(L, -3);

    lua_pushstring(L, "__newindex");
    lua_pushcfunction(L, &PropertySetter);
    lua_settable(L, -3);
}

// Lua -> C++ 브리지
int ScriptEngine::MethodThunk(lua_State* L) {
    Method* method = (Method*)lua_touserdata(L, lua_upvalueindex(1));
    Object* obj = (Object*)lua_touserdata(L, 1);

    std::vector<Variant> args;
    for (int i = 0; i < method->GetParamCount(); i++) {
        Parameter* param = method->GetParameter(i);

        if (param->GetType()->IsA<FloatType>()) {
            args.push_back(Variant((float)lua_tonumber(L, i + 2)));
        }
        else if (param->GetType()->IsA<IntType>()) {
            args.push_back(Variant((int)lua_tointeger(L, i + 2)));
        }
    }

    Variant result = method->Invoke(obj, args.data(), args.size());

    if (result.IsValid()) {
        if (result.IsFloat()) {
            lua_pushnumber(L, result.GetFloat());
            return 1;
        }
    }

    return 0;
}
```

스크립트에서 사용:

```lua
-- Lua 스크립트
local character = GetPlayer()
print(character.Health)  -- PropertyGetter 호출
character.Health = 50    -- PropertySetter 호출
character:TakeDamage(25) -- MethodThunk 호출
```

리플렉션이 없다면 모든 클래스에 대해 수동으로 바인딩 코드를 작성해야 한다. 리플렉션이 있으면 한 번의 RegisterClass 호출로 끝난다.

### 3) 직렬화: 객체 저장과 로딩

```cpp
// JSON 직렬화 (자동)
void Serializer::SaveToJson(Object* obj, JsonWriter& json) {
    Class* type = obj->GetClass();

    json.WriteStartObject();
    json.WriteProperty("$type", type->GetName());

    for (Property* prop : type->GetProperties()) {
        if (prop->HasFlag(PropertyFlags::Transient)) {
            continue;
        }

        json.WritePropertyName(prop->GetName());

        if (prop->IsA<FloatProperty>()) {
            float value = prop->GetValue<float>(obj);
            json.WriteFloat(value);
        }
        else if (prop->IsA<IntProperty>()) {
            json.WriteInt(prop->GetValue<int>(obj));
        }
        else if (prop->IsA<BoolProperty>()) {
            json.WriteBool(prop->GetValue<bool>(obj));
        }
        else if (prop->IsA<ArrayProperty>()) {
            ArrayProperty* arrayProp = (ArrayProperty*)prop;
            void* arrayData = prop->GetValuePtr(obj);
            int count = arrayProp->GetCount(arrayData);

            json.WriteStartArray();
            for (int i = 0; i < count; i++) {
                void* elem = arrayProp->GetElement(arrayData, i);
                WriteValue(json, arrayProp->GetInnerType(), elem);
            }
            json.WriteEndArray();
        }
        else if (prop->IsA<ObjectProperty>()) {
            Object* value = prop->GetValue<Object*>(obj);
            if (value) {
                SaveToJson(value, json);
            } else {
                json.WriteNull();
            }
        }
    }

    json.WriteEndObject();
}

// JSON 역직렬화 (자동)
Object* Serializer::LoadFromJson(JsonReader& json) {
    json.ReadStartObject();

    const char* typeName = json.ReadProperty("$type").GetString();
    Class* type = Class::Find(typeName);
    if (!type) {
        Error("Unknown type: %s", typeName);
        return nullptr;
    }

    Object* obj = type->CreateInstance();

    while (json.HasMoreProperties()) {
        const char* propName = json.ReadPropertyName();
        Property* prop = type->FindProperty(propName);

        if (!prop) {
            json.SkipValue();
            continue;
        }

        if (prop->IsA<FloatProperty>()) {
            float value = json.ReadFloat();
            prop->SetValue(obj, value);
        }
    }

    json.ReadEndObject();
    return obj;
}
```

저장된 JSON:

```json
{
  "$type": "Character",
  "Health": 75.5,
  "Ammo": 20,
  "Position": {"x": 10, "y": 0, "z": 5},
  "Inventory": [
    {"$type": "Weapon", "Damage": 25},
    {"$type": "HealthPack", "HealAmount": 50}
  ]
}
```

리플렉션의 진가는 버전 호환성에서 드러난다.

```cpp
// 구버전 데이터 로딩
Object* obj = LoadFromJson(oldFile);
// - 새로운 프로퍼티는 기본값 사용
// - 제거된 프로퍼티는 무시 (SkipValue)
// - 이름이 바뀐 프로퍼티는 메타데이터로 처리

Property* newProp = type->FindProperty("CurrentHealth");
if (newProp->HasMetadata("OldName")) {
    const char* oldName = newProp->GetMetadata("OldName");
    // "Health"를 "CurrentHealth"로 매핑
}
```


---

## 결론

리플렉션은 에디터, 스크립트 바인딩, 일반 직렬화와 네트워크 schema를 하나의 타입 정의에 연결하는 강력한 기반이다. 작은 시스템은 명시적 serializer와 binding 함수로도 충분하지만, 노출 타입이 많아질수록 선택적 메타데이터와 코드 생성이 중복과 drift를 줄인다.

세 가지 접근법:

1. **매크로 기반**: 간단하지만 수동 작업이 많다. 소규모 프로젝트에 적합.
2. **코드 생성**: 별도 파서나 헤더 도구가 등록 코드를 만든다. 규모가 커져도 일관된 정책을 적용할 수 있지만 빌드 그래프와 파서 호환성을 소유해야 한다.
3. **컴파일러 AST 활용**: Clang tooling 같은 실제 C++ parser를 사용한다. 언어 해석은 정확하지만 compiler version, build flags와 tool integration 비용이 따른다.

표준 RTTI는 동적 타입 식별과 안전한 cast를 제공하지만 멤버 열거와 도메인 메타데이터는 제공하지 않는다. 엔진 타입 테이블은 RTTI와 병존하거나 이를 사용하지 않을 수 있으며, 가상 dispatch의 vptr와 리플렉션 descriptor를 동일한 구조로 가정해서는 안 된다.

C++26 작업 초안의 static reflection은 선언에서 serializer와 런타임 타입 테이블을 생성할 표준 기반을 제공한다. 하지만 stable ID, 저장 데이터 마이그레이션, hot reload, 접근 정책과 런타임 이름 조회는 여전히 엔진이 설계해야 한다. 도입 판단은 표준의 존재가 아니라 지원 compiler matrix와 기존 schema의 전환 비용으로 내린다.

리플렉션은 "프로그램이 자신을 본다"는 것 이상이다. 프로그램이 자신을 이해하고, 설명하고, 수정할 수 있게 만드는 것이다. 이것이 게임 엔진을 단순한 라이브러리가 아니라 살아있는 개발 환경으로 만드는 핵심이다.

## 더 읽을거리

- [Unreal Object Handling](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-object-handling-in-unreal-engine) - UObject 시스템 개요
- [Unreal Header Tool 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-header-tool-for-unreal-engine) - UHT의 작동 방식
- [Qt Meta-Object System](https://doc.qt.io/qt-6/metaobjects.html) - moc의 역사와 설계
- [P2996: Reflection for C++26](https://wg21.link/P2996) - static reflection 제안
- [RTTR (Run Time Type Reflection)](https://www.rttr.org/) - 템플릿/매크로 등록 기반 런타임 리플렉션
- [Boost.Describe](https://www.boost.org/doc/libs/release/libs/describe/) - 매크로 기반 간단한 리플렉션
- [metapp](https://github.com/wqking/metapp) - 경량 리플렉션 라이브러리
- Tim Sweeney, "Core Technology of Unreal Engine 3", GDC 2005 - UObject 시스템 설계 철학
- Chris Butcher, "Lessons from the Core Engine Architecture of Destiny", GDC 2015 - Bungie 엔진의 오브젝트 시스템 설계
- Jason Gregory, "Game Engine Architecture", 3rd Edition - Chapter 16: Runtime Gameplay Foundation Systems
- Martin Reddy, "API Design for C++" - Chapter 11: Scripting
