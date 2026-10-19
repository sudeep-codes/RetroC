#pragma once

#include "location.hpp"
#include <string>
#include <sstream>

namespace retroc {

enum class ErrorCode {
    E101, // unterminated string / bad escape
    E102, // unexpected character
    E201, // syntax error
    E301, // type mismatch
    E302, // unknown field
    E303, // wrong arity / argument type for builtin
    E304, // lambda used outside a builtin argument
    E401, // undefined name
    E402, // recursion not allowed
    E403, // duplicate or shadowing name
    E501, // runtime error
    E601  // invalid edit
};

inline const char* error_code_str(ErrorCode code) {
    switch (code) {
        case ErrorCode::E101: return "E101";
        case ErrorCode::E102: return "E102";
        case ErrorCode::E201: return "E201";
        case ErrorCode::E301: return "E301";
        case ErrorCode::E302: return "E302";
        case ErrorCode::E303: return "E303";
        case ErrorCode::E304: return "E304";
        case ErrorCode::E401: return "E401";
        case ErrorCode::E402: return "E402";
        case ErrorCode::E403: return "E403";
        case ErrorCode::E501: return "E501";
        case ErrorCode::E601: return "E601";
    }
    return "UNKNOWN";
}

struct CompileError {
    ErrorCode code;
    SourceLocation loc;
    std::string message;

    std::string format() const {
        std::ostringstream oss;
        oss << loc.to_string() << ": " << error_code_str(code) << ": " << message;
        return oss.str();
    }
};

} // namespace retroc
