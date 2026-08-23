#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lab::lang {

struct SourceLocation {
    int line = 1;
    int column = 1;
};

struct Diagnostic {
    SourceLocation location;
    std::string message;
};

enum class TokenKind {
    Number,
    Identifier,
    Let,
    If,
    Else,
    While,
    True,
    False,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    Semicolon,
    Plus,
    Minus,
    Star,
    Slash,
    Equal,
    EqualEqual,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    End,
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    double number = 0.0;
    SourceLocation location;
};

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}

    std::vector<Token> Scan(std::vector<Diagnostic>& diagnostics) {
        std::vector<Token> tokens;
        while (!AtEnd()) {
            SkipTrivia();
            if (AtEnd()) break;
            const SourceLocation start{line_, column_};
            const char c = Advance();

            if (IsDigit(c)) {
                tokens.push_back(ScanNumber(start, c, diagnostics));
            } else if (IsIdentifierStart(c)) {
                tokens.push_back(ScanIdentifier(start, c));
            } else {
                ScanPunctuation(start, c, tokens, diagnostics);
            }
        }
        tokens.push_back({TokenKind::End, "", 0.0, {line_, column_}});
        return tokens;
    }

private:
    bool AtEnd() const { return offset_ >= source_.size(); }
    char Peek(size_t distance = 0) const {
        const size_t index = offset_ + distance;
        return index < source_.size() ? source_[index] : '\0';
    }
    char Advance() {
        const char c = source_[offset_++];
        if (c == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return c;
    }

    static bool IsDigit(char c) { return c >= '0' && c <= '9'; }
    static bool IsIdentifierStart(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    static bool IsIdentifierPart(char c) {
        return IsIdentifierStart(c) || IsDigit(c);
    }

    void SkipTrivia() {
        for (;;) {
            while (Peek() == ' ' || Peek() == '\t' || Peek() == '\r' || Peek() == '\n')
                Advance();
            if (Peek() == '/' && Peek(1) == '/') {
                while (!AtEnd() && Peek() != '\n') Advance();
                continue;
            }
            return;
        }
    }

    Token ScanNumber(SourceLocation start, char first,
                     std::vector<Diagnostic>& diagnostics) {
        std::string text(1, first);
        while (IsDigit(Peek())) text.push_back(Advance());
        if (Peek() == '.' && IsDigit(Peek(1))) {
            text.push_back(Advance());
            while (IsDigit(Peek())) text.push_back(Advance());
        }

        double value = 0.0;
        const char* begin = text.data();
        const char* end = begin + text.size();
        const auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value))
            diagnostics.push_back({start, "유한한 숫자 리터럴이 아니다"});
        return {TokenKind::Number, std::move(text), value, start};
    }

    Token ScanIdentifier(SourceLocation start, char first) {
        std::string text(1, first);
        while (IsIdentifierPart(Peek())) text.push_back(Advance());
        static const std::unordered_map<std::string, TokenKind> keywords{
            {"let", TokenKind::Let},       {"if", TokenKind::If},
            {"else", TokenKind::Else},     {"while", TokenKind::While},
            {"true", TokenKind::True},     {"false", TokenKind::False},
        };
        const auto it = keywords.find(text);
        return {it == keywords.end() ? TokenKind::Identifier : it->second,
                std::move(text), 0.0, start};
    }

    void ScanPunctuation(SourceLocation start, char c, std::vector<Token>& tokens,
                         std::vector<Diagnostic>& diagnostics) {
        auto one = [&](TokenKind kind) {
            tokens.push_back({kind, std::string(1, c), 0.0, start});
        };
        auto oneOrTwo = [&](char expected, TokenKind oneKind, TokenKind twoKind) {
            if (Peek() == expected) {
                Advance();
                tokens.push_back({twoKind, std::string() + c + expected, 0.0, start});
            } else {
                one(oneKind);
            }
        };

        switch (c) {
            case '(': one(TokenKind::LeftParen); break;
            case ')': one(TokenKind::RightParen); break;
            case '{': one(TokenKind::LeftBrace); break;
            case '}': one(TokenKind::RightBrace); break;
            case ';': one(TokenKind::Semicolon); break;
            case '+': one(TokenKind::Plus); break;
            case '-': one(TokenKind::Minus); break;
            case '*': one(TokenKind::Star); break;
            case '/': one(TokenKind::Slash); break;
            case '=': oneOrTwo('=', TokenKind::Equal, TokenKind::EqualEqual); break;
            case '!':
                if (Peek() == '=') {
                    Advance();
                    tokens.push_back({TokenKind::BangEqual, "!=", 0.0, start});
                } else {
                    diagnostics.push_back({start, "`!`는 단독 연산자로 지원하지 않는다"});
                }
                break;
            case '<': oneOrTwo('=', TokenKind::Less, TokenKind::LessEqual); break;
            case '>': oneOrTwo('=', TokenKind::Greater, TokenKind::GreaterEqual); break;
            default:
                diagnostics.push_back({start, std::string("알 수 없는 문자: ") + c});
                break;
        }
    }

    std::string_view source_;
    size_t offset_ = 0;
    int line_ = 1;
    int column_ = 1;
};

enum class OpCode : uint8_t {
    Constant,
    LoadGlobal,
    StoreGlobal,
    Add,
    Subtract,
    Multiply,
    Divide,
    Negate,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    JumpIfFalse,
    Jump,
    CallNative,
    Pop,
    Halt,
};

// snippet-begin: bytecode-structs
struct Instruction {
    OpCode op = OpCode::Halt;
    int32_t operand = 0;
    SourceLocation location;
};

struct Program {
    std::vector<Instruction> code;
    std::vector<double> constants;
    std::vector<std::string> names;
};
// snippet-end: bytecode-structs

struct CompileResult {
    std::optional<Program> program;
    std::vector<Diagnostic> diagnostics;
};

class Compiler {
public:
    CompileResult Compile(std::string_view source) {
        CompileResult result;
        tokens_ = Lexer(source).Scan(result.diagnostics);
        diagnostics_ = &result.diagnostics;
        current_ = 0;
        nesting_ = 0;
        program_ = {};

        while (!Check(TokenKind::End) && diagnostics_->empty()) Statement();
        if (diagnostics_->empty()) {
            Emit(OpCode::Halt, 0, Peek().location);
            result.program = std::move(program_);
        }
        diagnostics_ = nullptr;
        return result;
    }

private:
    enum class Precedence : int {
        None,
        Equality,
        Comparison,
        Term,
        Factor,
        Unary,
    };

    const Token& Peek(size_t distance = 0) const {
        const size_t index = current_ + distance;
        return tokens_[index < tokens_.size() ? index : tokens_.size() - 1];
    }
    const Token& Previous() const { return tokens_[current_ - 1]; }
    bool Check(TokenKind kind) const { return Peek().kind == kind; }
    bool Match(TokenKind kind) {
        if (!Check(kind)) return false;
        ++current_;
        return true;
    }
    const Token& Consume(TokenKind kind, std::string message) {
        if (Check(kind)) return tokens_[current_++];
        diagnostics_->push_back({Peek().location, std::move(message)});
        return Peek();
    }

    // 신뢰하지 않는 소스가 C++ 호출 스택을 고갈시키지 못하도록 파서 재귀에 상한을 둔다.
    static constexpr int kMaxNestingDepth = 128;

    bool EnterNesting(SourceLocation location) {
        if (++nesting_ > kMaxNestingDepth) {
            if (diagnostics_->empty())
                diagnostics_->push_back({location, "중첩 깊이가 파서 한도를 초과했다"});
            return false;
        }
        return true;
    }
    void LeaveNesting() { --nesting_; }

    int32_t Emit(OpCode op, int32_t operand, SourceLocation location) {
        program_.code.push_back({op, operand, location});
        return static_cast<int32_t>(program_.code.size() - 1);
    }
    void PatchJump(int32_t instruction) {
        program_.code[static_cast<size_t>(instruction)].operand =
            static_cast<int32_t>(program_.code.size());
    }
    int32_t AddConstant(double value) {
        program_.constants.push_back(value);
        return static_cast<int32_t>(program_.constants.size() - 1);
    }
    int32_t InternName(const std::string& name) {
        for (size_t i = 0; i < program_.names.size(); ++i)
            if (program_.names[i] == name) return static_cast<int32_t>(i);
        program_.names.push_back(name);
        return static_cast<int32_t>(program_.names.size() - 1);
    }

    void Statement() {
        if (Match(TokenKind::Let)) {
            VariableDeclaration();
        } else if (Match(TokenKind::If)) {
            IfStatement();
        } else if (Match(TokenKind::While)) {
            WhileStatement();
        } else if (Check(TokenKind::Identifier) && Peek(1).kind == TokenKind::Equal) {
            AssignmentStatement();
        } else {
            Expression();
            Consume(TokenKind::Semicolon, "표현식 뒤에 `;`가 필요하다");
            Emit(OpCode::Pop, 0, Previous().location);
        }
    }

    void VariableDeclaration() {
        const Token name = Consume(TokenKind::Identifier, "`let` 뒤에 변수 이름이 필요하다");
        Consume(TokenKind::Equal, "변수 초기화에 `=`가 필요하다");
        Expression();
        Consume(TokenKind::Semicolon, "변수 선언 뒤에 `;`가 필요하다");
        Emit(OpCode::StoreGlobal, InternName(name.text), name.location);
        Emit(OpCode::Pop, 0, name.location);
    }

    void AssignmentStatement() {
        const Token name = tokens_[current_++];
        ++current_;  // '='
        Expression();
        Consume(TokenKind::Semicolon, "할당 뒤에 `;`가 필요하다");
        Emit(OpCode::StoreGlobal, InternName(name.text), name.location);
        Emit(OpCode::Pop, 0, name.location);
    }

    void Block() {
        if (!EnterNesting(Peek().location)) {
            LeaveNesting();
            return;
        }
        Consume(TokenKind::LeftBrace, "블록 시작에 `{`가 필요하다");
        while (!Check(TokenKind::RightBrace) && !Check(TokenKind::End) &&
               diagnostics_->empty()) {
            Statement();
        }
        Consume(TokenKind::RightBrace, "블록 끝에 `}`가 필요하다");
        LeaveNesting();
    }

    void IfStatement() {
        Consume(TokenKind::LeftParen, "`if` 조건 앞에 `(`가 필요하다");
        Expression();
        Consume(TokenKind::RightParen, "`if` 조건 뒤에 `)`가 필요하다");
        const int32_t falseJump = Emit(OpCode::JumpIfFalse, -1, Previous().location);
        Emit(OpCode::Pop, 0, Previous().location);
        Block();
        const int32_t endJump = Emit(OpCode::Jump, -1, Previous().location);
        PatchJump(falseJump);
        Emit(OpCode::Pop, 0, Previous().location);
        if (Match(TokenKind::Else)) Block();
        PatchJump(endJump);
    }

    void WhileStatement() {
        const int32_t loopStart = static_cast<int32_t>(program_.code.size());
        Consume(TokenKind::LeftParen, "`while` 조건 앞에 `(`가 필요하다");
        Expression();
        Consume(TokenKind::RightParen, "`while` 조건 뒤에 `)`가 필요하다");
        const int32_t exitJump = Emit(OpCode::JumpIfFalse, -1, Previous().location);
        Emit(OpCode::Pop, 0, Previous().location);
        Block();
        Emit(OpCode::Jump, loopStart, Previous().location);
        PatchJump(exitJump);
        Emit(OpCode::Pop, 0, Previous().location);
    }

    static Precedence InfixPrecedence(TokenKind kind) {
        switch (kind) {
            case TokenKind::EqualEqual:
            case TokenKind::BangEqual: return Precedence::Equality;
            case TokenKind::Less:
            case TokenKind::LessEqual:
            case TokenKind::Greater:
            case TokenKind::GreaterEqual: return Precedence::Comparison;
            case TokenKind::Plus:
            case TokenKind::Minus: return Precedence::Term;
            case TokenKind::Star:
            case TokenKind::Slash: return Precedence::Factor;
            default: return Precedence::None;
        }
    }

    void Expression() { ParsePrecedence(Precedence::Equality); }

    // snippet-begin: parse-precedence
    void ParsePrecedence(Precedence minimum) {
        if (!EnterNesting(Peek().location)) {
            LeaveNesting();
            return;
        }
        Prefix();
        while (diagnostics_->empty() &&
               static_cast<int>(InfixPrecedence(Peek().kind)) >=
                   static_cast<int>(minimum) &&
               InfixPrecedence(Peek().kind) != Precedence::None) {
            const Token op = tokens_[current_++];
            const auto precedence = InfixPrecedence(op.kind);
            ParsePrecedence(static_cast<Precedence>(static_cast<int>(precedence) + 1));
            Emit(BinaryOp(op.kind), 0, op.location);
        }
        LeaveNesting();
    }
    // snippet-end: parse-precedence

    void Prefix() {
        const Token token = tokens_[current_++];
        switch (token.kind) {
            case TokenKind::Number:
                Emit(OpCode::Constant, AddConstant(token.number), token.location);
                break;
            case TokenKind::True:
                Emit(OpCode::Constant, AddConstant(1.0), token.location);
                break;
            case TokenKind::False:
                Emit(OpCode::Constant, AddConstant(0.0), token.location);
                break;
            case TokenKind::Minus:
                ParsePrecedence(Precedence::Unary);
                Emit(OpCode::Negate, 0, token.location);
                break;
            case TokenKind::LeftParen:
                Expression();
                Consume(TokenKind::RightParen, "괄호를 닫는 `)`가 필요하다");
                break;
            case TokenKind::Identifier:
                if (Match(TokenKind::LeftParen)) {
                    Expression();
                    Consume(TokenKind::RightParen, "호스트 호출을 닫는 `)`가 필요하다");
                    Emit(OpCode::CallNative, InternName(token.text), token.location);
                } else {
                    Emit(OpCode::LoadGlobal, InternName(token.text), token.location);
                }
                break;
            default:
                diagnostics_->push_back({token.location, "표현식을 시작할 수 없는 토큰이다"});
                break;
        }
    }

    static OpCode BinaryOp(TokenKind kind) {
        switch (kind) {
            case TokenKind::Plus: return OpCode::Add;
            case TokenKind::Minus: return OpCode::Subtract;
            case TokenKind::Star: return OpCode::Multiply;
            case TokenKind::Slash: return OpCode::Divide;
            case TokenKind::EqualEqual: return OpCode::Equal;
            case TokenKind::BangEqual: return OpCode::NotEqual;
            case TokenKind::Less: return OpCode::Less;
            case TokenKind::LessEqual: return OpCode::LessEqual;
            case TokenKind::Greater: return OpCode::Greater;
            case TokenKind::GreaterEqual: return OpCode::GreaterEqual;
            default: return OpCode::Halt;
        }
    }

    std::vector<Token> tokens_;
    size_t current_ = 0;
    int nesting_ = 0;
    Program program_;
    std::vector<Diagnostic>* diagnostics_ = nullptr;
};

// snippet-begin: known-opcode
// `Instruction::op`은 손상된 캐시나 주입된 명령열에서 열거자에 없는 값을 가질 수 있다.
// `OpCode`의 기반 타입이 `uint8_t`라 0~255 전부가 표현 가능한 값이므로 이 검사가
// 없으면 미지의 값이 검증기의 switch를 그냥 통과한다.
//
// `default:` 대신 열거자를 전부 적는다. 명령을 추가하면서 여기를 빠뜨리면 컴파일러가
// 미처리 열거자를 경고한다 — 침묵하는 통과보다 시끄러운 경고가 낫다.
inline bool IsKnownOpCode(OpCode op) {
    switch (op) {
        case OpCode::Constant:
        case OpCode::LoadGlobal:
        case OpCode::StoreGlobal:
        case OpCode::Add:
        case OpCode::Subtract:
        case OpCode::Multiply:
        case OpCode::Divide:
        case OpCode::Negate:
        case OpCode::Equal:
        case OpCode::NotEqual:
        case OpCode::Less:
        case OpCode::LessEqual:
        case OpCode::Greater:
        case OpCode::GreaterEqual:
        case OpCode::JumpIfFalse:
        case OpCode::Jump:
        case OpCode::CallNative:
        case OpCode::Pop:
        case OpCode::Halt:
            return true;
    }
    return false;
}
// snippet-end: known-opcode

inline bool Verify(const Program& program, std::string& error) {
    if (program.code.empty() || program.code.back().op != OpCode::Halt) {
        error = "프로그램은 Halt로 끝나야 한다";
        return false;
    }

    const int unknown = -2;
    std::vector<int> depth(program.code.size(), unknown);
    std::queue<size_t> work;
    depth[0] = 0;
    work.push(0);

    auto requireOperand = [&](const Instruction& instruction, size_t limit,
                              const char* label) -> bool {
        if (instruction.operand < 0 ||
            static_cast<size_t>(instruction.operand) >= limit) {
            error = std::string(label) + " 인덱스가 범위를 벗어났다";
            return false;
        }
        return true;
    };

    // 도달 가능성과 무관하게 파일 전체의 명령 코드와 정적 피연산자를 먼저 검사한다.
    for (const Instruction& instruction : program.code) {
        if (!IsKnownOpCode(instruction.op)) {
            error = "알 수 없는 명령 코드다";
            return false;
        }
        switch (instruction.op) {
            case OpCode::Constant:
                if (!requireOperand(instruction, program.constants.size(), "상수"))
                    return false;
                break;
            case OpCode::LoadGlobal:
            case OpCode::StoreGlobal:
            case OpCode::CallNative:
                if (!requireOperand(instruction, program.names.size(), "이름"))
                    return false;
                break;
            case OpCode::JumpIfFalse:
            case OpCode::Jump:
                if (!requireOperand(instruction, program.code.size(), "분기"))
                    return false;
                break;
            default:
                break;
        }
    }

    while (!work.empty()) {
        const size_t pc = work.front();
        work.pop();
        const Instruction& instruction = program.code[pc];
        int required = 0;
        int delta = 0;
        bool terminal = false;

        switch (instruction.op) {
            case OpCode::Constant:
                if (!requireOperand(instruction, program.constants.size(), "상수")) return false;
                delta = 1;
                break;
            case OpCode::LoadGlobal:
                if (!requireOperand(instruction, program.names.size(), "이름")) return false;
                delta = 1;
                break;
            case OpCode::StoreGlobal:
                if (!requireOperand(instruction, program.names.size(), "이름")) return false;
                required = 1;
                break;
            case OpCode::Add:
            case OpCode::Subtract:
            case OpCode::Multiply:
            case OpCode::Divide:
            case OpCode::Equal:
            case OpCode::NotEqual:
            case OpCode::Less:
            case OpCode::LessEqual:
            case OpCode::Greater:
            case OpCode::GreaterEqual:
                required = 2;
                delta = -1;
                break;
            case OpCode::Negate:
                required = 1;
                break;
            case OpCode::CallNative:
                if (!requireOperand(instruction, program.names.size(), "네이티브 이름"))
                    return false;
                required = 1;
                break;
            case OpCode::Pop:
                required = 1;
                delta = -1;
                break;
            case OpCode::JumpIfFalse:
                required = 1;
                if (!requireOperand(instruction, program.code.size(), "분기")) return false;
                break;
            case OpCode::Jump:
                if (!requireOperand(instruction, program.code.size(), "분기")) return false;
                break;
            case OpCode::Halt:
                terminal = true;
                break;
        }

        if (depth[pc] < required) {
            error = "스택 언더플로가 가능한 바이트코드다";
            return false;
        }
        const int nextDepth = depth[pc] + delta;
        auto enqueue = [&](size_t target) -> bool {
            if (depth[target] == unknown) {
                depth[target] = nextDepth;
                work.push(target);
                return true;
            }
            if (depth[target] != nextDepth) {
                error = "분기 합류점의 스택 깊이가 일치하지 않는다";
                return false;
            }
            return true;
        };

        if (terminal) continue;
        if (instruction.op == OpCode::Jump) {
            if (!enqueue(static_cast<size_t>(instruction.operand))) return false;
            continue;
        }
        if (instruction.op == OpCode::JumpIfFalse &&
            !enqueue(static_cast<size_t>(instruction.operand))) {
            return false;
        }
        if (pc + 1 >= program.code.size()) {
            error = "제어 흐름이 코드 밖으로 떨어진다";
            return false;
        }
        if (!enqueue(pc + 1)) return false;
    }
    return true;
}

struct RunResult {
    bool ok = false;
    uint64_t instructions = 0;
    std::optional<Diagnostic> diagnostic;
};

class VirtualMachine {
public:
    using NativeFunction = std::function<double(double)>;

    void Bind(std::string name, NativeFunction function) {
        natives_[std::move(name)] = std::move(function);
    }

    RunResult Run(const Program& program, uint64_t instructionBudget = 100000,
                  size_t stackLimit = 256) {
        std::string verificationError;
        if (!Verify(program, verificationError))
            return Failure({}, "바이트코드 검증 실패: " + verificationError, 0);

        globals_.assign(program.names.size(), std::nullopt);
        stack_.clear();
        size_t pc = 0;
        uint64_t executed = 0;

        auto pop = [&]() {
            const double value = stack_.back();
            stack_.pop_back();
            return value;
        };
        auto push = [&](double value, SourceLocation location) -> std::optional<RunResult> {
            if (!std::isfinite(value))
                return Failure(location, "연산 결과가 유한하지 않다", executed);
            if (stack_.size() >= stackLimit)
                return Failure(location, "VM 스택 한도를 초과했다", executed);
            stack_.push_back(value);
            return std::nullopt;
        };

        while (pc < program.code.size()) {
            const Instruction instruction = program.code[pc++];
            if (++executed > instructionBudget)
                return Failure(instruction.location, "명령 실행 예산을 초과했다", executed);

            switch (instruction.op) {
                case OpCode::Constant:
                    if (auto failure = push(
                            program.constants[static_cast<size_t>(instruction.operand)],
                            instruction.location))
                        return *failure;
                    break;
                case OpCode::LoadGlobal: {
                    const size_t index = static_cast<size_t>(instruction.operand);
                    if (!globals_[index])
                        return Failure(instruction.location,
                                       "초기화되지 않은 변수: " + program.names[index],
                                       executed);
                    if (auto failure = push(*globals_[index], instruction.location))
                        return *failure;
                    break;
                }
                case OpCode::StoreGlobal:
                    globals_[static_cast<size_t>(instruction.operand)] = stack_.back();
                    break;
                case OpCode::Negate:
                    stack_.back() = -stack_.back();
                    break;
                case OpCode::Pop:
                    pop();
                    break;
                case OpCode::JumpIfFalse:
                    if (stack_.back() == 0.0)
                        pc = static_cast<size_t>(instruction.operand);
                    break;
                case OpCode::Jump:
                    pc = static_cast<size_t>(instruction.operand);
                    break;
                case OpCode::CallNative: {
                    const std::string& name =
                        program.names[static_cast<size_t>(instruction.operand)];
                    const auto found = natives_.find(name);
                    if (found == natives_.end())
                        return Failure(instruction.location,
                                       "등록되지 않은 호스트 함수: " + name, executed);
                    const double argument = pop();
                    const double value = found->second(argument);
                    if (auto failure = push(value, instruction.location)) return *failure;
                    break;
                }
                case OpCode::Halt:
                    return {true, executed, std::nullopt};
                default: {
                    const double rhs = pop();
                    const double lhs = pop();
                    double value = 0.0;
                    switch (instruction.op) {
                        case OpCode::Add: value = lhs + rhs; break;
                        case OpCode::Subtract: value = lhs - rhs; break;
                        case OpCode::Multiply: value = lhs * rhs; break;
                        case OpCode::Divide:
                            if (rhs == 0.0)
                                return Failure(instruction.location, "0으로 나눌 수 없다",
                                               executed);
                            value = lhs / rhs;
                            break;
                        case OpCode::Equal: value = lhs == rhs ? 1.0 : 0.0; break;
                        case OpCode::NotEqual: value = lhs != rhs ? 1.0 : 0.0; break;
                        case OpCode::Less: value = lhs < rhs ? 1.0 : 0.0; break;
                        case OpCode::LessEqual: value = lhs <= rhs ? 1.0 : 0.0; break;
                        case OpCode::Greater: value = lhs > rhs ? 1.0 : 0.0; break;
                        case OpCode::GreaterEqual: value = lhs >= rhs ? 1.0 : 0.0; break;
                        default:
                            return Failure(instruction.location, "알 수 없는 명령이다",
                                           executed);
                    }
                    if (auto failure = push(value, instruction.location)) return *failure;
                    break;
                }
            }
        }
        return Failure({}, "Halt에 도달하지 못했다", executed);
    }

    std::optional<double> Global(const Program& program, std::string_view name) const {
        for (size_t i = 0; i < program.names.size(); ++i)
            if (program.names[i] == name && i < globals_.size()) return globals_[i];
        return std::nullopt;
    }

    // 실행 뒤 남은 값 스택의 높이. 컴파일러가 내는 모든 문장은 스택 중립이므로 `Halt`에
    // 도달한 실행은 0으로 끝나야 하고, 실행 사이에 이 값이 이월되어서도 안 된다. 관측할
    // 수단이 없으면 두 불변식 중 어느 것도 검사할 수 없다.
    size_t StackDepth() const { return stack_.size(); }

private:
    static RunResult Failure(SourceLocation location, std::string message,
                             uint64_t instructions) {
        return {false, instructions, Diagnostic{location, std::move(message)}};
    }

    std::unordered_map<std::string, NativeFunction> natives_;
    std::vector<std::optional<double>> globals_;
    std::vector<double> stack_;
};

inline std::string FormatDiagnostic(const Diagnostic& diagnostic) {
    std::ostringstream out;
    out << diagnostic.location.line << ':' << diagnostic.location.column << ": "
        << diagnostic.message;
    return out.str();
}

}  // namespace lab::lang
