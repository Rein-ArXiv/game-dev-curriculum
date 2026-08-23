#pragma once

// 문법 유도 fuzz 배터리의 생성기·변형기·축소기와 실패 어휘.
//
// 용어는 이론 축의 property/fuzz 편을 그대로 따른다.
//   property  생성된 모든 입력이 지켜야 하는 관계. 고정 기대값이 아니다.
//   oracle    그 관계를 판정하는 근거. 여기서는 정답표가 없으므로 부분 oracle을 쓴다 —
//             자기 자신과의 비교(두 번 컴파일·두 번 실행)와 닫힌 실패 어휘다.
//   shrink    실패를 유지한 채 입력을 줄여 사람이 읽을 반례로 만드는 것.
//   fuzz      무작위 생성과 변형으로 그 property를 깨려는 탐색.
//
// 외부 의존은 없다. 난수는 전부 고정 시드에서 나오고 벽시계나 프로세스 시간을 읽지 않는다.
// 그래야 실패가 시드 하나로 재현된다.

#include "language.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lab::lang::fuzz {

// ── 결정적 난수 ──────────────────────────────────────────────────────────────
// snippet-begin: fuzz-rng
class Rng {
public:
    explicit Rng(uint64_t seed) : state_(seed) {}

    // splitmix64. 표준 라이브러리 엔진은 같은 시드에서도 구현마다 다른 수열을 낼 수 있어
    // 쓰지 않는다. 반례 재현이 플랫폼에 의존하면 시드를 적는 의미가 없다.
    uint64_t Next() {
        state_ += 0x9E3779B97F4A7C15ull;
        uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // 나머지 연산의 편향은 bound가 2^64에 비해 극히 작아 무시한다. 편향이 있어도
    // 결정적이므로 재현성에는 영향이 없다.
    uint32_t Below(uint32_t bound) {
        return bound == 0 ? 0u : static_cast<uint32_t>(Next() % bound);
    }
    int Between(int low, int high) {
        return low + static_cast<int>(Below(static_cast<uint32_t>(high - low + 1)));
    }
    bool Chance(uint32_t percent) { return Below(100) < percent; }

private:
    uint64_t state_;
};

// 표본 i의 시드. master를 그냥 증가시키면 인접 표본이 상관되어 같은 모양이 반복된다.
inline uint64_t SampleSeed(uint64_t master, uint64_t index) {
    uint64_t z = master + 0x9E3779B97F4A7C15ull * (index + 1);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
// snippet-end: fuzz-rng

// ── 토큰열과 렌더링 ─────────────────────────────────────────────────────────
using TokenList = std::vector<std::string>;

// 토큰을 소스로 되돌린다. `;` `{` `}` 뒤에서 줄을 바꾸므로 생성된 소스는 여러 줄이 된다 —
// 진단 위치가 상수 `{1, 1}`로 고정돼 있어도 통과하는 일이 없게 하려는 것이다.
inline std::string Render(const TokenList& tokens) {
    std::string out;
    bool lineStart = true;
    for (const std::string& token : tokens) {
        if (token.empty()) continue;
        if (!lineStart) out.push_back(' ');
        out += token;
        lineStart = false;
        if (token == ";" || token == "{" || token == "}") {
            out.push_back('\n');
            lineStart = true;
        }
    }
    if (!lineStart) out.push_back('\n');
    return out;
}

// 진단 위치가 실제 소스 안을 가리키는가. 줄 수와 그 줄의 길이를 모두 본다.
inline bool LocationWithinSource(const std::string& source, SourceLocation location) {
    if (location.line < 1 || location.column < 1) return false;
    int line = 1;
    int column = 1;
    int lineLength = 0;
    // 해당 줄의 길이를 찾는다. 줄 끝 다음 열(길이 + 1)까지 허용한다 — End 토큰의 위치다.
    for (size_t i = 0; i <= source.size(); ++i) {
        const bool atEnd = i == source.size();
        if (atEnd || source[i] == '\n') {
            if (line == location.line) {
                lineLength = column - 1;
                return location.column <= lineLength + 1;
            }
            ++line;
            column = 1;
            if (atEnd) break;
            continue;
        }
        ++column;
    }
    return false;
}

inline int LineCount(const std::string& source) {
    int lines = 1;
    for (const char c : source)
        if (c == '\n') ++lines;
    return lines;
}

// ── 문법 유도 생성기 ────────────────────────────────────────────────────────
struct GeneratorLimits {
    int maxStatements = 6;
    int maxExpressionDepth = 3;
    int maxBlockDepth = 2;
};

struct Sample {
    TokenList tokens;
    bool hasLoop = false;
    bool hasUnboundedLoop = false;
    bool hasNativeCall = false;
    bool hasUnregisteredNative = false;
    bool hasUndeclaredRead = false;
    bool hasNestedBlock = false;
    bool hasLiteralZeroDivisor = false;
};

// 이 lab의 문법만 만든다. 무효 입력은 별도 생성기(변형기)가 맡는다 — 한 생성기가
// 유효·무효를 섞으면 어떤 계약을 검사하는지가 흐려진다.
// snippet-begin: fuzz-generator
class ProgramGenerator {
public:
    ProgramGenerator(Rng& rng, GeneratorLimits limits) : rng_(rng), limits_(limits) {}

    Sample Generate() {
        sample_ = Sample{};
        declared_.clear();
        nameCounter_ = 0;

        TokenList out;
        // 첫 선언은 항상 둔다. 이후 문장이 참조할 초기화된 전역이 최소 하나 있어야
        // "초기화되지 않은 변수"가 생성기의 사고가 아니라 의도한 버킷이 된다.
        out.push_back("let");
        out.push_back(DeclareName());
        out.push_back("=");
        out.push_back(NumberLiteral());
        out.push_back(";");

        const int count = rng_.Between(1, limits_.maxStatements);
        for (int i = 0; i < count; ++i) Statement(out, 0);

        sample_.tokens = std::move(out);
        return sample_;
    }

private:
    void Statement(TokenList& out, int blockDepth) {
        const uint32_t roll = rng_.Below(100);
        const bool canNest = blockDepth < limits_.maxBlockDepth;
        if (roll < 26) {
            LetStatement(out);
        } else if (roll < 48) {
            AssignStatement(out);
        } else if (roll < 64 || !canNest) {
            ExpressionStatement(out);
        } else if (roll < 82) {
            IfStatement(out, blockDepth);
        } else {
            WhileStatement(out, blockDepth);
        }
    }

    void LetStatement(TokenList& out) {
        out.push_back("let");
        out.push_back(DeclareName());
        out.push_back("=");
        Expression(out, 0);
        out.push_back(";");
    }

    void AssignStatement(TokenList& out) {
        out.push_back(ExistingName());
        out.push_back("=");
        Expression(out, 0);
        out.push_back(";");
    }

    void ExpressionStatement(TokenList& out) {
        Expression(out, 0);
        out.push_back(";");
    }

    void Block(TokenList& out, int blockDepth) {
        out.push_back("{");
        const int count = rng_.Between(0, 2);
        for (int i = 0; i < count; ++i) Statement(out, blockDepth + 1);
        out.push_back("}");
        if (blockDepth >= 1) sample_.hasNestedBlock = true;
    }

    void IfStatement(TokenList& out, int blockDepth) {
        out.push_back("if");
        out.push_back("(");
        Expression(out, 1);
        out.push_back(")");
        Block(out, blockDepth);
        if (rng_.Chance(45)) {
            out.push_back("else");
            Block(out, blockDepth);
        }
    }

    // 루프는 세 형태로 나뉜다. 감소하는 루프는 정상 종료하고, 감소가 없는 루프는 명령
    // 예산이, 제곱 루프는 유한성 가드가 멈춘다. 셋 다 "멈춘다"는 같은 property를 받지만
    // 멈추는 방어가 서로 다르므로 한 형태만 생성하면 나머지 방어는 검사되지 않는다.
    void WhileStatement(TokenList& out, int blockDepth) {
        const std::string counter = DeclareName();
        out.push_back("let");
        out.push_back(counter);
        out.push_back("=");
        out.push_back(std::to_string(rng_.Between(2, 6)));
        out.push_back(";");

        out.push_back("while");
        out.push_back("(");
        out.push_back(counter);
        out.push_back(">");
        out.push_back("0");
        out.push_back(")");
        out.push_back("{");
        const int bodyDepth =
            blockDepth + 1 < limits_.maxBlockDepth ? blockDepth + 1 : limits_.maxBlockDepth;
        const int bodyStatements = rng_.Between(0, 2);
        for (int i = 0; i < bodyStatements; ++i) Statement(out, bodyDepth);
        if (blockDepth >= 1) sample_.hasNestedBlock = true;

        const uint32_t form = rng_.Below(100);
        if (form < 8) {
            sample_.hasUnboundedLoop = true;  // 감소 없음: 예산이 멈춘다.
        } else if (form < 16) {
            out.push_back(counter);
            out.push_back("=");
            out.push_back(counter);
            out.push_back("*");
            out.push_back(counter);
            out.push_back(";");
            sample_.hasUnboundedLoop = true;  // 제곱: 유한성 가드가 멈춘다.
        } else {
            out.push_back(counter);
            out.push_back("=");
            out.push_back(counter);
            out.push_back("-");
            out.push_back("1");
            out.push_back(";");
        }
        out.push_back("}");
        sample_.hasLoop = true;
    }

    void Expression(TokenList& out, int depth) {
        if (depth >= limits_.maxExpressionDepth) {
            Primary(out, depth);
            return;
        }
        const uint32_t roll = rng_.Below(100);
        if (roll < 45) {
            Expression(out, depth + 1);
            const std::string op = BinaryOperator();
            out.push_back(op);
            if (op == "/" && rng_.Chance(12)) {
                out.push_back("0");  // 0 나눗셈 가드로 가는 경로.
                sample_.hasLiteralZeroDivisor = true;
            } else {
                Expression(out, depth + 1);
            }
        } else if (roll < 60) {
            out.push_back("-");
            Expression(out, depth + 1);
        } else if (roll < 75) {
            out.push_back("(");
            Expression(out, depth + 1);
            out.push_back(")");
        } else {
            Primary(out, depth);
        }
    }

    void Primary(TokenList& out, int depth) {
        // `unset`과 `ghost`의 비중은 낮게 둔다. 실행 실패가 흔하면 정상 종료 경로가
        // 표본에서 밀려나 VM의 정상 경로가 거의 검사되지 않는다.
        const uint32_t roll = rng_.Below(100);
        if (roll < 42) {
            out.push_back(NumberLiteral());
        } else if (roll < 76) {
            out.push_back(ExistingName());
        } else if (roll < 84) {
            out.push_back(rng_.Chance(50) ? "true" : "false");
        } else if (roll < 99) {
            const bool bound = !rng_.Chance(8);
            out.push_back(bound ? (rng_.Chance(50) ? "twice" : "half") : "ghost");
            out.push_back("(");
            Expression(out, depth + 1);
            out.push_back(")");
            sample_.hasNativeCall = true;
            if (!bound) sample_.hasUnregisteredNative = true;
        } else {
            out.push_back("unset");  // 선언되지 않은 전역.
            sample_.hasUndeclaredRead = true;
        }
    }

    std::string BinaryOperator() {
        static const char* const kOperators[] = {"+",  "-",  "*",  "/",  "==",
                                                 "!=", "<",  "<=", ">",  ">="};
        return kOperators[rng_.Below(10)];
    }

    // 균등 난수는 경계를 거의 만들지 못한다. 0·1과 매우 큰 값의 비중을 명시적으로 준다.
    std::string NumberLiteral() {
        const uint32_t roll = rng_.Below(100);
        if (roll < 45) return std::to_string(rng_.Between(0, 12));
        if (roll < 58) return rng_.Chance(50) ? "0" : "1";
        if (roll < 70) return std::to_string(rng_.Between(100, 100000));
        if (roll < 82) {
            static const char* const kDecimals[] = {"0.5", "1.25", "3.125", "0.001", "2.5"};
            return kDecimals[rng_.Below(5)];
        }
        if (roll < 92) return "99999999999999999999";
        return "1000000000000000000000000000000";
    }

    std::string DeclareName() {
        std::string name = "v" + std::to_string(nameCounter_++);
        declared_.push_back(name);
        return name;
    }

    const std::string& ExistingName() {
        return declared_[rng_.Below(static_cast<uint32_t>(declared_.size()))];
    }

    Rng& rng_;
    GeneratorLimits limits_;
    Sample sample_;
    std::vector<std::string> declared_;
    int nameCounter_ = 0;
};
// snippet-end: fuzz-generator

// ── 변형 생성기 ─────────────────────────────────────────────────────────────
enum class Mutation {
    Delete,
    Duplicate,
    Replace,
    Insert,
    Swap,
    Truncate,
    DeepNesting,
    Count,
};

inline const char* MutationName(Mutation kind) {
    switch (kind) {
        case Mutation::Delete: return "delete";
        case Mutation::Duplicate: return "duplicate";
        case Mutation::Replace: return "replace";
        case Mutation::Insert: return "insert";
        case Mutation::Swap: return "swap";
        case Mutation::Truncate: return "truncate";
        case Mutation::DeepNesting: return "deep-nesting";
        case Mutation::Count: break;
    }
    return "?";
}

// 대체·삽입에 쓰는 어휘. 문법에 있는 토큰과 렉서가 모르는 문자를 함께 넣는다.
inline const std::vector<std::string>& MutationVocabulary() {
    static const std::vector<std::string> vocabulary{
        "(",  ")",  "{",   "}",   ";",     "+",     "-",    "*",     "/",   "=",
        "==", "!=", "<",   "<=",  ">",     ">=",    "let",  "if",    "else",
        "while", "true", "false", "v0",    "unset", "ghost", "twice",
        "0",  "1",  "999", "1.5", "@",     "$",     "#",    "!",     "?",   "`",
        ".",  ",",  ":",   "&",   "|",     "~",     "[",    "]",     "\\",
    };
    return vocabulary;
}

// snippet-begin: fuzz-mutator
// 유효 프로그램을 토큰 단위로 깨뜨린다. 바이트 단위 변형과 달리 토큰 경계를 유지하므로
// 렉서에서 바로 죽지 않고 파서 안쪽까지 도달하는 입력이 나온다.
inline TokenList ApplyMutations(const TokenList& input, Rng& rng, unsigned& kinds) {
    TokenList tokens = input;
    kinds = 0;
    const int rounds = rng.Between(1, 3);
    for (int round = 0; round < rounds && !tokens.empty(); ++round) {
        const uint32_t roll = rng.Below(100);
        const size_t position = rng.Below(static_cast<uint32_t>(tokens.size()));
        Mutation kind = Mutation::Replace;

        if (roll < 20) {
            kind = Mutation::Delete;
            tokens.erase(tokens.begin() + static_cast<std::ptrdiff_t>(position));
        } else if (roll < 34) {
            kind = Mutation::Duplicate;
            tokens.insert(tokens.begin() + static_cast<std::ptrdiff_t>(position),
                          tokens[position]);
        } else if (roll < 56) {
            kind = Mutation::Replace;
            const auto& vocabulary = MutationVocabulary();
            tokens[position] = vocabulary[rng.Below(static_cast<uint32_t>(vocabulary.size()))];
        } else if (roll < 72) {
            kind = Mutation::Insert;
            const auto& vocabulary = MutationVocabulary();
            tokens.insert(tokens.begin() + static_cast<std::ptrdiff_t>(position),
                          vocabulary[rng.Below(static_cast<uint32_t>(vocabulary.size()))]);
        } else if (roll < 84) {
            kind = Mutation::Swap;
            if (position + 1 < tokens.size())
                std::swap(tokens[position], tokens[position + 1]);
        } else if (roll < 94) {
            kind = Mutation::Truncate;
            tokens.resize(position);
        } else {
            // 과거 실결함의 부류: 중첩 괄호로 파서 재귀를 밀어붙인다. 파서 한도(128)를
            // 넘기는 길이만 만든다 — 한도가 사라지면 이 표본이 프로세스를 죽인다.
            kind = Mutation::DeepNesting;
            tokens.insert(tokens.begin() + static_cast<std::ptrdiff_t>(position),
                          std::string(static_cast<size_t>(rng.Between(160, 600)), '('));
        }
        kinds |= 1u << static_cast<unsigned>(kind);
    }
    return tokens;
}
// snippet-end: fuzz-mutator

// ── 바이트코드 변형 ─────────────────────────────────────────────────────────
// 토큰 변형은 컴파일러를 통과한 코드만 만들므로 검증기를 밀어붙이지 못한다. 손상된
// 캐시 파일과 주입된 명령열이 검증기의 실제 위협 모델이라 명령 단위 변형을 따로 둔다.
inline Program MutateBytecode(const Program& program, Rng& rng, bool& touchedOpCode) {
    Program mutated = program;
    touchedOpCode = false;
    if (mutated.code.empty()) return mutated;

    const int rounds = rng.Between(1, 2);
    for (int round = 0; round < rounds && !mutated.code.empty(); ++round) {
        const size_t index = rng.Below(static_cast<uint32_t>(mutated.code.size()));
        const uint32_t roll = rng.Below(100);
        if (roll < 30) {
            // opcode 바이트를 통째로 바꾼다. 열거자 범위 밖 값도 포함한다.
            mutated.code[index].op = static_cast<OpCode>(rng.Below(256));
            touchedOpCode = true;
        } else if (roll < 60) {
            const uint32_t choice = rng.Below(4);
            if (choice == 0) mutated.code[index].operand = -rng.Between(1, 4);
            else if (choice == 1) mutated.code[index].operand = rng.Between(0, 40);
            else if (choice == 2) mutated.code[index].operand = 2147483647;
            else mutated.code[index].operand += rng.Between(-3, 3);
        } else if (roll < 75) {
            mutated.code.resize(index + 1);  // 꼬리 절단. Halt가 사라질 수 있다.
        } else if (roll < 85 && !mutated.constants.empty()) {
            mutated.constants.pop_back();
        } else if (roll < 92 && !mutated.names.empty()) {
            mutated.names.pop_back();
        } else {
            mutated.code.insert(mutated.code.begin() + static_cast<std::ptrdiff_t>(index),
                                mutated.code[index]);
        }
    }
    return mutated;
}

// ── 실패 어휘 ───────────────────────────────────────────────────────────────
// 실패 메시지의 집합이 닫혀 있다는 것 자체가 property다. 통제되지 않은 새 실패 경로가
// 생기면 여기 없는 문자열이 나오고 배터리가 그것을 잡는다.
inline bool StartsWith(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

inline bool IsKnownCompileMessage(const std::string& message) {
    static const char* const kExact[] = {
        "유한한 숫자 리터럴이 아니다",
        "`!`는 단독 연산자로 지원하지 않는다",
        "`let` 뒤에 변수 이름이 필요하다",
        "변수 초기화에 `=`가 필요하다",
        "변수 선언 뒤에 `;`가 필요하다",
        "할당 뒤에 `;`가 필요하다",
        "표현식 뒤에 `;`가 필요하다",
        "블록 시작에 `{`가 필요하다",
        "블록 끝에 `}`가 필요하다",
        "`if` 조건 앞에 `(`가 필요하다",
        "`if` 조건 뒤에 `)`가 필요하다",
        "`while` 조건 앞에 `(`가 필요하다",
        "`while` 조건 뒤에 `)`가 필요하다",
        "괄호를 닫는 `)`가 필요하다",
        "호스트 호출을 닫는 `)`가 필요하다",
        "표현식을 시작할 수 없는 토큰이다",
        "중첩 깊이가 파서 한도를 초과했다",
    };
    for (const char* candidate : kExact)
        if (message == candidate) return true;
    return StartsWith(message, "알 수 없는 문자: ");
}

inline bool IsKnownVerifyMessage(const std::string& message) {
    static const char* const kExact[] = {
        "프로그램은 Halt로 끝나야 한다",
        "스택 언더플로가 가능한 바이트코드다",
        "분기 합류점의 스택 깊이가 일치하지 않는다",
        "제어 흐름이 코드 밖으로 떨어진다",
        "알 수 없는 명령 코드다",
        "상수 인덱스가 범위를 벗어났다",
        "이름 인덱스가 범위를 벗어났다",
        "네이티브 이름 인덱스가 범위를 벗어났다",
        "분기 인덱스가 범위를 벗어났다",
    };
    for (const char* candidate : kExact)
        if (message == candidate) return true;
    return false;
}

inline bool IsKnownRuntimeMessage(const std::string& message) {
    static const char* const kExact[] = {
        "연산 결과가 유한하지 않다",
        "VM 스택 한도를 초과했다",
        "0으로 나눌 수 없다",
        "명령 실행 예산을 초과했다",
        "알 수 없는 명령이다",
        "Halt에 도달하지 못했다",
    };
    for (const char* candidate : kExact)
        if (message == candidate) return true;
    if (StartsWith(message, "초기화되지 않은 변수: ")) return true;
    if (StartsWith(message, "등록되지 않은 호스트 함수: ")) return true;
    if (StartsWith(message, "바이트코드 검증 실패: "))
        return IsKnownVerifyMessage(message.substr(std::string("바이트코드 검증 실패: ").size()));
    return false;
}

// ── 실행 관측 ───────────────────────────────────────────────────────────────
inline void BindStandardNatives(VirtualMachine& vm) {
    vm.Bind("twice", [](double value) { return value * 2.0; });
    vm.Bind("half", [](double value) { return value * 0.5; });
}

struct RunObservation {
    bool ok = false;
    uint64_t instructions = 0;
    std::string message;
    SourceLocation location;
    std::vector<std::optional<double>> globals;
    // 남은 값 스택의 높이도 관측에 넣는다. 이것이 빠지면 실행 사이에 스택이 이월돼도
    // 결과가 같아 보인다 — 아래쪽에 쌓인 값은 어떤 명령도 읽지 않기 때문이다.
    size_t stackDepth = 0;

    bool operator==(const RunObservation& other) const {
        return ok == other.ok && instructions == other.instructions &&
               message == other.message && location.line == other.location.line &&
               location.column == other.location.column && globals == other.globals &&
               stackDepth == other.stackDepth;
    }
};

inline RunObservation Observe(VirtualMachine& vm, const Program& program, uint64_t budget,
                              size_t stackLimit) {
    RunObservation observation;
    const RunResult result = vm.Run(program, budget, stackLimit);
    observation.ok = result.ok;
    observation.instructions = result.instructions;
    if (result.diagnostic) {
        observation.message = result.diagnostic->message;
        observation.location = result.diagnostic->location;
    }
    observation.globals.reserve(program.names.size());
    for (const std::string& name : program.names)
        observation.globals.push_back(vm.Global(program, name));
    observation.stackDepth = vm.StackDepth();
    return observation;
}

inline bool SameBytecode(const Program& left, const Program& right) {
    if (left.code.size() != right.code.size()) return false;
    for (size_t i = 0; i < left.code.size(); ++i) {
        if (left.code[i].op != right.code[i].op) return false;
        if (left.code[i].operand != right.code[i].operand) return false;
        if (left.code[i].location.line != right.code[i].location.line) return false;
        if (left.code[i].location.column != right.code[i].location.column) return false;
    }
    return left.constants == right.constants && left.names == right.names;
}

// ── 반례 축소 ───────────────────────────────────────────────────────────────
// snippet-begin: fuzz-shrink
// 구간 삭제 delta debugging. 실패를 유지하는 삭제만 남기고 구간 크기를 절반씩 줄인다.
// 호출 예산을 두는 이유는 축소 자체가 배터리보다 오래 걸리면 안 되기 때문이다.
template <typename Predicate>
TokenList Shrink(const TokenList& tokens, const Predicate& stillFails, int callBudget = 600) {
    TokenList best = tokens;
    int calls = 0;
    for (size_t chunk = best.size() / 2 + 1;; chunk /= 2) {
        bool changed = true;
        while (changed && calls < callBudget) {
            changed = false;
            for (size_t start = 0; start + chunk <= best.size() && calls < callBudget;) {
                TokenList candidate(best.begin(),
                                    best.begin() + static_cast<std::ptrdiff_t>(start));
                candidate.insert(candidate.end(),
                                 best.begin() + static_cast<std::ptrdiff_t>(start + chunk),
                                 best.end());
                ++calls;
                if (!candidate.empty() && stillFails(candidate)) {
                    best.swap(candidate);
                    changed = true;
                } else {
                    ++start;
                }
            }
        }
        if (chunk <= 1) break;
    }
    return best;
}
// snippet-end: fuzz-shrink

}  // namespace lab::lang::fuzz
