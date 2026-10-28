#include "common/value.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstring>

namespace retroc {

// FNV-1a 64-bit hash helper
static inline uint64_t hash_combine(uint64_t h, uint64_t v) {
    h ^= v;
    h *= 1099511628211ULL;
    return h;
}

static inline uint64_t hash_bytes(const void* ptr, size_t len) {
    uint64_t h = 14695981039346656037ULL;
    const uint8_t* p = static_cast<const uint8_t*>(ptr);
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

std::string format_float(double val) {
    if (std::isnan(val)) return "nan";
    if (std::isinf(val)) return val < 0 ? "-inf" : "inf";

    // Format shortest round-trip decimal
    std::ostringstream ss;
    ss << std::setprecision(17) << val;
    std::string s = ss.str();
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos && s.find('E') == std::string::npos) {
        s += ".0";
    }
    return s;
}

bool Value::operator==(const Value& other) const {
    if (kind != other.kind) return false;
    switch (kind) {
        case ValueKind::INT:
            return int_val == other.int_val;
        case ValueKind::FLOAT:
            return float_val == other.float_val;
        case ValueKind::BOOL:
            return bool_val == other.bool_val;
        case ValueKind::STRING:
            return string_val == other.string_val;
        case ValueKind::ARRAY:
            if (array_val.size() != other.array_val.size()) return false;
            for (size_t i = 0; i < array_val.size(); ++i) {
                if (!(array_val[i] == other.array_val[i])) return false;
            }
            return true;
        case ValueKind::RECORD:
            if (record_val.size() != other.record_val.size()) return false;
            for (size_t i = 0; i < record_val.size(); ++i) {
                if (record_val[i].name != other.record_val[i].name) return false;
                if (!record_val[i].value || !other.record_val[i].value) {
                    if (record_val[i].value != other.record_val[i].value) return false;
                } else if (!(*record_val[i].value == *other.record_val[i].value)) {
                    return false;
                }
            }
            return true;
    }
    return false;
}

uint64_t Value::hash() const {
    uint64_t h = 14695981039346656037ULL;
    h = hash_combine(h, static_cast<uint64_t>(kind));
    switch (kind) {
        case ValueKind::INT:
            h = hash_combine(h, static_cast<uint64_t>(int_val));
            break;
        case ValueKind::FLOAT: {
            uint64_t bits = 0;
            std::memcpy(&bits, &float_val, sizeof(bits));
            h = hash_combine(h, bits);
            break;
        }
        case ValueKind::BOOL:
            h = hash_combine(h, bool_val ? 1 : 0);
            break;
        case ValueKind::STRING:
            h = hash_combine(h, hash_bytes(string_val.data(), string_val.size()));
            break;
        case ValueKind::ARRAY:
            h = hash_combine(h, array_val.size());
            for (const auto& elem : array_val) {
                h = hash_combine(h, elem.hash());
            }
            break;
        case ValueKind::RECORD:
            h = hash_combine(h, record_val.size());
            for (const auto& f : record_val) {
                h = hash_combine(h, hash_bytes(f.name.data(), f.name.size()));
                if (f.value) {
                    h = hash_combine(h, f.value->hash());
                }
            }
            break;
    }
    return h;
}

std::string Value::to_string() const {
    switch (kind) {
        case ValueKind::INT:
            return std::to_string(int_val);
        case ValueKind::FLOAT:
            return format_float(float_val);
        case ValueKind::BOOL:
            return bool_val ? "true" : "false";
        case ValueKind::STRING: {
            std::string out = "\"";
            for (char c : string_val) {
                if (c == '"') out += "\\\"";
                else if (c == '\\') out += "\\\\";
                else if (c == '\n') out += "\\n";
                else if (c == '\t') out += "\\t";
                else out += c;
            }
            out += "\"";
            return out;
        }
        case ValueKind::ARRAY: {
            std::string out = "[";
            for (size_t i = 0; i < array_val.size(); ++i) {
                if (i > 0) out += ", ";
                out += array_val[i].to_string();
            }
            out += "]";
            return out;
        }
        case ValueKind::RECORD: {
            std::string out = "{";
            for (size_t i = 0; i < record_val.size(); ++i) {
                if (i > 0) out += ", ";
                out += record_val[i].name + ": " + (record_val[i].value ? record_val[i].value->to_string() : "null");
            }
            out += "}";
            return out;
        }
    }
    return "";
}

// splitmix64 finalizer: spreads bits so that summing mixed terms behaves like
// a random 64-bit value per (RecordId, value) pair.
static inline uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

uint64_t collection_item_term(RecordId id, uint64_t value_hash) {
    return mix64(mix64(id) ^ value_hash);
}

uint64_t collection_hash_from_sum(uint64_t term_sum, size_t size) {
    uint64_t h = 14695981039346656037ULL;
    h = hash_combine(h, static_cast<uint64_t>(ValueKind::ARRAY));
    h = hash_combine(h, size);
    h = hash_combine(h, term_sum);
    return h;
}

uint64_t collection_term_sum(const Collection& col) {
    uint64_t sum = 0;
    for (const auto& item : col) {
        sum += collection_item_term(item.id, item.value.hash());
    }
    return sum;
}

uint64_t hash_collection(const Collection& col) {
    return collection_hash_from_sum(collection_term_sum(col), col.size());
}

} // namespace retroc
