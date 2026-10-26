#include <iostream>
#include <fstream>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

void generate_w1(const std::string& out_dir, size_t n, uint32_t seed = 42) {
    fs::create_directories(out_dir);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> temp_dist(-20, 120);
    std::uniform_int_distribution<int> hum_dist(0, 100);

    // CSV: id,temp,humidity,valid
    std::ofstream csv(fs::path(out_dir) / ("w1_" + std::to_string(n) + ".csv"));
    csv << "id,temp,humidity,valid\n";
    for (size_t i = 0; i < n; ++i) {
        int t = temp_dist(rng);
        int h = hum_dist(rng);
        bool valid = (t >= -10 && t <= 100);
        csv << i << "," << t << "," << h << "," << (valid ? "true" : "false") << "\n";
    }

    // Pipeline:
    // filter valid -> map normalize -> map scale -> filter range -> reduce sum -> scalar avg
    std::ofstream rc(fs::path(out_dir) / ("w1_" + std::to_string(n) + ".rc"));
    rc << "input raw: [{ id: int, temp: int, humidity: int, valid: bool }] from \"w1_" << n << ".csv\";\n"
       << "let valid_readings = filter(raw, r => r.valid);\n"
       << "let normalized = map(valid_readings, r => { id: r.id, temp: r.temp + 10 });\n"
       << "let scaled = map(normalized, r => { id: r.id, temp: r.temp * 2 });\n"
       << "let in_range = filter(scaled, r => r.temp >= 10 and r.temp <= 200);\n"
       << "let total = reduce(in_range, 0, (acc, r) => acc + r.temp);\n"
       << "output total;\n";
}

void generate_w2(const std::string& out_dir, size_t n_users, size_t n_orders, uint32_t seed = 43) {
    fs::create_directories(out_dir);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> uid_dist(0, static_cast<int>(n_users - 1));
    std::uniform_int_distribution<int> amt_dist(10, 500);

    // users.csv: uid,active
    std::ofstream u_csv(fs::path(out_dir) / "w2_users.csv");
    u_csv << "uid,active\n";
    for (size_t i = 0; i < n_users; ++i) {
        u_csv << i << "," << ((i % 2 == 0) ? "true" : "false") << "\n";
    }

    // orders.csv: oid,uid,amt
    std::ofstream o_csv(fs::path(out_dir) / "w2_orders.csv");
    o_csv << "oid,uid,amt\n";
    for (size_t i = 0; i < n_orders; ++i) {
        o_csv << i << "," << uid_dist(rng) << "," << amt_dist(rng) << "\n";
    }

    // Pipeline: join -> map amount -> filter -> reduce
    std::ofstream rc(fs::path(out_dir) / "w2.rc");
    rc << "input users: [{ uid: int, active: bool }] from \"w2_users.csv\";\n"
       << "input orders: [{ oid: int, uid: int, amt: int }] from \"w2_orders.csv\";\n"
       << "let joined = join(users, orders, u => u.uid, o => o.uid);\n"
       << "let amounts = map(joined, j => j.right.amt);\n"
       << "let filtered = filter(amounts, a => a > 50);\n"
       << "let total_amt = reduce(filtered, 0, (acc, a) => acc + a);\n"
       << "output total_amt;\n";
}

void generate_w3(const std::string& out_dir, size_t n, uint32_t seed = 44) {
    fs::create_directories(out_dir);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> val_dist(1, 100);

    // CSV: id,val
    std::ofstream csv(fs::path(out_dir) / "w3.csv");
    csv << "id,val\n";
    for (size_t i = 0; i < n; ++i) {
        csv << i << "," << val_dist(rng) << "\n";
    }

    // Pipeline: 12 chained map/filter stages + reduce
    std::ofstream rc(fs::path(out_dir) / "w3.rc");
    rc << "input raw: [{ id: int, val: int }] from \"w3.csv\";\n";
    std::string prev = "raw";
    for (int i = 1; i <= 12; ++i) {
        std::string cur = "s" + std::to_string(i);
        if (i % 2 == 1) {
            rc << "let " << cur << " = map(" << prev << ", x => { id: x.id, val: x.val + " << i << " });\n";
        } else {
            rc << "let " << cur << " = filter(" << prev << ", x => x.val > " << i << ");\n";
        }
        prev = cur;
    }
    rc << "let total = reduce(" << prev << ", 0, (acc, x) => acc + x.val);\n"
       << "output total;\n";
}

void generate_w4(const std::string& out_dir, size_t n, uint32_t seed = 45) {
    fs::create_directories(out_dir);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> val_dist(1, 1000);

    // CSV: id,val
    std::ofstream csv(fs::path(out_dir) / "w4.csv");
    csv << "id,val\n";
    for (size_t i = 0; i < n; ++i) {
        csv << i << "," << val_dist(rng) << "\n";
    }

    // Pipeline: map -> reduce
    std::ofstream rc(fs::path(out_dir) / "w4.rc");
    rc << "input raw: [{ id: int, val: int }] from \"w4.csv\";\n"
       << "let mapped = map(raw, x => x.val * 2);\n"
       << "let total = reduce(mapped, 0, (acc, x) => acc + x);\n"
       << "output total;\n";
}

int main(int argc, char** argv) {
    std::string out_dir = "bench/data";
    if (argc > 1) {
        out_dir = argv[1];
    }
    std::cout << "Generating benchmark workloads in: " << out_dir << "..." << std::endl;

    // W1 at 10k, 100k, and 1M
    generate_w1(out_dir, 10000);
    generate_w1(out_dir, 100000);
    generate_w1(out_dir, 1000000);

    // W2: 500 users, 2000 orders
    generate_w2(out_dir, 500, 2000);

    // W3: 100000 records, 12 chained stages
    generate_w3(out_dir, 100000);

    // W4: 100000 records, reduce heavy
    generate_w4(out_dir, 100000);

    std::cout << "Workload generation complete." << std::endl;
    return 0;
}
