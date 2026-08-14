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
    // 소스 실행. 파싱·실행 성공 시 true.
    bool Run(const std::string& src) {
        ok_ = true;
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
    // --- 토큰 ---
    enum class T { Num, Ident, Op, LParen, RParen, LBrace, RBrace, KwIf, KwElse, KwWhile, End };
    struct Tok { T type; double num = 0; std::string text; };

    void Lex(const std::string& s) {
        toks_.clear();
        size_t i = 0;
        auto isIdent = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
        while (i < s.size()) {
            char c = s[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';') { ++i; continue; }
            if (c >= '0' && c <= '9') {
                size_t j = i; double v = 0; while (j < s.size() && s[j] >= '0' && s[j] <= '9') v = v * 10 + (s[j++] - '0');
                if (j < s.size() && s[j] == '.') { ++j; double f = 0.1; while (j < s.size() && s[j] >= '0' && s[j] <= '9') { v += (s[j++] - '0') * f; f *= 0.1; } }
                toks_.push_back({T::Num, v, {}}); i = j; continue;
            }
            if (isIdent(c)) {
                size_t j = i; while (j < s.size() && (isIdent(s[j]) || (s[j] >= '0' && s[j] <= '9'))) ++j;
                std::string id = s.substr(i, j - i); i = j;
                if (id == "if") toks_.push_back({T::KwIf, 0, id});
                else if (id == "else") toks_.push_back({T::KwElse, 0, id});
                else if (id == "while") toks_.push_back({T::KwWhile, 0, id});
                else toks_.push_back({T::Ident, 0, id});
                continue;
            }
            if (c == '(') { toks_.push_back({T::LParen, 0, "("}); ++i; continue; }
            if (c == ')') { toks_.push_back({T::RParen, 0, ")"}); ++i; continue; }
            if (c == '{') { toks_.push_back({T::LBrace, 0, "{"}); ++i; continue; }
            if (c == '}') { toks_.push_back({T::RBrace, 0, "}"}); ++i; continue; }
            // 연산자(다중 문자 우선).
            std::string two = s.substr(i, 2);
            if (two == "==" || two == "!=" || two == "<=" || two == ">=") { toks_.push_back({T::Op, 0, two}); i += 2; continue; }
            if (std::string("+-*/<>=").find(c) != std::string::npos) { toks_.push_back({T::Op, 0, std::string(1, c)}); ++i; continue; }
            ok_ = false; ++i;   // 알 수 없는 문자
        }
        toks_.push_back({T::End, 0, {}});
    }

    // --- AST ---
    struct Node {
        enum Kind { Num, Var, Bin, Un, Assign, If, While, Block } kind;
        double      num = 0;
        std::string name;     // Var/Assign
        std::string op;       // Bin/Un
        std::vector<std::unique_ptr<Node>> kids;
    };
    static std::unique_ptr<Node> Make(Node::Kind k) { auto n = std::make_unique<Node>(); n->kind = k; return n; }

    const Tok& Peek() const { return toks_[pos_]; }
    const Tok& Next() { return toks_[pos_++]; }
    bool AcceptOp(const std::string& o) { if (Peek().type == T::Op && Peek().text == o) { ++pos_; return true; } return false; }

    // 블록: '{' 문* '}' 또는 단일 문.
    std::unique_ptr<Node> ParseBlock() {
        if (Peek().type == T::LBrace) {
            ++pos_;
            auto b = Make(Node::Block);
            while (Peek().type != T::RBrace && Peek().type != T::End) b->kids.push_back(ParseStatement());
            if (Peek().type == T::RBrace) ++pos_; else ok_ = false;
            return b;
        }
        auto b = Make(Node::Block);
        b->kids.push_back(ParseStatement());
        return b;
    }
    std::unique_ptr<Node> ParseBlockUntilEnd() {
        auto b = Make(Node::Block);
        while (Peek().type != T::End) b->kids.push_back(ParseStatement());
        return b;
    }

    std::unique_ptr<Node> ParseStatement() {
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
        while (Peek().type == T::Op) {
            std::string o = Peek().text;
            if (o == "<" || o == ">" || o == "<=" || o == ">=" || o == "==" || o == "!=") {
                ++pos_; auto n = Make(Node::Bin); n->op = o; n->kids.push_back(std::move(l)); n->kids.push_back(ParseAdd()); l = std::move(n);
            } else break;
        }
        return l;
    }
    std::unique_ptr<Node> ParseAdd() {
        auto l = ParseMul();
        while (Peek().type == T::Op && (Peek().text == "+" || Peek().text == "-")) {
            std::string o = Next().text; auto n = Make(Node::Bin); n->op = o; n->kids.push_back(std::move(l)); n->kids.push_back(ParseMul()); l = std::move(n);
        }
        return l;
    }
    std::unique_ptr<Node> ParseMul() {
        auto l = ParseUnary();
        while (Peek().type == T::Op && (Peek().text == "*" || Peek().text == "/")) {
            std::string o = Next().text; auto n = Make(Node::Bin); n->op = o; n->kids.push_back(std::move(l)); n->kids.push_back(ParseUnary()); l = std::move(n);
        }
        return l;
    }
    std::unique_ptr<Node> ParseUnary() {
        if (Peek().type == T::Op && Peek().text == "-") { ++pos_; auto n = Make(Node::Un); n->op = "-"; n->kids.push_back(ParseUnary()); return n; }
        return ParsePrimary();
    }
    std::unique_ptr<Node> ParsePrimary() {
        if (Peek().type == T::Num) { auto n = Make(Node::Num); n->num = Next().num; return n; }
        if (Peek().type == T::Ident) { auto n = Make(Node::Var); n->name = Next().text; return n; }
        if (Peek().type == T::LParen) { ++pos_; auto e = ParseExpr(); if (Peek().type == T::RParen) ++pos_; else ok_ = false; return e; }
        ok_ = false; return Make(Node::Num);
    }

    double Eval(const Node* n) {
        switch (n->kind) {
            case Node::Num: return n->num;
            case Node::Var: { auto it = vars_.find(n->name); return it == vars_.end() ? 0.0 : it->second; }
            case Node::Un:  return -Eval(n->kids[0].get());
            case Node::Assign: { double v = Eval(n->kids[0].get()); vars_[n->name] = v; return v; }
            case Node::Block: { double last = 0; for (auto& k : n->kids) last = Eval(k.get()); return last; }
            case Node::If: { if (Eval(n->kids[0].get()) != 0) return Eval(n->kids[1].get()); if (n->kids.size() > 2) return Eval(n->kids[2].get()); return 0; }
            case Node::While: { int guard = 0; while (Eval(n->kids[0].get()) != 0) { Eval(n->kids[1].get()); if (++guard > 1000000) { ok_ = false; break; } } return 0; }
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
    std::unordered_map<std::string, double>   vars_;
};

}  // namespace tools
}  // namespace mega
