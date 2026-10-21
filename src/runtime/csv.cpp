#include "runtime/csv.hpp"
#include "bytecode/bytecode.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace retroc {

static std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r')) {
        start++;
    }
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) {
        end--;
    }
    return s.substr(start, end - start);
}

static std::vector<std::string> parse_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string cur;
    bool in_quotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                cur += '"';
                i++;
            } else {
                in_quotes = !in_quotes;
            }
        } else if (c == ',' && !in_quotes) {
            fields.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    fields.push_back(trim(cur));
    return fields;
}

Collection load_csv(const std::string& path, const TypePtr& elem_type) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw RuntimeError("Cannot open CSV file: " + path);
    }

    std::string header_line;
    if (!std::getline(file, header_line)) {
        return {};
    }

    auto headers = parse_csv_line(header_line);
    
    // Map column name to index in CSV
    std::unordered_map<std::string, size_t> col_map;
    for (size_t i = 0; i < headers.size(); ++i) {
        col_map[headers[i]] = i;
    }

    // Verify all fields in elem_type exist in CSV
    if (elem_type && elem_type->kind == TypeKind::RECORD) {
        for (const auto& f : elem_type->fields) {
            if (col_map.find(f.name) == col_map.end()) {
                throw RuntimeError("CSV header missing field '" + f.name + "'");
            }
        }
    }

    Collection result;
    std::string line;
    RecordId next_id = 0;

    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        auto row = parse_csv_line(trimmed);
        std::vector<RecordField> record_fields;

        if (elem_type && elem_type->kind == TypeKind::RECORD) {
            for (const auto& f : elem_type->fields) {
                size_t col_idx = col_map[f.name];
                std::string cell_str = col_idx < row.size() ? row[col_idx] : "";
                Value val;
                switch (f.type->kind) {
                    case TypeKind::INT:
                        val = Value::make_int(std::stoll(cell_str));
                        break;
                    case TypeKind::FLOAT:
                        val = Value::make_float(std::stod(cell_str));
                        break;
                    case TypeKind::BOOL:
                        val = Value::make_bool(cell_str == "true" || cell_str == "1");
                        break;
                    case TypeKind::STRING:
                        val = Value::make_string(cell_str);
                        break;
                    default:
                        throw RuntimeError("Non-flat type in CSV record field: " + f.name);
                }
                record_fields.push_back(RecordField{f.name, std::make_shared<Value>(std::move(val))});
            }
        }
        result.push_back(CollectionItem{next_id++, Value::make_record(std::move(record_fields))});
    }

    return result;
}

} // namespace retroc
