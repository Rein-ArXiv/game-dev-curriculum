// script — 미니 스크립트 언어. 렉서 → 파서 → AST → 트리 워킹 인터프리터.
//
// 게임 로직(웨이브 규칙, 이벤트 조건)을 코드 재배포 없이 데이터로 기술하기 위한
// 작은 DSL 이다. 변수·산술·비교·if/else·while 을 지원한다. 트리 워킹은 바이트코드
// VM 보다 단순하고 디버깅이 쉬워 시작점으로 적합하다(tools 스크립팅 챕터).
//
//   x = 5
//   y = x * 2 + 1
//   if y > 10 { z = 1 } else { z = 0 }
//   while x > 0 { x = x - 1 }
//
// ── 실행 예산: 이 인터프리터의 입력은 신뢰 경계 밖이다 ─────────────────
//
// 이 DSL 이 읽는 파일(games/defense/assets/balance.script)은 "재빌드 없이 교체할
// 수 있다"고 문서가 광고한다. 즉 소스는 우리가 고르는 입력이 아니고, "끝나지 않는
// 프로그램"도 그 입력의 하나다.
//
// 예전 방어는 While 노드 **하나마다** 지역 카운터를 두는 것뿐이었다:
//
//     case Node::While: { int guard = 0; while (...) { ...; if (++guard > 1000000) break; } }
//
// 지역 카운터는 While 노드에 진입할 때마다 0 으로 되돌아간다. 그래서 중첩되면
// 상한이 더해지지 않고 **곱해진다** — 두 겹이면 10^12 회다. 이 방어에는 구멍이
// 둘 더 있었다: 파서가 모르는 토큰을 소비하지 않고 돌아와 최상위 문 루프가
// 전진하지 않았고(`}` 한 글자짜리 스크립트가 노드를 무한 할당한다), 재귀 하강에
// 깊이 제한이 없어 괄호를 충분히 겹치면 스택이 넘쳤다. 셋 다 교체 가능한 파일
// 하나로 `mega_defense` 와 `mega_defense_server` 를 콘텐츠 로드 지점에 세운다.
// 두 실행 파일 모두 로더의 `if (!script.Run(...)) return fallback;` 에 기대므로,
// 실행이 끝나지 않으면 "전체 폴백"이라는 약속에 도달하지조차 못한다.
//
// 그래서 방어는 While 노드가 아니라 **인터프리터 실행 전체**에 하나씩 걸린다.
//
//   1) 스텝 예산  — Eval 의 노드 방문 수에 대한 전역 상한. Run 마다 0 에서 시작하고
//                   중첩과 무관하게 합산되므로 곱해지지 않는다.
//   2) 중첩 깊이  — 파서 재귀 깊이 상한. 같은 깊이가 곧 Eval 재귀 깊이이므로
//                   파싱과 실행의 스택 사용량이 함께 묶인다.
//   3) 전진 보장  — 파서는 모르는 토큰을 반드시 소비하고, 첫 오류에서 멈춘다.
//
// 셋 다 조용한 실패가 아니라 `Error()` 로 읽는 진단 문자열을 남긴다. 형식은
// `<이름>:<줄>: <무엇이>` 이며 이름은 SetName() 으로 준 값(보통 파일 경로)이다.
// 호출자는 그 문자열을 그대로 찍고 기본 구성으로 진행하면 된다.

#pragma once

#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mega {
namespace tools {

class Script {
public:
    // Eval 노드 방문 수의 기본 상한. 밸런스 DSL 의 실제 사용량은 수십 스텝
    // 규모이므로 이 값은 순전히 여유분이다 — 잘못 짠(또는 악의로 짠) 반복문도
    // 이 상한에서 잘려 수십 ms 안에 오류로 끝난다.
    static constexpr long long kDefaultStepBudget = 1000000;

    // 파서 재귀 깊이 상한(= Eval 재귀 깊이 상한). 스택 오버플로는 폴백이 아니라
    // 프로세스의 죽음이므로 스텝 예산과 같은 급의 방어가 필요하다.
    static constexpr int kMaxNestingDepth = 64;

    // 진단에 쓸 이름. 보통 파일 경로를 준다 — Error() 문자열의 앞부분이 되어
    // "어느 스크립트가" 를 답한다.
    void SetName(const std::string& name) { name_ = name.empty() ? "<script>" : name; }
    const std::string& Name() const { return name_; }

    // 예산은 호출자가 조일 수 있다(0 이하는 허용하지 않는다 — 예산 0 은 예산이
    // 없는 것과 같은 실수를 부른다).
    void      SetStepBudget(long long budget) { budget_ = budget > 0 ? budget : 1; }
    long long StepBudget() const { return budget_; }
    long long StepsUsed() const { return steps_; }
    bool      BudgetExceeded() const { return budgetExceeded_; }

    // 실패 원인. 성공하면 빈 문자열이다.
    const std::string& Error() const { return error_; }

    // 소스 실행. 파싱·실행 성공 시 true. 실패하면 Error() 가 이유를 담는다.
    bool Run(const std::string& src) {
        ok_ = true;
        error_.clear();
        budgetExceeded_ = false;
        steps_ = 0;
        depth_ = 0;
        Lex(src);
        pos_ = 0;
        std::unique_ptr<Node> program = ParseBlockUntilEnd();
        if (!ok_) return false;
        Eval(program.get());
        return ok_;
    }

    bool   Get(const std::string& name, double& out) const {
        auto it = vars_.find(name);
        if (it == vars_.end()) return false;
        out = it->second; return true;
    }
    double GetOr(const std::string& name, double def) const {
        auto it = vars_.find(name); return it == vars_.end() ? def : it->second;
    }

private:
    // --- 진단 ---
    // 첫 오류만 남긴다. 뒤따르는 오류는 대개 첫 오류의 파생이라, 덮어쓰면
    // 원인에서 멀어진 위치가 보고된다.
    void Fail(int line, const std::string& what) {
        if (!ok_) return;
        ok_ = false;
        error_ = name_ + ":" + std::to_string(line) + ": " + what;
    }
    static std::string HexByte(unsigned char b) {
        static const char digits[] = "0123456789ABCDEF";
        std::string out = "0x";
        out += digits[b >> 4];
        out += digits[b & 0x0F];
        return out;
    }

    // --- 토큰 ---
    enum class T { Num, Ident, Op, LParen, RParen, LBrace, RBrace, KwIf, KwElse, KwWhile, End };
    struct Tok { T type; double num = 0; std::string text; int line = 1; };

    void Lex(const std::string& s) {
        toks_.clear();
        size_t i = 0;
        int line = 1;
        auto isIdent = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
        while (i < s.size()) {
            char c = s[i];
            if (c == '\n') { ++line; ++i; continue; }
            if (c == ' ' || c == '\t' || c == '\r' || c == ';') { ++i; continue; }
            if (c >= '0' && c <= '9') {
                size_t j = i; double v = 0; while (j < s.size() && s[j] >= '0' && s[j] <= '9') v = v * 10 + (s[j++] - '0');
                if (j < s.size() && s[j] == '.') { ++j; double f = 0.1; while (j < s.size() && s[j] >= '0' && s[j] <= '9') { v += (s[j++] - '0') * f; f *= 0.1; } }
                toks_.push_back({T::Num, v, {}, line}); i = j; continue;
            }
            if (isIdent(c)) {
                size_t j = i; while (j < s.size() && (isIdent(s[j]) || (s[j] >= '0' && s[j] <= '9'))) ++j;
                std::string id = s.substr(i, j - i); i = j;
                if (id == "if") toks_.push_back({T::KwIf, 0, id, line});
                else if (id == "else") toks_.push_back({T::KwElse, 0, id, line});
                else if (id == "while") toks_.push_back({T::KwWhile, 0, id, line});
                else toks_.push_back({T::Ident, 0, id, line});
                continue;
            }
            if (c == '(') { toks_.push_back({T::LParen, 0, "(", line}); ++i; continue; }
            if (c == ')') { toks_.push_back({T::RParen, 0, ")", line}); ++i; continue; }
            if (c == '{') { toks_.push_back({T::LBrace, 0, "{", line}); ++i; continue; }
            if (c == '}') { toks_.push_back({T::RBrace, 0, "}", line}); ++i; continue; }
            // 연산자(다중 문자 우선).
            std::string two = s.substr(i, 2);
            if (two == "==" || two == "!=" || two == "<=" || two == ">=") { toks_.push_back({T::Op, 0, two, line}); i += 2; continue; }
            if (std::string("+-*/<>=").find(c) != std::string::npos) { toks_.push_back({T::Op, 0, std::string(1, c), line}); ++i; continue; }
            // 바이트 값으로 보고한다. 원문 바이트를 그대로 끼워 넣으면 UTF-8 이
            // 아닌 조각이 진단 문자열을 깨뜨린다.
            Fail(line, "알 수 없는 문자(바이트 " + HexByte(static_cast<unsigned char>(c)) + ")");
            ++i;
        }
        toks_.push_back({T::End, 0, {}, line});
    }

    // --- AST ---
    struct Node {
        enum Kind { Num, Var, Bin, Un, Assign, If, While, Block } kind;
        double      num = 0;
        std::string name;     // Var/Assign
        std::string op;       // Bin/Un
        int         line = 1; // 진단용 소스 줄
        std::vector<std::unique_ptr<Node>> kids;
    };
    // Make 가 정적이 아닌 이유: 노드를 만든 시점의 토큰 줄을 함께 새긴다.
    // 예산 초과는 실행 중에 터지므로, 어느 노드에서 터졌는지가 곧 어느 줄인지다.
    std::unique_ptr<Node> Make(Node::Kind k) {
        auto n = std::make_unique<Node>();
        n->kind = k;
        n->line = CurrentLine();
        return n;
    }
    int CurrentLine() const {
        if (toks_.empty()) return 1;
        return toks_[pos_ < toks_.size() ? pos_ : toks_.size() - 1].line;
    }

    // 재귀 하강의 깊이를 세는 RAII 가드. 상한을 넘으면 그 자리에서 오류로 만들고,
    // 각 파스 함수가 !ok_ 를 보고 즉시 되돌아 나오므로 더 깊이 들어가지 않는다.
    class Depth {
    public:
        explicit Depth(Script* s) : script_(s) {
            if (++script_->depth_ > kMaxNestingDepth)
                script_->Fail(script_->CurrentLine(),
                              "중첩 깊이가 상한 " + std::to_string(kMaxNestingDepth) + " 을 넘었다");
        }
        ~Depth() { --script_->depth_; }
        Depth(const Depth&) = delete;
        Depth& operator=(const Depth&) = delete;
    private:
        Script* script_;
    };

    const Tok& Peek() const { return toks_[pos_]; }
    const Tok& Next() { return toks_[pos_++]; }
    bool AcceptOp(const std::string& o) { if (Peek().type == T::Op && Peek().text == o) { ++pos_; return true; } return false; }

    // 블록: '{' 문* '}' 또는 단일 문.
    std::unique_ptr<Node> ParseBlock() {
        if (Peek().type == T::LBrace) {
            ++pos_;
            auto b = Make(Node::Block);
            while (ok_ && Peek().type != T::RBrace && Peek().type != T::End) b->kids.push_back(ParseStatement());
            if (!ok_) return b;
            if (Peek().type == T::RBrace) ++pos_;
            else Fail(CurrentLine(), "블록을 닫는 '}' 가 없다");
            return b;
        }
        auto b = Make(Node::Block);
        b->kids.push_back(ParseStatement());
        return b;
    }
    std::unique_ptr<Node> ParseBlockUntilEnd() {
        auto b = Make(Node::Block);
        // ok_ 검사가 여기 있어야 하는 이유: ParseStatement 가 아무 토큰도 소비하지
        // 못한 채 오류로 돌아오는 경우에도 이 루프가 전진한다고 가정할 수 없다.
        // (ParsePrimary 의 전진 보장과 함께 이 루프의 종료를 이중으로 못박는다.)
        while (ok_ && Peek().type != T::End) b->kids.push_back(ParseStatement());
        return b;
    }

    std::unique_ptr<Node> ParseStatement() {
        Depth guard(this);
        if (!ok_) return Make(Node::Num);
        if (Peek().type == T::KwIf) {
            ++pos_;
            auto n = Make(Node::If);
            n->kids.push_back(ParseExpr());
            n->kids.push_back(ParseBlock());
            if (Peek().type == T::KwElse) { ++pos_; n->kids.push_back(ParseBlock()); }
            return n;
        }
        if (Peek().type == T::KwWhile) {
            ++pos_;
            auto n = Make(Node::While);
            n->kids.push_back(ParseExpr());
            n->kids.push_back(ParseBlock());
            return n;
        }
        // 할당: Ident '=' Expr
        if (Peek().type == T::Ident && toks_[pos_ + 1].type == T::Op && toks_[pos_ + 1].text == "=") {
            std::string name = Next().text;
            ++pos_;                            // '='
            auto n = Make(Node::Assign);
            n->name = name;
            n->kids.push_back(ParseExpr());
            return n;
        }
        return ParseExpr();                    // 표현식 문
    }

    std::unique_ptr<Node> ParseExpr() { return ParseCompare(); }
    std::unique_ptr<Node> ParseCompare() {
        auto l = ParseAdd();
        while (ok_ && Peek().type == T::Op) {
            std::string o = Peek().text;
            if (o == "<" || o == ">" || o == "<=" || o == ">=" || o == "==" || o == "!=") {
                ++pos_; auto n = Make(Node::Bin); n->op = o; n->kids.push_back(std::move(l)); n->kids.push_back(ParseAdd()); l = std::move(n);
            } else break;
        }
        return l;
    }
    std::unique_ptr<Node> ParseAdd() {
        auto l = ParseMul();
        while (ok_ && Peek().type == T::Op && (Peek().text == "+" || Peek().text == "-")) {
            std::string o = Next().text; auto n = Make(Node::Bin); n->op = o; n->kids.push_back(std::move(l)); n->kids.push_back(ParseMul()); l = std::move(n);
        }
        return l;
    }
    std::unique_ptr<Node> ParseMul() {
        auto l = ParseUnary();
        while (ok_ && Peek().type == T::Op && (Peek().text == "*" || Peek().text == "/")) {
            std::string o = Next().text; auto n = Make(Node::Bin); n->op = o; n->kids.push_back(std::move(l)); n->kids.push_back(ParseUnary()); l = std::move(n);
        }
        return l;
    }
    std::unique_ptr<Node> ParseUnary() {
        // 깊이 가드가 여기 있는 이유: 표현식의 모든 재귀 경로(단항 '-' 사슬,
        // 괄호 중첩)가 반드시 이 함수를 지난다.
        Depth guard(this);
        if (!ok_) return Make(Node::Num);
        if (Peek().type == T::Op && Peek().text == "-") { ++pos_; auto n = Make(Node::Un); n->op = "-"; n->kids.push_back(ParseUnary()); return n; }
        return ParsePrimary();
    }
    std::unique_ptr<Node> ParsePrimary() {
        if (Peek().type == T::Num) { auto n = Make(Node::Num); n->num = Next().num; return n; }
        if (Peek().type == T::Ident) { auto n = Make(Node::Var); n->name = Next().text; return n; }
        if (Peek().type == T::LParen) {
            ++pos_;
            auto e = ParseExpr();
            if (Peek().type == T::RParen) ++pos_;
            else Fail(CurrentLine(), "괄호를 닫는 ')' 가 없다");
            return e;
        }
        // 전진 보장. 모르는 토큰을 그대로 두고 돌아오면 호출자의 문 루프가
        // 같은 토큰을 영원히 다시 본다 — 수정 전에는 `}` 한 글자가 그렇게
        // 파싱 단계에서 프로세스를 세웠다.
        const int line = CurrentLine();
        const std::string what = Peek().type == T::End
            ? std::string("표현식이 끝나기 전에 소스가 끝났다")
            : std::string("표현식 자리에 올 수 없는 토큰 '") + Peek().text + "'";
        if (Peek().type != T::End) ++pos_;
        Fail(line, what);
        return Make(Node::Num);
    }

    // 예산 초과는 결과가 아니라 오류다. 여기서 ok_ 를 내리면 Eval 의 첫 줄이
    // 모든 상위 프레임을 즉시 되감고, 루프 조건도 0 으로 읽혀 반복이 끝난다.
    void FailBudget(const Node* n) {
        if (budgetExceeded_) return;
        budgetExceeded_ = true;
        Fail(n ? n->line : 0,
             "실행 스텝 예산 " + std::to_string(budget_) +
             " 을 넘겨 중단했다(끝나지 않는 반복으로 보인다)");
    }

    double Eval(const Node* n) {
        // 예산 초과·실행 오류 뒤에는 한 노드도 더 방문하지 않는다.
        if (!ok_) return 0;
        if (++steps_ > budget_) { FailBudget(n); return 0; }
        switch (n->kind) {
            case Node::Num: return n->num;
            case Node::Var: { auto it = vars_.find(n->name); return it == vars_.end() ? 0.0 : it->second; }
            case Node::Un:  return -Eval(n->kids[0].get());
            case Node::Assign: { double v = Eval(n->kids[0].get()); if (!ok_) return 0; vars_[n->name] = v; return v; }
            case Node::Block: { double last = 0; for (auto& k : n->kids) { if (!ok_) break; last = Eval(k.get()); } return last; }
            case Node::If: { if (Eval(n->kids[0].get()) != 0) return Eval(n->kids[1].get()); if (n->kids.size() > 2) return Eval(n->kids[2].get()); return 0; }
            // 지역 guard 는 없앴다. While 노드마다 다시 0 이 되는 카운터는 중첩되면
            // 곱해져 상한 구실을 못 한다 — 상한은 위의 전역 스텝 예산 하나뿐이다.
            case Node::While: { while (ok_ && Eval(n->kids[0].get()) != 0) Eval(n->kids[1].get()); return 0; }
            case Node::Bin: {
                double a = Eval(n->kids[0].get()), b = Eval(n->kids[1].get());
                const std::string& o = n->op;
                if (o == "+") return a + b; if (o == "-") return a - b; if (o == "*") return a * b;
                if (o == "/") return b != 0 ? a / b : 0;
                if (o == "<") return a < b; if (o == ">") return a > b;
                if (o == "<=") return a <= b; if (o == ">=") return a >= b;
                if (o == "==") return a == b; if (o == "!=") return a != b;
                return 0;
            }
        }
        return 0;
    }

    std::vector<Tok>                          toks_;
    size_t                                    pos_ = 0;
    bool                                      ok_ = true;
    std::string                               name_ = "<script>";
    std::string                               error_;
    long long                                 budget_ = kDefaultStepBudget;
    long long                                 steps_ = 0;
    bool                                      budgetExceeded_ = false;
    int                                       depth_ = 0;
    std::unordered_map<std::string, double>   vars_;
};

}  // namespace tools
}  // namespace mega
