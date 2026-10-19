#pragma once

#include <string>

namespace retroc {

struct SourceLocation {
    int line{1};
    int col{1};

    std::string to_string() const {
        return std::to_string(line) + ":" + std::to_string(col);
    }
};

} // namespace retroc
