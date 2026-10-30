#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <utility>
#include <ostream>

namespace retroc {

using RecordId = uint64_t;

enum class ValueKind {
    INT,
    FLOAT,
    BOOL,
    STRING,
    ARRAY,
    RECORD
};

struct Value;
using ValuePtr = std::shared_ptr<Value>;

struct RecordField {
    std::string name;
    std::shared_ptr<Value> value;
};

struct Value {
    ValueKind kind;
    int64_t int_val = 0;
    double float_val = 0.0;
    bool bool_val = false;
    std::string string_val;
    std::vector<Value> array_val;
    std::vector<RecordField> record_val; // stored in canonical/declaration order

    Value() : kind(ValueKind::INT), int_val(0) {}

    static Value make_int(int64_t v) {
        Value val;
        val.kind = ValueKind::INT;
        val.int_val = v;
        return val;
    }

    static Value make_float(double v) {
        Value val;
        val.kind = ValueKind::FLOAT;
        val.float_val = v;
        return val;
    }

    static Value make_bool(bool v) {
        Value val;
        val.kind = ValueKind::BOOL;
        val.bool_val = v;
        return val;
    }

    static Value make_string(std::string v) {
        Value val;
        val.kind = ValueKind::STRING;
        val.string_val = std::move(v);
        return val;
    }

    static Value make_array(std::vector<Value> elems) {
        Value val;
        val.kind = ValueKind::ARRAY;
        val.array_val = std::move(elems);
        return val;
    }

    static Value make_record(std::vector<RecordField> fields) {
        Value val;
        val.kind = ValueKind::RECORD;
        val.record_val = std::move(fields);
        return val;
    }

    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }

    // 64-bit structural hash
    uint64_t hash() const;

    // Hash consistent with operator==: equal values hash equally (0.0 and -0.0
    // compare equal but have different bits). Used to bucket join keys.
    uint64_t equality_hash() const;

    // Deterministic string representation per LANGUAGE_SPEC.md section 6
    std::string to_string() const;
};

struct ValueHasher {
    size_t operator()(const Value& v) const noexcept {
        return static_cast<size_t>(v.hash());
    }
};

// Represents a collection of records with stable RecordIds: vector of (RecordId, Value)
struct CollectionItem {
    RecordId id;
    Value value;
};

using Collection = std::vector<CollectionItem>;

// Deterministic float printing per LANGUAGE_SPEC.md section 6:
// "shortest round-trip decimal, always with a . (e.g. 3.0, 0.25)"
std::string format_float(double val);

// Collection hash = f(size, sum of per-record terms). The sum is commutative, so
// replacing, adding or removing one record updates it in O(1) instead of
// rehashing the whole collection. Order is still captured: RecordIds are unique
// and collections are sorted by RecordId (I7), so the (id, value) set determines
// the sequence.
uint64_t collection_item_term(RecordId id, uint64_t value_hash);
uint64_t collection_hash_from_sum(uint64_t term_sum, size_t size);
uint64_t collection_term_sum(const Collection& col);

// Hash a collection directly without copying elements into a Value::ARRAY
uint64_t hash_collection(const Collection& col);

} // namespace retroc
