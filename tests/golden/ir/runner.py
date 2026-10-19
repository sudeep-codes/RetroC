import os
import subprocess
import sys

def run_golden_tests():
    retroc_exe = sys.argv[1] if len(sys.argv) > 1 else "./build/retroc.exe"
    golden_dir = "tests/golden/ir"
    
    passed = 0
    total = 0
    
    files = sorted([f for f in os.listdir(golden_dir) if f.endswith(".rc")])
    for f in files:
        base = os.path.splitext(f)[0]
        rc_path = os.path.join(golden_dir, f)
        expected_path = os.path.join(golden_dir, base + ".expected")
        
        if not os.path.exists(expected_path):
            print(f"FAILED: missing {expected_path}")
            sys.exit(1)
            
        res = subprocess.run([retroc_exe, "dump-ir", rc_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        actual = (res.stdout if res.returncode == 0 else res.stderr).replace("\r\n", "\n")
            
        with open(expected_path, "r", encoding="utf-8") as exp_f:
            expected = exp_f.read().replace("\r\n", "\n")
            
        if actual.strip() != expected.strip():
            print(f"FAILED: mismatch in {f}")
            print("--- Expected ---")
            print(expected[:300])
            print("--- Actual ---")
            print(actual[:300])
            sys.exit(1)
            
        passed += 1
        total += 1
        
    print(f"All {passed}/{total} IR golden tests PASSED.")

if __name__ == "__main__":
    run_golden_tests()
