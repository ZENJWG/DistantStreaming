#include "asc/HdResidency.h"
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
    using namespace asc::hd;
    assert(EligibleForEviction(false, 100, 99, 5000));
    assert(!EligibleForEviction(true, 100, 95, 5000));
    assert(EligibleForEviction(true, 6000, 95, 5000));
    assert(!EligibleForEviction(true, 6000, 0, 5000));
    assert(EligibleForEviction(true, 25, UINT32_MAX-6000, 5000));
    assert(HasCapacity(100, 200, 300));
    assert(!HasCapacity(100, 201, 300));
    assert(!HasCapacity(UINT64_MAX, 10, 500));
    assert(RetryReady(100, 0));
    assert(!RetryReady(100, 500));
    assert(RetryReady(500, 500));
    assert(RetryReady(25, UINT32_MAX-25));
    std::cout << "HD residency policy tests passed\\n";
}
