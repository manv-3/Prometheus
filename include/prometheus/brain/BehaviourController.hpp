#pragma once

// Dev B shipped BehaviourFSM.hpp (PR #2) as the real behaviour selection
// component. This file is a forwarding header kept so any include that used
// the placeholder name during skeleton work still compiles.
// Use BehaviourFSM directly for all new code.
#include "prometheus/brain/BehaviourFSM.hpp"
