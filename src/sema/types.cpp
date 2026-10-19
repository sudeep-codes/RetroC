#include "sema/types.hpp"
#include <algorithm>

namespace retroc {

bool types_equal(const TypePtr& a, const TypePtr& b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;

    switch (a->kind) {
        case TypeKind::INT:
        case TypeKind::FLOAT:
        case TypeKind::BOOL:
        case TypeKind::STRING:
            return true;
        case TypeKind::ARRAY:
            return types_equal(a->element_type, b->element_type);
        case TypeKind::RECORD: {
            if (a->fields.size() != b->fields.size()) return false;
            // Record type equality: field order is irrelevant for typing (LANGUAGE_SPEC Section 3)
            for (const auto& fa : a->fields) {
                auto it = std::find_if(b->fields.begin(), b->fields.end(), [&](const FieldType& fb) {
                    return fb.name == fa.name;
                });
                if (it == b->fields.end()) return false;
                if (!types_equal(fa.type, it->type)) return false;
            }
            return true;
        }
    }
    return false;
}

bool is_numeric(const TypePtr& t) {
    if (!t) return false;
    return t->kind == TypeKind::INT || t->kind == TypeKind::FLOAT;
}

bool is_comparable(const TypePtr& a, const TypePtr& b) {
    if (!types_equal(a, b)) return false;
    if (!a) return false;
    // <, <=, >, >= work on int, float, string
    return a->kind == TypeKind::INT || a->kind == TypeKind::FLOAT || a->kind == TypeKind::STRING;
}

bool is_valid_join_key(const TypePtr& t) {
    if (!t) return false;
    // Join key type K must be int, string, or bool (LANGUAGE_SPEC Section 4)
    return t->kind == TypeKind::INT || t->kind == TypeKind::STRING || t->kind == TypeKind::BOOL;
}

} // namespace retroc
