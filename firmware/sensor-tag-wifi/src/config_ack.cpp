#include "config_ack.h"

bool preValidate(const ConfigParser::ConfigUpdate& /*parsed*/,
                 AckEntry* /*outEntries*/, size_t* outCount) {
    // PR-1: no cross-key rules.  PR-2 will add feat_* mutual exclusion here.
    if (outCount) *outCount = 0;
    return true;
}
