#include "model/network_model.h"

#include <deque>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr)                                                          \
    do {                                                                     \
        if (!(expr)) {                                                       \
            std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ':' \
                      << __LINE__ << ")\n";                                  \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

void TestOrderedInputBuffer() {
    netlab::OrderedInputBuffer buffer;
    std::vector<int> applied;

    buffer.Receive(2, 30);
    buffer.Receive(0, 10);
    buffer.Drain([&](int value) { applied.push_back(value); });
    CHECK((applied == std::vector<int>{10}));
    CHECK(buffer.LastApplied() == 0);
    CHECK(buffer.BufferedCount() == 1);

    buffer.Receive(1, 20);
    buffer.Drain([&](int value) { applied.push_back(value); });
    CHECK((applied == std::vector<int>{10, 20, 30}));
    CHECK(buffer.LastApplied() == 2);
    CHECK(buffer.BufferedCount() == 0);

    buffer.Receive(1, 999);
    buffer.Receive(2, 999);
    buffer.Drain([&](int value) { applied.push_back(value); });
    CHECK(applied.size() == 3);
}

void TestCumulativeAck() {
    int newest = -1;
    CHECK(netlab::AcceptCumulativeAck(0, newest));
    CHECK(newest == 0);
    CHECK(netlab::AcceptCumulativeAck(3, newest));
    CHECK(newest == 3);
    CHECK(!netlab::AcceptCumulativeAck(2, newest));
    CHECK(newest == 3);
    CHECK(netlab::AcceptCumulativeAck(3, newest));
}

void TestDelaySampler() {
    for (uint32_t value = 0; value < 100; ++value) {
        const int delay = netlab::SampleDelayFrames(6.0f, 2.0f, value);
        CHECK(delay >= 4 && delay <= 8);
    }
    CHECK(netlab::SampleDelayFrames(0.0f, 5.0f, 0) == 0);
    CHECK(netlab::SampleDelayFrames(500.0f, 0.0f, 0) == 200);
}

void TestMutualMatchWindow() {
    CHECK(netlab::MutuallyCompatibleMatch(50, 0.0f, 0.0f, 50.0f, 10.0f));
    CHECK(!netlab::MutuallyCompatibleMatch(100, 10.0f, 0.0f, 50.0f, 10.0f));
    CHECK(netlab::MutuallyCompatibleMatch(100, 10.0f, 10.0f, 50.0f, 10.0f));
    CHECK(!netlab::MutuallyCompatibleMatch(-1, 10.0f, 10.0f, 50.0f, 10.0f));
}

void TestLossRecoveryAndReordering() {
    netlab::OrderedInputBuffer buffer;
    std::vector<int> applied;

    for (int sequence : {0, 2, 4, 2, 5})
        buffer.Receive(sequence, sequence + 1);
    buffer.Drain([&](int input) { applied.push_back(input); });
    CHECK((applied == std::vector<int>{1}));
    CHECK(buffer.BufferedCount() == 3);

    // Lost commands 1 and 3 arrive through a reliable resend path.
    buffer.Receive(3, 4);
    buffer.Receive(1, 2);
    buffer.Drain([&](int input) { applied.push_back(input); });
    CHECK((applied == std::vector<int>{1, 2, 3, 4, 5, 6}));
    CHECK(buffer.LastApplied() == 5);
    CHECK(buffer.BufferedCount() == 0);
}

void TestReconciliation() {
    struct Command {
        int sequence;
        int input;
    };
    std::deque<Command> pending = {
        {3, 1}, {4, 1}, {5, -1},
    };

    const float reconciled = netlab::ReconcileScalar(
        0.40f, 3, pending,
        [](const Command& command) { return command.sequence; },
        [](const Command& command) { return command.input; },
        [](float position, int input) {
            return std::clamp(position + input * 0.1f, 0.0f, 1.0f);
        });
    CHECK(std::fabs(reconciled - 0.40f) < 1e-6f);
    CHECK(pending.size() == 2);
    CHECK(pending.front().sequence == 4);

    const float converged = netlab::ReconcileScalar(
        0.35f, 5, pending,
        [](const Command& command) { return command.sequence; },
        [](const Command& command) { return command.input; },
        [](float position, int input) {
            return position + input * 0.1f;
        });
    CHECK(std::fabs(converged - 0.35f) < 1e-6f);
    CHECK(pending.empty());
}

}  // namespace

int main() {
    TestOrderedInputBuffer();
    TestCumulativeAck();
    TestDelaySampler();
    TestMutualMatchWindow();
    TestLossRecoveryAndReordering();
    TestReconciliation();
    if (failures != 0) {
        std::cerr << failures << " network model test(s) failed\n";
        return 1;
    }
    std::cout << "network model tests passed\n";
    return 0;
}
