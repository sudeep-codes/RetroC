import os
import subprocess
import sys

def run_golden_tests():
    retroc_exe = sys.argv[1] if len(sys.argv) > 1 else "./build/retroc.exe"
    golden_dir = "tests/golden/lexer"
    
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
            
        res = subprocess.run([retroc_exe, "dump-tokens", rc_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res.returncode != 0:
            print(f"FAILED: retroc dump-tokens {rc_path} returned {res.returncode}")
            print(res.stderr)
            sys.exit(1)
            
        with open(expected_path, "r", encoding="utf-8") as exp_f:
            expected = exp_f.read().replace("\r\n", "\n")
            
        actual = res.stdout.replace("\r\n", "\n")
        if actual != expected:
            print(f"FAILED: mismatch in {f}")
            print("--- Expected ---")
            print(expected[:200])
            print("--- Actual ---")
            print(actual[:200])
            sys.exit(1)
            
        passed += 1
        total += 1
        
    print(f"All {passed}/{total} lexer golden tests PASSED.")

if __name__ == "__main__":
    run_golden_tests()
