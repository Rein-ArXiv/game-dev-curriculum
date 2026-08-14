#include "sim/aos.h"
#include "sim/archetype.h"
#include "sim/isimulation.h"
#include "sim/soa.h"

namespace ppl {

std::unique_ptr<ISimulation> CreateSimulation(Layout layout) {
    switch (layout) {
        case Layout::SoA:       return std::make_unique<SoASimulation>();
        case Layout::AoS:       return std::make_unique<AoSSimulation>();
        case Layout::Archetype: return std::make_unique<ArchetypeSimulation>();
    }
    return std::make_unique<SoASimulation>();
}

}  // namespace ppl
